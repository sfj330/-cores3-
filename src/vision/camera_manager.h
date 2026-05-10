#pragma once

#include <cstdint>
#include <cstddef>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct CameraFrame {
    uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    size_t size = 0;
    bool valid = false;
    bool locked = false;
};

struct CameraJpeg {
    uint8_t* data = nullptr;
    size_t length = 0;
    bool valid = false;
};

class CameraManager {
public:
    CameraManager();
    bool begin();
    bool end();

    bool startCapture();
    bool stopCapture();
    bool isInitialized() const;

    CameraFrame getDisplayFrame();
    CameraFrame getDetectionFrame();
    void releaseFrame(CameraFrame& frame);

    bool captureJpegToFile(const char* path, String& status);
    bool captureJpegToMemory(CameraJpeg& jpeg, String& status);
    void releaseJpeg(CameraJpeg& jpeg);

    bool isRunning() const;
    bool isInCooldown() const;
    unsigned long lastFailTime() const;
    void resetFailure();

    static void scaleDown(const uint8_t* src, uint8_t* dst,
                          int srcW, int srcH, int dstW, int dstH);

private:
    bool ensureMutex();
    void recordFail();

    bool initialized_ = false;
    bool capturing_ = false;
    SemaphoreHandle_t cameraMutex_ = nullptr;

    int consecutiveFailCount_ = 0;
    unsigned long lastFailTime_ = 0;
    static constexpr int MAX_CONSECUTIVE_FAILS = 3;
    static constexpr unsigned long FAIL_COOLDOWN_MS = 10000;
};
