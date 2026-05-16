#pragma once

#include "vision/face_detector.h"

class FaceTrackingController {
public:
    void reset();
    bool update(const FaceResult& face, int frameWidth, int frameHeight,
                float& panDeltaDeg, float& tiltDeltaDeg);

    bool hasFace() const;
    bool isCentered() const;
    const char* statusText() const;

private:
    float smoothX_ = 0.0f;
    float smoothY_ = 0.0f;
    bool hasFace_ = false;
    bool centered_ = false;
    const char* status_ = "Face tracking idle";
};
