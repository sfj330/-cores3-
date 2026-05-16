#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include "config/app_config.h"

enum class ServoHitZone {
    SERVO_HIT_NONE,
    SERVO_HIT_BACK
};

class ServoTestUI {
public:
    ServoTestUI();
    void begin();
    void show();
    void hide();
    void update();
    void markDirty();

    void setTelemetry(bool ready, const String& status,
                      float ax, float ay, float az,
                      float filteredPan, float filteredTilt,
                      float panDeg, float tiltDeg,
                      bool released);

    ServoHitZone hitTest(int x, int y) const;

private:
    void drawBackButton();
    void drawServoIcon();
    void drawRow(int x, int y, const char* label, const String& value, uint16_t accent);
    void drawBar(int x, int y, int w, const char* label, float value, uint16_t accent);

    M5Canvas canvas_;
    bool spriteReady_ = false;
    bool visible_ = false;
    bool dirty_ = true;

    bool ready_ = false;
    bool released_ = true;
    String status_ = "Servo idle";
    float ax_ = 0.0f;
    float ay_ = 0.0f;
    float az_ = 0.0f;
    float panBar_ = 0.0f;
    float tiltBar_ = 0.0f;
    float panDeg_ = 90.0f;
    float tiltDeg_ = 90.0f;

    static constexpr int BACK_X = 5;
    static constexpr int BACK_Y = 5;
    static constexpr int BACK_W = 74;
    static constexpr int BACK_H = 26;
};
