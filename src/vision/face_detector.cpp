#include "face_detector.h"
#include "config/app_config.h"
#include <Arduino.h>
#include <cmath>

FaceDetector::FaceDetector() = default;

bool FaceDetector::begin() {
    enabled_ = FACE_DETECTION_ENABLED_ON_BOOT;
    backendAvailable_ = true;
    if (!statusPrinted_) {
        statusPrinted_ = true;
        Serial.println("FaceDetector: skin-color heuristic backend active");
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

static inline bool isSkinPixelRGB565(uint16_t pixel) {
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5) & 0x3F;
    uint8_t b5 = pixel & 0x1F;

    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);

    if (r < 60 || g < 30 || b < 15) return false;
    if (r < g || r < b) return false;
    if ((int)r - (int)g < 15) return false;
    if ((int)r - (int)b < 15) return false;
    if (g > 200 && b > 200) return false;
    return true;
}

FaceResult FaceDetector::detect(const uint8_t* frameData, int width, int height) {
    if (!enabled_ || frameData == nullptr || width <= 0 || height <= 0) {
        return FaceResult{};
    }

    const uint16_t* pixels = reinterpret_cast<const uint16_t*>(frameData);
    constexpr int step = 4;
    int skinCount = 0;
    long sumX = 0;
    long sumY = 0;
    int minX = width;
    int maxX = 0;
    int minY = height;
    int maxY = 0;

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            uint16_t pixel = pixels[y * width + x];
            pixel = (pixel >> 8) | (pixel << 8);

            if (isSkinPixelRGB565(pixel)) {
                skinCount++;
                sumX += x;
                sumY += y;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    int totalSampled = (width / step) * (height / step);
    if (totalSampled <= 0) {
        return FaceResult{};
    }

    float skinRatio = static_cast<float>(skinCount) / static_cast<float>(totalSampled);
    if (skinCount < 30 || skinRatio < 0.03f || skinRatio > 0.6f) {
        return FaceResult{};
    }

    int blobW = maxX - minX;
    int blobH = maxY - minY;
    if (blobW < 20 || blobH < 20) {
        return FaceResult{};
    }

    float aspect = static_cast<float>(blobW) / static_cast<float>(blobH);
    if (aspect < 0.4f || aspect > 2.5f) {
        return FaceResult{};
    }

    FaceResult result;
    result.detected = true;
    result.centerX = static_cast<int>(sumX / skinCount);
    result.centerY = static_cast<int>(sumY / skinCount);
    result.width = blobW;
    result.height = blobH;
    result.confidence = skinRatio > 0.15f ? 0.8f : 0.5f;
    return result;
}
