#include "BleHid.h"
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include "esp_gap_ble_api.h"

namespace {
uint8_t reportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, // Keyboard, report 1
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,              // Modifiers
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,              // Reserved
    0x19, 0x00, 0x2a, 0xff, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0xc0,        // Six key usages
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02, // Consumer control, report 2
    0x15, 0x00, 0x26, 0xff, 0x03, 0x19, 0x00, 0x2a, 0xff, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xc0
};

void hostKey(uint8_t slot, char (&key)[6]) {
    snprintf(key, sizeof(key), "host%u", static_cast<unsigned>(slot));
}
}

bool BleHid::begin(const char* deviceName) {
    if (!NimBLEDevice::init(deviceName)) return false;
    for (auto& host : _hosts) host = NimBLEAddress{};
    _slot = 0;
    Preferences prefs;
    if (prefs.begin("ble_hosts", true)) {
        _slot = prefs.getUChar("selected", 0) % HOST_COUNT;
        for (uint8_t slot = 0; slot < HOST_COUNT; ++slot) {
            char key[6];
            hostKey(slot, key);
            ble_addr_t stored{};
            if (prefs.getBytesLength(key) == sizeof(stored)) {
                prefs.getBytes(key, &stored, sizeof(stored));
                _hosts[slot] = NimBLEAddress(stored);
                if (!NimBLEDevice::isBonded(_hosts[slot])) _hosts[slot] = NimBLEAddress{};
            }
        }
    }

    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    _server = NimBLEDevice::createServer();
    _server->advertiseOnDisconnect(false);
    NimBLEHIDDevice hid(_server);
    hid.setReportMap(reportMap, sizeof(reportMap));
    hid.setHidInfo(0, 0x02);
    hid.setManufacturer("AlphaPi");
    _keyboard = hid.getInputReport(1);
    _consumer = hid.getInputReport(2);
    const uint8_t released[8] = {};
    _keyboard->setValue(released, 8);
    _consumer->setValue(released, 2);
    hid.startServices();

    auto* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03c1);
    adv->addServiceUUID(hid.getHidService()->getUUID());
    adv->setName(deviceName);
    startAdvertising();
    return true;
}

void BleHid::startAdvertising() {
    auto* adv = NimBLEDevice::getAdvertising();
    const auto* address = _hosts[_slot].getBase();
    // Let the controller resolve a bonded host's private address using its IRK.
    if (_hosts[_slot].isNull() && !_pairing) return;
    const bool filtered = !_hosts[_slot].isNull() && ble_gap_wl_set(address, 1) == 0;
    adv->setScanFilter(false, filtered);
    if (!adv->isAdvertising()) adv->start();
}

void BleHid::end() {
    NimBLEDevice::deinit(true);
    _server = nullptr;
    _pending = _ready = NO_CONNECTION;
    _connected = _switching = _pairing = false;
}

void BleHid::selectHost(uint8_t slot, bool pair) {
    if (!_server || slot >= HOST_COUNT || (slot == _slot && !pair && !_pairing)) return;
    NimBLEDevice::getAdvertising()->stop();
    _ready = NO_CONNECTION;
    _switching = true;
    _slot = slot;
    _pairing = pair;
    _pairingSince = millis();

    Preferences prefs;
    if (prefs.begin("ble_hosts", false)) prefs.putUChar("selected", _slot);
    updateConnection();
}

void BleHid::updateConnection() {
    if (!_server) return;
    auto* adv = NimBLEDevice::getAdvertising();
    if (_pairing && millis() - _pairingSince >= 60000) {
        _pairing = false;
        _switching = true;
        adv->stop();
    }

    const auto peers = _server->getPeerDevices();
    if (peers.empty()) {
        _pending = _ready = NO_CONNECTION;
        if (_switching && _pairing && !_hosts[_slot].isNull()) {
            // Replace the old bond after the previous link has closed.
            if (NimBLEDevice::deleteBond(_hosts[_slot])) {
                Preferences prefs;
                char key[6];
                hostKey(_slot, key);
                if (prefs.begin("ble_hosts", false)) prefs.remove(key);
                _hosts[_slot] = NimBLEAddress{};
            } else {
                _pairing = false;
            }
        }
        _switching = false;
        if (!adv->isAdvertising()) startAdvertising();
        return;
    }

    const uint16_t handle = peers.front();
    if (_switching || (_hosts[_slot].isNull() && !_pairing)) {
        _server->disconnect(handle);
        return;
    }
    const bool wasReady = _ready == handle;
    _ready = NO_CONNECTION;

    ble_gap_conn_desc peer{};
    if (ble_gap_conn_find(handle, &peer) != 0) return;
    const NimBLEAddress identity(peer.peer_id_addr);
    for (uint8_t slot = 0; slot < HOST_COUNT; ++slot) {
        if (slot != _slot && !_hosts[slot].isNull() && _hosts[slot] == identity) {
            _server->disconnect(handle);
            return;
        }
    }

    if (wasReady && peer.sec_state.encrypted && identity == _hosts[_slot]) {
        _ready = handle;
        return;
    }
    if (!_pairing && !NimBLEDevice::isBonded(identity)) {
        _server->disconnect(handle);
        return;
    }
    if (_pending != handle) {
        _pending = handle;
        _pendingSince = millis();
        if (!peer.sec_state.encrypted) NimBLEDevice::startSecurity(handle);
    }
    if (!peer.sec_state.encrypted || !peer.sec_state.bonded ||
        !NimBLEDevice::isBonded(identity)) {
        if (millis() - _pendingSince > 15000) _server->disconnect(handle);
        return;
    }
    if (_hosts[_slot].isNull()) {
        char key[6];
        hostKey(_slot, key);
        Preferences prefs;
        if (!prefs.begin("ble_hosts", false) ||
            prefs.putBytes(key, identity.getBase(), sizeof(ble_addr_t)) != sizeof(ble_addr_t)) {
            _server->disconnect(handle);
            return;
        }
        _hosts[_slot] = identity;
        _pairing = false;
    }
    if (_hosts[_slot] != identity) {
        _server->disconnect(handle);
        return;
    }
    _ready = handle;
}

bool BleHid::isConnected() {
    updateConnection();
    const bool connected = _ready != NO_CONNECTION;
    if (connected != _connected) {
        _connected = connected;
        if (_connectionCallback) _connectionCallback(connected);
    }
    return connected;
}

// 把 esp_power_level_t 枚举换算回 dBm：相对 ESP_PWR_LVL_N0 的档数 × 3
// （IDF 4.x/5.x 枚举数值不同，但 N0 都是基准档，这样换算对两版都成立）
static int8_t pwrEnumToDbm(esp_power_level_t lvl) {
    return (int8_t)(((int)lvl - (int)ESP_PWR_LVL_N0) * 3);
}

void BleHid::setTxPower(int8_t dbm) {
    esp_power_level_t lvl;
    switch (dbm) {
        case -12: lvl = ESP_PWR_LVL_N12; break;
        case -9:  lvl = ESP_PWR_LVL_N9;  break;
        case -6:  lvl = ESP_PWR_LVL_N6;  break;
        case -3:  lvl = ESP_PWR_LVL_N3;  break;
        case 3:   lvl = ESP_PWR_LVL_P3;  break;
        case 6:   lvl = ESP_PWR_LVL_P6;  break;
        case 9:   lvl = ESP_PWR_LVL_P9;  break;
        case 0:
        default:  lvl = ESP_PWR_LVL_N0;  break;
    }
    // 广播、扫描、默认及所有连接句柄类型统一设置，覆盖广播期和连接期
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, lvl);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, lvl);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, lvl);
    for (int i = 0; i <= 9; i++) {
        esp_ble_tx_power_set((esp_ble_power_type_t)(ESP_BLE_PWR_TYPE_CONN_HDL0 + i), lvl);
    }
    // 读回实际生效值打印到串口验证（串口调试可用）
    int8_t rdAdv = pwrEnumToDbm(esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV));
    int8_t rdDef = pwrEnumToDbm(esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_DEFAULT));
    Serial.printf("BLE TX power set to %d dBm (readback adv=%d conn=%d)\n", dbm, rdAdv, rdDef);
}

void BleHid::applyAdvInterval(uint8_t mode) {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) return;

    // 单位 0.625ms，与库内 setMinInterval(0x20)/setMaxInterval(0x40) 同一量纲
    uint16_t minU = 0x20, maxU = 0x40;              // 0: 秒连 20~40ms（库默认）
    if (mode == 1)      { minU = 400; maxU = 480; }  // 1: 平衡 250~300ms
    else if (mode == 2) { minU = 800; maxU = 960; }  // 2: 省电 500~600ms
    adv->setMinInterval(minU);
    adv->setMaxInterval(maxU);

    if (adv->isAdvertising()) {
        adv->stop();
        startAdvertising();
    }
}

void BleHid::sendReport(NimBLECharacteristic* report, uint8_t* data, size_t length) {
    if (!isConnected()) return;
    const uint16_t handle = _ready;
    if (!report->notify(data, length, handle)) return;
    delay(20);
    memset(data, 0, length);
    report->notify(data, length, handle);
}

void BleHid::sendKey(uint8_t key) {
    uint8_t keys[3] = {key, 0, 0};
    sendCombination(keys);
}

void BleHid::sendCombination(uint8_t keys[3]) {
    uint8_t types[3] = {};
    sendCombination(keys, types);
}

void BleHid::sendCombination(uint8_t keys[3], uint8_t types[3]) {
    uint8_t report[8] = {};
    uint8_t position = 2;
    for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t key = keys[i];
        if (!key || types[i] != 0) continue;
        if (key >= 0xe0 && key <= 0xe7) {
            report[0] |= 1U << (key - 0xe0);
        } else {
            report[position++] = key;
        }
    }
    if (report[0] || position > 2) sendReport(_keyboard, report, sizeof(report));
    for (uint8_t i = 0; i < 3; ++i) {
        if (keys[i] && types[i] == 1) sendMedia(keys[i]);
    }
}

void BleHid::sendMedia(uint16_t mediaKey) {
    uint8_t report[] = {static_cast<uint8_t>(mediaKey), static_cast<uint8_t>(mediaKey >> 8)};
    sendReport(_consumer, report, sizeof(report));
}

void BleHid::setConnectionCallback(void (*callback)(bool connected)) {
    _connectionCallback = callback;
}
