#include "servo/servo_motion_controller.h"
#include "config/app_config.h"
#include <cmath>

void ServoMotionController::attach(ServoController& servo) {
    servo_ = &servo;
    syncFromServo();
}

bool ServoMotionController::ensureReady(unsigned long now) {
    if (servo_ == nullptr) {
        status_ = "Servo controller missing";
        return false;
    }
    if (servo_->isReady()) {
        syncFromServo();
        return true;
    }
    if (lastBeginAttempt_ != 0 && now - lastBeginAttempt_ < SERVO_MOTION_BEGIN_RETRY_MS) {
        status_ = servo_->statusText();
        return false;
    }

    lastBeginAttempt_ = now;
    bool ok = servo_->begin();
    status_ = servo_->statusText();
    if (ok) {
        currentPan_ = servo_->panAngle();
        currentTilt_ = servo_->tiltAngle();
        currentInitialized_ = true;
    }
    return ok;
}

bool ServoMotionController::isReady() const {
    return servo_ != nullptr && servo_->isReady();
}

bool ServoMotionController::isReleased() const {
    return servo_ == nullptr || servo_->isReleased();
}

const String& ServoMotionController::statusText() const {
    return status_;
}

void ServoMotionController::update(unsigned long now) {
    if (!active_ && sequence_ == Sequence::NONE) return;
    if (!ensureReady(now)) return;

    advanceSequence(now);

    if (lastUpdate_ != 0 && now - lastUpdate_ < SERVO_MOTION_UPDATE_MS) return;

    float dtSec = SERVO_MOTION_UPDATE_MS / 1000.0f;
    if (lastUpdate_ != 0) {
        dtSec = (now - lastUpdate_) / 1000.0f;
        if (dtSec <= 0.0f) {
            dtSec = SERVO_MOTION_UPDATE_MS / 1000.0f;
        }
    }
    lastUpdate_ = now;

    syncFromServo();

    float maxDelta = speedDps_ * dtSec;
    float panDelta = targetPan_ - currentPan_;
    float tiltDelta = targetTilt_ - currentTilt_;

    if (panDelta > maxDelta) panDelta = maxDelta;
    if (panDelta < -maxDelta) panDelta = -maxDelta;
    if (tiltDelta > maxDelta) tiltDelta = maxDelta;
    if (tiltDelta < -maxDelta) tiltDelta = -maxDelta;

    currentPan_ = clampAngle(currentPan_ + panDelta);
    currentTilt_ = clampAngle(currentTilt_ + tiltDelta);

    if (servo_ != nullptr) {
        bool needWrite = fabsf(panDelta) > 0.05f ||
                         fabsf(tiltDelta) > 0.05f ||
                         servo_->isReleased();
        if (needWrite) {
            servo_->setPanTilt(currentPan_, currentTilt_);
        }
    }
}

void ServoMotionController::suspend() {
    active_ = false;
    sequence_ = Sequence::NONE;
    sequenceStep_ = 0;
    nextSequenceStepAt_ = 0;
    lastUpdate_ = 0;
    status_ = "Servo motion suspended";
}

bool ServoMotionController::command(ServoMotionAction action) {
    switch (action) {
        case ServoMotionAction::CENTER:
            center();
            return true;
        case ServoMotionAction::LEFT:
            lookOffset(-SERVO_FACE_PAN_OFFSET_DEG, 0.0f, SERVO_FACE_MOTION_SPEED_DPS, "Look left");
            return true;
        case ServoMotionAction::RIGHT:
            lookOffset(SERVO_FACE_PAN_OFFSET_DEG, 0.0f, SERVO_FACE_MOTION_SPEED_DPS, "Look right");
            return true;
        case ServoMotionAction::UP:
            lookOffset(0.0f, -SERVO_FACE_SMALL_TILT_DEG, SERVO_FACE_MOTION_SPEED_DPS, "Look up");
            return true;
        case ServoMotionAction::DOWN:
            lookOffset(0.0f, SERVO_FACE_TILT_OFFSET_DEG, SERVO_FACE_MOTION_SPEED_DPS, "Look down");
            return true;
        case ServoMotionAction::NOD:
            startSequence(Sequence::NOD, "Nod");
            return true;
        case ServoMotionAction::SHAKE:
            startSequence(Sequence::SHAKE, "Shake");
            return true;
        case ServoMotionAction::RELEASE:
            return release();
        default:
            status_ = "Unknown servo action";
            return false;
    }
}

bool ServoMotionController::release() {
    active_ = false;
    sequence_ = Sequence::NONE;
    sequenceStep_ = 0;
    nextSequenceStepAt_ = 0;
    if (!ensureReady(millis())) {
        return false;
    }
    bool ok = servo_ != nullptr && servo_->release();
    status_ = ok ? "PWM released" : "PWM release failed";
    return ok;
}

void ServoMotionController::center(float speedDps) {
    setActualTarget(static_cast<float>(SERVO_PAN_CENTER_DEG),
                    static_cast<float>(SERVO_TILT_CENTER_DEG),
                    speedDps,
                    "Servo center");
}

void ServoMotionController::lookOffset(float panOffsetDeg, float tiltOffsetDeg,
                                       float speedDps, const char* status) {
    setActualTarget(clampAngle(static_cast<float>(SERVO_PAN_CENTER_DEG) + applyPanOffset(panOffsetDeg)),
                    clampAngle(static_cast<float>(SERVO_TILT_CENTER_DEG) + applyTiltOffset(tiltOffsetDeg)),
                    speedDps,
                    status);
}

void ServoMotionController::nudge(float panDeltaDeg, float tiltDeltaDeg,
                                  float speedDps, const char* status) {
    if (!currentInitialized_) {
        targetPan_ = static_cast<float>(SERVO_PAN_CENTER_DEG);
        targetTilt_ = static_cast<float>(SERVO_TILT_CENTER_DEG);
    }
    setActualTarget(clampAngle(targetPan_ + applyPanOffset(panDeltaDeg)),
                    clampAngle(targetTilt_ + applyTiltOffset(tiltDeltaDeg)),
                    speedDps,
                    status);
}

float ServoMotionController::targetPan() const {
    return targetPan_;
}

float ServoMotionController::targetTilt() const {
    return targetTilt_;
}

float ServoMotionController::clampAngle(float angleDeg) const {
    if (angleDeg < SERVO_SAFE_MIN_DEG) return static_cast<float>(SERVO_SAFE_MIN_DEG);
    if (angleDeg > SERVO_SAFE_MAX_DEG) return static_cast<float>(SERVO_SAFE_MAX_DEG);
    return angleDeg;
}

float ServoMotionController::applyPanOffset(float offsetDeg) const {
    return SERVO_PAN_INVERT ? -offsetDeg : offsetDeg;
}

float ServoMotionController::applyTiltOffset(float offsetDeg) const {
    return SERVO_TILT_INVERT ? -offsetDeg : offsetDeg;
}

void ServoMotionController::setActualTarget(float panDeg, float tiltDeg,
                                            float speedDps, const char* status) {
    syncFromServo();
    targetPan_ = clampAngle(panDeg);
    targetTilt_ = clampAngle(tiltDeg);
    speedDps_ = speedDps > 1.0f ? speedDps : SERVO_FACE_MOTION_SPEED_DPS;
    active_ = true;
    if (status != nullptr) {
        status_ = status;
    }
}

void ServoMotionController::startSequence(Sequence sequence, const char* status) {
    syncFromServo();
    sequence_ = sequence;
    sequenceStep_ = 0;
    nextSequenceStepAt_ = 0;
    active_ = true;
    speedDps_ = SERVO_FACE_MOTION_SPEED_DPS;
    if (status != nullptr) {
        status_ = status;
    }
}

void ServoMotionController::advanceSequence(unsigned long now) {
    if (sequence_ == Sequence::NONE) return;
    if (nextSequenceStepAt_ != 0 && now < nextSequenceStepAt_) return;

    switch (sequence_) {
        case Sequence::NOD:
            if (sequenceStep_ == 0) {
                lookOffset(0.0f, -SERVO_FACE_SMALL_TILT_DEG, SERVO_FACE_MOTION_SPEED_DPS, "Nod up");
                nextSequenceStepAt_ = now + 450;
            } else if (sequenceStep_ == 1) {
                lookOffset(0.0f, SERVO_FACE_TILT_OFFSET_DEG, SERVO_FACE_MOTION_SPEED_DPS, "Nod down");
                nextSequenceStepAt_ = now + 500;
            } else {
                center(SERVO_FACE_MOTION_SPEED_DPS);
                sequence_ = Sequence::NONE;
            }
            sequenceStep_++;
            break;
        case Sequence::SHAKE:
            if (sequenceStep_ == 0) {
                lookOffset(-SERVO_FACE_PAN_OFFSET_DEG, 0.0f, SERVO_FACE_MOTION_SPEED_DPS, "Shake left");
                nextSequenceStepAt_ = now + 420;
            } else if (sequenceStep_ == 1) {
                lookOffset(SERVO_FACE_PAN_OFFSET_DEG, 0.0f, SERVO_FACE_MOTION_SPEED_DPS, "Shake right");
                nextSequenceStepAt_ = now + 520;
            } else if (sequenceStep_ == 2) {
                lookOffset(-SERVO_FACE_PAN_OFFSET_DEG * 0.6f, 0.0f, SERVO_FACE_MOTION_SPEED_DPS, "Shake left");
                nextSequenceStepAt_ = now + 360;
            } else {
                center(SERVO_FACE_MOTION_SPEED_DPS);
                sequence_ = Sequence::NONE;
            }
            sequenceStep_++;
            break;
        default:
            sequence_ = Sequence::NONE;
            break;
    }
}

void ServoMotionController::syncFromServo() {
    if (currentInitialized_ || servo_ == nullptr || !servo_->isReady()) return;
    currentPan_ = servo_->panAngle();
    currentTilt_ = servo_->tiltAngle();
    targetPan_ = currentPan_;
    targetTilt_ = currentTilt_;
    currentInitialized_ = true;
}
