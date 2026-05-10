#include "menu_ui.h"
#include "ui/ui_theme.h"
#include <M5CoreS3.h>
#include <cmath>

namespace {
struct AppLayout {
    int cx;
    int y;
};

constexpr AppLayout APP_LAYOUT[MenuUI::APP_COUNT] = {
    {58, 44},
    {160, 44},
    {262, 44},
    {109, 130},
    {211, 130}
};

const char* appTitle(int index) {
    switch (index) {
        case 0: return "Wi-Fi";
        case 1: return "Camera";
        case 2: return "Timer";
        case 3: return "Music";
        case 4: return "System";
        default: return "";
    }
}

uint16_t appAccent(int index, bool wifiConnected) {
    switch (index) {
        case 0: return wifiConnected ? UiTheme::GREEN : UiTheme::CYAN;
        case 1: return UiTheme::PINK;
        case 2: return UiTheme::AMBER;
        case 3: return UiTheme::BLUE;
        case 4: return UiTheme::GREEN;
        default: return UiTheme::CYAN;
    }
}
}

MenuUI::MenuUI() : canvas_(&M5.Lcd) {}

void MenuUI::begin() {
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    spriteReady_ = canvas_.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT) != nullptr;
    if (spriteReady_) {
        canvas_.fillSprite(UiTheme::BG);
    }
}

void MenuUI::show() {
    visible_ = true;
    dirty_ = true;
}

void MenuUI::hide() {
    visible_ = false;
}

int MenuUI::appAt(int x, int y) const {
    for (int i = 0; i < APP_COUNT; ++i) {
        int appX = APP_LAYOUT[i].cx - APP_W / 2;
        int appY = APP_LAYOUT[i].y;
        if (x >= appX && x < appX + APP_W && y >= appY && y < appY + APP_H) {
            return i;
        }
    }
    return -1;
}

void MenuUI::setWifiStatus(const char* status, const char* ip) {
    String newStatus = status ? String(status) : String();
    String newIp = ip ? String(ip) : String();
    if (newStatus != wifiStatus_ || newIp != wifiIp_) {
        wifiStatus_ = newStatus;
        wifiIp_ = newIp;
        dirty_ = true;
    }
}

void MenuUI::setVisionStatus(const char* status) {
    String newStatus = status ? String(status) : String();
    if (newStatus != visionStatus_) {
        visionStatus_ = newStatus;
        dirty_ = true;
    }
}

void MenuUI::markDirty() {
    dirty_ = true;
}

MenuHitZone MenuUI::hitTest(int x, int y) const {
    if (x >= BACK_X && x < BACK_X + BACK_W && y >= BACK_Y && y < BACK_Y + BACK_H) {
        return MenuHitZone::MENU_HIT_BACK;
    }
    return appAt(x, y) >= 0 ? MenuHitZone::MENU_HIT_APP : MenuHitZone::MENU_HIT_NONE;
}

void MenuUI::update() {
    if (!visible_ || !spriteReady_ || !dirty_) return;

    canvas_.fillSprite(UiTheme::BG);
    UiTheme::drawTitle(canvas_, "CoreS3 Pet", "Tap an app", UiTheme::CYAN);
    for (int i = 0; i < APP_COUNT; ++i) {
        drawApp(i);
    }
    drawBackButton();

    canvas_.pushSprite(0, 0);
    dirty_ = false;
}

void MenuUI::drawApp(int index) {
    bool wifiConnected = wifiStatus_ == "Connected";
    uint16_t accent = appAccent(index, wifiConnected);
    int x = APP_LAYOUT[index].cx - ICON_SIZE / 2;
    int y = APP_LAYOUT[index].y;

    drawAppIcon(index, x, y, ICON_SIZE, accent);

    canvas_.setTextDatum(TC_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT, UiTheme::BG);
    canvas_.drawString(appTitle(index), APP_LAYOUT[index].cx, y + ICON_SIZE + 9);

    if (index == 0) {
        canvas_.setTextColor(wifiConnected ? UiTheme::GREEN : UiTheme::TEXT_DIM, UiTheme::BG);
        canvas_.drawString(wifiConnected ? "Online" : "Offline", APP_LAYOUT[index].cx, y + ICON_SIZE + 22);
    }
    canvas_.setTextDatum(TL_DATUM);
}

void MenuUI::drawAppIcon(int index, int x, int y, int size, uint16_t accent) {
    uint16_t fg = UiTheme::BG;
    uint16_t shade = UiTheme::PANEL_2;
    int cx = x + size / 2;
    int cy = y + size / 2;

    canvas_.fillRoundRect(x, y, size, size, 13, accent);
    canvas_.drawRoundRect(x, y, size, size, 13, UiTheme::TEXT);
    canvas_.drawFastHLine(x + 10, y + 5, size - 20, UiTheme::TEXT);
    canvas_.fillCircle(x + size - 11, y + 10, 3, shade);

    switch (index) {
        case 0:
            canvas_.drawArc(cx, cy + 11, 22, 20, 218, 322, fg);
            canvas_.drawArc(cx, cy + 11, 15, 13, 224, 316, fg);
            canvas_.drawArc(cx, cy + 11, 8, 6, 232, 308, fg);
            canvas_.fillCircle(cx, cy + 18, 4, fg);
            break;
        case 1:
            canvas_.fillRoundRect(x + 9, y + 17, size - 18, 24, 5, fg);
            canvas_.fillRoundRect(x + 16, y + 12, 13, 7, 2, fg);
            canvas_.fillCircle(cx, cy + 4, 11, accent);
            canvas_.drawCircle(cx, cy + 4, 12, fg);
            canvas_.fillCircle(x + size - 14, y + 21, 3, accent);
            break;
        case 2:
            canvas_.drawCircle(cx, cy, 17, fg);
            canvas_.drawCircle(cx, cy, 12, fg);
            canvas_.drawLine(cx, cy, cx, cy - 12, fg);
            canvas_.drawLine(cx, cy, cx + 10, cy + 7, fg);
            canvas_.fillCircle(cx, cy, 3, fg);
            canvas_.drawFastHLine(cx - 9, y + 9, 18, fg);
            break;
        case 3:
            canvas_.fillRoundRect(cx + 4, y + 13, 7, 25, 3, fg);
            canvas_.drawFastHLine(cx + 8, y + 13, 15, fg);
            canvas_.drawFastVLine(cx + 22, y + 13, 9, fg);
            canvas_.fillCircle(cx - 4, y + 39, 8, fg);
            canvas_.fillCircle(cx + 15, y + 32, 8, fg);
            canvas_.fillCircle(cx - 4, y + 39, 4, accent);
            canvas_.fillCircle(cx + 15, y + 32, 4, accent);
            break;
        case 4:
            canvas_.drawCircle(cx, cy, 15, fg);
            canvas_.drawCircle(cx, cy, 6, fg);
            for (int i = 0; i < 8; ++i) {
                float angle = i * 0.785398f;
                int x1 = cx + static_cast<int>(cos(angle) * 17.0f);
                int y1 = cy + static_cast<int>(sin(angle) * 17.0f);
                int x2 = cx + static_cast<int>(cos(angle) * 22.0f);
                int y2 = cy + static_cast<int>(sin(angle) * 22.0f);
                canvas_.drawLine(x1, y1, x2, y2, fg);
            }
            break;
    }
}

void MenuUI::drawBackButton() {
    UiTheme::drawBackButton(canvas_, BACK_X, BACK_Y, BACK_W, BACK_H);
}
