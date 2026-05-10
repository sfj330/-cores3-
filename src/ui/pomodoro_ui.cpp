#include "pomodoro_ui.h"
#include "config/app_config.h"
#include "ui/ui_theme.h"
#include <M5CoreS3.h>
#include <cmath>
#include <utility>

namespace {
constexpr uint16_t COLOR_FOCUS = UiTheme::CYAN;
constexpr uint16_t COLOR_SHORT = UiTheme::GREEN;
constexpr uint16_t COLOR_LONG = UiTheme::PINK;
constexpr uint16_t COLOR_DEEP = UiTheme::AMBER;
}

PomodoroUI::PomodoroUI() : canvas_(&M5.Lcd) {}

bool PomodoroUI::ensureSprite() {
    int w = M5.Lcd.width();
    int h = M5.Lcd.height();
    if (spriteReady_ && canvas_.width() == w && canvas_.height() == h) {
        return true;
    }

    if (spriteReady_) {
        canvas_.deleteSprite();
        spriteReady_ = false;
    }

    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    spriteReady_ = canvas_.createSprite(w, h) != nullptr;
    if (spriteReady_) {
        canvas_.fillSprite(UiTheme::BG);
    }
    dirty_ = true;
    return spriteReady_;
}

void PomodoroUI::begin() {
    baseRotation_ = M5.Lcd.getRotation() & 3;
    appliedRotation_ = baseRotation_;
    ensureSprite();
}

void PomodoroUI::show() {
    visible_ = true;
    baseRotation_ = M5.Lcd.getRotation() & 3;
    appliedRotation_ = baseRotation_;
    currentOrientation_ = PomoOrientation::UNKNOWN;
    ensureSprite();
    reset();
    dirty_ = true;
}

void PomodoroUI::hide() {
    visible_ = false;
    restoreDisplayRotation();
}

void PomodoroUI::restoreDisplayRotation() {
    if ((M5.Lcd.getRotation() & 3) != baseRotation_) {
        M5.Lcd.setRotation(baseRotation_);
    }
    appliedRotation_ = baseRotation_;
}

bool PomodoroUI::isVisible() const {
    return visible_;
}

PomodoroState PomodoroUI::getState() const {
    return state_;
}

int PomodoroUI::getActivePreset() const {
    return activePreset_;
}

void PomodoroUI::selectPreset(int index) {
    if (selectionLocked_) {
        return;
    }
    if (index >= 0 && index < PRESET_COUNT && index != activePreset_) {
        activePreset_ = index;
        dirty_ = true;
    }
}

uint8_t PomodoroUI::rotationForOrientation(PomoOrientation o) const {
    uint8_t offset = 0;
    switch (o) {
        case PomoOrientation::RIGHT: offset = 1; break;
        case PomoOrientation::BOTTOM: offset = 2; break;
        case PomoOrientation::LEFT: offset = 3; break;
        case PomoOrientation::TOP:
        default: offset = 0; break;
    }
    return (baseRotation_ + offset) & 3;
}

int PomodoroUI::presetIndexForOrientation(PomoOrientation o) const {
    for (int i = 0; i < PRESET_COUNT; ++i) {
        if (presets_[i].orientation == o) {
            return i;
        }
    }
    return -1;
}

void PomodoroUI::syncPresetToCurrentOrientation() {
    int index = presetIndexForOrientation(currentOrientation_);
    if (index >= 0) {
        selectPreset(index);
    }
}

uint16_t PomodoroUI::accentColor(int index) const {
    switch (index) {
        case 0: return COLOR_FOCUS;
        case 1: return COLOR_SHORT;
        case 2: return COLOR_LONG;
        case 3: return COLOR_DEEP;
        default: return UiTheme::TEXT;
    }
}

uint16_t PomodoroUI::activeAccentColor() const {
    return accentColor(activePreset_);
}

const char* PomodoroUI::shortPresetLabel(int index) const {
    switch (index) {
        case 0: return "25m";
        case 1: return "5m";
        case 2: return "15m";
        case 3: return "50m";
        default: return "--";
    }
}

const char* PomodoroUI::statusText() const {
    switch (state_) {
        case PomodoroState::RUNNING: return paused_ ? "PAUSED" : "FOCUSING";
        case PomodoroState::RINGING: return "DONE";
        default: return "READY";
    }
}

void PomodoroUI::applyDisplayRotation(PomoOrientation o) {
    uint8_t nextRotation = rotationForOrientation(o);
    if (nextRotation != appliedRotation_) {
        M5.Lcd.setRotation(nextRotation);
        appliedRotation_ = nextRotation;
    }
    ensureSprite();
    dirty_ = true;
}

void PomodoroUI::setOrientation(PomoOrientation o) {
    if (o == PomoOrientation::UNKNOWN || o == PomoOrientation::FLAT || o == currentOrientation_) {
        return;
    }

    currentOrientation_ = o;
    applyDisplayRotation(o);

    if (!selectionLocked_) {
        syncPresetToCurrentOrientation();
    }
}

void PomodoroUI::togglePause() {
    if (state_ == PomodoroState::IDLE) {
        state_ = PomodoroState::RUNNING;
        elapsed_ = 0;
        lastTick_ = millis();
        paused_ = false;
        selectionLocked_ = true;
        completionNotified_ = false;
    } else if (state_ == PomodoroState::RINGING) {
        reset();
    } else {
        paused_ = !paused_;
        if (!paused_) {
            lastTick_ = millis();
        }
    }
    dirty_ = true;
}

void PomodoroUI::reset() {
    state_ = PomodoroState::IDLE;
    elapsed_ = 0;
    paused_ = false;
    selectionLocked_ = false;
    completionNotified_ = false;
    syncPresetToCurrentOrientation();
    dirty_ = true;
}

void PomodoroUI::setCompleteCallback(std::function<void(int)> cb) {
    completeCallback_ = std::move(cb);
}

void PomodoroUI::markDirty() {
    dirty_ = true;
}

PomoHitZone PomodoroUI::hitTest(int x, int y) const {
    int w = M5.Lcd.width();
    int h = M5.Lcd.height();
    int btnY = h - 42;
    int startX = w / 4 - BTN_W / 2;
    int resetX = (w * 3) / 4 - BTN_W / 2;
    if (startX < 8) startX = 8;
    if (resetX + BTN_W > w - 8) resetX = w - BTN_W - 8;

    if (x >= BACK_X && x < BACK_X + BACK_W && y >= BACK_Y && y < BACK_Y + BACK_H) {
        return PomoHitZone::POMO_HIT_BACK;
    }
    if (y >= btnY && y < btnY + BTN_H) {
        if (x >= startX && x < startX + BTN_W) return PomoHitZone::POMO_HIT_START;
        if (x >= resetX && x < resetX + BTN_W) return PomoHitZone::POMO_HIT_RESET;
    }
    return PomoHitZone::POMO_HIT_NONE;
}

void PomodoroUI::update() {
    if (!visible_) return;
    if (!ensureSprite()) return;

    unsigned long now = millis();

    if (!paused_ && state_ == PomodoroState::RUNNING) {
        elapsed_ += now - lastTick_;
        lastTick_ = now;

        unsigned long duration = presets_[activePreset_].durationMs;
        if (elapsed_ >= duration) {
            state_ = PomodoroState::RINGING;
            ringStart_ = now;
            elapsed_ = duration;
            if (!completionNotified_) {
                completionNotified_ = true;
                if (completeCallback_) {
                    completeCallback_(activePreset_);
                }
            }
            dirty_ = true;
        }
    }

    if (state_ == PomodoroState::RINGING && (now - ringStart_ > POMODORO_RING_MS)) {
        reset();
    }

    if (!dirty_ && (now - lastDrawTime_ < REDRAW_INTERVAL_MS)) return;

    drawBackground();
    drawPresetSelector();
    drawProgressRing();
    drawTimer();
    drawControls();
    drawBackButton();

    if (state_ == PomodoroState::RINGING) {
        drawNotification();
    }

    canvas_.pushSprite(0, 0);
    lastDrawTime_ = now;
    dirty_ = false;
}

void PomodoroUI::drawBackground() {
    int w = canvas_.width();
    int h = canvas_.height();
    uint16_t accent = activeAccentColor();

    canvas_.fillSprite(UiTheme::BG);
    canvas_.fillRect(0, 0, w, 35, UiTheme::PANEL);
    canvas_.drawFastHLine(0, 35, w, accent);

    canvas_.setTextDatum(TC_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT, UiTheme::PANEL);
    canvas_.drawString("Pomodoro", w / 2, 9);

    int pillW = 64;
    int pillX = w - pillW - 8;
    if (pillX > BACK_X + BACK_W + 6) {
        UiTheme::drawStatusPill(canvas_, pillX, 7, pillW, statusText(), accent);
    }

    canvas_.setTextDatum(TL_DATUM);
}

void PomodoroUI::drawPresetSelector() {
    int w = canvas_.width();
    int boxGap = 5;
    int startX = 8;
    int boxW = (w - startX * 2 - boxGap * (PRESET_COUNT - 1)) / PRESET_COUNT;
    if (boxW < 46) boxW = 46;
    int boxH = 31;
    int y = 43;

    canvas_.setTextSize(1);
    canvas_.setTextDatum(MC_DATUM);
    for (int i = 0; i < PRESET_COUNT; ++i) {
        int x = startX + i * (boxW + boxGap);
        uint16_t accent = accentColor(i);
        uint16_t bgColor = (i == activePreset_) ? accent : UiTheme::PANEL;
        uint16_t txtColor = (i == activePreset_) ? UiTheme::BG : UiTheme::TEXT_DIM;

        canvas_.fillRoundRect(x, y, boxW, boxH, 6, bgColor);
        if (i != activePreset_) {
            canvas_.drawRoundRect(x, y, boxW, boxH, 6, UiTheme::PANEL_LIGHT);
            canvas_.fillCircle(x + boxW / 2, y + 6, 2, accent);
        }
        canvas_.setTextColor(txtColor, bgColor);
        canvas_.drawString(shortPresetLabel(i), x + boxW / 2, y + 18);
    }
    canvas_.setTextDatum(TL_DATUM);
}

void PomodoroUI::drawTimer() {
    int w = canvas_.width();
    int h = canvas_.height();
    unsigned long duration = presets_[activePreset_].durationMs;
    unsigned long remaining = elapsed_ >= duration ? 0 : duration - elapsed_;
    int minutes = (remaining / 60000) % 60;
    int seconds = (remaining / 1000) % 60;
    int cy = h / 2 + (h < 260 ? 10 : 18);

    canvas_.setTextDatum(MC_DATUM);
    canvas_.setTextColor(UiTheme::TEXT_DIM, UiTheme::BG);
    canvas_.setTextSize(1);
    canvas_.drawString(presets_[activePreset_].label, w / 2, cy - 36);

    canvas_.setTextColor(UiTheme::TEXT, UiTheme::BG);
    canvas_.setTextSize(4);
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", minutes, seconds);
    canvas_.drawString(timeBuf, w / 2, cy);

    canvas_.setTextSize(1);
    canvas_.setTextColor(activeAccentColor(), UiTheme::BG);
    canvas_.drawString(statusText(), w / 2, cy + 38);

    canvas_.setTextColor(UiTheme::TEXT_DIM, UiTheme::BG);
    canvas_.drawString(ImuOrientation::orientationName(currentOrientation_), w / 2, cy + 53);
    canvas_.setTextDatum(TL_DATUM);
}

void PomodoroUI::drawProgressRing() {
    int w = canvas_.width();
    int h = canvas_.height();
    int cx = w / 2;
    int cy = h / 2 + (h < 260 ? 10 : 18);
    int r = (w < h ? w : h) / 5;
    if (r < 42) r = 42;
    if (r > 56) r = 56;

    unsigned long duration = presets_[activePreset_].durationMs;
    float progress = (duration > 0) ? (float)elapsed_ / duration : 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    canvas_.fillCircle(cx, cy, r + 8, UiTheme::PANEL);
    canvas_.drawCircle(cx, cy, r + 8, UiTheme::PANEL_LIGHT);
    canvas_.drawCircle(cx, cy, r - 16, UiTheme::PANEL_LIGHT);

    int segments = 72;
    int litSegments = (int)(progress * segments + 0.5f);
    for (int i = 0; i < segments; ++i) {
        float angle = -PI / 2 + (i / (float)segments) * 2 * PI;
        int x = cx + (int)(r * cos(angle));
        int y = cy + (int)(r * sin(angle));
        canvas_.fillCircle(x, y, 2, UiTheme::PANEL_LIGHT);
    }
    for (int i = 0; i < litSegments; ++i) {
        float angle = -PI / 2 + (i / (float)segments) * 2 * PI;
        int x = cx + (int)(r * cos(angle));
        int y = cy + (int)(r * sin(angle));
        canvas_.fillCircle(x, y, 3, activeAccentColor());
    }
}

void PomodoroUI::drawControls() {
    int w = canvas_.width();
    int h = canvas_.height();
    int btnY = h - 42;
    int startX = w / 4 - BTN_W / 2;
    int resetX = (w * 3) / 4 - BTN_W / 2;
    if (startX < 8) startX = 8;
    if (resetX + BTN_W > w - 8) resetX = w - BTN_W - 8;

    canvas_.setTextSize(1);
    canvas_.setTextDatum(TL_DATUM);

    uint16_t startColor = (state_ == PomodoroState::IDLE || paused_) ? activeAccentColor() : UiTheme::PANEL_LIGHT;
    const char* label = (state_ == PomodoroState::IDLE || paused_) ? "START" : "PAUSE";

    canvas_.fillRoundRect(startX, btnY, BTN_W, BTN_H, 6, startColor);
    canvas_.drawRoundRect(startX, btnY, BTN_W, BTN_H, 6, UiTheme::TEXT);
    canvas_.setTextColor(UiTheme::BG, startColor);
    canvas_.setTextDatum(MC_DATUM);
    canvas_.drawString(label, startX + BTN_W / 2, btnY + BTN_H / 2 + 1);

    canvas_.fillRoundRect(resetX, btnY, BTN_W, BTN_H, 6, UiTheme::PANEL);
    canvas_.drawRoundRect(resetX, btnY, BTN_W, BTN_H, 6, UiTheme::RED);
    canvas_.setTextColor(UiTheme::RED, UiTheme::PANEL);
    canvas_.drawString("RESET", resetX + BTN_W / 2, btnY + BTN_H / 2 + 1);
    canvas_.setTextDatum(TL_DATUM);
}

void PomodoroUI::drawBackButton() {
    UiTheme::drawBackButton(canvas_, BACK_X, BACK_Y, BACK_W, BACK_H);
}

void PomodoroUI::drawNotification() {
    int w = canvas_.width();
    int h = canvas_.height();
    uint16_t accent = activeAccentColor();
    canvas_.fillRoundRect(w / 2 - 92, h / 2 - 34, 184, 68, 8, UiTheme::PANEL);
    canvas_.drawRoundRect(w / 2 - 92, h / 2 - 34, 184, 68, 8, accent);
    canvas_.setTextColor(accent, UiTheme::PANEL);
    canvas_.setTextSize(2);
    canvas_.setTextDatum(MC_DATUM);
    canvas_.drawString("TIME'S UP", w / 2, h / 2 - 8);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT, UiTheme::PANEL);
    canvas_.drawString("SESSION DONE", w / 2, h / 2 + 18);
    canvas_.setTextDatum(TL_DATUM);
}
