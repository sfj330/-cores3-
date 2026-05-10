#include "power_manager.h"
#include "config/app_config.h"
#include <M5CoreS3.h>

PowerManager::PowerManager() = default;

bool PowerManager::begin() {
    // Configure ADC for battery reading
    // CoreS3 battery voltage is typically on GPIO 38 or internal ADC
    voltage_ = readVoltage();
    update();
    return true;
}

void PowerManager::update() {
    voltage_ = readVoltage();

    // Calculate percentage (linear approximation)
    if (voltage_ >= BATTERY_FULL_VOLTAGE) {
        percentage_ = 1.0f;
    } else if (voltage_ <= BATTERY_EMPTY_VOLTAGE) {
        percentage_ = 0.0f;
    } else {
        percentage_ = (voltage_ - BATTERY_EMPTY_VOLTAGE) /
                      (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE);
    }

    // Check low battery
    bool wasLow = lowBattery_;
    lowBattery_ = (voltage_ < BATTERY_LOW_THRESHOLD && voltage_ > 0.1f);

    if (lowBattery_ && !wasLow && callback_) {
        callback_(voltage_);
    }
}

float PowerManager::readVoltage() {
    // -------------------------------------------------
    // PLACEHOLDER: Read battery voltage
    //
    // CoreS3 battery voltage reading depends on PMU API:
    //   - Some M5CoreS3 versions expose M5.Axp.getBatteryVoltage()
    //   - Others use M5.Power.getBatteryVoltage()
    //
    // For now, return 0.0 (USB powered / unknown).
    // Replace with actual PMU read when hardware is available.
    // -------------------------------------------------

    return 0.0f;
}

float PowerManager::getVoltage() const {
    return voltage_;
}

float PowerManager::getPercentage() const {
    return percentage_;
}

bool PowerManager::isLowBattery() const {
    return lowBattery_;
}

void PowerManager::setLowBatteryCallback(LowBatteryCallback cb) {
    callback_ = std::move(cb);
}

void PowerManager::enterSleep() {
    sleeping_ = true;
    M5.Lcd.setBrightness(24);
}

void PowerManager::exitSleep() {
    sleeping_ = false;
    M5.Lcd.setBrightness(255);
}
