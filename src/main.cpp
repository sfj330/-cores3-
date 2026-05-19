#include <M5CoreS3.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esp_system.h"
#include <cmath>

#include "config/app_config.h"
#include "app/app_state.h"
#include "app/gesture_manager.h"
#include "app/event_bus.h"
#include "app/affinity_manager.h"
#include "app/system_status.h"
#include "ui/face_ui.h"
#include "ui/menu_ui.h"
#include "ui/camera_debug_ui.h"
#include "ui/pomodoro_ui.h"
#include "ui/info_ui.h"
#include "ui/music_ui.h"
#include "ui/affinity_ui.h"
#include "ui/settings_ui.h"
#include "ui/ui_theme.h"
#include "audio/music_manager.h"
#include "servo/servo_controller.h"
#include "servo/servo_motion_controller.h"
#include "vision/camera_manager.h"
#include "vision/face_detector.h"
#include "vision/face_tracker.h"
#include "vision/face_tracking_controller.h"
#include "vision/imu_orientation.h"
#include "ai/xiaozhi_client.h"
#include "ai/voice_state.h"
#include "power/power_manager.h"
#include "network/wifi_manager.h"
#include "network/vision_client.h"
#include "storage/storage_manager.h"

FaceUI gFaceUI;
MenuUI gMenuUI;
CameraDebugUI gCameraDebugUI;
PomodoroUI gPomodoroUI;
InfoUI gInfoUI;
MusicUI gMusicUI;
AffinityUI gAffinityUI;
SettingsUI gSettingsUI;
CameraManager gCameraManager;
FaceDetector gFaceDetector;
FaceTracker gFaceTracker;
ImuOrientation gImuOrientation;
XiaoZhiClient gXiaoZhiClient;
PowerManager gPowerManager;
WifiManager gWifiManager;
StorageManager gStorageManager;
VisionClient gVisionClient;
MusicManager gMusicManager;
ServoController gServoController;
ServoMotionController gServoMotionController;
FaceTrackingController gFaceTrackingController;
AffinityManager gAffinityManager;

static float gCurrentFps = 0.0f;
static unsigned long gLastFpsCalc = 0;
static int gFrameCount = 0;

static TaskHandle_t uiTaskHandle = nullptr;
static TaskHandle_t touchTaskHandle = nullptr;
static TaskHandle_t cameraTaskHandle = nullptr;
static TaskHandle_t visionTaskHandle = nullptr;
static TaskHandle_t aiTaskHandle = nullptr;
static TaskHandle_t powerTaskHandle = nullptr;
static TaskHandle_t networkTaskHandle = nullptr;
static SemaphoreHandle_t displayMutex = nullptr;

static bool gSickActive = false;
static unsigned long gSickUntil = 0;
static int gShakeCount = 0;
static unsigned long gLastShakeTime = 0;
static bool gNtpSynced = false;
static volatile bool gNeedActivationRequest = false;
static volatile bool gNeedActivationCheck = false;
static volatile bool gNeedOpenAudioChannel = false;
static volatile bool gNeedCameraDeinitForAi = false;
static volatile bool gNeedForegroundCameraStart = false;
static bool gActivationCodeRequested = false;
static unsigned long gLastActivationCheck = 0;
static volatile bool gPomodoroCompletionPending = false;
static volatile int gPomodoroCompletionPreset = 0;
static bool gPomodoroFaceEmotionPending = false;
static bool gPomodoroFaceEmotionActive = false;
static FaceEmotion gPomodoroFaceEmotion = FaceEmotion::NORMAL;
static unsigned long gPomodoroFaceEmotionUntil = 0;
static bool gPomodoroMelodyActive = false;
static bool gPomodoroMelodyErrorPrinted = false;
static uint8_t gPomodoroMelodyIndex = 0;
static unsigned long gPomodoroNextNoteAt = 0;

enum class PendingAiTool : uint8_t {
    NONE = 0,
    CAMERA_OPEN,
    CAMERA_CLOSE,
    CAMERA_CAPTURE_PHOTO,
    VISION_DESCRIBE,
    POMODORO_OPEN,
    MUSIC_CONTROL,
    PET_REACT,
    SERVO_CONTROL,
    DEVICE_STATUS,
    DEVICE_CONTROL
};

enum class MusicAction : uint8_t {
    NONE = 0,
    PLAY_PAUSE,
    STOP,
    NEXT
};

enum class PetReaction : uint8_t {
    NONE = 0,
    HAPPY,
    SHY,
    CURIOUS,
    SLEEPY,
    SURPRISED,
    SICK
};

enum class ServoAiAction : uint8_t {
    NONE = 0,
    CENTER,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    NOD,
    SHAKE,
    DANCE,
    RELEASE
};

enum class DevicePageAction : uint8_t {
    NONE = 0,
    FACE,
    MENU,
    WIFI,
    SYSTEM,
    CAMERA,
    MUSIC,
    POMODORO,
    AI
};

enum class DeviceSleepAction : uint8_t {
    NONE = 0,
    WAKE,
    SLEEP
};

struct DeviceControlRequest {
    DevicePageAction page = DevicePageAction::NONE;
    bool hasBrightness = false;
    SystemBrightnessLevel brightness = SystemBrightnessLevel::BRIGHT;
    bool hasVolume = false;
    SystemVolumeLevel volume = SystemVolumeLevel::NORMAL;
    DeviceSleepAction sleep = DeviceSleepAction::NONE;
};

static volatile PendingAiTool gPendingAiTool = PendingAiTool::NONE;
static int gPendingAiCallId = -1;
static int gPendingAiIntParam = 0;
static volatile bool gPendingAiBoolParam = false;
static volatile MusicAction gPendingAiMusicAction = MusicAction::NONE;
static volatile PetReaction gPendingAiPetReaction = PetReaction::NONE;
static volatile ServoAiAction gPendingAiServoAction = ServoAiAction::NONE;
static String gPendingAiStringParam = "";
static DeviceControlRequest gPendingDeviceControl;

static AppStateEnum gPomodoroReturnTo = AppStateEnum::MENU;
static AppStateEnum gMusicReturnTo = AppStateEnum::MENU;

static FaceEmotion gLastServoPoseEmotion = FaceEmotion::NORMAL;

static volatile bool gPhotoFaceTrackingActive = false;
static volatile bool gFaceTrackHasFace = false;
static volatile bool gFaceTrackCentered = false;
static volatile bool gFaceTrackCorrectionPending = false;
static float gFaceTrackPanDeltaDeg = 0.0f;
static float gFaceTrackTiltDeltaDeg = 0.0f;

static bool gPetReactActive = false;
static FaceEmotion gPetReactEmotion = FaceEmotion::NORMAL;
static FaceEmotion gPetReactPrevEmotion = FaceEmotion::NORMAL;
static unsigned long gPetReactUntil = 0;
static constexpr unsigned long PET_REACT_DURATION_MS = 4000;

static bool gDanceActive = false;
static bool gDanceStartedMusic = false;
static FaceEmotion gDancePrevEmotion = FaceEmotion::NORMAL;

static bool gAiListeningAffinityAwarded = false;
static bool gAffinityWelcomeActive = false;
static unsigned long gAffinityWelcomeUntil = 0;
static unsigned long gNextCompanionMotionAt = 0;
static unsigned long gNextSpeakingNodAt = 0;
static bool gSpeakingNodDown = false;

enum class AiVisionStatus {
    IDLE,
    OPENING,
    PREVIEW,
    RECOGNIZING,
    DONE,
    ERROR
};

enum class FaceVisionRuntime {
    IDLE,
    BURST,
    COOLDOWN
};

static AiVisionStatus gAiVisionStatus = AiVisionStatus::IDLE;
static String gAiVisionStatusText = "Vision idle";
static String gAiVisionResultText = "";
static SystemStatusViewModel gSystemStatus;
static SystemBrightnessLevel gUserBrightnessLevel = SystemBrightnessLevel::NORMAL;
static SystemVolumeLevel gUserVolumeLevel = SystemVolumeLevel::NORMAL;
static FaceVisionRuntime gFaceVisionRuntime = FaceVisionRuntime::IDLE;
static unsigned long gFaceVisionBurstUntil = 0;
static unsigned long gFaceVisionNextIdleScanAt = 0;
static bool gFaceVisionStartPending = false;
static const char* gFaceVisionPendingReason = nullptr;
static const char* gForegroundCameraReason = nullptr;
static unsigned long gBrownoutSafeUntil = 0;
static esp_reset_reason_t gBootResetReason = ESP_RST_UNKNOWN;

struct PomodoroMelodyNote {
    float frequency;
    uint16_t durationMs;
    uint16_t gapMs;
};

static constexpr PomodoroMelodyNote POMODORO_DONE_MELODY[] = {
    {784.0f, 120, 25},
    {988.0f, 120, 25},
    {1175.0f, 160, 30},
    {1568.0f, 240, 40},
    {1319.0f, 180, 0}
};
static constexpr unsigned long POMODORO_FACE_EMOTION_MS = 4000;

static void gestureEventHandler(const GestureEvent& event);
static void stateChangeHandler(AppStateEnum state);
static void handleEvent(const Event& event);
static void handlePomodoroComplete(int presetIndex);
static void processPomodoroCompletion(unsigned long now);
static void updatePomodoroMelody(unsigned long now);
static void handleXiaoZhiMcpTool(const String& toolName, JsonObjectConst arguments, int callId);
static void handleXiaoZhiTranscript(const String& text);
static void handleXiaoZhiTranscriptModern(const String& text);
static void processPendingAiTool();
static String describeAiVisionScene(JsonObjectConst arguments, bool& ok);
static bool ensureAiVisionPreview(const char* status);
static void closeAiVisionPreview();
static void setAiVisionStatus(AiVisionStatus status, const char* text, const char* result = nullptr);
static void updateInfoUiData();
static void updateMusicUiData();
static void updateAffinityUiData();
static void stopMusicForExclusiveAudio();
static SystemStatusViewModel captureSystemStatus();
static SystemVoiceState currentSystemVoiceState();
static void applyBrightnessLevel(SystemBrightnessLevel level, bool rememberPreference = true);
static void applyVolumeLevel(SystemVolumeLevel level);
static void enterSleepMode();
static void wakeFromSleepMode();
static bool navigateDevicePage(DevicePageAction page);
static String devicePageLabel(DevicePageAction page);
static String brightnessActionLabel(SystemBrightnessLevel level);
static String volumeActionLabel(SystemVolumeLevel level);
static String buildDeviceStatusResponse(const String& detail);
static String processDeviceStatus(int callId, const String& detail);
static String processDeviceControl(int callId);
static bool hasPendingDeviceControlAction();
static void updateSharedServoMotion(unsigned long now);
static void updateCompanionMotion(unsigned long now);
static void updateDanceLifecycle(unsigned long now);
static void addAffinity(int delta, const char* reason);
static bool startDanceMusic(String& note);
static void applyServoPoseForEmotion(FaceEmotion emotion);
static void applyServoPoseForPetReaction(PetReaction reaction);
static bool prepareFaceTrackingForPhoto(String& note);
static bool ensureCameraTaskStarted();
static void requestForegroundCameraStart(const char* reason);
static bool openForegroundCameraNow(const char* reason);
static void processForegroundCameraStart();
static bool isBrownoutSafeModeActive(unsigned long now);
static bool isFaceVisionBurstActive();
static void requestFaceVisionBurst(unsigned long now, unsigned long durationMs, const char* reason);
static bool startFaceVisionCapture(unsigned long now);
static void stopFaceVisionBurst(const char* reason);
static void updateFaceVisionRuntime(unsigned long now);
static ServoMotionAction toServoMotionAction(ServoAiAction action);
static const char* servoAiActionName(ServoAiAction action);

static bool takeDisplayLock(TickType_t timeout = pdMS_TO_TICKS(20)) {
    return displayMutex == nullptr || xSemaphoreTake(displayMutex, timeout) == pdTRUE;
}

static void giveDisplayLock() {
    if (displayMutex != nullptr) {
        xSemaphoreGive(displayMutex);
    }
}

static void drawLcdDarkBackground() {
    M5.Lcd.fillScreen(UiTheme::BG);
}

static void stopMusicForExclusiveAudio() {
    if (gMusicManager.state() == MusicPlaybackState::PLAYING ||
        gMusicManager.state() == MusicPlaybackState::PAUSED) {
        gMusicManager.stop();
        bool idle = gMusicManager.waitForIdle(1200);
        if (!idle) {
            Serial.println("Music: wait for idle timed out before exclusive audio");
        }
        gMusicManager.releaseSpeaker();
        gMusicUI.markDirty();
    } else if (M5.Speaker.isRunning()) {
        gMusicManager.releaseSpeaker();
    }
}

static SystemVoiceState currentSystemVoiceState() {
    switch (gXiaoZhiClient.getState()) {
        case VoiceState::LISTENING: return SystemVoiceState::LISTENING;
        case VoiceState::THINKING: return SystemVoiceState::THINKING;
        case VoiceState::SPEAKING: return SystemVoiceState::SPEAKING;
        case VoiceState::ERROR: return SystemVoiceState::ERROR;
        case VoiceState::IDLE:
        default:
            return SystemVoiceState::IDLE;
    }
}

static SystemStatusViewModel captureSystemStatus() {
    SystemStatusViewModel status;
    status.wifi.connected = gWifiManager.isConnected();
    status.wifi.configured = gWifiManager.isConfigured();
    status.wifi.rssi = gWifiManager.rssi();
    status.wifi.ip = gWifiManager.ipString();

    status.sd.ready = gStorageManager.isReady();
    status.sd.statusText = gStorageManager.statusText();

    switch (gMusicManager.state()) {
        case MusicPlaybackState::PLAYING: status.music.playbackState = SystemMusicState::PLAYING; break;
        case MusicPlaybackState::PAUSED: status.music.playbackState = SystemMusicState::PAUSED; break;
        case MusicPlaybackState::ERROR: status.music.playbackState = SystemMusicState::ERROR; break;
        case MusicPlaybackState::STOPPED:
        default:
            status.music.playbackState = SystemMusicState::STOPPED;
            break;
    }
    status.music.currentTitle = gMusicManager.currentTitle();
    status.music.statusText = gMusicManager.statusText();

    status.ai.activated = gXiaoZhiClient.isActivated();
    status.ai.wsConnected = gXiaoZhiClient.isWsConnected();
    status.ai.audioChannelOpen = gXiaoZhiClient.isAudioChannelOpen();
    status.ai.voiceState = currentSystemVoiceState();
    status.ai.visionReady = gXiaoZhiClient.hasVisionEndpoint();

    status.memory.heapKb = ESP.getFreeHeap() / 1024;
    status.memory.psramKb = ESP.getFreePsram() / 1024;

    status.control.brightnessLevel = gUserBrightnessLevel;
    status.control.volumeLevel = gUserVolumeLevel;
    return status;
}

static void applyBrightnessLevel(SystemBrightnessLevel level, bool rememberPreference) {
    int brightness = 255;
    switch (level) {
        case SystemBrightnessLevel::DIM: brightness = 48; break;
        case SystemBrightnessLevel::NORMAL: brightness = 160; break;
        case SystemBrightnessLevel::BRIGHT:
        default:
            brightness = 255;
            break;
    }
    if (rememberPreference) {
        gUserBrightnessLevel = level;
    }
    M5.Lcd.setBrightness(brightness);
}

static void applyVolumeLevel(SystemVolumeLevel level) {
    gUserVolumeLevel = level;
    uint8_t musicVolume = 64;
    uint8_t aiVolume = 160;
    switch (level) {
        case SystemVolumeLevel::QUIET:
            musicVolume = 40;
            aiVolume = 96;
            break;
        case SystemVolumeLevel::NORMAL:
            musicVolume = 64;
            aiVolume = 160;
            break;
        case SystemVolumeLevel::LOUD:
            musicVolume = 80;
            aiVolume = 200;
            break;
    }
    gMusicManager.setVolumePreset(musicVolume);
    gXiaoZhiClient.setTtsVolume(aiVolume);
}

static void enterSleepMode() {
    gPowerManager.enterSleep();
    M5.Lcd.setBrightness(24);
}

static void wakeFromSleepMode() {
    gPowerManager.exitSleep();
    applyBrightnessLevel(gUserBrightnessLevel, false);
}

static bool navigateDevicePage(DevicePageAction page) {
    AppState& appState = AppState::instance();
    AppStateEnum current = appState.getState();

    if (current == AppStateEnum::AI_VISION && page != DevicePageAction::AI) {
        closeAiVisionPreview();
    }
    if (page != DevicePageAction::AI && gXiaoZhiClient.isAudioChannelOpen()) {
        gXiaoZhiClient.pauseForForegroundTool();
    }

    switch (page) {
        case DevicePageAction::FACE:
            if (current == AppStateEnum::AI) {
                gXiaoZhiClient.closeAudioChannel();
            }
            appState.setState(AppStateEnum::FACE);
            return true;
        case DevicePageAction::MENU:
            appState.setState(AppStateEnum::MENU);
            return true;
        case DevicePageAction::WIFI:
            appState.setState(AppStateEnum::WIFI_INFO);
            return true;
        case DevicePageAction::SYSTEM:
            appState.setState(AppStateEnum::SYSTEM_INFO);
            return true;
        case DevicePageAction::CAMERA:
            appState.setState(AppStateEnum::CAMERA_DEBUG);
            return true;
        case DevicePageAction::MUSIC:
            gMusicReturnTo = AppStateEnum::AI;
            appState.setState(AppStateEnum::MUSIC);
            return true;
        case DevicePageAction::POMODORO:
            gPomodoroReturnTo = AppStateEnum::AI;
            appState.setState(AppStateEnum::POMODORO);
            return true;
        case DevicePageAction::AI:
            appState.setState(AppStateEnum::AI);
            return true;
        case DevicePageAction::NONE:
        default:
            return false;
    }
}

static String devicePageLabel(DevicePageAction page) {
    switch (page) {
        case DevicePageAction::FACE: return "主页";
        case DevicePageAction::MENU: return "菜单页";
        case DevicePageAction::WIFI: return "网络页";
        case DevicePageAction::SYSTEM: return "系统页";
        case DevicePageAction::CAMERA: return "相机页";
        case DevicePageAction::MUSIC: return "音乐页";
        case DevicePageAction::POMODORO: return "番茄钟";
        case DevicePageAction::AI: return "小智页";
        case DevicePageAction::NONE:
        default:
            return "";
    }
}

static String brightnessActionLabel(SystemBrightnessLevel level) {
    switch (level) {
        case SystemBrightnessLevel::DIM: return "把屏幕调暗了";
        case SystemBrightnessLevel::NORMAL: return "把屏幕调到正常亮度了";
        case SystemBrightnessLevel::BRIGHT:
        default:
            return "把屏幕调亮了";
    }
}

static String volumeActionLabel(SystemVolumeLevel level) {
    switch (level) {
        case SystemVolumeLevel::QUIET: return "把音量调小了";
        case SystemVolumeLevel::NORMAL: return "把音量调回正常了";
        case SystemVolumeLevel::LOUD:
        default:
            return "把音量调大了";
    }
}

static String buildDeviceStatusResponse(const String& detail) {
    SystemStatusViewModel status = captureSystemStatus();
    String response = systemStatusBriefSentence1(status);
    response += " ";
    response += systemStatusBriefSentence2(status);
    if (detail == "full") {
        response += " ";
        response += systemStatusMemorySentence(status);
    }
    return response;
}

static bool hasPendingDeviceControlAction() {
    return gPendingDeviceControl.page != DevicePageAction::NONE ||
           gPendingDeviceControl.hasBrightness ||
           gPendingDeviceControl.hasVolume ||
           gPendingDeviceControl.sleep != DeviceSleepAction::NONE;
}

static void updateInfoUiData() {
    gSystemStatus = captureSystemStatus();
    gInfoUI.setWifiStatus(gWifiManager.statusText().c_str(),
                          gWifiManager.ipString().c_str(),
                          gWifiManager.rssi(),
                          gWifiManager.isConfigured());
    gInfoUI.setSystemStatus(gSystemStatus);
}

static void updateMusicUiData() {
    gMusicUI.setState(gMusicManager.currentTitle(),
                      gMusicManager.statusText(),
                      gMusicManager.currentIndex(),
                      gMusicManager.trackCount(),
                      gMusicManager.state());
}

static void updateAffinityUiData() {
    gAffinityUI.setState(gAffinityManager.value(),
                         gAffinityManager.levelName(),
                         gAffinityManager.moodName(),
                         gAffinityManager.recent().c_str());
}

static void addAffinity(int delta, const char* reason) {
    gAffinityManager.add(delta, reason);
    gAffinityUI.markDirty();
}

static bool startDanceMusic(String& note) {
    MusicPlaybackState mstate = gMusicManager.state();
    if (mstate == MusicPlaybackState::PLAYING) {
        note = "dance with current music";
        gDanceStartedMusic = false;
        return true;
    }

    if (gMusicManager.trackCount() == 0) {
        gMusicManager.scan();
    }
    if (gMusicManager.trackCount() == 0) {
        note = "dance without music";
        gDanceStartedMusic = false;
        return false;
    }

    int index = gMusicManager.currentIndex();
    if (index < 0 || index >= gMusicManager.trackCount()) {
        index = 0;
    }
    String title = gMusicManager.trackName(index);
    bool ok = gMusicManager.play(index);
    gMusicUI.markDirty();
    if (ok) {
        note = "dance with music: " + title;
        gDanceStartedMusic = true;
        return true;
    }

    note = "dance without music";
    gDanceStartedMusic = false;
    return false;
}

static ServoMotionAction toServoMotionAction(ServoAiAction action) {
    switch (action) {
        case ServoAiAction::CENTER: return ServoMotionAction::CENTER;
        case ServoAiAction::LEFT: return ServoMotionAction::LEFT;
        case ServoAiAction::RIGHT: return ServoMotionAction::RIGHT;
        case ServoAiAction::UP: return ServoMotionAction::UP;
        case ServoAiAction::DOWN: return ServoMotionAction::DOWN;
        case ServoAiAction::NOD: return ServoMotionAction::NOD;
        case ServoAiAction::SHAKE: return ServoMotionAction::SHAKE;
        case ServoAiAction::DANCE: return ServoMotionAction::DANCE;
        case ServoAiAction::RELEASE: return ServoMotionAction::RELEASE;
        default: return ServoMotionAction::NONE;
    }
}

static const char* servoAiActionName(ServoAiAction action) {
    switch (action) {
        case ServoAiAction::CENTER: return "center";
        case ServoAiAction::LEFT: return "left";
        case ServoAiAction::RIGHT: return "right";
        case ServoAiAction::UP: return "up";
        case ServoAiAction::DOWN: return "down";
        case ServoAiAction::NOD: return "nod";
        case ServoAiAction::SHAKE: return "shake";
        case ServoAiAction::DANCE: return "dance";
        case ServoAiAction::RELEASE: return "release";
        default: return "unknown";
    }
}

static void applyServoPoseForPetReaction(PetReaction reaction) {
    switch (reaction) {
        case PetReaction::HAPPY:
            gServoMotionController.command(ServoMotionAction::NOD);
            break;
        case PetReaction::SHY:
            gServoMotionController.lookOffset(-10.0f, SERVO_FACE_TILT_OFFSET_DEG,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Shy pose");
            break;
        case PetReaction::CURIOUS:
            gServoMotionController.lookOffset(SERVO_FACE_PAN_OFFSET_DEG, -6.0f,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Curious pose");
            break;
        case PetReaction::SLEEPY:
            gServoMotionController.lookOffset(0.0f, SERVO_FACE_TILT_OFFSET_DEG,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Sleepy pose");
            break;
        case PetReaction::SURPRISED:
            gServoMotionController.lookOffset(0.0f, -SERVO_FACE_TILT_OFFSET_DEG,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Surprised pose");
            break;
        case PetReaction::SICK:
            gServoMotionController.command(ServoMotionAction::SHAKE);
            break;
        default:
            gServoMotionController.center(SERVO_FACE_MOTION_SPEED_DPS);
            break;
    }
}

static void applyServoPoseForEmotion(FaceEmotion emotion) {
    switch (emotion) {
        case FaceEmotion::NORMAL:
            gServoMotionController.center(SERVO_FACE_MOTION_SPEED_DPS);
            break;
        case FaceEmotion::HAPPY:
            gServoMotionController.command(ServoMotionAction::NOD);
            break;
        case FaceEmotion::CURIOUS:
            gServoMotionController.lookOffset(SERVO_FACE_PAN_OFFSET_DEG, -5.0f,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Curious pose");
            break;
        case FaceEmotion::LISTENING:
            gServoMotionController.lookOffset(0.0f, -5.0f,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Listening pose");
            break;
        case FaceEmotion::THINKING:
            gServoMotionController.lookOffset(12.0f, -8.0f,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Thinking pose");
            break;
        case FaceEmotion::SPEAKING:
            gServoMotionController.lookOffset(0.0f, -2.0f,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Speaking pose");
            break;
        case FaceEmotion::SURPRISED:
            gServoMotionController.lookOffset(0.0f, -SERVO_FACE_TILT_OFFSET_DEG,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Surprised pose");
            break;
        case FaceEmotion::SLEEPY:
            gServoMotionController.lookOffset(0.0f, SERVO_FACE_TILT_OFFSET_DEG,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Sleepy pose");
            break;
        case FaceEmotion::TRACKING:
            break;
        case FaceEmotion::SHY:
            gServoMotionController.lookOffset(-10.0f, SERVO_FACE_TILT_OFFSET_DEG,
                                              SERVO_FACE_MOTION_SPEED_DPS, "Shy pose");
            break;
        case FaceEmotion::SICK:
            gServoMotionController.command(ServoMotionAction::SHAKE);
            break;
    }
}

static void updateSharedServoMotion(unsigned long now) {
    AppStateEnum state = AppState::instance().getState();
    if (gFaceTrackCorrectionPending &&
        (state == AppStateEnum::FACE || state == AppStateEnum::CAMERA_DEBUG || state == AppStateEnum::AI_VISION)) {
        float panDelta = gFaceTrackPanDeltaDeg;
        float tiltDelta = gFaceTrackTiltDeltaDeg;
        gFaceTrackCorrectionPending = false;
        gServoMotionController.nudge(panDelta, tiltDelta,
                                     SERVO_TRACKING_MOTION_SPEED_DPS,
                                     "Face tracking");
    }

    if ((state == AppStateEnum::FACE || state == AppStateEnum::AI) &&
        !gServoMotionController.isDanceActive()) {
        FaceEmotion emotion = AppState::instance().getEmotion();
        if (emotion != gLastServoPoseEmotion) {
            gLastServoPoseEmotion = emotion;
            applyServoPoseForEmotion(emotion);
        }
    }

    if (state == AppStateEnum::FACE ||
        state == AppStateEnum::AI ||
        state == AppStateEnum::CAMERA_DEBUG ||
        state == AppStateEnum::AI_VISION) {
        gServoMotionController.update(now);
    }
}

static void updateDanceLifecycle(unsigned long now) {
    (void)now;
    if (!gDanceActive || gServoMotionController.isDanceActive()) return;

    gDanceActive = false;
    if (gDanceStartedMusic) {
        gMusicManager.stop();
        gMusicUI.markDirty();
        gDanceStartedMusic = false;
    }

    AppStateEnum state = AppState::instance().getState();
    if (state == AppStateEnum::AI && gXiaoZhiClient.isAudioChannelOpen()) {
        gXiaoZhiClient.resumeFromForegroundTool();
    }
    if (state == AppStateEnum::FACE || state == AppStateEnum::AI) {
        if (AppState::instance().getEmotion() == FaceEmotion::HAPPY) {
            AppState::instance().setEmotion(gDancePrevEmotion);
            gLastServoPoseEmotion = gDancePrevEmotion;
            applyServoPoseForEmotion(gDancePrevEmotion);
        }
        gFaceUI.setStatusText("Dance done", UiTheme::TEXT_DIM, 1200);
    }
}

static void updateCompanionMotion(unsigned long now) {
    AppStateEnum state = AppState::instance().getState();
    if (state == AppStateEnum::CAMERA_DEBUG ||
        state == AppStateEnum::AI_VISION ||
        state == AppStateEnum::POMODORO ||
        state == AppStateEnum::MUSIC ||
        state == AppStateEnum::AFFINITY ||
        isFaceVisionBurstActive() ||
        isBrownoutSafeModeActive(now) ||
        gServoMotionController.isDanceActive() ||
        gPetReactActive ||
        gSickActive) {
        return;
    }

    if (state == AppStateEnum::AI &&
        AppState::instance().getEmotion() == FaceEmotion::SPEAKING &&
        now >= gNextSpeakingNodAt) {
        gSpeakingNodDown = !gSpeakingNodDown;
        gServoMotionController.lookOffset(0.0f, gSpeakingNodDown ? 5.0f : -4.0f,
                                          38.0f, "Speaking nod");
        gNextSpeakingNodAt = now + 1200;
        return;
    }

    if (state != AppStateEnum::FACE) return;
    FaceEmotion emotion = AppState::instance().getEmotion();
    if (emotion != FaceEmotion::NORMAL && emotion != FaceEmotion::HAPPY) return;
    if (now < gNextCompanionMotionAt) return;

    int affinity = gAffinityManager.value();
    unsigned long interval = affinity >= 75 ? 4500 : (affinity >= 50 ? 6500 : 9000);
    gNextCompanionMotionAt = now + interval + random(0, 1800);

    // Time-aware behavior: occasionally show time-appropriate emotion
    if (gNtpSynced && random(0, 8) == 0) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            int hour = timeinfo.tm_hour;
            if (hour >= 23 || hour < 6) {
                AppState::instance().setEmotion(FaceEmotion::SLEEPY);
                gFaceUI.setTemporaryGaze(0.0f, 0.3f, 2000);
                gServoMotionController.lookOffset(0.0f, 12.0f, 22.0f, "Sleepy time");
                gSickActive = true;
                gSickUntil = now + 3000;
                return;
            } else if (hour >= 6 && hour < 9) {
                AppState::instance().setEmotion(FaceEmotion::HAPPY);
                gServoMotionController.command(ServoMotionAction::NOD);
                return;
            }
        }
    }

    int pattern = random(0, affinity >= 75 ? 6 : (affinity >= 50 ? 4 : 3));
    if (pattern == 0) {
        float pan = random(0, 2) == 0 ? -8.0f : 8.0f;
        gFaceUI.setTemporaryGaze(pan < 0 ? -0.45f : 0.45f, -0.05f, 1100);
        gServoMotionController.lookOffset(pan, -2.0f, 30.0f, "Companion glance");
    } else if (pattern == 1) {
        gFaceUI.setTemporaryGaze(0.0f, -0.25f, 1000);
        gServoMotionController.lookOffset(0.0f, -5.0f, 28.0f, "Companion look");
    } else if (pattern == 2) {
        gFaceUI.setTemporaryGaze(0.0f, 0.18f, 900);
        gServoMotionController.lookOffset(0.0f, 4.0f, 28.0f, "Companion dip");
    } else if (pattern == 3) {
        gServoMotionController.command(ServoMotionAction::NOD);
    } else if (pattern == 4) {
        // High affinity: playful head tilt
        AppState::instance().setEmotion(FaceEmotion::CURIOUS);
        gFaceUI.setTemporaryGaze(0.6f, -0.2f, 1500);
        gServoMotionController.lookOffset(12.0f, -4.0f, 32.0f, "Playful tilt");
        gSickActive = true;
        gSickUntil = now + 1800;
    } else {
        // High affinity: happy wiggle
        AppState::instance().setEmotion(FaceEmotion::HAPPY);
        gServoMotionController.command(ServoMotionAction::SHAKE);
        M5.Speaker.tone(1400, 40);
        gSickActive = true;
        gSickUntil = now + 2000;
    }
}

static bool prepareFaceTrackingForPhoto(String& note) {
    note = "";

    if (!gFaceDetector.backendAvailable()) {
        note = "face tracking unavailable";
        return false;
    }

    gFaceDetector.setEnabled(true);
    if (!gFaceDetector.isEnabled()) {
        note = "face tracking disabled";
        return false;
    }

    gFaceTrackingController.reset();
    gPhotoFaceTrackingActive = true;
    gFaceTrackHasFace = false;
    gFaceTrackCentered = false;
    gFaceTrackCorrectionPending = false;

    unsigned long start = millis();
    while (millis() - start < SERVO_PHOTO_TRACK_SETTLE_MS) {
        unsigned long now = millis();
        updateSharedServoMotion(now);
        if (gFaceTrackCentered) {
            break;
        }
        delay(20);
    }

    bool hadFace = gFaceTrackHasFace;
    bool centered = gFaceTrackCentered;
    gPhotoFaceTrackingActive = false;

    if (centered) {
        note = "face centered";
    } else if (hadFace) {
        note = "face tracking timeout";
    } else {
        note = "no face detected";
    }
    return centered;
}

static void updateAiStatusText() {
    if (AppState::instance().getState() != AppStateEnum::AI) return;

    if (!gWifiManager.isConnected()) {
        gFaceUI.setStatusText("No Wi-Fi", UiTheme::RED);
        return;
    }

    if (!gXiaoZhiClient.isActivated()) {
        String code = gXiaoZhiClient.getActivationCode();
        if (code.length() > 0) {
            gFaceUI.setStatusText(("Code: " + code + " @ xiaozhi.me").c_str(), UiTheme::AMBER);
        } else {
            String err = gXiaoZhiClient.getLastError();
            if (err.length() > 0) {
                gFaceUI.setStatusText(err.c_str(), UiTheme::RED, 5000);
            } else {
                gFaceUI.setStatusText("Requesting code...", UiTheme::CYAN);
            }
        }
        return;
    }

    if (!gXiaoZhiClient.isWsConnected()) {
        String err = gXiaoZhiClient.getLastError();
        if (gXiaoZhiClient.getState() == VoiceState::ERROR && err.length() > 0) {
            gFaceUI.setStatusText(err.c_str(), UiTheme::RED);
        } else {
            gFaceUI.setStatusText("Connecting...", UiTheme::AMBER);
        }
        return;
    }

    if (gXiaoZhiClient.isPreparingListening()) {
        gFaceUI.setStatusText("Preparing mic...", UiTheme::AMBER);
        return;
    }

    VoiceState vs = gXiaoZhiClient.getState();
    switch (vs) {
        case VoiceState::LISTENING:
            gFaceUI.setStatusText(gXiaoZhiClient.isListeningStarted() ? "Listening" : "Preparing mic...",
                                  gXiaoZhiClient.isListeningStarted() ? UiTheme::CYAN : UiTheme::AMBER);
            break;
        case VoiceState::THINKING:
            gFaceUI.setStatusText("Thinking", UiTheme::AMBER);
            break;
        case VoiceState::SPEAKING:
            gFaceUI.setStatusText("Speaking", UiTheme::GREEN);
            break;
        case VoiceState::ERROR:
            {
                String err = gXiaoZhiClient.getLastError();
                gFaceUI.setStatusText(err.length() > 0 ? err.c_str() : "Error", UiTheme::RED);
            }
            break;
        default:
            gFaceUI.setStatusText("Tap to talk", UiTheme::TEXT_DIM);
            break;
    }
}

static void handleFaceTap(int x, int y) {
    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;
    AppState& appState = AppState::instance();
    gPomodoroFaceEmotionActive = false;
    addAffinity(1, "Touch");

    if (y < cy / 2) {
        appState.setEmotion(FaceEmotion::HAPPY);
        gFaceUI.setTemporaryGaze(0.0f, -0.5f, 1500);
        gServoMotionController.command(ServoMotionAction::NOD);
        M5.Speaker.tone(1400, 60);
        delay(70);
        M5.Speaker.tone(1800, 60);
    } else if (y > cy + cy / 2) {
        appState.setEmotion(FaceEmotion::SHY);
        gFaceUI.setTemporaryGaze(0.0f, 0.5f, 1500);
        gServoMotionController.lookOffset(-10.0f, SERVO_FACE_TILT_OFFSET_DEG,
                                          SERVO_FACE_MOTION_SPEED_DPS, "Face tap shy");
        M5.Speaker.tone(600, 100);
    } else if (x < cx) {
        gFaceUI.setTemporaryGaze(-1.0f, 0.0f, 1200);
        gServoMotionController.command(ServoMotionAction::LEFT);
        if (appState.getEmotion() != FaceEmotion::TRACKING) {
            appState.setEmotion(FaceEmotion::CURIOUS);
        }
        M5.Speaker.tone(1000, 50);
    } else {
        gFaceUI.setTemporaryGaze(1.0f, 0.0f, 1200);
        gServoMotionController.command(ServoMotionAction::RIGHT);
        if (appState.getEmotion() != FaceEmotion::TRACKING) {
            appState.setEmotion(FaceEmotion::CURIOUS);
        }
        M5.Speaker.tone(1100, 50);
    }
    gLastServoPoseEmotion = appState.getEmotion();
}

void uiTask(void* pvParameters) {
    const TickType_t delayTicks = pdMS_TO_TICKS(1000 / 20);

    while (true) {
        unsigned long now = millis();
        AppStateEnum state = AppState::instance().getState();
        FaceEmotion emotion = AppState::instance().getEmotion();

        if (takeDisplayLock()) {
            switch (state) {
                case AppStateEnum::FACE:
                    gFaceUI.setExpression(static_cast<int>(emotion));
                    gFaceUI.update();
                    break;

                case AppStateEnum::MENU:
                    gMenuUI.update();
                    break;

                case AppStateEnum::WIFI_INFO:
                case AppStateEnum::SYSTEM_INFO:
                    updateInfoUiData();
                    gInfoUI.update();
                    break;

                case AppStateEnum::CAMERA_DEBUG:
                    gCameraDebugUI.setFps(gCurrentFps);
                    gCameraDebugUI.update();
                    break;

                case AppStateEnum::AI_VISION:
                    gCameraDebugUI.setFps(gCurrentFps);
                    gCameraDebugUI.update();
                    break;

                case AppStateEnum::POMODORO:
                    gPomodoroUI.update();
                    break;

                case AppStateEnum::MUSIC:
                    updateMusicUiData();
                    gMusicUI.update();
                    break;

                case AppStateEnum::AFFINITY:
                    updateAffinityUiData();
                    gAffinityUI.update();
                    break;

                case AppStateEnum::AI:
                    updateAiStatusText();
                    gFaceUI.setExpression(static_cast<int>(emotion));
                    gFaceUI.update();
                    break;

                case AppStateEnum::SETTINGS:
                    gSettingsUI.update();
                    break;

                case AppStateEnum::SLEEP:
                    break;
            }
            giveDisplayLock();
        }

        gFrameCount++;
        if (now - gLastFpsCalc >= 1000) {
            gCurrentFps = gFrameCount;
            gFrameCount = 0;
            gLastFpsCalc = now;
        }

        vTaskDelay(delayTicks);
    }
}

void touchTask(void* pvParameters) {
    static GestureManager gestureManager;
    const TickType_t delayTicks = pdMS_TO_TICKS(1000 / TOUCH_SAMPLING_HZ);

    gestureManager.setCallback(gestureEventHandler);

    while (true) {
        M5.update();

        TouchPoint tp;
        tp.touched = M5.Touch.getCount() > 0;

        if (tp.touched) {
            auto t = M5.Touch.getDetail();
            tp.x = t.x;
            tp.y = t.y;
        }

        gestureManager.update(tp);

        vTaskDelay(delayTicks);
    }
}

void cameraTask(void* pvParameters) {
    const TickType_t delayTicks = pdMS_TO_TICKS(1000 / CAMERA_FPS);

    while (true) {
        if (gCameraManager.isRunning()) {
            AppStateEnum state = AppState::instance().getState();

            if (state == AppStateEnum::CAMERA_DEBUG || state == AppStateEnum::AI_VISION) {
                CameraFrame frame = gCameraManager.getDisplayFrame();

                if (frame.valid) {
                    if (takeDisplayLock(pdMS_TO_TICKS(10))) {
                        gCameraDebugUI.pushCameraFrame((uint16_t*)frame.data, frame.width, frame.height);
                        giveDisplayLock();
                    }
                    gCameraManager.releaseFrame(frame);
                }
            }
        }

        vTaskDelay(delayTicks);
    }
}

void visionTask(void* pvParameters) {
    const TickType_t delayTicks = pdMS_TO_TICKS(1000 / DETECTION_FPS);

    while (true) {
        AppStateEnum state = AppState::instance().getState();

        if (state == AppStateEnum::FACE ||
            state == AppStateEnum::CAMERA_DEBUG ||
            state == AppStateEnum::AI_VISION) {
            if (gCameraManager.isRunning() && gFaceDetector.isEnabled() && gFaceDetector.backendAvailable()) {
                CameraFrame frame = gCameraManager.getDetectionFrame();
                if (frame.valid) {
                    int frameWidth = frame.width;
                    int frameHeight = frame.height;
                    FaceResult face = gFaceDetector.detect(frame.data, frame.width, frame.height);
                    gCameraManager.releaseFrame(frame);
                    gFaceTracker.update(face);

                    if (gPhotoFaceTrackingActive &&
                        (state == AppStateEnum::CAMERA_DEBUG || state == AppStateEnum::AI_VISION)) {
                        float panDelta = 0.0f;
                        float tiltDelta = 0.0f;
                        bool hasFace = gFaceTrackingController.update(face, frameWidth, frameHeight,
                                                                      panDelta, tiltDelta);
                        gFaceTrackHasFace = hasFace;
                        gFaceTrackCentered = hasFace && gFaceTrackingController.isCentered();
                        if (hasFace && (fabsf(panDelta) > 0.01f || fabsf(tiltDelta) > 0.01f)) {
                            gFaceTrackPanDeltaDeg = panDelta;
                            gFaceTrackTiltDeltaDeg = tiltDelta;
                            gFaceTrackCorrectionPending = true;
                        }
                    }

                    if (state == AppStateEnum::FACE && face.detected) {
                        float normX = (float)(face.centerX - frameWidth / 2) / (float)(frameWidth / 2);
                        float normY = (float)(face.centerY - frameHeight / 2) / (float)(frameHeight / 2);
                        float panDelta = -normX * SERVO_FACE_TRACK_GAIN_DEG;
                        float tiltDelta = normY * SERVO_FACE_TRACK_GAIN_DEG * 0.6f;
                        if (fabsf(normX) > SERVO_FACE_TRACK_DEADBAND ||
                            fabsf(normY) > SERVO_FACE_TRACK_DEADBAND) {
                            gFaceTrackPanDeltaDeg = panDelta;
                            gFaceTrackTiltDeltaDeg = tiltDelta;
                            gFaceTrackCorrectionPending = true;
                        }
                    }

                    if (gFaceTracker.hasFace()) {
                        gFaceUI.setGazeOffset(gFaceTracker.getGazeX(), gFaceTracker.getGazeY());

                        if (state == AppStateEnum::FACE) {
                            FaceEmotion currentEmotion = AppState::instance().getEmotion();
                            if (currentEmotion == FaceEmotion::NORMAL || currentEmotion == FaceEmotion::TRACKING) {
                                AppState::instance().setEmotion(FaceEmotion::TRACKING);
                            }
                        }

                        Event detectedEvent;
                        detectedEvent.type = EventType::FACE_DETECTED;
                        EventBus::instance().publish(detectedEvent);
                    } else {
                        if (state == AppStateEnum::FACE &&
                            AppState::instance().getEmotion() == FaceEmotion::TRACKING) {
                            AppState::instance().setEmotion(FaceEmotion::NORMAL);
                        }

                        gFaceUI.setGazeOffset(0, 0);

                        Event lostEvent;
                        lostEvent.type = EventType::FACE_LOST;
                        EventBus::instance().publish(lostEvent);
                    }
                }
            }
        }

        vTaskDelay(delayTicks);
    }
}

void aiTask(void* pvParameters) {
    const TickType_t delayTicks = pdMS_TO_TICKS(20);

    while (true) {
        if (gXiaoZhiClient.isConnected()) {
            gXiaoZhiClient.process();
        }

        if (gNeedActivationRequest) {
            gNeedActivationRequest = false;
            if (!gXiaoZhiClient.isActivated()) {
                Serial.println("AI task: requesting activation code...");
                gXiaoZhiClient.requestActivationCode();
                if (gXiaoZhiClient.isActivated()) {
                    gNeedOpenAudioChannel = true;
                }
            }
        }

        if (gNeedActivationCheck) {
            gNeedActivationCheck = false;
            gXiaoZhiClient.checkActivation();
            if (gXiaoZhiClient.isActivated()) {
                gNeedOpenAudioChannel = true;
            }
        }

        if (gNeedOpenAudioChannel) {
            gNeedOpenAudioChannel = false;
            Serial.println("AI task: opening audio channel...");
            if (gXiaoZhiClient.openAudioChannel()) {
                gXiaoZhiClient.startListening();
            }
        }

        vTaskDelay(delayTicks);
    }
}

void powerTask(void* pvParameters) {
    const TickType_t delayTicks = pdMS_TO_TICKS(POWER_TASK_INTERVAL_MS);

    while (true) {
        gPowerManager.update();

        Event powerEvent;
        powerEvent.type = EventType::POWER_EVENT;
        powerEvent.floatParam = gPowerManager.getVoltage();
        EventBus::instance().publish(powerEvent);

        vTaskDelay(delayTicks);
    }
}

void networkTask(void* pvParameters) {
    const TickType_t delayTicks = pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS);

    while (true) {
        gWifiManager.update();

        if (!gNtpSynced && gWifiManager.isConnected()) {
            configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 3000)) {
                gNtpSynced = true;
                Serial.printf("NTP synced: %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
            }
        }

        if (AppState::instance().getState() == AppStateEnum::MENU) {
            gMenuUI.setWifiStatus(gWifiManager.statusText().c_str(), gWifiManager.ipString().c_str());
        }

        vTaskDelay(delayTicks);
    }
}

static void handleCameraShot() {
    if (!gStorageManager.ensureReady()) {
        gCameraDebugUI.setCaptureStatus(gStorageManager.statusText().c_str());
        gCameraDebugUI.setSdReady(false);
        return;
    }

    gCameraDebugUI.setSdReady(true);
    gCameraDebugUI.setCaptureStatus("Capturing...");

    String path = gStorageManager.nextPhotoPath();
    if (path.length() == 0) {
        gCameraDebugUI.setCaptureStatus("Path error");
        return;
    }

    String trackingNote;
    gCameraDebugUI.setCaptureStatus(gFaceDetector.backendAvailable() ?
                                    "Centering face..." :
                                    "Capturing (no face tracking)");
    prepareFaceTrackingForPhoto(trackingNote);

    String status;
    bool ok = gCameraManager.captureJpegToFile(path.c_str(), status);
    if (ok) {
        gCameraDebugUI.setLastPhotoPath(path.c_str());
        addAffinity(2, "Photo saved");
        if (trackingNote.length() > 0) {
            status += "; ";
            status += trackingNote;
        }
        gCameraDebugUI.setCaptureStatus(status.c_str());
        Serial.printf("Photo saved: %s (%s)\n", path.c_str(), trackingNote.c_str());
    } else {
        gCameraDebugUI.setCaptureStatus(status.c_str());
        Serial.printf("Photo failed: %s\n", status.c_str());
    }
}

static void setAiVisionStatus(AiVisionStatus status, const char* text, const char* result) {
    gAiVisionStatus = status;
    gAiVisionStatusText = text ? String(text) : String();
    if (result != nullptr) {
        gAiVisionResultText = result;
    }
    gCameraDebugUI.setVisionStatus(gAiVisionStatusText.c_str());
    gCameraDebugUI.setVisionResult(gAiVisionResultText.c_str());
}

static bool containsAny(const String& text, const char* a, const char* b = nullptr, const char* c = nullptr) {
    return text.indexOf(a) >= 0 ||
           (b != nullptr && text.indexOf(b) >= 0) ||
           (c != nullptr && text.indexOf(c) >= 0);
}

static DevicePageAction parseDevicePageArg(const String& page) {
    String value = page;
    value.toLowerCase();
    if (value == "face") return DevicePageAction::FACE;
    if (value == "menu") return DevicePageAction::MENU;
    if (value == "wifi") return DevicePageAction::WIFI;
    if (value == "system") return DevicePageAction::SYSTEM;
    if (value == "camera") return DevicePageAction::CAMERA;
    if (value == "music") return DevicePageAction::MUSIC;
    if (value == "pomodoro") return DevicePageAction::POMODORO;
    if (value == "ai") return DevicePageAction::AI;
    return DevicePageAction::NONE;
}

static bool parseBrightnessArg(const String& value, SystemBrightnessLevel& level) {
    String text = value;
    text.toLowerCase();
    if (text == "dim") {
        level = SystemBrightnessLevel::DIM;
        return true;
    }
    if (text == "normal") {
        level = SystemBrightnessLevel::NORMAL;
        return true;
    }
    if (text == "bright") {
        level = SystemBrightnessLevel::BRIGHT;
        return true;
    }
    return false;
}

static bool parseVolumeArg(const String& value, SystemVolumeLevel& level) {
    String text = value;
    text.toLowerCase();
    if (text == "quiet") {
        level = SystemVolumeLevel::QUIET;
        return true;
    }
    if (text == "normal") {
        level = SystemVolumeLevel::NORMAL;
        return true;
    }
    if (text == "loud") {
        level = SystemVolumeLevel::LOUD;
        return true;
    }
    return false;
}

static DeviceSleepAction parseSleepArg(const String& value) {
    String text = value;
    text.toLowerCase();
    if (text == "wake") return DeviceSleepAction::WAKE;
    if (text == "sleep") return DeviceSleepAction::SLEEP;
    return DeviceSleepAction::NONE;
}

static void handleXiaoZhiTranscriptLegacy(const String& text) {
    String lower = text;
    lower.toLowerCase();
    bool asksDescribe = containsAny(text, "这是什么", "看到什么", "看到了什么") ||
                        containsAny(text, "识别", "认一下", "是什么东西") ||
                        (text.indexOf("看到") >= 0 && text.indexOf("什么") >= 0);
    bool asksOpen = containsAny(text, "打开摄像头", "开启摄像头", "开摄像头") ||
                    containsAny(text, "打开相机", "开启相机", "让我看看") ||
                    containsAny(lower, "open camera", "camera on", "look");
    bool asksPomodoro = containsAny(text, "打开番茄钟", "进入番茄钟", "番茄钟模式") ||
                        containsAny(text, "打开计时器", "进入计时器", "专注计时") ||
                        (containsAny(text, "打开", "进入", "切到") &&
                         containsAny(text, "番茄钟", "计时器", "专注"));
    bool asksCapture = containsAny(text, "拍张照", "拍照", "保存照片") ||
                       containsAny(text, "照一张", "拍一张", "照张相") ||
                       containsAny(text, "拍个照") ||
                       containsAny(lower, "take photo", "capture", "save photo");
    bool asksMusicPlay = containsAny(text, "播放音乐", "放音乐", "播放歌曲") ||
                         containsAny(text, "放首歌") ||
                         containsAny(lower, "play music", "play song");
    bool asksMusicPause = containsAny(text, "暂停音乐", "暂停播放", "音乐暂停");
    bool asksMusicStop = containsAny(text, "停止音乐", "关掉音乐", "关闭音乐") ||
                         containsAny(text, "别放了");
    bool asksMusicNext = containsAny(text, "下一首", "换一首", "切歌") ||
                         containsAny(text, "换个歌");
    bool asksReact = containsAny(text, "开心一下", "卖个萌", "害羞一下") ||
                     containsAny(text, "装困", "惊讶一下", "好奇一下") ||
                     containsAny(text, "卖萌", "做个鬼脸") ||
                     containsAny(text, "生个病", "装病");

    bool asksServoLeft = containsAny(text, "看左边", "往左看", "转左边") ||
                         containsAny(lower, "look left", "turn left");
    bool asksServoRight = containsAny(text, "看右边", "往右看", "转右边") ||
                          containsAny(lower, "look right", "turn right");
    bool asksServoUp = containsAny(text, "抬头", "往上看", "看上面") ||
                       containsAny(lower, "look up", "head up");
    bool asksServoDown = containsAny(text, "低头", "往下看", "看下面") ||
                         containsAny(lower, "look down", "head down");
    bool asksServoCenter = containsAny(text, "回中", "回正", "看前面") ||
                           containsAny(text, "回到中间") ||
                           containsAny(lower, "center servo", "look center", "look forward");
    bool asksServoNod = containsAny(text, "点点头", "点头") ||
                        containsAny(lower, "nod");
    bool asksServoShake = containsAny(text, "摇摇头", "摇头") ||
                          containsAny(lower, "shake head");
    bool asksServoDance = containsAny(text, "跳舞", "跳一段舞", "来段舞") ||
                          containsAny(text, "扭一扭", "蹦迪") ||
                          containsAny(lower, "dance");
    bool asksServoRelease = containsAny(text, "释放舵机", "松开舵机", "关闭舵机") ||
                            containsAny(lower, "release servo");
    bool asksServo = asksServoLeft || asksServoRight || asksServoUp || asksServoDown ||
                     asksServoCenter || asksServoNod || asksServoShake || asksServoDance ||
                     asksServoRelease;

    if (asksServo) {
        gPendingAiTool = PendingAiTool::SERVO_CONTROL;
        gPendingAiCallId = -1;
        if (asksServoRelease) gPendingAiServoAction = ServoAiAction::RELEASE;
        else if (asksServoDance) gPendingAiServoAction = ServoAiAction::DANCE;
        else if (asksServoNod) gPendingAiServoAction = ServoAiAction::NOD;
        else if (asksServoShake) gPendingAiServoAction = ServoAiAction::SHAKE;
        else if (asksServoLeft) gPendingAiServoAction = ServoAiAction::LEFT;
        else if (asksServoRight) gPendingAiServoAction = ServoAiAction::RIGHT;
        else if (asksServoUp) gPendingAiServoAction = ServoAiAction::UP;
        else if (asksServoDown) gPendingAiServoAction = ServoAiAction::DOWN;
        else gPendingAiServoAction = ServoAiAction::CENTER;
        Serial.println("AI transcript fallback: servo control");
    } else if (asksDescribe) {
        gPendingAiTool = PendingAiTool::VISION_DESCRIBE;
        gPendingAiCallId = -1;
        Serial.println("AI transcript fallback: describe scene");
    } else if (asksCapture) {
        gPendingAiTool = PendingAiTool::CAMERA_CAPTURE_PHOTO;
        gPendingAiCallId = -1;
        Serial.println("AI transcript fallback: capture photo");
    } else if (asksPomodoro) {
        gPendingAiTool = PendingAiTool::POMODORO_OPEN;
        gPendingAiCallId = -1;
        gPendingAiIntParam = 0;
        gPendingAiBoolParam = false;
        if (containsAny(text, "50分钟", "五十分钟")) {
            gPendingAiIntParam = 50;
            gPendingAiBoolParam = true;
        } else if (containsAny(text, "25分钟", "二十五分钟")) {
            gPendingAiIntParam = 25;
            gPendingAiBoolParam = true;
        } else if (containsAny(text, "15分钟", "十五分钟")) {
            gPendingAiIntParam = 15;
            gPendingAiBoolParam = true;
        } else if (containsAny(text, "5分钟", "五分钟")) {
            gPendingAiIntParam = 5;
            gPendingAiBoolParam = true;
        }
        if (containsAny(text, "开始", "启动", "倒计时") || containsAny(text, "计时")) {
            gPendingAiBoolParam = true;
        }
        Serial.println("AI transcript fallback: open pomodoro");
    } else if (asksMusicPlay) {
        gPendingAiTool = PendingAiTool::MUSIC_CONTROL;
        gPendingAiCallId = -1;
        gPendingAiMusicAction = MusicAction::PLAY_PAUSE;
        Serial.println("AI transcript fallback: music play");
    } else if (asksMusicPause) {
        gPendingAiTool = PendingAiTool::MUSIC_CONTROL;
        gPendingAiCallId = -1;
        gPendingAiMusicAction = MusicAction::PLAY_PAUSE;
        Serial.println("AI transcript fallback: music pause");
    } else if (asksMusicStop) {
        gPendingAiTool = PendingAiTool::MUSIC_CONTROL;
        gPendingAiCallId = -1;
        gPendingAiMusicAction = MusicAction::STOP;
        Serial.println("AI transcript fallback: music stop");
    } else if (asksMusicNext) {
        gPendingAiTool = PendingAiTool::MUSIC_CONTROL;
        gPendingAiCallId = -1;
        gPendingAiMusicAction = MusicAction::NEXT;
        Serial.println("AI transcript fallback: music next");
    } else if (asksReact) {
        gPendingAiTool = PendingAiTool::PET_REACT;
        gPendingAiCallId = -1;
        if (containsAny(text, "开心", "卖萌", "萌")) {
            gPendingAiPetReaction = PetReaction::HAPPY;
        } else if (containsAny(text, "害羞")) {
            gPendingAiPetReaction = PetReaction::SHY;
        } else if (containsAny(text, "好奇")) {
            gPendingAiPetReaction = PetReaction::CURIOUS;
        } else if (containsAny(text, "困", "装困")) {
            gPendingAiPetReaction = PetReaction::SLEEPY;
        } else if (containsAny(text, "惊讶")) {
            gPendingAiPetReaction = PetReaction::SURPRISED;
        } else if (containsAny(text, "病", "装病", "生个病")) {
            gPendingAiPetReaction = PetReaction::SICK;
        } else {
            gPendingAiPetReaction = PetReaction::HAPPY;
        }
        Serial.println("AI transcript fallback: pet react");
    } else if (asksOpen) {
        gPendingAiTool = PendingAiTool::CAMERA_OPEN;
        gPendingAiCallId = -1;
        Serial.println("AI transcript fallback: open camera");
    }
}

static void handleXiaoZhiTranscript(const String& text) {
    String lower = text;
    lower.toLowerCase();

    bool asksStatus = containsAny(text, "现在状态怎么样", "系统状态", "网络状态") ||
                      containsAny(lower, "device status", "system status", "network status");
    bool asksOpenSystem = containsAny(text, "打开系统页", "进入系统页", "系统页");
    bool asksOpenMusic = containsAny(text, "打开音乐", "进入音乐页", "音乐页");
    bool asksOpenCameraPage = containsAny(text, "打开相机页", "进入相机页", "打开相机页面");
    bool asksOpenPomodoroPage = containsAny(text, "打开番茄钟", "进入番茄钟", "打开计时器");
    bool asksGoHome = containsAny(text, "回到主页", "回主页", "打开主页");
    bool asksBrighter = containsAny(text, "调亮一点", "亮一点", "调亮屏幕");
    bool asksDimmer = containsAny(text, "调暗一点", "暗一点", "调暗屏幕");
    bool asksLouder = containsAny(text, "大声一点", "音量大一点", "调大音量");
    bool asksQuieter = containsAny(text, "小声一点", "音量小一点", "调小音量");
    bool asksSleep = containsAny(text, "睡觉", "休眠", "进入睡眠");
    bool asksWake = containsAny(text, "唤醒", "醒来", "叫醒");

    if (asksStatus) {
        gPendingAiTool = PendingAiTool::DEVICE_STATUS;
        gPendingAiCallId = -1;
        gPendingAiStringParam = "brief";
        Serial.println("AI transcript fallback: device status");
        return;
    }

    if (asksOpenSystem || asksOpenMusic || asksOpenCameraPage || asksOpenPomodoroPage ||
        asksGoHome || asksBrighter || asksDimmer || asksLouder || asksQuieter ||
        asksSleep || asksWake) {
        gPendingDeviceControl = DeviceControlRequest{};
        if (asksOpenSystem) gPendingDeviceControl.page = DevicePageAction::SYSTEM;
        else if (asksOpenMusic) gPendingDeviceControl.page = DevicePageAction::MUSIC;
        else if (asksOpenCameraPage) gPendingDeviceControl.page = DevicePageAction::CAMERA;
        else if (asksOpenPomodoroPage) gPendingDeviceControl.page = DevicePageAction::POMODORO;
        else if (asksGoHome) gPendingDeviceControl.page = DevicePageAction::FACE;

        if (asksBrighter) {
            gPendingDeviceControl.hasBrightness = true;
            gPendingDeviceControl.brightness = SystemBrightnessLevel::BRIGHT;
        } else if (asksDimmer) {
            gPendingDeviceControl.hasBrightness = true;
            gPendingDeviceControl.brightness = SystemBrightnessLevel::DIM;
        }

        if (asksLouder) {
            gPendingDeviceControl.hasVolume = true;
            gPendingDeviceControl.volume = SystemVolumeLevel::LOUD;
        } else if (asksQuieter) {
            gPendingDeviceControl.hasVolume = true;
            gPendingDeviceControl.volume = SystemVolumeLevel::QUIET;
        }

        if (asksWake) gPendingDeviceControl.sleep = DeviceSleepAction::WAKE;
        else if (asksSleep) gPendingDeviceControl.sleep = DeviceSleepAction::SLEEP;

        gPendingAiTool = PendingAiTool::DEVICE_CONTROL;
        gPendingAiCallId = -1;
        Serial.println("AI transcript fallback: device control");
        return;
    }

    handleXiaoZhiTranscriptLegacy(text);
}

static void handleXiaoZhiTranscriptModern(const String& text) {
    String lower = text;
    lower.toLowerCase();

    bool asksStatus = containsAny(text, "现在状态怎么样", "系统状态", "网络状态") ||
                      containsAny(lower, "device status", "system status", "network status");
    bool asksOpenSystem = containsAny(text, "打开系统页", "进入系统页", "系统页");
    bool asksOpenMusic = containsAny(text, "打开音乐", "进入音乐页", "音乐页");
    bool asksOpenCameraPage = containsAny(text, "打开相机页", "进入相机页", "打开相机页面");
    bool asksOpenPomodoroPage = containsAny(text, "打开番茄钟", "进入番茄钟", "打开计时器");
    bool asksOpenWifiPage = containsAny(text, "打开网络页", "打开wifi页", "进入网络页");
    bool asksOpenAiPage = containsAny(text, "打开小智", "回到小智", "进入小智");
    bool asksOpenMenu = containsAny(text, "打开菜单", "回到菜单", "菜单页");
    bool asksGoHome = containsAny(text, "回到主页", "回主页", "打开主页");
    bool asksBrighter = containsAny(text, "调亮一点", "亮一点", "调亮屏幕");
    bool asksDimmer = containsAny(text, "调暗一点", "暗一点", "调暗屏幕");
    bool asksLouder = containsAny(text, "大声一点", "音量大一点", "调大音量");
    bool asksQuieter = containsAny(text, "小声一点", "音量小一点", "调小音量");
    bool asksSleep = containsAny(text, "睡觉", "休眠", "进入睡眠");
    bool asksWake = containsAny(text, "唤醒", "醒来", "叫醒");

    if (asksStatus) {
        gPendingAiTool = PendingAiTool::DEVICE_STATUS;
        gPendingAiCallId = -1;
        gPendingAiStringParam = "brief";
        Serial.println("AI transcript modern: device status");
        return;
    }

    if (asksOpenSystem || asksOpenMusic || asksOpenCameraPage || asksOpenPomodoroPage ||
        asksOpenWifiPage || asksOpenAiPage || asksOpenMenu || asksGoHome ||
        asksBrighter || asksDimmer || asksLouder || asksQuieter || asksSleep || asksWake) {
        gPendingDeviceControl = DeviceControlRequest{};
        if (asksOpenSystem) gPendingDeviceControl.page = DevicePageAction::SYSTEM;
        else if (asksOpenMusic) gPendingDeviceControl.page = DevicePageAction::MUSIC;
        else if (asksOpenCameraPage) gPendingDeviceControl.page = DevicePageAction::CAMERA;
        else if (asksOpenPomodoroPage) gPendingDeviceControl.page = DevicePageAction::POMODORO;
        else if (asksOpenWifiPage) gPendingDeviceControl.page = DevicePageAction::WIFI;
        else if (asksOpenAiPage) gPendingDeviceControl.page = DevicePageAction::AI;
        else if (asksOpenMenu) gPendingDeviceControl.page = DevicePageAction::MENU;
        else if (asksGoHome) gPendingDeviceControl.page = DevicePageAction::FACE;

        if (asksBrighter) {
            gPendingDeviceControl.hasBrightness = true;
            gPendingDeviceControl.brightness = SystemBrightnessLevel::BRIGHT;
        } else if (asksDimmer) {
            gPendingDeviceControl.hasBrightness = true;
            gPendingDeviceControl.brightness = SystemBrightnessLevel::DIM;
        }

        if (asksLouder) {
            gPendingDeviceControl.hasVolume = true;
            gPendingDeviceControl.volume = SystemVolumeLevel::LOUD;
        } else if (asksQuieter) {
            gPendingDeviceControl.hasVolume = true;
            gPendingDeviceControl.volume = SystemVolumeLevel::QUIET;
        }

        if (asksWake) gPendingDeviceControl.sleep = DeviceSleepAction::WAKE;
        else if (asksSleep) gPendingDeviceControl.sleep = DeviceSleepAction::SLEEP;

        gPendingAiTool = PendingAiTool::DEVICE_CONTROL;
        gPendingAiCallId = -1;
        Serial.println("AI transcript modern: device control");
        return;
    }

    handleXiaoZhiTranscript(text);
}

static void processCameraOpen(int callId) {
    gXiaoZhiClient.pauseForForegroundTool();
    bool ok = ensureAiVisionPreview("Camera on");
    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId,
            ok ? "Camera is open. I can look through the CoreS3 camera now." : "Camera failed to open.",
            !ok);
    }
}

static void processCameraClose(int callId) {
    closeAiVisionPreview();
    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId, "Camera is closed.", false);
    }
}

static void processCameraCapturePhoto(int callId) {
    gXiaoZhiClient.pauseForForegroundTool();

    if (!ensureAiVisionPreview("Capturing...")) {
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId,
                "Camera failed to open, cannot capture photo.", true);
        }
        return;
    }

    if (!gStorageManager.ensureReady()) {
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId,
                "SD card not available: " + gStorageManager.statusText(), true);
        }
        return;
    }

    String path = gStorageManager.nextPhotoPath();
    if (path.length() == 0) {
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId, "Failed to generate photo path.", true);
        }
        return;
    }

    String trackingNote;
    setAiVisionStatus(AiVisionStatus::PREVIEW,
                      gFaceDetector.backendAvailable() ? "Centering face..." : "Capturing photo",
                      gAiVisionResultText.c_str());
    prepareFaceTrackingForPhoto(trackingNote);

    String status;
    bool ok = gCameraManager.captureJpegToFile(path.c_str(), status);
    if (ok) {
        Serial.printf("Voice photo saved: %s (%s)\n", path.c_str(), trackingNote.c_str());
        addAffinity(2, "Photo saved");
        String result = "Photo saved: " + path;
        if (trackingNote.length() > 0) {
            result += "; ";
            result += trackingNote;
        }
        setAiVisionStatus(AiVisionStatus::DONE, "Photo saved", trackingNote.c_str());
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId, result, false);
        }
    } else {
        Serial.printf("Voice photo failed: %s\n", status.c_str());
        setAiVisionStatus(AiVisionStatus::ERROR, status.c_str(), status.c_str());
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId, "Photo capture failed: " + status, true);
        }
    }
}

static void processVisionDescribe(int callId) {
    gXiaoZhiClient.pauseForForegroundTool();

    if (!gXiaoZhiClient.hasVisionEndpoint()) {
        setAiVisionStatus(AiVisionStatus::ERROR, "Vision service missing", "No vision endpoint");
        if (callId >= 0) gXiaoZhiClient.queueMcpToolTextResult(callId, "Vision service is not available.", true);
        return;
    }

    if (!ensureAiVisionPreview("Preparing vision")) {
        if (callId >= 0) gXiaoZhiClient.queueMcpToolTextResult(callId, "Camera is unavailable.", true);
        return;
    }

    setAiVisionStatus(AiVisionStatus::RECOGNIZING, "Recognizing...", "");
    CameraJpeg jpeg;
    String status;
    if (!gCameraManager.captureJpegToMemory(jpeg, status)) {
        setAiVisionStatus(AiVisionStatus::ERROR, status.c_str(), status.c_str());
        if (callId >= 0) gXiaoZhiClient.queueMcpToolTextResult(callId, "Camera capture failed: " + status, true);
        return;
    }

    String description;
    bool visionOk = gVisionClient.describeImage(gXiaoZhiClient.getVisionUrl().c_str(),
                                                gXiaoZhiClient.getVisionToken().c_str(),
                                                jpeg.data, jpeg.length,
                                                description, status);
    gCameraManager.releaseJpeg(jpeg);

    if (!visionOk) {
        setAiVisionStatus(AiVisionStatus::ERROR, status.c_str(), status.c_str());
        if (callId >= 0) gXiaoZhiClient.queueMcpToolTextResult(callId, "Recognition failed: " + status, true);
        return;
    }

    setAiVisionStatus(AiVisionStatus::DONE, "Vision result", description.c_str());
    if (callId >= 0) gXiaoZhiClient.queueMcpToolTextResult(callId, "I can see: " + description, false);
}

static int pomodoroPresetForMinutes(int minutes) {
    switch (minutes) {
        case 25: return 0;
        case 5:  return 1;
        case 15: return 2;
        case 50: return 3;
        default: return -1;
    }
}

static const char* pomodoroMinutesLabel(int minutes) {
    switch (minutes) {
        case 25: return "Focus 25m";
        case 5:  return "Short 5m";
        case 15: return "Long 15m";
        case 50: return "Deep 50m";
        default: return "Custom";
    }
}

static void processPomodoroOpen(int callId) {
    int minutes = gPendingAiIntParam;
    bool autoStart = gPendingAiBoolParam;

    gXiaoZhiClient.pauseForForegroundTool();

    if (AppState::instance().getState() == AppStateEnum::AI_VISION) {
        gCameraManager.stopCapture();
        gCameraDebugUI.setCameraReady(false);
        setAiVisionStatus(AiVisionStatus::IDLE, "Vision closed", "");
    }

    gPomodoroReturnTo = AppStateEnum::AI;
    AppState::instance().setState(AppStateEnum::POMODORO);

    if (minutes > 0) {
        int preset = pomodoroPresetForMinutes(minutes);
        if (preset >= 0) {
            gPomodoroUI.selectPreset(preset);
        }
    }

    if (autoStart && gPomodoroUI.getState() == PomodoroState::IDLE) {
        gPomodoroUI.togglePause();
    }

    gPomodoroUI.markDirty();

    String resultMsg = "Pomodoro page opened.";
    if (minutes > 0) {
        int preset = pomodoroPresetForMinutes(minutes);
        if (preset >= 0) {
            resultMsg = String("Pomodoro set to ") + pomodoroMinutesLabel(minutes) +
                       (autoStart ? " and started." : ".");
        }
    }

    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId, resultMsg, false);
    }

    Serial.printf("AI request: open Pomodoro minutes=%d autoStart=%d\n", minutes, autoStart);
}

static void processMusicControl(int callId) {
    MusicAction action = gPendingAiMusicAction;
    String resultMsg;
    bool isError = false;

    switch (action) {
        case MusicAction::PLAY_PAUSE: {
            MusicPlaybackState mstate = gMusicManager.state();
            if (mstate == MusicPlaybackState::STOPPED || mstate == MusicPlaybackState::ERROR) {
                if (gMusicManager.trackCount() == 0) {
                    gMusicManager.scan();
                }
                if (gMusicManager.trackCount() == 0) {
                    resultMsg = "No music files found on SD card.";
                    isError = true;
                    break;
                }
                String title = gMusicManager.trackName(0);
                gMusicManager.play(0);
                resultMsg = "Playing: " + title;

                if (AppState::instance().getState() == AppStateEnum::AI) {
                    gXiaoZhiClient.pauseForForegroundTool();
                    gMusicReturnTo = AppStateEnum::AI;
                    AppState::instance().setState(AppStateEnum::MUSIC);
                }
            } else if (mstate == MusicPlaybackState::PLAYING) {
                gMusicManager.togglePause();
                resultMsg = "Music paused.";
            } else if (mstate == MusicPlaybackState::PAUSED) {
                gMusicManager.togglePause();
                resultMsg = "Music resumed.";
            }
            gMusicUI.markDirty();
            break;
        }
        case MusicAction::STOP:
            gMusicManager.stop();
            gMusicUI.markDirty();
            resultMsg = "Music stopped.";
            break;
        case MusicAction::NEXT:
            if (gMusicManager.trackCount() == 0) {
                gMusicManager.scan();
            }
            if (gMusicManager.trackCount() == 0) {
                resultMsg = "No music files found on SD card.";
                isError = true;
            } else {
                int nextIdx = gMusicManager.currentIndex() + 1;
                if (nextIdx >= gMusicManager.trackCount()) nextIdx = 0;
                String title = gMusicManager.trackName(nextIdx);
                gMusicManager.next();
                gMusicUI.markDirty();
                resultMsg = "Playing: " + title;
            }
            break;
        default:
            resultMsg = "Unknown music action.";
            isError = true;
            break;
    }

    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId, resultMsg, isError);
    }
}

static String processDeviceStatus(int callId, const String& detail) {
    String normalized = detail;
    normalized.toLowerCase();
    if (normalized != "full") {
        normalized = "brief";
    }
    String result = buildDeviceStatusResponse(normalized);
    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId, result, false);
    }
    return result;
}

static String processDeviceControl(int callId) {
    DeviceControlRequest request = gPendingDeviceControl;
    gPendingDeviceControl = DeviceControlRequest{};

    if (request.page == DevicePageAction::NONE &&
        !request.hasBrightness &&
        !request.hasVolume &&
        request.sleep == DeviceSleepAction::NONE) {
        String response = "这次没有收到有效的设备控制指令。";
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId, response, true);
        }
        return response;
    }

    if (request.sleep == DeviceSleepAction::WAKE && AppState::instance().getState() == AppStateEnum::SLEEP) {
        AppState::instance().setState(AppStateEnum::FACE);
        if (takeDisplayLock()) {
            wakeFromSleepMode();
            gFaceUI.wake();
            giveDisplayLock();
        }
    } else if (request.sleep == DeviceSleepAction::WAKE && gPowerManager.isSleeping()) {
        wakeFromSleepMode();
    }

    String response = "我已经处理好了。";
    bool changed = false;

    if (request.page != DevicePageAction::NONE) {
        if (navigateDevicePage(request.page)) {
            response = String("我已经切到") + devicePageLabel(request.page) + "了。";
            changed = true;
        }
    }

    if (request.hasBrightness) {
        applyBrightnessLevel(request.brightness, true);
        response = String("我已经") + brightnessActionLabel(request.brightness);
        if (request.page != DevicePageAction::NONE) {
            response += "，也切到" + devicePageLabel(request.page) + "了。";
        } else {
            response += "。";
        }
        changed = true;
    }

    if (request.hasVolume) {
        applyVolumeLevel(request.volume);
        response = String("我已经") + volumeActionLabel(request.volume);
        if (request.hasBrightness) {
            response = String("我已经") + brightnessActionLabel(request.brightness) + "，也" +
                       volumeActionLabel(request.volume) + "。";
        } else if (request.page != DevicePageAction::NONE) {
            response += "，也切到" + devicePageLabel(request.page) + "了。";
        } else {
            response += "。";
        }
        changed = true;
    }

    if (request.sleep == DeviceSleepAction::SLEEP) {
        AppState::instance().setState(AppStateEnum::SLEEP);
        if (takeDisplayLock()) {
            enterSleepMode();
            giveDisplayLock();
        }
        response = "我先睡一会儿了。";
        changed = true;
    }

    if (!changed) {
        response = "这次没有收到有效的设备控制指令。";
    }

    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId, response, !changed);
    }
    return response;
}

static FaceEmotion petReactionToEmotion(PetReaction reaction) {
    switch (reaction) {
        case PetReaction::HAPPY: return FaceEmotion::HAPPY;
        case PetReaction::SHY: return FaceEmotion::SHY;
        case PetReaction::CURIOUS: return FaceEmotion::CURIOUS;
        case PetReaction::SLEEPY: return FaceEmotion::SLEEPY;
        case PetReaction::SURPRISED: return FaceEmotion::SURPRISED;
        case PetReaction::SICK: return FaceEmotion::SICK;
        default: return FaceEmotion::HAPPY;
    }
}

static const char* petReactionName(PetReaction reaction) {
    switch (reaction) {
        case PetReaction::HAPPY: return "Happy";
        case PetReaction::SHY: return "Shy";
        case PetReaction::CURIOUS: return "Curious";
        case PetReaction::SLEEPY: return "Sleepy";
        case PetReaction::SURPRISED: return "Surprised";
        case PetReaction::SICK: return "Sick";
        default: return "Happy";
    }
}

static void processPetReact(int callId) {
    PetReaction reaction = gPendingAiPetReaction;
    if (reaction == PetReaction::NONE) {
        reaction = PetReaction::HAPPY;
    }

    FaceEmotion emotion = petReactionToEmotion(reaction);
    gPetReactPrevEmotion = AppState::instance().getEmotion();
    gPetReactEmotion = emotion;
    gPetReactActive = true;
    gPetReactUntil = millis() + PET_REACT_DURATION_MS;
    AppState::instance().setEmotion(emotion);
    gLastServoPoseEmotion = emotion;
    applyServoPoseForPetReaction(reaction);

    const char* name = petReactionName(reaction);
    gFaceUI.setStatusText(name, UiTheme::CYAN, PET_REACT_DURATION_MS);
    addAffinity(2, name);

    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId,
            String("CoreS3 reacts: ") + name + "!", false);
    }

    Serial.printf("AI pet react: %s\n", name);
}

static void processServoControl(int callId) {
    ServoAiAction action = gPendingAiServoAction;
    if (action == ServoAiAction::NONE) {
        action = ServoAiAction::CENTER;
    }

    ServoMotionAction motionAction = toServoMotionAction(action);
    bool ok = false;
    if (motionAction == ServoMotionAction::RELEASE) {
        ok = gServoMotionController.release();
    } else if (motionAction == ServoMotionAction::DANCE) {
        String musicNote = "dance without music";
        if (gXiaoZhiClient.isAudioChannelOpen()) {
            gXiaoZhiClient.pauseForForegroundTool();
        }
        ok = gServoMotionController.ensureReady() &&
             gServoMotionController.command(motionAction);
        if (ok) {
            gDanceActive = true;
            gDancePrevEmotion = AppState::instance().getEmotion();
            AppState::instance().setEmotion(FaceEmotion::HAPPY);
            gLastServoPoseEmotion = FaceEmotion::HAPPY;
            gFaceUI.setStatusText("Dance!", UiTheme::PINK, 6500);
            addAffinity(5, "Dance");
            startDanceMusic(musicNote);
        } else {
            gDanceStartedMusic = false;
        }
        String result = String("Servo action: ") + servoAiActionName(action);
        result += ok ? String("; ") + musicNote : String(" failed: ") + gServoMotionController.statusText();
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId, result, !ok);
        }
        Serial.printf("AI servo control: %s ok=%d\n", servoAiActionName(action), ok ? 1 : 0);
        return;
    } else {
        ok = gServoMotionController.ensureReady() &&
             gServoMotionController.command(motionAction);
    }

    String result = String("Servo action: ") + servoAiActionName(action);
    if (!ok) {
        result += " failed: ";
        result += gServoMotionController.statusText();
    }

    if (callId >= 0) {
        gXiaoZhiClient.queueMcpToolTextResult(callId, result, !ok);
    }

    Serial.printf("AI servo control: %s ok=%d\n", servoAiActionName(action), ok ? 1 : 0);
}

static void processPendingAiTool() {
    PendingAiTool tool = gPendingAiTool;
    if (tool == PendingAiTool::NONE) return;
    gPendingAiTool = PendingAiTool::NONE;

    int callId = gPendingAiCallId;
    gPendingAiCallId = -1;

    switch (tool) {
        case PendingAiTool::CAMERA_OPEN:
            processCameraOpen(callId);
            break;
        case PendingAiTool::CAMERA_CLOSE:
            processCameraClose(callId);
            break;
        case PendingAiTool::CAMERA_CAPTURE_PHOTO:
            processCameraCapturePhoto(callId);
            break;
        case PendingAiTool::VISION_DESCRIBE:
            processVisionDescribe(callId);
            break;
        case PendingAiTool::POMODORO_OPEN:
            processPomodoroOpen(callId);
            break;
        case PendingAiTool::MUSIC_CONTROL:
            processMusicControl(callId);
            break;
        case PendingAiTool::PET_REACT:
            processPetReact(callId);
            break;
        case PendingAiTool::SERVO_CONTROL:
            processServoControl(callId);
            break;
        case PendingAiTool::DEVICE_STATUS:
            processDeviceStatus(callId, gPendingAiStringParam);
            gPendingAiStringParam = "";
            break;
        case PendingAiTool::DEVICE_CONTROL:
            processDeviceControl(callId);
            break;
        default:
            break;
    }
}

static bool ensureAiVisionPreview(const char* status) {
    if (AppState::instance().getState() != AppStateEnum::AI_VISION) {
        AppState::instance().setState(AppStateEnum::AI_VISION);
    }

    setAiVisionStatus(AiVisionStatus::OPENING, status ? status : "Opening camera", nullptr);
    return openForegroundCameraNow("AI Vision tool");
}

static void closeAiVisionPreview() {
    if (AppState::instance().getState() == AppStateEnum::AI_VISION) {
        gCameraManager.stopCapture();
        gCameraDebugUI.setCameraReady(false);
        setAiVisionStatus(AiVisionStatus::IDLE, "Vision closed", "");
        AppState::instance().setState(AppStateEnum::AI);
    }
}

static String describeAiVisionScene(JsonObjectConst arguments, bool& ok) {
    ok = false;
    String prompt = "";
    if (!arguments.isNull()) {
        prompt = arguments["prompt"] | "";
    }
    (void)prompt;

    if (!gXiaoZhiClient.hasVisionEndpoint()) {
        setAiVisionStatus(AiVisionStatus::ERROR, "Vision service missing", "No vision endpoint");
        return "Vision service is not available on this XiaoZhi session.";
    }

    if (!ensureAiVisionPreview("Preparing vision")) {
        return "I tried to open the camera, but the camera is unavailable.";
    }

    setAiVisionStatus(AiVisionStatus::RECOGNIZING, "Recognizing...", "");
    CameraJpeg jpeg;
    String status;
    if (!gCameraManager.captureJpegToMemory(jpeg, status)) {
        setAiVisionStatus(AiVisionStatus::ERROR, status.c_str(), status.c_str());
        return "Camera capture failed: " + status;
    }

    String description;
    bool visionOk = gVisionClient.describeImage(gXiaoZhiClient.getVisionUrl().c_str(),
                                                gXiaoZhiClient.getVisionToken().c_str(),
                                                jpeg.data, jpeg.length,
                                                description, status);
    gCameraManager.releaseJpeg(jpeg);

    if (!visionOk) {
        setAiVisionStatus(AiVisionStatus::ERROR, status.c_str(), status.c_str());
        return "I opened the camera, but visual recognition failed: " + status;
    }

    ok = true;
    setAiVisionStatus(AiVisionStatus::DONE, "Vision result", description.c_str());
    return String("I can see: ") + description;
}

static void handleXiaoZhiMcpTool(const String& toolName, JsonObjectConst arguments, int callId) {
    Serial.printf("AI MCP tool (async): %s callId=%d\n", toolName.c_str(), callId);

    if (toolName == "self.camera.open") {
        gPendingAiTool = PendingAiTool::CAMERA_OPEN;
        gPendingAiCallId = callId;
        return;
    }

    if (toolName == "self.camera.close") {
        gPendingAiTool = PendingAiTool::CAMERA_CLOSE;
        gPendingAiCallId = callId;
        return;
    }

    if (toolName == "self.camera.capture_photo") {
        gPendingAiTool = PendingAiTool::CAMERA_CAPTURE_PHOTO;
        gPendingAiCallId = callId;
        return;
    }

    if (toolName == "self.vision.describe_scene") {
        gPendingAiTool = PendingAiTool::VISION_DESCRIBE;
        gPendingAiCallId = callId;
        return;
    }

    if (toolName == "self.pomodoro.open") {
        int minutes = 0;
        bool autoStart = false;
        if (!arguments.isNull()) {
            if (!arguments["minutes"].isNull()) {
                minutes = arguments["minutes"].as<int>();
            }
            if (!arguments["auto_start"].isNull()) {
                autoStart = arguments["auto_start"].as<bool>();
            }
        }
        gPendingAiTool = PendingAiTool::POMODORO_OPEN;
        gPendingAiCallId = callId;
        gPendingAiIntParam = minutes;
        gPendingAiBoolParam = autoStart;
        return;
    }

    if (toolName == "self.music.control") {
        MusicAction action = MusicAction::NONE;
        if (!arguments.isNull()) {
            String actionStr = arguments["action"] | "";
            if (actionStr == "play_pause") action = MusicAction::PLAY_PAUSE;
            else if (actionStr == "stop") action = MusicAction::STOP;
            else if (actionStr == "next") action = MusicAction::NEXT;
        }
        gPendingAiTool = PendingAiTool::MUSIC_CONTROL;
        gPendingAiCallId = callId;
        gPendingAiMusicAction = action;
        return;
    }

    if (toolName == "self.pet.react") {
        PetReaction reaction = PetReaction::NONE;
        if (!arguments.isNull()) {
            String reactionStr = arguments["reaction"] | "";
            if (reactionStr == "happy") reaction = PetReaction::HAPPY;
            else if (reactionStr == "shy") reaction = PetReaction::SHY;
            else if (reactionStr == "curious") reaction = PetReaction::CURIOUS;
            else if (reactionStr == "sleepy") reaction = PetReaction::SLEEPY;
            else if (reactionStr == "surprised") reaction = PetReaction::SURPRISED;
            else if (reactionStr == "sick") reaction = PetReaction::SICK;
        }
        gPendingAiTool = PendingAiTool::PET_REACT;
        gPendingAiCallId = callId;
        gPendingAiPetReaction = reaction;
        return;
    }

    if (toolName == "self.servo.control") {
        ServoAiAction action = ServoAiAction::NONE;
        if (!arguments.isNull()) {
            String actionStr = arguments["action"] | "";
            if (actionStr == "center") action = ServoAiAction::CENTER;
            else if (actionStr == "left") action = ServoAiAction::LEFT;
            else if (actionStr == "right") action = ServoAiAction::RIGHT;
            else if (actionStr == "up") action = ServoAiAction::UP;
            else if (actionStr == "down") action = ServoAiAction::DOWN;
            else if (actionStr == "nod") action = ServoAiAction::NOD;
            else if (actionStr == "shake") action = ServoAiAction::SHAKE;
            else if (actionStr == "dance") action = ServoAiAction::DANCE;
            else if (actionStr == "release") action = ServoAiAction::RELEASE;
        }
        gPendingAiTool = PendingAiTool::SERVO_CONTROL;
        gPendingAiCallId = callId;
        gPendingAiServoAction = action;
        return;
    }

    if (toolName == "self.device.status") {
        String detail = "brief";
        if (!arguments.isNull()) {
            detail = arguments["detail"] | "brief";
        }
        gPendingAiTool = PendingAiTool::DEVICE_STATUS;
        gPendingAiCallId = callId;
        gPendingAiStringParam = detail;
        return;
    }

    if (toolName == "self.device.control") {
        DeviceControlRequest request;
        if (!arguments.isNull()) {
            request.page = parseDevicePageArg(arguments["page"] | "");

            String brightnessStr = arguments["brightness"] | "";
            SystemBrightnessLevel brightness = SystemBrightnessLevel::BRIGHT;
            if (parseBrightnessArg(brightnessStr, brightness)) {
                request.hasBrightness = true;
                request.brightness = brightness;
            }

            String volumeStr = arguments["volume"] | "";
            SystemVolumeLevel volume = SystemVolumeLevel::NORMAL;
            if (parseVolumeArg(volumeStr, volume)) {
                request.hasVolume = true;
                request.volume = volume;
            }

            request.sleep = parseSleepArg(arguments["sleep"] | "");
        }
        gPendingDeviceControl = request;
        gPendingAiTool = PendingAiTool::DEVICE_CONTROL;
        gPendingAiCallId = callId;
        return;
    }

    gXiaoZhiClient.queueMcpToolTextResult(callId, "Unknown CoreS3 tool: " + toolName, true);
}

static void gestureEventHandler(const GestureEvent& event) {
    AppState& appState = AppState::instance();
    AppStateEnum currentState = appState.getState();

    Event gestureEvent;
    gestureEvent.type = EventType::GESTURE_EVENT;
    gestureEvent.intParam = static_cast<int>(event.type);
    EventBus::instance().publish(gestureEvent);

    switch (currentState) {
        case AppStateEnum::FACE:
            switch (event.type) {
                case GestureType::RIGHT_SWIPE:
                    requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "face right swipe");
                    appState.setState(AppStateEnum::MENU);
                    break;
                case GestureType::LEFT_SWIPE:
                    requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "face left swipe");
                    appState.setState(AppStateEnum::AI);
                    break;
                case GestureType::DOWN_SWIPE:
                    requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "face down swipe");
                    updateAffinityUiData();
                    appState.setState(AppStateEnum::AFFINITY);
                    break;
                case GestureType::UP_SWIPE:
                    requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "face up swipe");
                    gSettingsUI.setBrightness(static_cast<int>(gUserBrightnessLevel));
                    gSettingsUI.setVolume(static_cast<int>(gUserVolumeLevel));
                    appState.setState(AppStateEnum::SETTINGS);
                    break;
                case GestureType::SINGLE_TAP:
                    requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "face tap");
                    handleFaceTap(event.endX, event.endY);
                    break;
                case GestureType::DOUBLE_TAP:
                    requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "face double tap");
                    appState.setState(AppStateEnum::AI);
                    break;
                case GestureType::LONG_PRESS:
                    appState.setState(AppStateEnum::SLEEP);
                    if (takeDisplayLock()) {
                        enterSleepMode();
                        giveDisplayLock();
                    }
                    break;
                default:
                    break;
            }
            break;

        case AppStateEnum::AI:
            switch (event.type) {
                case GestureType::RIGHT_SWIPE:
                    gXiaoZhiClient.closeAudioChannel();
                    appState.setState(AppStateEnum::FACE);
                    break;
                case GestureType::SINGLE_TAP:
                    if (gXiaoZhiClient.isActivated()) {
                        if (gXiaoZhiClient.getState() == VoiceState::LISTENING) {
                            gXiaoZhiClient.stopListening();
                        } else {
                            gXiaoZhiClient.startListening();
                        }
                    } else if (gWifiManager.isConnected()) {
                        gNeedActivationRequest = true;
                    }
                    break;
                case GestureType::LONG_PRESS:
                    gXiaoZhiClient.closeAudioChannel();
                    appState.setState(AppStateEnum::FACE);
                    break;
                default:
                    break;
            }
            break;

        case AppStateEnum::MENU: {
            MenuHitZone hit = gMenuUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP) {
                if (hit == MenuHitZone::MENU_HIT_BACK) {
                    appState.setState(AppStateEnum::FACE);
                    break;
                }
                if (hit == MenuHitZone::MENU_HIT_APP) {
                    switch (gMenuUI.appAt(event.endX, event.endY)) {
                        case 0: appState.setState(AppStateEnum::WIFI_INFO); break;
                        case 1: appState.setState(AppStateEnum::CAMERA_DEBUG); break;
                        case 2: gPomodoroReturnTo = AppStateEnum::MENU; appState.setState(AppStateEnum::POMODORO); break;
                        case 3: gMusicReturnTo = AppStateEnum::MENU; appState.setState(AppStateEnum::MUSIC); break;
                        case 4: appState.setState(AppStateEnum::SYSTEM_INFO); break;
                    }
                    break;
                }
            }
            switch (event.type) {
                case GestureType::LEFT_SWIPE:
                    appState.setState(AppStateEnum::FACE);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::WIFI_INFO:
        case AppStateEnum::SYSTEM_INFO: {
            InfoHitZone hit = gInfoUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP && hit == InfoHitZone::INFO_HIT_BACK) {
                appState.setState(AppStateEnum::MENU);
                break;
            }
            switch (event.type) {
                case GestureType::LEFT_SWIPE:
                    appState.setState(AppStateEnum::MENU);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::CAMERA_DEBUG: {
            CameraHitZone hit = gCameraDebugUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP) {
                if (hit == CameraHitZone::HIT_BACK) {
                    appState.setState(AppStateEnum::MENU);
                    break;
                }
                if (hit == CameraHitZone::HIT_SHOT) {
                    handleCameraShot();
                    break;
                }
            }
            switch (event.type) {
                case GestureType::LEFT_SWIPE:
                    appState.setState(AppStateEnum::MENU);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::AI_VISION: {
            CameraHitZone hit = gCameraDebugUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP && hit == CameraHitZone::HIT_BACK) {
                closeAiVisionPreview();
                break;
            }
            switch (event.type) {
                case GestureType::RIGHT_SWIPE:
                    closeAiVisionPreview();
                    break;
                case GestureType::LONG_PRESS:
                    closeAiVisionPreview();
                    gXiaoZhiClient.closeAudioChannel();
                    appState.setState(AppStateEnum::FACE);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::POMODORO: {
            PomoHitZone hit = gPomodoroUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP) {
                if (hit == PomoHitZone::POMO_HIT_BACK) {
                    appState.setState(gPomodoroReturnTo);
                    break;
                }
                if (hit == PomoHitZone::POMO_HIT_START) {
                    gPomodoroUI.togglePause();
                    break;
                }
                if (hit == PomoHitZone::POMO_HIT_RESET) {
                    gPomodoroUI.reset();
                    gPomodoroUI.markDirty();
                    break;
                }
                gPomodoroUI.togglePause();
                break;
            }
            switch (event.type) {
                case GestureType::LEFT_SWIPE:
                    appState.setState(gPomodoroReturnTo);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::MUSIC: {
            MusicHitZone hit = gMusicUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP) {
                if (hit == MusicHitZone::MUSIC_HIT_BACK) {
                    appState.setState(gMusicReturnTo);
                    break;
                }
                if (hit == MusicHitZone::MUSIC_HIT_PLAY) {
                    gMusicManager.togglePause();
                    gMusicUI.markDirty();
                    break;
                }
                if (hit == MusicHitZone::MUSIC_HIT_STOP) {
                    gMusicManager.stop();
                    gMusicUI.markDirty();
                    break;
                }
                if (hit == MusicHitZone::MUSIC_HIT_NEXT) {
                    gMusicManager.next();
                    gMusicUI.markDirty();
                    break;
                }
            }
            switch (event.type) {
                case GestureType::LEFT_SWIPE:
                    appState.setState(gMusicReturnTo);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::AFFINITY: {
            AffinityHitZone hit = gAffinityUI.hitTest(event.endX, event.endY);
            if (event.type == GestureType::SINGLE_TAP &&
                hit == AffinityHitZone::AFFINITY_HIT_BACK) {
                appState.setState(AppStateEnum::FACE);
                break;
            }
            switch (event.type) {
                case GestureType::LEFT_SWIPE:
                case GestureType::UP_SWIPE:
                    appState.setState(AppStateEnum::FACE);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::SETTINGS: {
            if (event.type == GestureType::LEFT_SWIPE ||
                event.type == GestureType::DOWN_SWIPE) {
                appState.setState(AppStateEnum::FACE);
                break;
            }
            if (event.type == GestureType::SINGLE_TAP) {
                SettingsHitZone hit = gSettingsUI.hitTest(event.endX, event.endY);
                switch (hit) {
                    case SettingsHitZone::BACK:
                        appState.setState(AppStateEnum::FACE);
                        break;
                    case SettingsHitZone::BRIGHTNESS_DIM:
                        applyBrightnessLevel(SystemBrightnessLevel::DIM);
                        gSettingsUI.setBrightness(0);
                        break;
                    case SettingsHitZone::BRIGHTNESS_NORMAL:
                        applyBrightnessLevel(SystemBrightnessLevel::NORMAL);
                        gSettingsUI.setBrightness(1);
                        break;
                    case SettingsHitZone::BRIGHTNESS_BRIGHT:
                        applyBrightnessLevel(SystemBrightnessLevel::BRIGHT);
                        gSettingsUI.setBrightness(2);
                        break;
                    case SettingsHitZone::VOLUME_QUIET:
                        applyVolumeLevel(SystemVolumeLevel::QUIET);
                        gSettingsUI.setVolume(0);
                        M5.Speaker.tone(800, 80);
                        break;
                    case SettingsHitZone::VOLUME_NORMAL:
                        applyVolumeLevel(SystemVolumeLevel::NORMAL);
                        gSettingsUI.setVolume(1);
                        M5.Speaker.tone(1000, 80);
                        break;
                    case SettingsHitZone::VOLUME_LOUD:
                        applyVolumeLevel(SystemVolumeLevel::LOUD);
                        gSettingsUI.setVolume(2);
                        M5.Speaker.tone(1200, 80);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case AppStateEnum::SLEEP:
            switch (event.type) {
                case GestureType::SINGLE_TAP:
                case GestureType::DOUBLE_TAP:
                    appState.setState(AppStateEnum::FACE);
                    if (takeDisplayLock()) {
                        wakeFromSleepMode();
                        gFaceUI.wake();
                        giveDisplayLock();
                    }
                    break;
                default:
                    break;
            }
            break;
    }
}

static void stateChangeHandler(AppStateEnum state) {
    if (state != AppStateEnum::CAMERA_DEBUG && state != AppStateEnum::AI_VISION) {
        gNeedForegroundCameraStart = false;
        gForegroundCameraReason = nullptr;
    }
    if (state != AppStateEnum::CAMERA_DEBUG && state != AppStateEnum::AI_VISION) {
        gPhotoFaceTrackingActive = false;
    }
    if (state != AppStateEnum::FACE &&
        state != AppStateEnum::CAMERA_DEBUG &&
        state != AppStateEnum::AI_VISION) {
        gFaceDetector.setEnabled(false);
        if (gCameraManager.isRunning()) {
            gCameraManager.stopCapture();
        }
    }
    if (state != AppStateEnum::AFFINITY) {
        gAffinityUI.hide();
    }
    if (state != AppStateEnum::SETTINGS) {
        gSettingsUI.hide();
    }

    switch (state) {
        case AppStateEnum::FACE:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            gFaceUI.clearStatusText();
            gFaceUI.setSpeakingMouthOpen(false);
            if (gPomodoroFaceEmotionPending) {
                gPomodoroFaceEmotionPending = false;
                gPomodoroFaceEmotionActive = true;
                gPomodoroFaceEmotionUntil = millis() + POMODORO_FACE_EMOTION_MS;
                AppState::instance().setEmotion(gPomodoroFaceEmotion);
            } else {
                gPomodoroFaceEmotionActive = false;
                AppState::instance().setEmotion(FaceEmotion::NORMAL);
                if (gAffinityManager.value() >= 75) {
                    AppState::instance().setEmotion(FaceEmotion::HAPPY);
                    gFaceUI.setStatusText("Best Friend", UiTheme::PINK, 1800);
                    gAffinityWelcomeActive = true;
                    gAffinityWelcomeUntil = millis() + 1800;
                } else if (gAffinityManager.value() < 25) {
                    AppState::instance().setEmotion(FaceEmotion::SHY);
                    gFaceUI.setStatusText("Shy", UiTheme::CYAN, 1800);
                    gAffinityWelcomeActive = true;
                    gAffinityWelcomeUntil = millis() + 1800;
                }
            }
            gSickActive = false;
            gSickUntil = 0;
            gActivationCodeRequested = false;
            requestFaceVisionBurst(millis(), FACE_VISION_TOUCH_BURST_MS, "enter face");
            gLastServoPoseEmotion = AppState::instance().getEmotion();
            applyServoPoseForEmotion(gLastServoPoseEmotion);
            break;

        case AppStateEnum::AI:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            stopMusicForExclusiveAudio();
            gFaceDetector.setEnabled(false);
            gCameraManager.stopCapture();
            gFaceUI.clearStatusText();
            gPetReactActive = false;
            gAiListeningAffinityAwarded = false;
            if (gWifiManager.isConnected()) {
                AppState::instance().setEmotion(FaceEmotion::LISTENING);
                if (gXiaoZhiClient.isActivated()) {
                    if (gXiaoZhiClient.isAudioChannelOpen()) {
                        gXiaoZhiClient.resumeFromForegroundTool();
                    } else {
                        gNeedOpenAudioChannel = true;
                    }
                } else if (!gXiaoZhiClient.hasActivationCode() && !gActivationCodeRequested) {
                    gNeedActivationRequest = true;
                    gActivationCodeRequested = true;
                }
            } else {
                AppState::instance().setEmotion(FaceEmotion::SURPRISED);
                Serial.println("AI: Wi-Fi not connected");
            }
            gLastServoPoseEmotion = AppState::instance().getEmotion();
            applyServoPoseForEmotion(gLastServoPoseEmotion);
            break;

        case AppStateEnum::AI_VISION: {
            gMenuUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            gCameraDebugUI.setMode(CameraViewMode::AI_VISION);
            gCameraDebugUI.setVisionStatus(gAiVisionStatusText.c_str());
            gCameraDebugUI.setVisionResult(gAiVisionResultText.c_str());
            gCameraDebugUI.show(CameraViewMode::AI_VISION);
            if (gFaceDetector.backendAvailable()) {
                gFaceDetector.setEnabled(true);
            }
            bool cameraReady = gCameraManager.isRunning();
            gCameraDebugUI.setCameraReady(cameraReady);
            if (cameraReady) {
                if (gAiVisionStatus == AiVisionStatus::IDLE) {
                    setAiVisionStatus(AiVisionStatus::PREVIEW, "Camera on", "");
                }
            } else if (gAiVisionStatus == AiVisionStatus::IDLE) {
                setAiVisionStatus(AiVisionStatus::OPENING, "Opening camera", "");
            }
            if (!cameraReady) {
                requestForegroundCameraStart("AI Vision page");
            }
            Serial.println(cameraReady ? "AI Vision preview already running" : "AI Vision camera opening deferred");
            break;
        }

        case AppStateEnum::MENU:
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            gMenuUI.setWifiStatus(gWifiManager.statusText().c_str(), gWifiManager.ipString().c_str());
            gMenuUI.show();
            break;

        case AppStateEnum::WIFI_INFO:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gMusicUI.hide();
            updateInfoUiData();
            gInfoUI.show(InfoPageMode::WIFI);
            break;

        case AppStateEnum::SYSTEM_INFO:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gMusicUI.hide();
            updateInfoUiData();
            gInfoUI.show(InfoPageMode::SYSTEM);
            break;

        case AppStateEnum::CAMERA_DEBUG:
            gMenuUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            gStorageManager.ensureReady();
            gCameraDebugUI.setSdReady(gStorageManager.isReady());
            gCameraDebugUI.setCaptureStatus(gStorageManager.statusText().c_str());
            gCameraDebugUI.show(CameraViewMode::DEBUG);
            if (gFaceDetector.backendAvailable()) {
                gFaceDetector.setEnabled(true);
            }
            {
                bool cameraReady = gCameraManager.isRunning();
                gCameraDebugUI.setCameraReady(cameraReady);
                if (cameraReady) {
                    gCameraDebugUI.setCaptureStatus(gStorageManager.statusText().c_str());
                } else {
                    gCameraDebugUI.setCaptureStatus("Opening camera...");
                    requestForegroundCameraStart("Camera debug page");
                }
                Serial.println(cameraReady ? "Camera debug already running" : "Camera debug opening deferred");
            }
            break;

        case AppStateEnum::POMODORO:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            gPomodoroUI.show();
            break;

        case AppStateEnum::MUSIC:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            if (gXiaoZhiClient.isAudioChannelOpen()) {
                gXiaoZhiClient.pauseForForegroundTool();
            }
            if (gMusicManager.state() == MusicPlaybackState::STOPPED ||
                gMusicManager.state() == MusicPlaybackState::ERROR ||
                gMusicManager.trackCount() == 0) {
                gMusicManager.scan();
            }
            updateMusicUiData();
            gMusicUI.show();
            break;

        case AppStateEnum::AFFINITY:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            updateAffinityUiData();
            gAffinityUI.show();
            break;

        case AppStateEnum::SETTINGS:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            gSettingsUI.show();
            break;

        case AppStateEnum::SLEEP:
            if (takeDisplayLock()) {
                drawLcdDarkBackground();
                M5.Lcd.setTextDatum(TC_DATUM);
                M5.Lcd.setTextSize(1);
                M5.Lcd.setTextColor(UiTheme::BLUE, UiTheme::BG);
                M5.Lcd.drawString("Sleep Mode", DISPLAY_WIDTH / 2, 100);
                M5.Lcd.setTextColor(UiTheme::TEXT_DIM, UiTheme::BG);
                M5.Lcd.drawString("Tap to wake", DISPLAY_WIDTH / 2, 120);
                M5.Lcd.setTextDatum(TL_DATUM);
                enterSleepMode();
                giveDisplayLock();
            }
            break;
    }
}

static void aiStateHandler(VoiceState voiceState) {
    AppState& appState = AppState::instance();
    if (voiceState == VoiceState::LISTENING || voiceState == VoiceState::SPEAKING) {
        stopMusicForExclusiveAudio();
    }
    if (appState.getState() == AppStateEnum::AI) {
        switch (voiceState) {
            case VoiceState::IDLE:       appState.setEmotion(FaceEmotion::NORMAL); break;
            case VoiceState::LISTENING:
                appState.setEmotion(FaceEmotion::LISTENING);
                if (!gAiListeningAffinityAwarded && gXiaoZhiClient.isListeningStarted()) {
                    addAffinity(1, "AI listening");
                    gAiListeningAffinityAwarded = true;
                }
                break;
            case VoiceState::THINKING:   appState.setEmotion(FaceEmotion::THINKING); break;
            case VoiceState::SPEAKING:   appState.setEmotion(FaceEmotion::SPEAKING); break;
            case VoiceState::ERROR:      appState.setEmotion(FaceEmotion::SURPRISED); break;
        }
    }
    Event aiEvent;
    aiEvent.type = EventType::AI_STATE_CHANGE;
    aiEvent.intParam = static_cast<int>(voiceState);
    EventBus::instance().publish(aiEvent);
}

static void lowBatteryHandler(float voltage) {
    AppState& appState = AppState::instance();
    stopFaceVisionBurst("low battery");
    if (appState.getState() == AppStateEnum::FACE) {
        appState.setEmotion(FaceEmotion::SLEEPY);
    }
}

static void handleEvent(const Event& event) {
}

static FaceEmotion pomodoroEmotionForPreset(int presetIndex) {
    switch (presetIndex) {
        case 0: return FaceEmotion::HAPPY;
        case 1: return FaceEmotion::SHY;
        case 2: return FaceEmotion::CURIOUS;
        case 3: return FaceEmotion::SLEEPY;
        default: return FaceEmotion::NORMAL;
    }
}

static void handlePomodoroComplete(int presetIndex) {
    gPomodoroCompletionPreset = presetIndex;
    gPomodoroCompletionPending = true;
}

static void startPomodoroMelody() {
    stopMusicForExclusiveAudio();
    gPomodoroMelodyActive = true;
    gPomodoroMelodyErrorPrinted = false;
    gPomodoroMelodyIndex = 0;
    gPomodoroNextNoteAt = 0;
}

static void processPomodoroCompletion(unsigned long now) {
    (void)now;
    if (!gPomodoroCompletionPending) return;

    int presetIndex = gPomodoroCompletionPreset;
    gPomodoroCompletionPending = false;
    gPomodoroFaceEmotion = pomodoroEmotionForPreset(presetIndex);
    gPomodoroFaceEmotionPending = true;
    gPomodoroFaceEmotionActive = false;
    addAffinity(3, "Pomodoro done");
    startPomodoroMelody();
    Serial.printf("Pomodoro complete: preset=%d emotion=%d\n",
                  presetIndex, static_cast<int>(gPomodoroFaceEmotion));
}

static void updatePomodoroMelody(unsigned long now) {
    if (!gPomodoroMelodyActive || now < gPomodoroNextNoteAt) return;

    constexpr uint8_t melodyCount = sizeof(POMODORO_DONE_MELODY) / sizeof(POMODORO_DONE_MELODY[0]);
    if (gPomodoroMelodyIndex >= melodyCount) {
        gPomodoroMelodyActive = false;
        return;
    }

    const PomodoroMelodyNote& note = POMODORO_DONE_MELODY[gPomodoroMelodyIndex];
    bool ok = M5.Speaker.tone(note.frequency, note.durationMs, 0, gPomodoroMelodyIndex == 0);
    if (!ok && !gPomodoroMelodyErrorPrinted) {
        gPomodoroMelodyErrorPrinted = true;
        Serial.println("Pomodoro melody: speaker tone failed");
    }
    gPomodoroNextNoteAt = now + note.durationMs + note.gapMs;
    gPomodoroMelodyIndex++;
}

static void bootStep(const char* msg, int line) {
    int y = 38 + line * 11;
    M5.Lcd.fillRect(10, y, DISPLAY_WIDTH - 20, 11, UiTheme::BG);
    M5.Lcd.setCursor(14, y);
    M5.Lcd.setTextColor(UiTheme::TEXT_DIM, UiTheme::BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.printf("%02d %s", line, msg);
    Serial.println(msg);
}

static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

static bool createTaskChecked(TaskFunction_t taskFn, const char* name,
                              uint32_t stackSize, UBaseType_t priority,
                              TaskHandle_t* handle, BaseType_t core) {
    if (handle != nullptr && *handle != nullptr) {
        return true;
    }
    BaseType_t result = xTaskCreatePinnedToCore(taskFn, name, stackSize, nullptr,
                                                priority, handle, core);
    if (result != pdPASS) {
        Serial.printf("Task create failed: %s, stack=%lu, core=%ld\n",
                      name, static_cast<unsigned long>(stackSize),
                      static_cast<long>(core));
        return false;
    }
    Serial.printf("Task created: %s\n", name);
    return true;
}

static bool ensureCameraTaskStarted() {
    return createTaskChecked(cameraTask, "Camera", CAMERA_TASK_STACK_SIZE,
                             CAMERA_TASK_PRIORITY, &cameraTaskHandle, 0);
}

static void requestForegroundCameraStart(const char* reason) {
    gForegroundCameraReason = reason;
    gNeedForegroundCameraStart = true;
}

static bool openForegroundCameraNow(const char* reason) {
    AppStateEnum state = AppState::instance().getState();
    if (state != AppStateEnum::CAMERA_DEBUG && state != AppStateEnum::AI_VISION) {
        return false;
    }
    gNeedForegroundCameraStart = false;
    gForegroundCameraReason = nullptr;

    gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
    gFaceVisionBurstUntil = 0;
    gFaceVisionStartPending = false;
    gFaceVisionPendingReason = nullptr;
    if (gFaceDetector.backendAvailable()) {
        gFaceDetector.setEnabled(true);
    }

    // A user-opened Camera/AI Vision page should not be blocked by a previous
    // background detection failure cooldown.
    gCameraManager.resetFailure();
    bool cameraReady = ensureCameraTaskStarted();
    cameraReady = cameraReady && (gCameraManager.isInitialized() || gCameraManager.begin());
    cameraReady = cameraReady && gCameraManager.startCapture();

    if (!cameraReady) {
        Serial.printf("Foreground camera start failed: %s; retrying reinit\n",
                      reason ? reason : "camera");
        gCameraManager.end();
        vTaskDelay(pdMS_TO_TICKS(80));
        gCameraManager.resetFailure();
        M5.In_I2C.release();
        vTaskDelay(pdMS_TO_TICKS(10));
        cameraReady = ensureCameraTaskStarted();
        cameraReady = cameraReady && gCameraManager.begin();
        cameraReady = cameraReady && gCameraManager.startCapture();
    }

    gCameraDebugUI.setCameraReady(cameraReady);
    if (state == AppStateEnum::CAMERA_DEBUG) {
        gCameraDebugUI.setCaptureStatus(cameraReady ? gStorageManager.statusText().c_str()
                                                    : "Camera start failed");
    } else {
        setAiVisionStatus(cameraReady ? AiVisionStatus::PREVIEW : AiVisionStatus::ERROR,
                          cameraReady ? "Camera on" : "Camera failed",
                          cameraReady ? gAiVisionResultText.c_str() : "Camera unavailable");
    }

    Serial.printf("Foreground camera %s: %s\n",
                  cameraReady ? "ready" : "failed",
                  reason ? reason : "camera");
    return cameraReady;
}

static void processForegroundCameraStart() {
    if (!gNeedForegroundCameraStart) {
        return;
    }

    AppStateEnum state = AppState::instance().getState();
    if (state != AppStateEnum::CAMERA_DEBUG && state != AppStateEnum::AI_VISION) {
        gNeedForegroundCameraStart = false;
        gForegroundCameraReason = nullptr;
        return;
    }

    const char* reason = gForegroundCameraReason;
    gNeedForegroundCameraStart = false;
    gForegroundCameraReason = nullptr;
    openForegroundCameraNow(reason);
}

static bool isBrownoutSafeModeActive(unsigned long now) {
    return gBrownoutSafeUntil != 0 &&
           static_cast<long>(now - gBrownoutSafeUntil) < 0;
}

static bool isFaceVisionBurstActive() {
    return gFaceVisionRuntime == FaceVisionRuntime::BURST;
}

static void requestFaceVisionBurst(unsigned long now, unsigned long durationMs, const char* reason) {
    if (!FACE_DETECTION_ENABLED_ON_BOOT || !gFaceDetector.backendAvailable()) {
        return;
    }
    if (AppState::instance().getState() != AppStateEnum::FACE) {
        return;
    }
    if (isBrownoutSafeModeActive(now) || gPowerManager.isLowBattery()) {
        gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
        gFaceVisionNextIdleScanAt = now + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
        gFaceVisionStartPending = false;
        gFaceDetector.setEnabled(false);
        gCameraManager.stopCapture();
        Serial.printf("Face vision skipped: %s (%s)\n",
                      isBrownoutSafeModeActive(now) ? "brownout safe mode" : "low battery",
                      reason ? reason : "");
        return;
    }

    unsigned long newDeadline = now + durationMs;
    if (gFaceVisionRuntime == FaceVisionRuntime::BURST &&
        static_cast<long>(newDeadline - gFaceVisionBurstUntil) < 0) {
        newDeadline = gFaceVisionBurstUntil;
    }
    gFaceVisionRuntime = FaceVisionRuntime::BURST;
    gFaceVisionBurstUntil = newDeadline;
    gFaceVisionNextIdleScanAt = newDeadline + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
    gFaceVisionStartPending = true;
    gFaceVisionPendingReason = reason;
    Serial.printf("Face vision queued: %s, %lums\n",
                  reason ? reason : "burst",
                  static_cast<unsigned long>(durationMs));
}

static bool startFaceVisionCapture(unsigned long now) {
    (void)now;
    bool cameraOk = ensureCameraTaskStarted();
    cameraOk = cameraOk && (gCameraManager.isInitialized() || gCameraManager.begin());
    cameraOk = cameraOk && gCameraManager.startCapture();
    if (!cameraOk) {
        gFaceDetector.setEnabled(false);
        gCameraManager.stopCapture();
        gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
        gFaceVisionBurstUntil = 0;
        gFaceVisionStartPending = false;
        gFaceVisionNextIdleScanAt = millis() + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
        Serial.printf("Face vision start failed: %s\n",
                      gFaceVisionPendingReason ? gFaceVisionPendingReason : "burst");
        return false;
    }

    gFaceDetector.setEnabled(true);
    gFaceVisionStartPending = false;
    Serial.printf("Face vision started: %s\n",
                  gFaceVisionPendingReason ? gFaceVisionPendingReason : "burst");
    return true;
}

static void stopFaceVisionBurst(const char* reason) {
    if (gFaceVisionRuntime != FaceVisionRuntime::BURST &&
        !gFaceDetector.isEnabled() &&
        !gCameraManager.isRunning()) {
        return;
    }

    gFaceDetector.setEnabled(false);
    if (AppState::instance().getState() == AppStateEnum::FACE) {
        gCameraManager.stopCapture();
        if (AppState::instance().getEmotion() == FaceEmotion::TRACKING) {
            AppState::instance().setEmotion(FaceEmotion::NORMAL);
        }
        gFaceUI.setGazeOffset(0, 0);
        gFaceTracker.update(FaceResult{});
    }
    gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
    gFaceVisionBurstUntil = 0;
    gFaceVisionStartPending = false;
    gFaceVisionPendingReason = nullptr;
    gFaceVisionNextIdleScanAt = millis() + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
    Serial.printf("Face vision stopped: %s\n", reason ? reason : "idle");
}

static void updateFaceVisionRuntime(unsigned long now) {
    AppStateEnum state = AppState::instance().getState();
    if (state != AppStateEnum::FACE) {
        if (gFaceVisionRuntime == FaceVisionRuntime::BURST) {
            gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
            gFaceVisionBurstUntil = 0;
            gFaceVisionStartPending = false;
            gFaceVisionNextIdleScanAt = now + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
            Serial.println("Face vision paused: left face");
        }
        return;
    }

    if (isBrownoutSafeModeActive(now) || gPowerManager.isLowBattery()) {
        if (gFaceVisionRuntime == FaceVisionRuntime::BURST ||
            gFaceDetector.isEnabled() ||
            gCameraManager.isRunning()) {
            stopFaceVisionBurst(isBrownoutSafeModeActive(now) ? "brownout safe mode" : "low battery");
        }
        gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
        gFaceVisionNextIdleScanAt = now + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
        return;
    }

    if (gFaceVisionRuntime == FaceVisionRuntime::BURST) {
        if (static_cast<long>(now - gFaceVisionBurstUntil) >= 0) {
            stopFaceVisionBurst("burst complete");
            return;
        }
        if (gFaceVisionStartPending ||
            !gCameraManager.isRunning() ||
            !gFaceDetector.isEnabled()) {
            startFaceVisionCapture(now);
        }
        return;
    }

    if (gFaceVisionNextIdleScanAt == 0) {
        gFaceVisionNextIdleScanAt = now + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
        gFaceVisionRuntime = FaceVisionRuntime::IDLE;
        return;
    }

    if (static_cast<long>(now - gFaceVisionNextIdleScanAt) >= 0) {
        requestFaceVisionBurst(now, FACE_VISION_IDLE_RESCAN_BURST_MS, "idle rescan");
    }
}

void setup() {
    Serial.begin(115200);
    unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 1000) {
        delay(10);
    }
    Serial.println("\n\n=== CoreS3 AI Pet Boot ===");

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    displayMutex = xSemaphoreCreateMutex();
    applyBrightnessLevel(SystemBrightnessLevel::NORMAL, true);
    drawLcdDarkBackground();
    M5.Lcd.setTextColor(UiTheme::TEXT, UiTheme::BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setCursor(12, 8);
    M5.Lcd.println("CoreS3 AI Pet // Boot");
    gBootResetReason = esp_reset_reason();
    esp_reset_reason_t resetReason = gBootResetReason;
    if (resetReason == ESP_RST_BROWNOUT) {
        gBrownoutSafeUntil = millis() + BROWNOUT_SAFE_MODE_MS;
        Serial.printf("Brownout safe mode active for %lums\n",
                      static_cast<unsigned long>(BROWNOUT_SAFE_MODE_MS));
    }
    M5.Lcd.setCursor(12, 24);
    M5.Lcd.printf("Reset: %s\n", resetReasonName(resetReason));
    Serial.printf("Reset reason: %s (%d)\n", resetReasonName(resetReason), static_cast<int>(resetReason));
    Serial.println("M5.begin() OK");
    Serial.printf("Heap: %u, PSRAM free/total: %u/%u\n",
                  ESP.getFreeHeap(), ESP.getFreePsram(), ESP.getPsramSize());
    delay(200);

    bootStep("AppState", 1);
    AppState::instance().registerCallback(stateChangeHandler);

    bootStep("FaceUI sprite...", 2);
    bool faceUiOk = gFaceUI.begin();
    bootStep(faceUiOk ? "FaceUI OK" : "FaceUI FAIL", 2);

    bootStep("MenuUI...", 3);
    gMenuUI.begin();
    gInfoUI.begin();
    bootStep("MenuUI OK", 3);

    bootStep("CameraDebugUI...", 4);
    gCameraDebugUI.begin();

    bootStep("PomodoroUI...", 5);
    gPomodoroUI.begin();
    gMusicUI.begin();
    gAffinityUI.begin();
    gSettingsUI.begin();
    gServoMotionController.attach(gServoController);
    gPomodoroUI.setCompleteCallback(handlePomodoroComplete);

    bootStep("WifiManager...", 6);
    gWifiManager.begin();
    bootStep(gWifiManager.isConfigured() ? "WiFi configured" : "WiFi not configured", 6);

    bootStep("StorageManager...", 7);
    bool sdOk = gStorageManager.begin();
    gMusicManager.begin(&gStorageManager);
    applyVolumeLevel(SystemVolumeLevel::NORMAL);
    bootStep(sdOk ? "SD ready" : "SD not found (will retry)", 7);

    gAffinityManager.begin();

    bootStep("IMU Orientation...", 8);
    gImuOrientation.begin();

    bootStep("Camera deferred", 9);
    if (CAMERA_INIT_ON_BOOT) {
        bool camOk = gCameraManager.begin();
        gCameraManager.stopCapture();
        bootStep(camOk ? "Camera OK" : "Camera FAIL (non-fatal)", 9);
    }

    bootStep("FaceDetector...", 10);
    gFaceDetector.begin();
    bootStep("FaceDetector OK", 10);

    bootStep("XiaoZhiClient...", 11);
    gXiaoZhiClient.begin();
    gXiaoZhiClient.setStateCallback(aiStateHandler);
    gXiaoZhiClient.setMcpToolCallback(handleXiaoZhiMcpTool);
    gXiaoZhiClient.setTranscriptCallback(handleXiaoZhiTranscriptModern);
    bootStep("XiaoZhiClient OK", 11);

    bootStep("PowerManager...", 12);
    gPowerManager.begin();
    gPowerManager.setLowBatteryCallback(lowBatteryHandler);
    bootStep("PowerManager OK", 12);

    bootStep("EventBus...", 13);
    EventBus::instance().subscribe(EventType::FACE_DETECTED, handleEvent);
    EventBus::instance().subscribe(EventType::FACE_LOST, handleEvent);

    bootStep("Creating tasks...", 14);

    // Boot animation: eyes opening
    {
        M5Canvas bootCanvas(&M5.Lcd);
        bootCanvas.setPsram(true);
        bootCanvas.setColorDepth(16);
        if (bootCanvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
            int cx = DISPLAY_WIDTH / 2;
            int cy = DISPLAY_HEIGHT / 2;
            int eyeSpacing = 70;
            int leftX = cx - eyeSpacing / 2;
            int rightX = cx + eyeSpacing / 2;

            for (int frame = 0; frame <= 8; ++frame) {
                bootCanvas.fillSprite(UiTheme::BG);
                int openH = frame * 7;
                if (openH > 0) {
                    int eyeR = 25;
                    bootCanvas.fillCircle(leftX, cy, eyeR, TFT_WHITE);
                    bootCanvas.fillCircle(rightX, cy, eyeR, TFT_WHITE);
                    int clipH = eyeR - openH / 2;
                    if (clipH > 0) {
                        bootCanvas.fillRect(leftX - eyeR - 1, cy - eyeR - 1, eyeR * 2 + 2, clipH, UiTheme::BG);
                        bootCanvas.fillRect(leftX - eyeR - 1, cy + eyeR - clipH + 1, eyeR * 2 + 2, clipH, UiTheme::BG);
                        bootCanvas.fillRect(rightX - eyeR - 1, cy - eyeR - 1, eyeR * 2 + 2, clipH, UiTheme::BG);
                        bootCanvas.fillRect(rightX - eyeR - 1, cy + eyeR - clipH + 1, eyeR * 2 + 2, clipH, UiTheme::BG);
                    }
                    if (frame >= 4) {
                        int pupilR = (frame - 4) * 3;
                        if (pupilR > 12) pupilR = 12;
                        bootCanvas.fillCircle(leftX, cy, pupilR, TFT_BLACK);
                        bootCanvas.fillCircle(rightX, cy, pupilR, TFT_BLACK);
                        if (pupilR > 4) {
                            bootCanvas.fillCircle(leftX - 3, cy - 3, 2, TFT_WHITE);
                            bootCanvas.fillCircle(rightX - 3, cy - 3, 2, TFT_WHITE);
                        }
                    }
                }
                bootCanvas.pushSprite(0, 0);
                delay(80);
            }
            bootCanvas.deleteSprite();
        }
        if (BOOT_CHIME_ENABLED && resetReason != ESP_RST_BROWNOUT) {
            M5.Speaker.tone(800, 80);
            delay(100);
            M5.Speaker.tone(1200, 80);
            delay(100);
            M5.Speaker.tone(1600, 100);
            delay(150);
        }
    }

    createTaskChecked(uiTask, "UI", UI_TASK_STACK_SIZE, UI_TASK_PRIORITY, &uiTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(touchTask, "Touch", TOUCH_TASK_STACK_SIZE, TOUCH_TASK_PRIORITY, &touchTaskHandle, 1);
    gCameraManager.setTouchTaskHandle(touchTaskHandle);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (gFaceDetector.backendAvailable()) {
        createTaskChecked(visionTask, "Vision", VISION_TASK_STACK_SIZE, VISION_TASK_PRIORITY, &visionTaskHandle, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    } else {
        Serial.println("Camera/Vision tasks deferred: local face backend unavailable");
    }
    createTaskChecked(aiTask, "AI", AI_TASK_STACK_SIZE, AI_TASK_PRIORITY, &aiTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(powerTask, "Power", POWER_TASK_STACK_SIZE, POWER_TASK_PRIORITY, &powerTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(networkTask, "Network", NETWORK_TASK_STACK_SIZE, NETWORK_TASK_PRIORITY, &networkTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (gFaceDetector.backendAvailable() && FACE_DETECTION_ENABLED_ON_BOOT) {
        if (resetReason == ESP_RST_BROWNOUT) {
            gFaceVisionRuntime = FaceVisionRuntime::COOLDOWN;
            gFaceVisionNextIdleScanAt = millis() + FACE_VISION_IDLE_RESCAN_INTERVAL_MS;
            Serial.println("Face vision boot burst skipped: brownout safe mode");
        } else {
            requestFaceVisionBurst(millis(), FACE_VISION_BOOT_BURST_MS, "boot");
        }
    }
    Serial.println("Setup complete!");
}

void loop() {
    static unsigned long lastHeartbeat = 0;
    unsigned long now = millis();
    processPomodoroCompletion(now);
    updatePomodoroMelody(now);
    processPendingAiTool();

    if (SERIAL_DIAGNOSTIC_HEARTBEAT && millis() - lastHeartbeat >= SERIAL_HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = millis();
        Serial.printf("Alive: heap=%u, psram=%u, state=%d, wifi=%d, camera=%d\n",
                      ESP.getFreeHeap(), ESP.getFreePsram(),
                      static_cast<int>(AppState::instance().getState()),
                      gWifiManager.isConnected() ? 1 : 0,
                      gCameraManager.isRunning() ? 1 : 0);
    }

    AppStateEnum state = AppState::instance().getState();

    if (gNeedCameraDeinitForAi) {
        gNeedCameraDeinitForAi = false;
        if (gCameraManager.isRunning()) {
            gCameraManager.stopCapture();
            Serial.println("AI: camera stopped for mic");
        }
    }

    processForegroundCameraStart();
    updateFaceVisionRuntime(now);

    updateSharedServoMotion(now);
    gServoMotionController.keepAlive(now);
    updateDanceLifecycle(now);
    updateCompanionMotion(now);

    if (state == AppStateEnum::AI &&
        !gAiListeningAffinityAwarded &&
        gXiaoZhiClient.getState() == VoiceState::LISTENING &&
        gXiaoZhiClient.isListeningStarted()) {
        addAffinity(1, "AI listening");
        gAiListeningAffinityAwarded = true;
    }

    if (state == AppStateEnum::POMODORO) {
        gImuOrientation.update();
        PomoOrientation stable = gImuOrientation.getStable();
        gPomodoroUI.setOrientation(stable);
    }

    if (state == AppStateEnum::SLEEP) {
        gImuOrientation.update();
        if (gImuOrientation.isShaking()) {
            AppState::instance().setState(AppStateEnum::FACE);
            if (takeDisplayLock()) {
                wakeFromSleepMode();
                gFaceUI.wake();
                giveDisplayLock();
            }
            AppState::instance().setEmotion(FaceEmotion::SURPRISED);
            gSickActive = true;
            gSickUntil = now + 1500;
            M5.Speaker.tone(800, 100);
            Serial.println("Shake wake from sleep");
        }
    }

    if (state == AppStateEnum::FACE) {
        gImuOrientation.update();
        if (gImuOrientation.isShaking()) {
            requestFaceVisionBurst(now, FACE_VISION_TOUCH_BURST_MS, "face shake");
            if (now - gLastShakeTime > 5000) gShakeCount = 0;
            gShakeCount++;
            gLastShakeTime = now;

            if (gShakeCount >= 3) {
                AppState::instance().setEmotion(FaceEmotion::SICK);
                gFaceUI.setTemporaryGaze(0.0f, 0.3f, 2000);
                gServoMotionController.command(ServoMotionAction::SHAKE);
                addAffinity(-1, "shaken too hard");
                Serial.println("Shake x3 -> SICK, affinity -1");
            } else {
                AppState::instance().setEmotion(FaceEmotion::SURPRISED);
                gFaceUI.setTemporaryGaze(0.0f, -0.3f, 1500);
                gServoMotionController.command(ServoMotionAction::NOD);
                M5.Speaker.tone(1200, 80);
                Serial.println("Shake -> SURPRISED");
            }
            gSickActive = true;
            gSickUntil = now + SICK_EMOTION_DURATION_MS;
        }

        TwistDirection twist = gImuOrientation.consumeTwist();
        if (twist == TwistDirection::CLOCKWISE) {
            requestFaceVisionBurst(now, FACE_VISION_TOUCH_BURST_MS, "face twist");
            AppState::instance().setEmotion(FaceEmotion::CURIOUS);
            gFaceUI.setTemporaryGaze(0.8f, 0.0f, 1200);
            gServoMotionController.command(ServoMotionAction::RIGHT);
            M5.Speaker.tone(1100, 40);
            gSickActive = true;
            gSickUntil = now + 1500;
        } else if (twist == TwistDirection::COUNTER_CLOCKWISE) {
            requestFaceVisionBurst(now, FACE_VISION_TOUCH_BURST_MS, "face twist");
            AppState::instance().setEmotion(FaceEmotion::CURIOUS);
            gFaceUI.setTemporaryGaze(-0.8f, 0.0f, 1200);
            gServoMotionController.command(ServoMotionAction::LEFT);
            M5.Speaker.tone(900, 40);
            gSickActive = true;
            gSickUntil = now + 1500;
        }

        if (gSickActive && now >= gSickUntil) {
            gSickActive = false;
            if (AppState::instance().getEmotion() == FaceEmotion::SICK) {
                AppState::instance().setEmotion(FaceEmotion::NORMAL);
            }
        }

        if (gPomodoroFaceEmotionActive && now >= gPomodoroFaceEmotionUntil) {
            gPomodoroFaceEmotionActive = false;
            if (AppState::instance().getEmotion() == gPomodoroFaceEmotion) {
                AppState::instance().setEmotion(FaceEmotion::NORMAL);
            }
        }

        if (gAffinityWelcomeActive && now >= gAffinityWelcomeUntil) {
            gAffinityWelcomeActive = false;
            FaceEmotion current = AppState::instance().getEmotion();
            if (current == FaceEmotion::HAPPY || current == FaceEmotion::SHY) {
                AppState::instance().setEmotion(FaceEmotion::NORMAL);
            }
        }
    }

    if (state == AppStateEnum::MUSIC) {
        gImuOrientation.update();
        TwistDirection twist = gImuOrientation.consumeTwist();
        if (twist == TwistDirection::CLOCKWISE) {
            gMusicManager.next();
            updateMusicUiData();
            gMusicUI.markDirty();
        }
    }

    if (gPetReactActive && now >= gPetReactUntil) {
        gPetReactActive = false;
        AppStateEnum st = AppState::instance().getState();
        if (st == AppStateEnum::FACE || st == AppStateEnum::AI) {
            if (AppState::instance().getEmotion() == gPetReactEmotion) {
                AppState::instance().setEmotion(gPetReactPrevEmotion);
            }
        }
    }

    if (XIAOZHI_REAL_ACTIVATION && state == AppStateEnum::AI && !gXiaoZhiClient.isActivated() && gWifiManager.isConnected()) {
        if (now - gLastActivationCheck >= 5000) {
            gLastActivationCheck = now;
            gNeedActivationCheck = true;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
