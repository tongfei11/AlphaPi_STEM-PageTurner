/*
 * BleHid.h - BLE HID 键盘和媒体键功能
 * 
 * 基于 NimBLE-Arduino
 * 
 * 支持：
 * - 键盘键（方向键、Page Up/Down 等）
 * - 媒体键（音量加减、播放暂停、上一曲/下一曲等）
 */

#ifndef BLEHID_H
#define BLEHID_H

#include <Arduino.h>
#include <NimBLEDevice.h>

constexpr uint8_t KEY_ESCAPE = 0x29;
constexpr uint8_t KEY_TAB = 0x2b;
constexpr uint8_t KEY_SPACE = 0x2c;
constexpr uint8_t KEY_DOWN = 0x51;
constexpr uint8_t KEY_UP = 0x52;
constexpr uint8_t KEY_KP_ENTER = 0x58;
constexpr uint8_t KEY_PAGE_UP = 0x4b;
constexpr uint8_t KEY_PAGE_DOWN = 0x4e;
constexpr uint8_t KEY_RIGHT = 0x4f;
constexpr uint8_t KEY_LEFT = 0x50;
constexpr uint16_t MEDIA_NEXT_TRACK = 0xb5;
constexpr uint16_t MEDIA_PREV_TRACK = 0xb6;
constexpr uint16_t MEDIA_STOP = 0xb7;
constexpr uint16_t MEDIA_PLAY_PAUSE = 0xcd;
constexpr uint16_t MEDIA_VOLUME_UP = 0xe9;
constexpr uint16_t MEDIA_VOLUME_DOWN = 0xea;

class BleHid {
public:
    
    // 初始化 BLE HID
    bool begin(const char* deviceName = "AlphaPi Turner");

    // 设置蓝牙发射功率（dBm，支持 -12/-9/-6/-3/0/3/6/9，须在 begin() 之后调用）
    void setTxPower(int8_t dbm);

    // 设置广播间隔档位（0=秒连20-40ms默认 1=平衡250ms 2=省电500ms，须在 begin() 之后调用）
    // 未连接待机时广播是射频耗电大头，拉长间隔可大幅省电，代价是手机重连稍慢
    void applyAdvInterval(uint8_t mode);
    
    // 停止 BLE HID
    void end();
    
    // 是否连接
    bool isConnected();
    
    // 发送键盘键（按下并释放）
    void sendKey(uint8_t key);
    
    // 发送组合键（最多3个键同时按下，然后释放，0表示不使用）
    void sendCombination(uint8_t keys[3]);
    
    // 发送组合键（支持键盘键和媒体键混合，types[i]=0键盘键，1媒体键）
    void sendCombination(uint8_t keys[3], uint8_t types[3]);
    
    // 发送媒体键（按下并释放）
    void sendMedia(uint16_t mediaKey);
    
    // 切换至指定配对位，可开启 60 秒配对窗口。
    void selectHost(uint8_t slot, bool pair);
    uint8_t selectedHost() const { return _slot; }

    // 设置连接状态回调
    void setConnectionCallback(void (*callback)(bool connected));
    
private:
    static constexpr uint8_t HOST_COUNT = 3;
    static constexpr uint16_t NO_CONNECTION = BLE_HS_CONN_HANDLE_NONE;

    NimBLEServer* _server = nullptr;
    NimBLECharacteristic* _keyboard = nullptr;
    NimBLECharacteristic* _consumer = nullptr;
    NimBLEAddress _hosts[HOST_COUNT];
    uint8_t _slot = 0;
    uint16_t _pending = NO_CONNECTION;
    uint16_t _ready = NO_CONNECTION;
    uint32_t _pendingSince = 0;
    bool _connected = false;
    bool _switching = false;
    bool _pairing = false;
    uint32_t _pairingSince = 0;
    void (*_connectionCallback)(bool connected) = nullptr;

    void updateConnection();
    void startAdvertising();
    void sendReport(NimBLECharacteristic* report, uint8_t* data, size_t length);
};

#endif // BLEHID_H
