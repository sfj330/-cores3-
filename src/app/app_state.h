#pragma once

#include <functional>

enum class AppStateEnum {
    FACE,
    MENU,
    WIFI_INFO,
    CAMERA_DEBUG,
    AI_VISION,
    POMODORO,
    MUSIC,
    SYSTEM_INFO,
    SERVO_TEST,
    AFFINITY,
    AI,
    SETTINGS,
    SLEEP
};

enum class FaceEmotion {
    NORMAL,
    HAPPY,
    CURIOUS,
    LISTENING,
    THINKING,
    SPEAKING,
    SURPRISED,
    SLEEPY,
    TRACKING,
    SHY,
    SICK
};

class AppState {
public:
    using StateCallback = std::function<void(AppStateEnum)>;

    static AppState& instance();

    void setState(AppStateEnum state);
    AppStateEnum getState() const;

    void setEmotion(FaceEmotion emotion);
    FaceEmotion getEmotion() const;

    void registerCallback(StateCallback cb);

private:
    AppState() = default;

    AppStateEnum currentState_ = AppStateEnum::FACE;
    FaceEmotion currentEmotion_ = FaceEmotion::NORMAL;
    StateCallback callback_ = nullptr;
};
