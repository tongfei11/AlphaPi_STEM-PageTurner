#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <Arduino.h>
#include <WebServer.h>

enum DeviceMode : uint8_t {
    MODE_PAGE = 0,
    MODE_ARROW = 1,
    MODE_MUSIC = 3,
    MODE_CUSTOM = 6,
    MODE_AUTO = 7
};

constexpr uint8_t KEY_TYPE_KEYBOARD = 0;
constexpr uint8_t KEY_TYPE_MEDIA = 1;

struct DeviceConfig {
    uint32_t autoIntervalMs = 5000;
    uint32_t sleepTimeoutMs = 120000;
    uint8_t currentMode = MODE_PAGE;
    bool modeCustomEnable = false;
    bool modeAutoEnable = false;
    uint8_t autoTargetMode = MODE_PAGE;
    uint8_t customKeyB[3] = {0x4E, 0, 0};
    uint8_t customKeyC[3] = {0x4B, 0, 0};
    uint8_t customKeyBType[3] = {};
    uint8_t customKeyCType[3] = {};
    uint8_t bleAdvMode = 0;
    int8_t bleTxPower = -6;
    uint8_t reserved[2] = {}; // Explicitly initialize the tail of the persisted record.
};

class WebConfig {
public:
    DeviceConfig* getConfig() { return &_config; }
    void loadConfigOnly();
    bool saveConfig();
    bool startWiFi();
    void stopWiFi();
    void handleClient();

private:
    WebServer _server{80};
    DeviceConfig _config;
    bool _running = false;
    bool _routesReady = false;

    void showPage(bool saved = false);
    void handleSave();
};

#endif
