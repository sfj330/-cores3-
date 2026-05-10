#pragma once

#include <cstdint>
#include <M5GFX.h>
#include "config/app_config.h"

class FaceUI {
public:
    FaceUI();
    bool begin();
    void update();

    void setGazeOffset(float dx, float dy);
    void setExpression(int emotion);
    void setSpeakingMouthOpen(bool open);
    void wake();

    void setTemporaryGaze(float dx, float dy, unsigned long durationMs);

    void setStatusText(const char* text, uint16_t color = 0x7BEF, unsigned long ttlMs = 0);
    void clearStatusText();

private:
    void drawFace();
    void drawEyes(int centerX, int centerY);
    void drawEyebrows(int centerX, int centerY);
    void drawMouth(int centerX, int centerY);
    void drawCheeks(int centerX, int centerY);
    void drawStatusText();

    void updateBlink();
    void updateGaze();
    void updateSpeakingAnimation();

    M5Canvas canvas_;
    bool spriteReady_ = false;

    bool isBlinking_ = false;
    unsigned long blinkStartTime_ = 0;
    unsigned long nextBlinkTime_ = 3000;

    float gazeX_ = 0.0f;
    float gazeY_ = 0.0f;
    float targetGazeX_ = 0.0f;
    float targetGazeY_ = 0.0f;

    float tempGazeX_ = 0.0f;
    float tempGazeY_ = 0.0f;
    bool tempGazeActive_ = false;
    unsigned long tempGazeEndTime_ = 0;

    int currentEmotion_ = 0;

    bool speakingMouthOpen_ = false;
    unsigned long lastSpeakAnimTime_ = 0;
    float mouthOpenAmount_ = 0.0f;

    bool sleeping_ = false;
    int brightness_ = 255;

    String statusText_;
    uint16_t statusColor_ = 0x7BEF;
    unsigned long statusEndTime_ = 0;
};
