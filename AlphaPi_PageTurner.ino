/*
 * AlphaPi BLE PageTurner — simplified fork of shj4588/AlphaPi_STEM-PageTurner.
 * ESP32-C3, A=10 B=1 C=3 (active high), matrix UART TX=8 RX=9.
 */
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "Buttons.h"
#include "BleHid.h"
#include "MatrixDisplay.h"
#include "WebConfig.h"
#include "ModePolicy.h"
#include "BuildVersion.h"

Buttons buttons;
BleHid bleHid;
MatrixDisplay display;
WebConfig webConfig;

enum class Screen { Normal, Hosts, Settings };
Screen screen = Screen::Normal;
bool bleEnabled = false;
bool wasConnected = false;
bool wasPairing = false;
bool blinkOn = true;
uint32_t blinkAt = 0;
uint32_t screenUntil = 0;
uint32_t activityAt = 0;
uint32_t menuAt = 0;
int8_t autoKey = -1;
uint32_t autoAt = 0;
bool chordActive = false;
bool chordFired = false;
uint32_t chordAt = 0;
bool ignoreUntilRelease = false;
uint32_t releasedAt = 0;

void showPattern(const uint8_t* pattern, uint32_t duration = 2000) {
    display.showPattern(pattern);
    screenUntil = duration ? millis() + duration : 0;
}

void showMode() {
    const uint8_t mode = webConfig.getConfig()->currentMode;
    if (mode == MODE_PAGE) display.showIcon("page");
    if (mode == MODE_ARROW) display.showIcon("arrow");
    // Video mode still sends the original next/previous-track media usages.
    if (mode == MODE_MUSIC) display.showIcon("play");
    if (mode == MODE_CUSTOM) display.showPattern(ICON_LETTER_C);
    if (mode == MODE_AUTO) display.showIcon("auto");
    screenUntil = millis() + 2000;
}

void showHost(bool connected, bool symbol = true) {
    uint8_t pattern[25] = {};
    if (symbol) {
        if (connected) {
            pattern[1] = pattern[2] = pattern[3] = 1;
            pattern[6] = pattern[8] = 1;
            pattern[11] = pattern[12] = pattern[13] = 1;
        } else {
            pattern[2] = pattern[6] = pattern[7] = pattern[8] = pattern[12] = 1;
        }
    }
    const uint8_t count = bleHid.selectedHost() + 1;
    const uint8_t start = count == 1 ? 22 : 21;
    for (uint8_t i = 0; i < count; ++i) pattern[start + i] = 1;
    showPattern(pattern, connected ? 2000 : 0);
}

void showHostMenu() {
    // A/B/C match the physical keys; bottom dots show the selected slot.
    const uint8_t a[25] = {
        0,1,1,1,0, 0,1,0,1,0, 0,1,1,1,0, 0,1,0,1,0, 0,0,0,0,0
    };
    const uint8_t b[25] = {
        0,1,1,0,0, 0,1,0,1,0, 0,1,1,0,0, 0,1,1,1,0, 0,0,0,0,0
    };
    const uint8_t c[25] = {
        0,1,1,1,0, 0,1,0,0,0, 0,1,0,0,0, 0,1,1,1,0, 0,0,0,0,0
    };
    uint8_t pattern[25];
    const uint8_t slot = bleHid.selectedHost();
    memcpy(pattern, slot == 0 ? a : slot == 1 ? b : c, sizeof(pattern));
    for (uint8_t i = 0; i <= slot; ++i) pattern[21 + i] = 1;
    showPattern(pattern, 0);
}

void startBLE() {
    bleEnabled = bleHid.begin("AlphaPi Turner");
    if (bleEnabled) {
        bleHid.setTxPower(webConfig.getConfig()->bleTxPower);
        bleHid.applyAdvInterval(webConfig.getConfig()->bleAdvMode);
    } else {
        display.showIcon("shake_off");
    }
    wasConnected = wasPairing = false;
}

void stopBLE() {
    if (bleEnabled) bleHid.end();
    bleEnabled = false;
    autoKey = -1;
}

void sendAction(uint8_t mode, bool next) {
    if (!bleEnabled || !bleHid.isConnected()) return;
    DeviceConfig* cfg = webConfig.getConfig();
    switch (mode) {
        case MODE_PAGE: bleHid.sendKey(next ? KEY_PAGE_DOWN : KEY_PAGE_UP); break;
        case MODE_ARROW: bleHid.sendKey(next ? KEY_RIGHT : KEY_LEFT); break;
        case MODE_MUSIC: bleHid.sendMedia(next ? MEDIA_NEXT_TRACK : MEDIA_PREV_TRACK); break;
        case MODE_CUSTOM:
            bleHid.sendCombination(next ? cfg->customKeyB : cfg->customKeyC,
                                   next ? cfg->customKeyBType : cfg->customKeyCType);
            break;
    }
    display.showIcon(next ? "arrow_right" : "arrow_left");
    screenUntil = millis() + 400;
}

void toggleSettings() {
    autoKey = -1;
    if (screen == Screen::Settings) {
        webConfig.stopWiFi();
        screen = Screen::Normal;
        startBLE();
        showMode();
    } else {
        stopBLE();
        screen = Screen::Settings;
        if (!webConfig.startWiFi()) {
            screen = Screen::Normal;
            startBLE();
            display.showIcon("shake_off");
            screenUntil = millis() + 2000;
            return;
        }
        const uint8_t w[25] = {
            1,0,0,0,1, 1,0,0,0,1, 1,0,1,0,1, 1,0,1,0,1, 0,1,0,1,0
        };
        showPattern(w, 0);
    }
    activityAt = millis();
}

void sleepIfIdle() {
    const uint32_t timeout = webConfig.getConfig()->sleepTimeoutMs;
    if (screen != Screen::Normal || autoKey >= 0 ||
        (bleEnabled && bleHid.isPairing()) || !timeout || millis() - activityAt < timeout) return;
    stopBLE();
    display.clear();
    display.end();
    gpio_wakeup_enable(GPIO_NUM_10, GPIO_INTR_HIGH_LEVEL);
    gpio_wakeup_enable(GPIO_NUM_1, GPIO_INTR_HIGH_LEVEL);
    gpio_wakeup_enable(GPIO_NUM_3, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    display.begin();
    startBLE();
    showMode();
    activityAt = millis();
    // A wake key must not also change mode or send a page command.
    ignoreUntilRelease = true;
    releasedAt = 0;
}

void setup() {
    Serial.begin(115200);
    Serial.println("AlphaPi PageTurner " BUILD_VERSION);
    display.begin();
    buttons.begin();
    webConfig.loadConfigOnly();
    startBLE();
    showMode();
    activityAt = millis();
}

void loop() {
    buttons.update();
    Button* keys[] = {&buttons.A, &buttons.B, &buttons.C};
    bool down[3];
    bool shortPress[3];
    bool longPress[3];
    uint8_t held = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        down[i] = keys[i]->isDown();
        shortPress[i] = keys[i]->pressed();
        longPress[i] = keys[i]->longPressed();
        held += down[i];
    }
    const uint32_t now = millis();
    if (held) activityAt = now;

    bool connected = false;
    bool pairing = false;
    if (bleEnabled) {
        connected = bleHid.isConnected();
        pairing = bleHid.isPairing();
        if (screen == Screen::Normal) {
            if (connected != wasConnected || pairing != wasPairing) {
                blinkAt = now;
                blinkOn = true;
                showHost(connected);
            } else if (pairing && now - blinkAt >= 200) {
                blinkAt = now;
                blinkOn = !blinkOn;
                showHost(false, blinkOn);
            }
        }
        if (!connected) autoKey = -1;
        wasConnected = connected;
        wasPairing = pairing;
    }
    if (screen == Screen::Settings) webConfig.handleClient();

    if (ignoreUntilRelease) {
        // Raw pins keep the wake guard active before the debouncer catches up.
        const bool rawHeld = digitalRead(10) || digitalRead(1) || digitalRead(3);
        if (held || rawHeld) releasedAt = 0;
        else if (!releasedAt) releasedAt = now;
        else if (now - releasedAt >= 50) {
            ignoreUntilRelease = false;
            releasedAt = 0;
            if (screen == Screen::Hosts) menuAt = now;
        }
        delay(10);
        return;
    }

    // Consume every multi-key gesture until all keys are released.
    if (held >= 2 && !chordActive) {
        chordActive = true;
        chordFired = false;
        chordAt = now;
    }
    if (chordActive) {
        if (held != 2 || !down[0] || !down[1]) chordFired = true;
        if (!chordFired && now - chordAt >= 1200) {
            chordFired = true;
            toggleSettings();
        }
        if (!held) chordActive = false;
        delay(10);
        return;
    }

    if (screen == Screen::Settings) {
        if (longPress[0]) {
            toggleSettings();
            ignoreUntilRelease = true;
            releasedAt = 0;
        }
    } else if (screen == Screen::Hosts) {
        bool selected = false;
        for (uint8_t i = 0; i < 3; ++i) {
            if (!shortPress[i] && !longPress[i]) continue;
            bleHid.selectHost(i, longPress[i]);
            screen = Screen::Normal;
            showHost(bleHid.isConnected());
            wasConnected = bleHid.isConnected();
            wasPairing = bleHid.isPairing();
            blinkOn = true;
            blinkAt = now;
            activityAt = now;
            ignoreUntilRelease = true;
            releasedAt = 0;
            selected = true;
            break;
        }
        if (!selected && now - menuAt >= 6000) {
            screen = Screen::Normal;
            if (pairing) showHost(false);
            else showMode();
        }
    } else if (longPress[0]) {
        autoKey = -1;
        screen = Screen::Hosts;
        menuAt = now;
        showHostMenu();
        ignoreUntilRelease = true;
        releasedAt = 0;
    } else if (!pairing) {
        DeviceConfig* cfg = webConfig.getConfig();
        if (shortPress[0]) {
            autoKey = -1;
            const uint8_t oldMode = cfg->currentMode;
            cfg->currentMode = nextMode(oldMode, cfg->modeCustomEnable, cfg->modeAutoEnable);
            if (webConfig.saveConfig()) showMode();
            else {
                cfg->currentMode = oldMode;
                display.showIcon("shake_off");
                screenUntil = now + 2000;
            }
        }
        for (uint8_t i = 1; i < 3; ++i) {
            if (!shortPress[i]) continue;
            if (cfg->currentMode == MODE_AUTO) {
                if (!connected) {
                    showHost(false);
                    continue;
                }
                autoKey = autoKey == static_cast<int8_t>(i) ? -1 : i;
                if (autoKey >= 0) {
                    sendAction(cfg->autoTargetMode, i == 1);
                    autoAt = now;
                } else showMode();
            } else sendAction(cfg->currentMode, i == 1);
        }
    }
    if (screen == Screen::Hosts && held) menuAt = now;
    DeviceConfig* cfg = webConfig.getConfig();
    if (autoKey >= 0 && screen == Screen::Normal && now - autoAt >= cfg->autoIntervalMs) {
        autoAt = now;
        sendAction(cfg->autoTargetMode, autoKey == 1);
    }
    if (screenUntil && static_cast<int32_t>(now - screenUntil) >= 0) {
        screenUntil = 0;
        if (screen == Screen::Normal && bleEnabled && !connected) showHost(false);
        else display.clear();
    }
    sleepIfIdle();
    delay(10);
}
