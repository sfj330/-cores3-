#include "servo_controller.h"
#include "config/app_config.h"
#include <M5CoreS3.h>
#include <cmath>

namespace {
constexpr uint8_t REG_MODE1 = 0x00;
constexpr uint8_t REG_MODE2 = 0x01;
constexpr uint8_t REG_LED0_ON_L = 0x06;
constexpr uint8_t REG_PRESCALE = 0xFE;

constexpr uint8_t MODE1_RESTART = 0x80;
constexpr uint8_t MODE1_AUTO_INCREMENT = 0x20;
constexpr uint8_t MODE1_SLEEP = 0x10;
constexpr uint8_t MODE2_OUTDRV = 0x04;
constexpr uint8_t FULL_OFF_BIT = 0x10;
constexpr float PCA9685_OSCILLATOR_HZ = 25000000.0f;
constexpr float PWM_STEPS = 4096.0f;
}

bool ServoController::begin() {
    if (permanentlyDisabled_) {
        return false;
    }

    if (!M5.Ex_I2C.begin()) {
        ready_ = false;
        released_ = true;
        setStatus("PortA I2C init failed");
        Serial.println("Servo: PortA I2C init failed");
        return false;
    }

    if (!M5.Ex_I2C.scanID(static_cast<uint8_t>(PCA9685_I2C_ADDR), PCA9685_I2C_FREQ_HZ)) {
        ready_ = false;
        released_ = true;
        scanFailCount_++;
        if (scanFailCount_ >= 3) {
            permanentlyDisabled_ = true;
            status_ = "PCA9685 not found, disabled";
            Serial.println("Servo: PCA9685 not found after 3 attempts, permanently disabled");
        } else {
            status_ = String("PCA9685 not found @0x") + String(PCA9685_I2C_ADDR, HEX);
            Serial.printf("Servo: PCA9685 not found at 0x%02X (attempt %d/3)\n", PCA9685_I2C_ADDR, scanFailCount_);
        }
        return false;
    }

    ready_ = true;
    if (!write8(REG_MODE1, MODE1_SLEEP) ||
        !write8(REG_MODE2, MODE2_OUTDRV) ||
        !setPwmFrequency(static_cast<float>(SERVO_PWM_FREQ_HZ))) {
        ready_ = false;
        released_ = true;
        setStatus("PCA9685 init failed");
        Serial.println("Servo: PCA9685 init failed");
        return false;
    }

    status_ = String("PCA9685 ready @0x") + String(PCA9685_I2C_ADDR, HEX);
    Serial.printf("Servo: PCA9685 ready at 0x%02X, pan ch=%d, tilt ch=%d\n",
                  PCA9685_I2C_ADDR, SERVO_PAN_CHANNEL, SERVO_TILT_CHANNEL);
    return true;
}

bool ServoController::isReady() const {
    return ready_;
}

bool ServoController::isReleased() const {
    return released_;
}

const String& ServoController::statusText() const {
    return status_;
}

bool ServoController::center() {
    bool ok = setPanTilt(SERVO_PAN_CENTER_DEG, SERVO_TILT_CENTER_DEG);
    if (ok) {
        setStatus("Centered");
    }
    return ok;
}

bool ServoController::release() {
    if (!ready_) {
        setStatus("Servo unavailable");
        return false;
    }

    bool ok = setChannelOff(SERVO_PAN_CHANNEL) && setChannelOff(SERVO_TILT_CHANNEL);
    if (ok) {
        released_ = true;
        setStatus("PWM released");
        Serial.println("Servo: PWM released");
    } else {
        setStatus("PWM release failed");
        Serial.println("Servo: PWM release failed");
    }
    return ok;
}

bool ServoController::setPanTilt(float panDeg, float tiltDeg) {
    if (!ready_) {
        setStatus("Servo unavailable");
        return false;
    }

    float safePan = clampServoAngle(panDeg);
    float safeTilt = clampServoAngle(tiltDeg);
    bool ok = setPwm(SERVO_PAN_CHANNEL, 0, angleToTicks(safePan)) &&
              setPwm(SERVO_TILT_CHANNEL, 0, angleToTicks(safeTilt));
    if (ok) {
        panAngle_ = safePan;
        tiltAngle_ = safeTilt;
        released_ = false;
        setStatus("Tracking tilt");
    } else {
        setStatus("Servo write failed");
        Serial.println("Servo: channel write failed");
    }
    return ok;
}

float ServoController::panAngle() const {
    return panAngle_;
}

float ServoController::tiltAngle() const {
    return tiltAngle_;
}

bool ServoController::write8(uint8_t reg, uint8_t value) {
    return M5.Ex_I2C.writeRegister8(static_cast<uint8_t>(PCA9685_I2C_ADDR),
                                    reg, value, PCA9685_I2C_FREQ_HZ);
}

bool ServoController::setPwmFrequency(float frequencyHz) {
    float prescaleValue = PCA9685_OSCILLATOR_HZ / (PWM_STEPS * frequencyHz) - 1.0f;
    uint8_t prescale = static_cast<uint8_t>(prescaleValue + 0.5f);

    if (!write8(REG_MODE1, MODE1_SLEEP)) return false;
    if (!write8(REG_PRESCALE, prescale)) return false;
    if (!write8(REG_MODE1, MODE1_AUTO_INCREMENT)) return false;
    delay(5);
    return write8(REG_MODE1, MODE1_RESTART | MODE1_AUTO_INCREMENT);
}

bool ServoController::setPwm(uint8_t channel, uint16_t onTick, uint16_t offTick) {
    if (channel >= 16) return false;

    uint8_t data[4] = {
        static_cast<uint8_t>(onTick & 0xFF),
        static_cast<uint8_t>((onTick >> 8) & 0x0F),
        static_cast<uint8_t>(offTick & 0xFF),
        static_cast<uint8_t>((offTick >> 8) & 0x0F)
    };
    uint8_t reg = REG_LED0_ON_L + 4 * channel;
    return M5.Ex_I2C.writeRegister(static_cast<uint8_t>(PCA9685_I2C_ADDR),
                                   reg, data, sizeof(data), PCA9685_I2C_FREQ_HZ);
}

bool ServoController::setChannelOff(uint8_t channel) {
    if (channel >= 16) return false;

    uint8_t data[4] = {0, 0, 0, FULL_OFF_BIT};
    uint8_t reg = REG_LED0_ON_L + 4 * channel;
    return M5.Ex_I2C.writeRegister(static_cast<uint8_t>(PCA9685_I2C_ADDR),
                                   reg, data, sizeof(data), PCA9685_I2C_FREQ_HZ);
}

float ServoController::clampServoAngle(float angleDeg) const {
    if (angleDeg < SERVO_SAFE_MIN_DEG) return static_cast<float>(SERVO_SAFE_MIN_DEG);
    if (angleDeg > SERVO_SAFE_MAX_DEG) return static_cast<float>(SERVO_SAFE_MAX_DEG);
    return angleDeg;
}

uint16_t ServoController::angleToTicks(float angleDeg) const {
    float clamped = angleDeg;
    if (clamped < 0.0f) clamped = 0.0f;
    if (clamped > 180.0f) clamped = 180.0f;

    float pulseUs = SERVO_MIN_PULSE_US +
                    (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * (clamped / 180.0f);
    float ticks = pulseUs * static_cast<float>(SERVO_PWM_FREQ_HZ) * PWM_STEPS / 1000000.0f;
    if (ticks < 0.0f) ticks = 0.0f;
    if (ticks > PWM_STEPS - 1.0f) ticks = PWM_STEPS - 1.0f;
    return static_cast<uint16_t>(ticks + 0.5f);
}

void ServoController::setStatus(const char* text) {
    status_ = text ? String(text) : String();
}
