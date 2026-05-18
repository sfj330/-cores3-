#pragma once

#include <Arduino.h>
#include <cstdint>
#include "config/app_config.h"
#include "servo/servo_controller.h"

enum class ServoMotionAction : uint8_t {
    NONE = 0,
    CENTER,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    NOD,
    SHAKE,
    DANCE,
    RELEASE
};

class ServoMotionController {
public:
    void attach(ServoController& servo);

    bool ensureReady(unsigned long now = millis());
    bool isReady() const;
    bool isReleased() const;
    bool isDanceActive() const;
    const String& statusText() const;

    void update(unsigned long now);
    void suspend();
    void keepAlive(unsigned long now);

    bool command(ServoMotionAction action);
    bool release();
    void center(float speedDps = SERVO_FACE_MOTION_SPEED_DPS);
    void lookOffset(float panOffsetDeg, float tiltOffsetDeg,
                    float speedDps = SERVO_FACE_MOTION_SPEED_DPS,
                    const char* status = "Servo pose");
    void nudge(float panDeltaDeg, float tiltDeltaDeg,
               float speedDps = SERVO_TRACKING_MOTION_SPEED_DPS,
               const char* status = "Face tracking");

    float targetPan() const;
    float targetTilt() const;

private:
    enum class Sequence : uint8_t {
        NONE = 0,
        NOD,
        SHAKE,
        DANCE
    };

    float clampAngle(float angleDeg) const;
    float applyPanOffset(float offsetDeg) const;
    float applyTiltOffset(float offsetDeg) const;
    void setActualTarget(float panDeg, float tiltDeg, float speedDps, const char* status);
    void startSequence(Sequence sequence, const char* status,
                       float speedDps = SERVO_FACE_MOTION_SPEED_DPS);
    void advanceSequence(unsigned long now);
    void syncFromServo();

    ServoController* servo_ = nullptr;
    bool active_ = false;
    bool currentInitialized_ = false;

    float currentPan_ = static_cast<float>(SERVO_PAN_CENTER_DEG);
    float currentTilt_ = static_cast<float>(SERVO_TILT_CENTER_DEG);
    float targetPan_ = static_cast<float>(SERVO_PAN_CENTER_DEG);
    float targetTilt_ = static_cast<float>(SERVO_TILT_CENTER_DEG);
    float speedDps_ = SERVO_FACE_MOTION_SPEED_DPS;

    Sequence sequence_ = Sequence::NONE;
    uint8_t sequenceStep_ = 0;
    unsigned long nextSequenceStepAt_ = 0;
    unsigned long lastUpdate_ = 0;
    unsigned long lastBeginAttempt_ = 0;
    unsigned long lastKeepAlive_ = 0;

    String status_ = "Servo motion idle";
};
