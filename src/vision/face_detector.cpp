#include "face_detector.h"
#include "config/app_config.h"
#include <Arduino.h>

FaceDetector::FaceDetector() = default;

bool FaceDetector::begin() {
    enabled_ = FACE_DETECTION_ENABLED_ON_BOOT;
    backendAvailable_ = false;
    if (!statusPrinted_) {
        statusPrinted_ = true;
        Serial.println("FaceDetector: disabled by configuration");
        Serial.println("FaceDetector: no fake or skin-color detection is active");
    }
    return true;
}

bool FaceDetector::end() {
    enabled_ = false;
    return true;
}

void FaceDetector::setEnabled(bool enabled) {
    enabled_ = enabled && backendAvailable_;
}

bool FaceDetector::isEnabled() const {
    return enabled_;
}

bool FaceDetector::backendAvailable() const {
    return backendAvailable_;
}

const char* FaceDetector::statusText() const {
    if (!backendAvailable_) {
        return "Face tracking unavailable";
    }
    return enabled_ ? "Face detection ready" : "Face detection disabled";
}

FaceResult FaceDetector::detect(const uint8_t* frameData, int width, int height) {
    (void)frameData;
    (void)width;
    (void)height;
    return FaceResult{};
}
