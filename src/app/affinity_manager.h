#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config/app_config.h"

class AffinityManager {
public:
    void begin();
    void add(int delta, const char* reason);
    void reset();

    int value() const;
    const char* levelName() const;
    const char* moodName() const;
    const String& recent() const;

private:
    void save();

    Preferences prefs_;
    int value_ = AFFINITY_DEFAULT_VALUE;
    String recent_ = "First meet";
    unsigned long lastSaveTime_ = 0;
};
