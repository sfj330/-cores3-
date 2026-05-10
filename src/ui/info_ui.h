#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include "config/app_config.h"

enum class InfoPageMode {
    WIFI,
    SYSTEM
};

enum class InfoHitZone {
    INFO_HIT_NONE,
    INFO_HIT_BACK
};

class InfoUI {
public:
    InfoUI();
    void begin();
    void show(InfoPageMode mode);
    void hide();
    void update();
    void markDirty();

    void setWifiStatus(const char* status, const char* ip, int rssi, bool configured);
    void setSystemStatus(float voltage, float percentage, bool lowBattery,
                         uint32_t heapKb, uint32_t psramKb, const char* visionStatus);

    InfoHitZone hitTest(int x, int y) const;

private:
    void drawWifiPage();
    void drawSystemPage();
    void drawBackButton();
    void drawRow(int x, int y, const char* label, const String& value, uint16_t accent);

    M5Canvas canvas_;
    bool spriteReady_ = false;
    bool visible_ = false;
    bool dirty_ = true;
    InfoPageMode mode_ = InfoPageMode::WIFI;

    String wifiStatus_ = "Unknown";
    String wifiIp_ = "--.--.--.--";
    int wifiRssi_ = 0;
    bool wifiConfigured_ = false;

    float voltage_ = 0.0f;
    float percentage_ = 0.0f;
    bool lowBattery_ = false;
    uint32_t heapKb_ = 0;
    uint32_t psramKb_ = 0;
    String visionStatus_ = "Vision: unknown";

    static constexpr int BACK_X = 8;
    static constexpr int BACK_Y = DISPLAY_HEIGHT - 32;
    static constexpr int BACK_W = 76;
    static constexpr int BACK_H = 24;
};
