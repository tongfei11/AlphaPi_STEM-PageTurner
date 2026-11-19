#include "WebConfig.h"
#include "ModePolicy.h"

#include <stddef.h>

#include <Preferences.h>
#include <WiFi.h>

namespace {
constexpr char kNamespace[] = "pageturner";
constexpr char kConfigKey[] = "stem_config_v1";
static_assert(sizeof(DeviceConfig) == 28, "Unexpected config record size");
constexpr uint32_t kMinAutoInterval = 500;
constexpr uint32_t kMaxAutoInterval = 3600000;
constexpr uint32_t kMinSleepSeconds = 30;
constexpr uint32_t kMaxSleepSeconds = 86400;

struct CustomFields {
    const char* usage;
    const char* type;
};

constexpr CustomFields kFieldsB[] = {
    {"custom_key_b_0", "ckb_type0"},
    {"custom_key_b_1", "ckb_type1"},
    {"custom_key_b_2", "ckb_type2"}
};
constexpr CustomFields kFieldsC[] = {
    {"custom_key_c_0", "ckc_type0"},
    {"custom_key_c_1", "ckc_type1"},
    {"custom_key_c_2", "ckc_type2"}
};

bool validTarget(long mode, bool customEnabled) {
    return mode >= MODE_PAGE && mode <= MODE_CUSTOM
           && modeEnabled(static_cast<uint8_t>(mode), customEnabled, false);
}

bool validTxPower(long power) {
    return power >= -12 && power <= 9 && power % 3 == 0;
}

void normalize(DeviceConfig& config) {
    const DeviceConfig defaults;
    if (!modeEnabled(config.currentMode, config.modeCustomEnable, config.modeAutoEnable)) {
        config.currentMode = MODE_PAGE;
    }
    if (!validTarget(config.autoTargetMode, config.modeCustomEnable)) {
        config.autoTargetMode = MODE_PAGE;
    }
    if (config.autoIntervalMs < kMinAutoInterval || config.autoIntervalMs > kMaxAutoInterval) {
        config.autoIntervalMs = defaults.autoIntervalMs;
    }
    if (config.sleepTimeoutMs != 0
        && (config.sleepTimeoutMs < kMinSleepSeconds * 1000
            || config.sleepTimeoutMs > kMaxSleepSeconds * 1000)) {
        config.sleepTimeoutMs = defaults.sleepTimeoutMs;
    }
    if (config.bleAdvMode > 2) config.bleAdvMode = defaults.bleAdvMode;
    if (!validTxPower(config.bleTxPower)) config.bleTxPower = defaults.bleTxPower;

    for (uint8_t i = 0; i < 3; ++i) {
        if (config.customKeyBType[i] > KEY_TYPE_MEDIA) config.customKeyBType[i] = KEY_TYPE_KEYBOARD;
        if (config.customKeyCType[i] > KEY_TYPE_MEDIA) config.customKeyCType[i] = KEY_TYPE_KEYBOARD;
    }
}

void loadCustomKeys(Preferences& prefs, const CustomFields (&fields)[3], const char* singleKey,
                    uint8_t (&usages)[3], uint8_t (&types)[3]) {
    // The oldest layout used one key per button, before multi-key combinations.
    if (!prefs.isKey(fields[0].usage)) {
        const uint8_t legacyUsage = prefs.getUChar(singleKey, 0);
        if (legacyUsage != 0) usages[0] = legacyUsage;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        usages[i] = prefs.getUChar(fields[i].usage, usages[i]);
        types[i] = prefs.getUChar(fields[i].type, types[i]);
    }
}

bool persistConfig(const DeviceConfig& config) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    return prefs.putBytes(kConfigKey, &config, sizeof(config)) == sizeof(config);
}

bool readNumber(WebServer& server, const char* name, long minimum, long maximum, long& result) {
    if (!server.hasArg(name)) return false;
    const String raw = server.arg(name);
    const bool negative = raw.startsWith("-");
    size_t index = negative ? 1 : 0;
    if (index == raw.length() || raw.length() > 9) return false;

    long value = 0;
    for (; index < raw.length(); ++index) {
        const char digit = raw[index];
        if (digit < '0' || digit > '9') return false;
        value = value * 10 + (digit - '0');
    }
    result = negative ? -value : value;
    return result >= minimum && result <= maximum;
}

bool readCustomKeys(WebServer& server, const CustomFields (&fields)[3],
                    uint8_t (&usages)[3], uint8_t (&types)[3]) {
    for (uint8_t i = 0; i < 3; ++i) {
        long usage, type;
        if (!readNumber(server, fields[i].usage, 0, 255, usage)
            || !readNumber(server, fields[i].type, KEY_TYPE_KEYBOARD, KEY_TYPE_MEDIA, type)) {
            return false;
        }
        usages[i] = static_cast<uint8_t>(usage);
        types[i] = static_cast<uint8_t>(type);
    }
    return true;
}

void appendOption(String& html, int value, int selected, const char* label) {
    html += "<option value='" + String(value) + "'";
    if (value == selected) html += " selected";
    html += ">";
    html += label;
    html += "</option>";
}

void appendNumber(String& html, const char* name, uint32_t value, uint32_t min, uint32_t max) {
    html += "<input type='number' name='" + String(name) + "' value='" + String(value)
            + "' min='" + String(min) + "' max='" + String(max) + "' required>";
}

void appendCustomKeys(String& html, char button, const CustomFields (&fields)[3],
                      const uint8_t (&usages)[3], const uint8_t (&types)[3]) {
    html += "<h3>按键 ";
    html += button;
    html += "</h3>";
    for (uint8_t i = 0; i < 3; ++i) {
        html += "<div class='row'><label>类型 " + String(i + 1) + "<select name='"
                + fields[i].type + "'>";
        appendOption(html, KEY_TYPE_KEYBOARD, types[i], "键盘");
        appendOption(html, KEY_TYPE_MEDIA, types[i], "媒体");
        html += "</select></label><label>HID 用法编号 " + String(i + 1);
        appendNumber(html, fields[i].usage, usages[i], 0, 255);
        html += "</label></div>";
    }
}
}

void WebConfig::loadConfigOnly() {
    _config = DeviceConfig{};
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return;

    uint8_t saved[sizeof(DeviceConfig)];
    if (prefs.getBytesLength(kConfigKey) == sizeof(saved)
        && prefs.getBytes(kConfigKey, saved, sizeof(saved)) == sizeof(saved)
        && saved[offsetof(DeviceConfig, modeCustomEnable)] <= 1
        && saved[offsetof(DeviceConfig, modeAutoEnable)] <= 1) {
        memcpy(&_config, saved, sizeof(saved));
    } else {
        _config.currentMode = prefs.getUChar("mode", _config.currentMode);
        // The full upstream firmware enabled everything by default.
        // Optional modes start hidden when first migrating to this fork.
        _config.modeCustomEnable = false;
        _config.modeAutoEnable = false;
        _config.autoTargetMode = prefs.getUChar("auto_target", _config.autoTargetMode);
        _config.autoIntervalMs = prefs.getUInt("auto_interval", _config.autoIntervalMs);
        _config.sleepTimeoutMs = prefs.getUInt("sleep_timeout", _config.sleepTimeoutMs);
        _config.bleAdvMode = prefs.getUChar("ble_adv", _config.bleAdvMode);
        _config.bleTxPower = prefs.getChar("ble_tx", _config.bleTxPower);
        loadCustomKeys(prefs, kFieldsB, "custom_key_b", _config.customKeyB, _config.customKeyBType);
        loadCustomKeys(prefs, kFieldsC, "custom_key_c", _config.customKeyC, _config.customKeyCType);
    }
    normalize(_config);
}

bool WebConfig::saveConfig() {
    return persistConfig(_config);
}

bool WebConfig::startWiFi() {
    if (_running) return true;

    const IPAddress address(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    if (!WiFi.mode(WIFI_AP) || !WiFi.softAPConfig(address, address, subnet)
        || !WiFi.softAP("AlphaPi-Config", "alphapi123")) {
        WiFi.mode(WIFI_OFF);
        return false;
    }
    if (!_routesReady) {
        _server.on("/", HTTP_GET, [this]() { showPage(); });
        _server.on("/save", HTTP_POST, [this]() { handleSave(); });
        _routesReady = true;
    }
    _server.begin();
    _running = true;
    return true;
}

void WebConfig::stopWiFi() {
    if (!_running) return;
    _server.stop();
    WiFi.mode(WIFI_OFF);
    _running = false;
}

void WebConfig::handleClient() {
    if (_running) _server.handleClient();
}

void WebConfig::showPage(bool saved) {
    String html;
    html.reserve(5500);
    html += R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AlphaPi 翻页器设置</title>
<style>
body{margin:0;background:#f5f7fa;color:#172033;font-family:system-ui,sans-serif}
main{max-width:38rem;margin:auto;padding:1rem}
fieldset{min-width:0;background:#fff;border:1px solid #ced6e0;border-radius:.75rem;padding:1rem;margin:0 0 1rem}
legend{font-weight:700}label{display:block;margin:.5rem 0}
input,select,button{font:inherit}input[type=number],select{display:block;width:100%;padding:.45rem;margin-top:.25rem;box-sizing:border-box}
.row{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}
button{background:#214abf;color:white;border:0;border-radius:.5rem;padding:.7rem 1.25rem}
</style></head><body><main><h1>AlphaPi 翻页器设置</h1>
)HTML";
    if (saved) html += "<p role='status'>设置已保存。长按设备 A 键退出设置模式。</p>";
    html += "<form method='post' action='/save'><fieldset><legend>自定义模式</legend>"
            "<label><input type='checkbox' id='custom' name='mode_custom' value='1'";
    if (_config.modeCustomEnable) html += " checked";
    html += "> 启用自定义模式</label><small>输入十进制 HID 用法编号，0 表示留空。</small>";
    appendCustomKeys(html, 'B', kFieldsB, _config.customKeyB, _config.customKeyBType);
    appendCustomKeys(html, 'C', kFieldsC, _config.customKeyC, _config.customKeyCType);
    html += "</fieldset><fieldset><legend>自动翻页</legend>"
            "<label><input type='checkbox' name='mode_auto' value='1'";
    if (_config.modeAutoEnable) html += " checked";
    html += "> 启用自动翻页</label><label>目标模式<select id='target' name='auto_target'>";
    appendOption(html, MODE_PAGE, _config.autoTargetMode, "翻页");
    appendOption(html, MODE_ARROW, _config.autoTargetMode, "方向键");
    appendOption(html, MODE_MUSIC, _config.autoTargetMode, "短视频");
    appendOption(html, MODE_CUSTOM, _config.autoTargetMode, "自定义");
    html += "</select></label><label>翻页间隔（毫秒）";
    appendNumber(html, "auto_interval", _config.autoIntervalMs, kMinAutoInterval, kMaxAutoInterval);
    html += "</label></fieldset><fieldset><legend>蓝牙与休眠</legend>"
            "<label>空闲休眠（秒，0 为关闭，最短 30 秒）";
    appendNumber(html, "idle_seconds", _config.sleepTimeoutMs / 1000, 0, kMaxSleepSeconds);
    html += "</label><label>蓝牙广播间隔<select name='ble_adv'>";
    appendOption(html, 0, _config.bleAdvMode, "快速连接");
    appendOption(html, 1, _config.bleAdvMode, "平衡");
    appendOption(html, 2, _config.bleAdvMode, "省电");
    html += "</select></label><label>蓝牙发射功率（dBm）<select name='ble_tx'>";
    for (int power = -12; power <= 9; power += 3) {
        appendOption(html, power, _config.bleTxPower, String(power).c_str());
    }
    html += R"HTML(</select></label></fieldset><button type="submit">保存设置</button></form>
<script>
const custom = document.querySelector('#custom');
const target = document.querySelector('#target');
const option = target.querySelector('[value="6"]');
function updateTarget() {
  option.disabled = !custom.checked;
  if (option.disabled && target.value === '6') target.value = '0';
}
custom.addEventListener('change', updateTarget);
updateTarget();
</script></main></body></html>)HTML";
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "text/html; charset=utf-8", html);
}

void WebConfig::handleSave() {
    DeviceConfig next = _config;
    const String custom = _server.arg("mode_custom");
    const String automatic = _server.arg("mode_auto");
    next.modeCustomEnable = custom == "1";
    next.modeAutoEnable = automatic == "1";

    long target, interval, idle, adv, power;
    if ((_server.hasArg("mode_custom") && custom != "1")
        || (_server.hasArg("mode_auto") && automatic != "1")
        || !readNumber(_server, "auto_target", MODE_PAGE, MODE_CUSTOM, target)
        || !validTarget(target, next.modeCustomEnable)
        || !readNumber(_server, "auto_interval", kMinAutoInterval, kMaxAutoInterval, interval)
        || !readNumber(_server, "idle_seconds", 0, kMaxSleepSeconds, idle)
        || (idle != 0 && idle < kMinSleepSeconds)
        || !readNumber(_server, "ble_adv", 0, 2, adv)
        || !readNumber(_server, "ble_tx", -12, 9, power)
        || !validTxPower(power)
        || !readCustomKeys(_server, kFieldsB, next.customKeyB, next.customKeyBType)
        || !readCustomKeys(_server, kFieldsC, next.customKeyC, next.customKeyCType)) {
        _server.send(400, "text/plain; charset=utf-8", "设置无效，请返回检查输入范围和自动翻页目标。");
        return;
    }
    next.autoTargetMode = static_cast<uint8_t>(target);
    next.autoIntervalMs = static_cast<uint32_t>(interval);
    next.sleepTimeoutMs = static_cast<uint32_t>(idle) * 1000;
    next.bleAdvMode = static_cast<uint8_t>(adv);
    next.bleTxPower = static_cast<int8_t>(power);
    normalize(next);
    if (!persistConfig(next)) {
        _server.send(500, "text/plain; charset=utf-8", "写入配置失败，请重试。");
        return;
    }
    _config = next;
    showPage(true);
}
