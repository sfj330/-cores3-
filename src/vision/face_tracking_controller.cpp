#include "vision/face_tracking_controller.h"
#include "config/app_config.h"
#include <Arduino.h>
#include <cmath>

void FaceTrackingController::reset() {
    smoothX_ = 0.0f;
    smoothY_ = 0.0f;
    hasFace_ = false;
    centered_ = false;
    status_ = "Face tracking idle";
}

bool FaceTrackingController::update(const FaceResult& face, int frameWidth, int frameHeight,
                                    float& panDeltaDeg, float& tiltDeltaDeg) {
    panDeltaDeg = 0.0f;
    tiltDeltaDeg = 0.0f;

    if (!face.detected || frameWidth <= 0 || frameHeight <= 0) {
        hasFace_ = false;
        centered_ = false;
        status_ = "No face detected";
        return false;
    }

    float nx = ((static_cast<float>(face.centerX) / static_cast<float>(frameWidth)) * 2.0f) - 1.0f;
    float ny = ((static_cast<float>(face.centerY) / static_cast<float>(frameHeight)) * 2.0f) - 1.0f;

    if (!hasFace_) {
        smoothX_ = nx;
        smoothY_ = ny;
        hasFace_ = true;
    } else {
        smoothX_ += (nx - smoothX_) * SERVO_FACE_TRACK_FILTER_ALPHA;
        smoothY_ += (ny - smoothY_) * SERVO_FACE_TRACK_FILTER_ALPHA;
    }

    bool panCentered = fabsf(smoothX_) < SERVO_FACE_TRACK_DEADBAND;
    bool tiltCentered = fabsf(smoothY_) < SERVO_FACE_TRACK_DEADBAND;
    centered_ = panCentered && tiltCentered;

    if (!panCentered) {
        panDeltaDeg = smoothX_ * SERVO_FACE_TRACK_GAIN_DEG;
    }
    if (!tiltCentered) {
        tiltDeltaDeg = smoothY_ * SERVO_FACE_TRACK_GAIN_DEG;
    }

    status_ = centered_ ? "Face centered" : "Centering face";
    return true;
}

bool FaceTrackingController::hasFace() const {
    return hasFace_;
}

bool FaceTrackingController::isCentered() const {
    return centered_;
}

const char* FaceTrackingController::statusText() const {
    return status_;
}
