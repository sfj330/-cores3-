#include "imu_orientation.h"
#include <M5CoreS3.h>
#include <Arduino.h>
#include <cmath>

ImuOrientation::ImuOrientation() = default;

bool ImuOrientation::begin() {
    return true;
}

void ImuOrientation::update() {
    M5.Imu.update();

    float ax = 0, ay = 0, az = 0;
    M5.Imu.getAccel(&ax, &ay, &az);

    float gx = 0, gy = 0, gz = 0;
    M5.Imu.getGyro(&gx, &gy, &gz);

    updateShakeDetection(ax, ay, az);
    updateTwistDetection(gz);

    PomoOrientation detected = PomoOrientation::UNKNOWN;

    float absX = fabsf(ax);
    float absY = fabsf(ay);
    float absZ = fabsf(az);

    if (absZ > TILT_THRESHOLD && absZ > absX && absZ > absY) {
        detected = PomoOrientation::FLAT;
    } else if (absX > TILT_THRESHOLD || absY > TILT_THRESHOLD) {
        if (absX > absY) {
            detected = (ax > 0) ? PomoOrientation::RIGHT : PomoOrientation::LEFT;
        } else {
            detected = (ay > 0) ? PomoOrientation::TOP : PomoOrientation::BOTTOM;
        }
    }

    current_ = detected;

    unsigned long now = millis();
    if (detected == candidate_) {
        if (detected != stable_ && (now - stableStart_ >= STABLE_THRESHOLD_MS)) {
            stable_ = detected;
        }
    } else {
        candidate_ = detected;
        stableStart_ = now;
    }
}

PomoOrientation ImuOrientation::getCurrent() const {
    return current_;
}

PomoOrientation ImuOrientation::getStable() const {
    return stable_;
}

bool ImuOrientation::isShaking() const {
    return shaking_;
}

const char* ImuOrientation::orientationName(PomoOrientation o) {
    switch (o) {
        case PomoOrientation::TOP: return "Top";
        case PomoOrientation::RIGHT: return "Right";
        case PomoOrientation::BOTTOM: return "Bottom";
        case PomoOrientation::LEFT: return "Left";
        case PomoOrientation::FLAT: return "Flat";
        default: return "Unknown";
    }
}

void ImuOrientation::updateShakeDetection(float ax, float ay, float az) {
    unsigned long now = millis();

    accelHistory_[shakeSampleIndex_][0] = ax;
    accelHistory_[shakeSampleIndex_][1] = ay;
    accelHistory_[shakeSampleIndex_][2] = az;
    shakeSampleIndex_ = (shakeSampleIndex_ + 1) % SHAKE_SAMPLE_COUNT;
    if (shakeSamplesFilled_ < SHAKE_SAMPLE_COUNT) {
        shakeSamplesFilled_++;
    }

    if (now - lastShakeTime_ < SHAKE_COOLDOWN_MS) {
        shaking_ = false;
        return;
    }

    if (shakeSamplesFilled_ < SHAKE_SAMPLE_COUNT) {
        shaking_ = false;
        return;
    }

    float meanX = 0, meanY = 0, meanZ = 0;
    for (int i = 0; i < SHAKE_SAMPLE_COUNT; ++i) {
        meanX += accelHistory_[i][0];
        meanY += accelHistory_[i][1];
        meanZ += accelHistory_[i][2];
    }
    meanX /= SHAKE_SAMPLE_COUNT;
    meanY /= SHAKE_SAMPLE_COUNT;
    meanZ /= SHAKE_SAMPLE_COUNT;

    float variance = 0;
    for (int i = 0; i < SHAKE_SAMPLE_COUNT; ++i) {
        float dx = accelHistory_[i][0] - meanX;
        float dy = accelHistory_[i][1] - meanY;
        float dz = accelHistory_[i][2] - meanZ;
        variance += dx * dx + dy * dy + dz * dz;
    }
    variance /= SHAKE_SAMPLE_COUNT;

    if (variance > SHAKE_VARIANCE_THRESHOLD) {
        shaking_ = true;
        lastShakeTime_ = now;
    } else {
        shaking_ = false;
    }
}

TwistDirection ImuOrientation::consumeTwist() {
    TwistDirection t = pendingTwist_;
    pendingTwist_ = TwistDirection::NONE;
    return t;
}

void ImuOrientation::updateTwistDetection(float gz) {
    unsigned long now = millis();
    if (now - lastTwistTime_ < TWIST_COOLDOWN_MS) return;

    if (gz > TWIST_THRESHOLD_DPS) {
        pendingTwist_ = TwistDirection::CLOCKWISE;
        lastTwistTime_ = now;
    } else if (gz < -TWIST_THRESHOLD_DPS) {
        pendingTwist_ = TwistDirection::COUNTER_CLOCKWISE;
        lastTwistTime_ = now;
    }
}
