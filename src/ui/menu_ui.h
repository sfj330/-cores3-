#pragma once

#include <cstdint>
#include <M5GFX.h>
#include "config/app_config.h"

enum class MenuHitZone {
    MENU_HIT_NONE,
    MENU_HIT_BACK,
    MENU_HIT_APP
};

class MenuUI {
public:
    MenuUI();
    void begin();
    void update();
    void show();
    void hide();

    int appAt(int x, int y) const;

    void setWifiStatus(const char* status, const char* ip);
    void setVisionStatus(const char* status);

    MenuHitZone hitTest(int x, int y) const;

    void markDirty();

    static constexpr int APP_COUNT = 5;

private:
    void drawApp(int index);
    void drawAppIcon(int index, int x, int y, int size, uint16_t accent);
    void drawBackButton();

    M5Canvas canvas_;
    bool spriteReady_ = false;
    bool visible_ = false;
    bool dirty_ = true;
    String wifiStatus_ = "Disconnected";
    String wifiIp_ = "--.--.--.--";
    String visionStatus_ = "Vision: unknown";

    static constexpr int BACK_X = 8;
    static constexpr int BACK_Y = DISPLAY_HEIGHT - 32;
    static constexpr int BACK_W = 76;
    static constexpr int BACK_H = 24;
    static constexpr int APP_W = 74;
    static constexpr int APP_H = 74;
    static constexpr int ICON_SIZE = 50;
};
