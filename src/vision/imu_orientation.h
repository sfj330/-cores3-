#pragma once

#include <cstdint>

enum class PomoOrientation {
    TOP,
    RIGHT,
    BOTTOM,
    LEFT,
    FLAT,
    UNKNOWN
};

enum class TwistDirection {
    NONE,
    CLOCKWISE,
    COUNTER_CLOCKWISE
};

class ImuOrientation {
public:
    ImuOrientation();

    bool begin();
    void update();

    PomoOrientation getCurrent() const;
    PomoOrientation getStable() const;
    bool isShaking() const;
    TwistDirection consumeTwist();

    static const char* orientationName(PomoOrientation o);

private:
    PomoOrientation current_ = PomoOrientation::UNKNOWN;
    PomoOrientation stable_ = PomoOrientation::UNKNOWN;
    unsigned long stableStart_ = 0;
    PomoOrientation candidate_ = PomoOrientation::UNKNOWN;

    static constexpr unsigned long STABLE_THRESHOLD_MS = 700;
    static constexpr float TILT_THRESHOLD = 0.6f;

    static constexpr int SHAKE_SAMPLE_COUNT = 10;
    static constexpr float SHAKE_VARIANCE_THRESHOLD = 0.15f;
    static constexpr unsigned long SHAKE_COOLDOWN_MS = 2000;

    float accelHistory_[SHAKE_SAMPLE_COUNT][3] = {};
    int shakeSampleIndex_ = 0;
    int shakeSamplesFilled_ = 0;
    bool shaking_ = false;
    unsigned long lastShakeTime_ = 0;

    // Gyro twist detection
    static constexpr float TWIST_THRESHOLD_DPS = 120.0f;
    static constexpr unsigned long TWIST_COOLDOWN_MS = 800;
    TwistDirection pendingTwist_ = TwistDirection::NONE;
    unsigned long lastTwistTime_ = 0;

    void updateShakeDetection(float ax, float ay, float az);
    void updateTwistDetection(float gz);
};
