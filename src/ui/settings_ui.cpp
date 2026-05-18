#include "settings_ui.h"
#include "ui/ui_theme.h"
#include <M5CoreS3.h>

static constexpr int BACK_X = 5;
static constexpr int BACK_Y = 5;
static constexpr int BACK_W = 74;
static constexpr int BACK_H = 26;

static constexpr int ROW_X = 20;
static constexpr int BTN_W = 80;
static constexpr int BTN_H = 36;
static constexpr int BTN_GAP = 10;

static constexpr int BRIGHT_ROW_Y = 60;
static constexpr int VOLUME_ROW_Y = 145;

SettingsUI::SettingsUI() : canvas_(&M5.Lcd) {}

void SettingsUI::begin() {
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    spriteReady_ = canvas_.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT) != nullptr;
}

void SettingsUI::show() {
    visible_ = true;
    dirty_ = true;
}

void SettingsUI::hide() {
    visible_ = false;
}

void SettingsUI::markDirty() {
    dirty_ = true;
}

void SettingsUI::setBrightness(int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    if (brightnessLevel_ != level) {
        brightnessLevel_ = level;
        dirty_ = true;
    }
}

void SettingsUI::setVolume(int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    if (volumeLevel_ != level) {
        volumeLevel_ = level;
        dirty_ = true;
    }
}

void SettingsUI::update() {
    if (!visible_ || !spriteReady_ || !dirty_) return;
    dirty_ = false;

    canvas_.fillSprite(UiTheme::BG);

    UiTheme::drawTitle(canvas_, "Settings", nullptr);
    drawBackButton();
    drawBrightnessRow();
    drawVolumeRow();

    canvas_.pushSprite(0, 0);
}

void SettingsUI::drawBackButton() {
    UiTheme::drawBackButton(canvas_, BACK_X, BACK_Y, BACK_W, BACK_H);
}

void SettingsUI::drawBrightnessRow() {
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT, UiTheme::BG);
    canvas_.setTextDatum(TL_DATUM);
    canvas_.drawString("Brightness", ROW_X, BRIGHT_ROW_Y - 18);

    const char* labels[] = {"Low", "Mid", "High"};
    drawSliderBar(ROW_X, BRIGHT_ROW_Y, BTN_W, brightnessLevel_, 3, labels, UiTheme::AMBER);
}

void SettingsUI::drawVolumeRow() {
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT, UiTheme::BG);
    canvas_.setTextDatum(TL_DATUM);
    canvas_.drawString("Volume", ROW_X, VOLUME_ROW_Y - 18);

    const char* labels[] = {"Low", "Mid", "High"};
    drawSliderBar(ROW_X, VOLUME_ROW_Y, BTN_W, volumeLevel_, 3, labels, UiTheme::CYAN);
}

void SettingsUI::drawSliderBar(int x, int y, int w, int activeIndex, int count,
                               const char* labels[], uint16_t color) {
    for (int i = 0; i < count; ++i) {
        int bx = x + i * (w + BTN_GAP);
        bool active = (i == activeIndex);
        uint16_t bg = active ? color : UiTheme::PANEL;
        uint16_t fg = active ? UiTheme::BG : UiTheme::TEXT_DIM;

        canvas_.fillRoundRect(bx, y, w, BTN_H, 6, bg);
        canvas_.setTextDatum(MC_DATUM);
        canvas_.setTextColor(fg, bg);
        canvas_.drawString(labels[i], bx + w / 2, y + BTN_H / 2);
    }
    canvas_.setTextDatum(TL_DATUM);
}

SettingsHitZone SettingsUI::hitTest(int x, int y) const {
    if (x >= BACK_X && x <= BACK_X + BACK_W && y >= BACK_Y && y <= BACK_Y + BACK_H) {
        return SettingsHitZone::BACK;
    }

    if (y >= BRIGHT_ROW_Y && y <= BRIGHT_ROW_Y + BTN_H) {
        for (int i = 0; i < 3; ++i) {
            int bx = ROW_X + i * (BTN_W + BTN_GAP);
            if (x >= bx && x <= bx + BTN_W) {
                return static_cast<SettingsHitZone>(
                    static_cast<int>(SettingsHitZone::BRIGHTNESS_DIM) + i);
            }
        }
    }

    if (y >= VOLUME_ROW_Y && y <= VOLUME_ROW_Y + BTN_H) {
        for (int i = 0; i < 3; ++i) {
            int bx = ROW_X + i * (BTN_W + BTN_GAP);
            if (x >= bx && x <= bx + BTN_W) {
                return static_cast<SettingsHitZone>(
                    static_cast<int>(SettingsHitZone::VOLUME_QUIET) + i);
            }
        }
    }

    return SettingsHitZone::NONE;
}
