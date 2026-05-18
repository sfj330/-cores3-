#include "app/affinity_manager.h"

static constexpr const char* NVS_NAMESPACE = "affinity";
static constexpr const char* NVS_KEY_VALUE = "val";
static constexpr unsigned long SAVE_DEBOUNCE_MS = 5000;

void AffinityManager::begin() {
    prefs_.begin(NVS_NAMESPACE, false);
    value_ = prefs_.getInt(NVS_KEY_VALUE, AFFINITY_DEFAULT_VALUE);
    if (value_ < AFFINITY_MIN_VALUE) value_ = AFFINITY_MIN_VALUE;
    if (value_ > AFFINITY_MAX_VALUE) value_ = AFFINITY_MAX_VALUE;
    recent_ = "Ready";
    Serial.printf("Affinity loaded: %d\n", value_);
}

void AffinityManager::add(int delta, const char* reason) {
    int prev = value_;
    value_ += delta;
    if (value_ < AFFINITY_MIN_VALUE) value_ = AFFINITY_MIN_VALUE;
    if (value_ > AFFINITY_MAX_VALUE) value_ = AFFINITY_MAX_VALUE;
    if (reason != nullptr && reason[0] != '\0') {
        recent_ = reason;
    }
    if (value_ != prev) {
        unsigned long now = millis();
        if (now - lastSaveTime_ >= SAVE_DEBOUNCE_MS) {
            save();
            lastSaveTime_ = now;
        }
    }
}

void AffinityManager::reset() {
    value_ = AFFINITY_DEFAULT_VALUE;
    recent_ = "Reset";
    save();
}

void AffinityManager::save() {
    prefs_.putInt(NVS_KEY_VALUE, value_);
}

int AffinityManager::value() const {
    return value_;
}

const char* AffinityManager::levelName() const {
    if (value_ < 25) return "Shy";
    if (value_ < 50) return "Familiar";
    if (value_ < 75) return "Close";
    return "Best Friend";
}

const char* AffinityManager::moodName() const {
    if (value_ < 25) return "Quiet";
    if (value_ < 50) return "Warm";
    if (value_ < 75) return "Happy";
    return "Lively";
}

const String& AffinityManager::recent() const {
    return recent_;
}
