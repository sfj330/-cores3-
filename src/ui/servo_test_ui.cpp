#include "servo_test_ui.h"
#include "ui/ui_theme.h"
#include <M5CoreS3.h>

namespace {
String signedValue(float value, int decimals) {
    String out;
    if (value >= 0.0f) {
        out += "+";
    }
    out += String(value, decimals);
    return out;
}

float clampUnit(float value) {
    if (value < -1.0f) return -1.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}
}

ServoTestUI::ServoTestUI() : canvas_(&M5.Lcd) {}

void ServoTestUI::begin() {
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    spriteReady_ = canvas_.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT) != nullptr;
    if (spriteReady_) {
        canvas_.fillSprite(UiTheme::BG);
    }
}

void ServoTestUI::show() {
    visible_ = true;
    dirty_ = true;
}

void ServoTestUI::hide() {
    visible_ = false;
}

void ServoTestUI::markDirty() {
    dirty_ = true;
}

void ServoTestUI::setTelemetry(bool ready, const String& status,
                               float ax, float ay, float az,
                               float filteredPan, float filteredTilt,
                               float panDeg, float tiltDeg,
                               bool released) {
    ready_ = ready;
    status_ = status;
    ax_ = ax;
    ay_ = ay;
    az_ = az;
    panBar_ = filteredPan;
    tiltBar_ = filteredTilt;
    panDeg_ = panDeg;
    tiltDeg_ = tiltDeg;
    released_ = released;
    dirty_ = true;
}

ServoHitZone ServoTestUI::hitTest(int x, int y) const {
    if (x >= BACK_X && x < BACK_X + BACK_W && y >= BACK_Y && y < BACK_Y + BACK_H) {
        return ServoHitZone::SERVO_HIT_BACK;
    }
    return ServoHitZone::SERVO_HIT_NONE;
}

void ServoTestUI::update() {
    if (!visible_ || !spriteReady_ || !dirty_) return;

    uint16_t accent = ready_ ? UiTheme::CYAN : UiTheme::RED;
    canvas_.fillSprite(UiTheme::BG);
    UiTheme::drawTitle(canvas_, "Servo Test", "Tilt -> PCA9685", accent);
    drawBackButton();
    drawServoIcon();

    uint16_t statusColor = ready_ ? (released_ ? UiTheme::AMBER : UiTheme::GREEN) : UiTheme::RED;
    UiTheme::drawStatusPill(canvas_, DISPLAY_WIDTH / 2 - 82, 50, 164,
                            status_.c_str(), statusColor, UiTheme::BG);

    drawRow(16, 82, "Pan ch", String(SERVO_PAN_CHANNEL) + "  " + String(panDeg_, 0) + " deg", UiTheme::CYAN);
    drawRow(166, 82, "Tilt ch", String(SERVO_TILT_CHANNEL) + "  " + String(tiltDeg_, 0) + " deg", UiTheme::AMBER);

    drawBar(20, 118, 130, "X -> Pan", panBar_, UiTheme::CYAN);
    drawBar(170, 118, 130, "Y -> Tilt", tiltBar_, UiTheme::AMBER);

    drawRow(20, 158, "Accel X", signedValue(ax_, 2) + " g", UiTheme::CYAN);
    drawRow(170, 158, "Accel Y", signedValue(ay_, 2) + " g", UiTheme::AMBER);
    drawRow(20, 188, "Accel Z", signedValue(az_, 2) + " g", UiTheme::TEXT_DIM);
    drawRow(170, 188, "Touch", released_ ? "Tap center" : "Hold release", UiTheme::BLUE);

    canvas_.pushSprite(0, 0);
    dirty_ = false;
}

void ServoTestUI::drawBackButton() {
    UiTheme::drawBackButton(canvas_, BACK_X, BACK_Y, BACK_W, BACK_H);
}

void ServoTestUI::drawServoIcon() {
    int cx = DISPLAY_WIDTH / 2;
    int y = 35;
    canvas_.drawFastHLine(cx - 26, y, 52, UiTheme::PANEL_LIGHT);
    canvas_.fillCircle(cx, y, 5, UiTheme::CYAN);
    canvas_.drawLine(cx, y, cx - 20, y - 10, UiTheme::TEXT_DIM);
    canvas_.drawLine(cx, y, cx + 20, y - 10, UiTheme::TEXT_DIM);
    canvas_.fillCircle(cx - 20, y - 10, 2, UiTheme::TEXT_DIM);
    canvas_.fillCircle(cx + 20, y - 10, 2, UiTheme::TEXT_DIM);
}

void ServoTestUI::drawRow(int x, int y, const char* label, const String& value, uint16_t accent) {
    String displayValue = value;
    if (displayValue.length() > 15) {
        displayValue = displayValue.substring(0, 12) + "...";
    }

    canvas_.fillRoundRect(x, y, 134, 22, 6, UiTheme::PANEL);
    canvas_.drawFastVLine(x + 6, y + 5, 12, accent);
    canvas_.setTextDatum(TL_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT_DIM, UiTheme::PANEL);
    canvas_.setCursor(x + 14, y + 7);
    canvas_.print(label);
    canvas_.setTextColor(UiTheme::TEXT, UiTheme::PANEL);
    canvas_.setCursor(x + 68, y + 7);
    canvas_.print(displayValue);
}

void ServoTestUI::drawBar(int x, int y, int w, const char* label, float value, uint16_t accent) {
    float v = clampUnit(value);
    int h = 24;
    int mid = x + w / 2;
    int fill = static_cast<int>((w / 2 - 4) * v);

    canvas_.setTextDatum(TL_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT_DIM, UiTheme::BG);
    canvas_.drawString(label, x, y - 13);

    canvas_.fillRoundRect(x, y, w, h, 6, UiTheme::PANEL);
    canvas_.drawFastVLine(mid, y + 4, h - 8, UiTheme::PANEL_LIGHT);
    if (fill >= 0) {
        canvas_.fillRoundRect(mid, y + 6, fill, h - 12, 4, accent);
    } else {
        canvas_.fillRoundRect(mid + fill, y + 6, -fill, h - 12, 4, accent);
    }
    canvas_.drawRoundRect(x, y, w, h, 6, UiTheme::PANEL_LIGHT);
}
