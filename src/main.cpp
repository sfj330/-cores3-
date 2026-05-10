#include <M5CoreS3.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esp_system.h"

#include "config/app_config.h"
#include "app/app_state.h"
#include "app/gesture_manager.h"
#include "app/event_bus.h"
#include "ui/face_ui.h"
#include "ui/menu_ui.h"
#include "ui/camera_debug_ui.h"
#include "ui/pomodoro_ui.h"
#include "ui/info_ui.h"
#include "ui/music_ui.h"
#include "ui/ui_theme.h"
#include "audio/music_manager.h"
#include "vision/camera_manager.h"
#include "vision/face_detector.h"
#include "vision/face_tracker.h"
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
static volatile bool gNeedActivationRequest = false;
static volatile bool gNeedActivationCheck = false;
static volatile bool gNeedOpenAudioChannel = false;
static volatile bool gPomodoroOpenPending = false;
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

enum class AiVisionStatus {
    IDLE,
    OPENING,
    PREVIEW,
    RECOGNIZING,
    DONE,
    ERROR
};

static AiVisionStatus gAiVisionStatus = AiVisionStatus::IDLE;
static String gAiVisionStatusText = "Vision idle";
static String gAiVisionResultText = "";
static volatile bool gAiVisionOpenPending = false;
static volatile bool gAiVisionDescribePending = false;
static int gAiVisionMcpCallId = -1;

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
static void processAiVisionVoiceFallback();
static void processPomodoroOpenRequest();
static String describeAiVisionScene(JsonObjectConst arguments, bool& ok);
static bool ensureAiVisionPreview(const char* status);
static void setAiVisionStatus(AiVisionStatus status, const char* text, const char* result = nullptr);
static void updateInfoUiData();
static void updateMusicUiData();
static void stopMusicForExclusiveAudio();

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
        gMusicUI.markDirty();
    }
}

static void updateInfoUiData() {
    gInfoUI.setWifiStatus(gWifiManager.statusText().c_str(),
                          gWifiManager.ipString().c_str(),
                          gWifiManager.rssi(),
                          gWifiManager.isConfigured());
    gInfoUI.setSystemStatus(gPowerManager.getVoltage(),
                            gPowerManager.getPercentage(),
                            gPowerManager.isLowBattery(),
                            ESP.getFreeHeap() / 1024,
                            ESP.getFreePsram() / 1024,
                            gAiVisionStatusText.c_str());
}

static void updateMusicUiData() {
    gMusicUI.setState(gMusicManager.currentTitle(),
                      gMusicManager.statusText(),
                      gMusicManager.currentIndex(),
                      gMusicManager.trackCount(),
                      gMusicManager.state());
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
        gFaceUI.setStatusText("Connecting...", UiTheme::AMBER);
        return;
    }

    VoiceState vs = gXiaoZhiClient.getState();
    switch (vs) {
        case VoiceState::LISTENING:
            gFaceUI.setStatusText("Listening", UiTheme::CYAN);
            break;
        case VoiceState::THINKING:
            gFaceUI.setStatusText("Thinking", UiTheme::AMBER);
            break;
        case VoiceState::SPEAKING:
            gFaceUI.setStatusText("Speaking", UiTheme::GREEN);
            break;
        case VoiceState::ERROR:
            gFaceUI.setStatusText("Error", UiTheme::RED);
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

    if (y < cy / 2) {
        appState.setEmotion(FaceEmotion::HAPPY);
        gFaceUI.setTemporaryGaze(0.0f, -0.5f, 1500);
    } else if (y > cy + cy / 2) {
        appState.setEmotion(FaceEmotion::SHY);
        gFaceUI.setTemporaryGaze(0.0f, 0.5f, 1500);
    } else if (x < cx) {
        gFaceUI.setTemporaryGaze(-1.0f, 0.0f, 1200);
        if (appState.getEmotion() != FaceEmotion::TRACKING) {
            appState.setEmotion(FaceEmotion::CURIOUS);
        }
    } else {
        gFaceUI.setTemporaryGaze(1.0f, 0.0f, 1200);
        if (appState.getEmotion() != FaceEmotion::TRACKING) {
            appState.setEmotion(FaceEmotion::CURIOUS);
        }
    }
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

                case AppStateEnum::AI:
                    updateAiStatusText();
                    gFaceUI.setExpression(static_cast<int>(emotion));
                    gFaceUI.update();
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

        if (state == AppStateEnum::FACE || state == AppStateEnum::CAMERA_DEBUG) {
            if (gCameraManager.isRunning() && gFaceDetector.isEnabled() && gFaceDetector.backendAvailable()) {
                CameraFrame frame = gCameraManager.getDetectionFrame();
                if (frame.valid) {
                    FaceResult face = gFaceDetector.detect(frame.data, frame.width, frame.height);
                    gCameraManager.releaseFrame(frame);
                    gFaceTracker.update(face);

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
            gXiaoZhiClient.openAudioChannel();
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

    String status;
    bool ok = gCameraManager.captureJpegToFile(path.c_str(), status);
    if (ok) {
        gCameraDebugUI.setLastPhotoPath(path.c_str());
        gCameraDebugUI.setCaptureStatus(status.c_str());
        Serial.printf("Photo saved: %s\n", path.c_str());
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

static void handleXiaoZhiTranscript(const String& text) {
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

    if (asksDescribe) {
        gAiVisionDescribePending = true;
        Serial.println("AI transcript fallback: describe scene");
    } else if (asksPomodoro) {
        gPomodoroOpenPending = true;
        Serial.println("AI transcript fallback: open pomodoro");
    } else if (asksOpen) {
        gAiVisionOpenPending = true;
        Serial.println("AI transcript fallback: open camera");
    }
}

static void processAiVisionVoiceFallback() {
    if (gAiVisionDescribePending) {
        gAiVisionDescribePending = false;
        int callId = gAiVisionMcpCallId;
        gAiVisionMcpCallId = -1;

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

    if (gAiVisionOpenPending) {
        gAiVisionOpenPending = false;
        int callId = gAiVisionMcpCallId;
        gAiVisionMcpCallId = -1;

        bool ok = ensureAiVisionPreview("Camera on");
        if (callId >= 0) {
            gXiaoZhiClient.queueMcpToolTextResult(callId,
                ok ? "Camera is open. I can look through the CoreS3 camera now." : "Camera failed to open.",
                !ok);
        }
    }
}

static bool ensureAiVisionPreview(const char* status) {
    if (AppState::instance().getState() != AppStateEnum::AI_VISION) {
        AppState::instance().setState(AppStateEnum::AI_VISION);
    }

    setAiVisionStatus(AiVisionStatus::OPENING, status ? status : "Opening camera", nullptr);
    bool cameraReady = gCameraManager.isInitialized() || gCameraManager.begin();
    cameraReady = cameraReady && gCameraManager.startCapture();
    gCameraDebugUI.setCameraReady(cameraReady);
    setAiVisionStatus(cameraReady ? AiVisionStatus::PREVIEW : AiVisionStatus::ERROR,
                      cameraReady ? "Camera on" : "Camera failed",
                      cameraReady ? gAiVisionResultText.c_str() : "Camera unavailable");
    Serial.println(cameraReady ? "AI Vision: camera on" : "AI Vision: camera failed");
    return cameraReady;
}

static void closeAiVisionPreview() {
    if (AppState::instance().getState() == AppStateEnum::AI_VISION) {
        gCameraManager.stopCapture();
        gCameraDebugUI.setCameraReady(false);
        setAiVisionStatus(AiVisionStatus::IDLE, "Vision closed", "");
        AppState::instance().setState(AppStateEnum::AI);
    }
}

static void processPomodoroOpenRequest() {
    if (!gPomodoroOpenPending) return;
    gPomodoroOpenPending = false;

    if (AppState::instance().getState() == AppStateEnum::AI_VISION) {
        gCameraManager.stopCapture();
        gCameraDebugUI.setCameraReady(false);
        setAiVisionStatus(AiVisionStatus::IDLE, "Vision closed", "");
    }
    AppState::instance().setState(AppStateEnum::POMODORO);
    Serial.println("AI request: open Pomodoro");
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
        gAiVisionOpenPending = true;
        gAiVisionMcpCallId = callId;
        return;
    }

    if (toolName == "self.camera.close") {
        closeAiVisionPreview();
        gXiaoZhiClient.queueMcpToolTextResult(callId, "Camera is closed.", false);
        return;
    }

    if (toolName == "self.vision.describe_scene") {
        gAiVisionDescribePending = true;
        gAiVisionMcpCallId = callId;
        return;
    }

    if (toolName == "self.pomodoro.open") {
        gPomodoroOpenPending = true;
        gXiaoZhiClient.queueMcpToolTextResult(callId, "Pomodoro page opened.", false);
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
                    appState.setState(AppStateEnum::MENU);
                    break;
                case GestureType::LEFT_SWIPE:
                    appState.setState(AppStateEnum::AI);
                    break;
                case GestureType::SINGLE_TAP:
                    handleFaceTap(event.endX, event.endY);
                    break;
                case GestureType::DOUBLE_TAP:
                    appState.setState(AppStateEnum::AI);
                    break;
                case GestureType::LONG_PRESS:
                    appState.setState(AppStateEnum::SLEEP);
                    if (takeDisplayLock()) {
                        gPowerManager.enterSleep();
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
                        case 2: appState.setState(AppStateEnum::POMODORO); break;
                        case 3: appState.setState(AppStateEnum::MUSIC); break;
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
                    appState.setState(AppStateEnum::MENU);
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
                    appState.setState(AppStateEnum::MENU);
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
                    appState.setState(AppStateEnum::MENU);
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
                    appState.setState(AppStateEnum::MENU);
                    break;
                default:
                    break;
            }
            break;
        }

        case AppStateEnum::SLEEP:
            switch (event.type) {
                case GestureType::SINGLE_TAP:
                case GestureType::DOUBLE_TAP:
                    appState.setState(AppStateEnum::FACE);
                    if (takeDisplayLock()) {
                        gPowerManager.exitSleep();
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
            }
            gSickActive = false;
            gSickUntil = 0;
            gActivationCodeRequested = false;
            if (gFaceDetector.backendAvailable() && gFaceDetector.isEnabled()) {
                if (!gCameraManager.isRunning()) {
                    bool camOk = gCameraManager.isInitialized() || gCameraManager.begin();
                    camOk = camOk && gCameraManager.startCapture();
                    Serial.println(camOk ? "Vision: camera started" : "Vision: camera start failed");
                }
            }
            break;

        case AppStateEnum::AI:
            gMenuUI.hide();
            gCameraDebugUI.hide();
            gPomodoroUI.hide();
            gInfoUI.hide();
            gMusicUI.hide();
            stopMusicForExclusiveAudio();
            gFaceUI.clearStatusText();
            if (gWifiManager.isConnected()) {
                AppState::instance().setEmotion(FaceEmotion::LISTENING);
                if (gXiaoZhiClient.isActivated()) {
                    gNeedOpenAudioChannel = true;
                } else if (!gXiaoZhiClient.hasActivationCode() && !gActivationCodeRequested) {
                    gNeedActivationRequest = true;
                    gActivationCodeRequested = true;
                }
            } else {
                AppState::instance().setEmotion(FaceEmotion::SURPRISED);
                Serial.println("AI: Wi-Fi not connected");
            }
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
            bool cameraReady = gCameraManager.isInitialized() || gCameraManager.begin();
            cameraReady = cameraReady && gCameraManager.startCapture();
            gCameraDebugUI.setCameraReady(cameraReady);
            if (!cameraReady) {
                setAiVisionStatus(AiVisionStatus::ERROR, "Camera failed", "Camera unavailable");
            } else if (gAiVisionStatus == AiVisionStatus::IDLE) {
                setAiVisionStatus(AiVisionStatus::PREVIEW, "Camera on", "");
            }
            Serial.println(cameraReady ? "AI Vision preview started" : "AI Vision camera start failed");
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
            if (!gCameraManager.isRunning()) {
                bool cameraReady = gCameraManager.isInitialized() || gCameraManager.begin();
                cameraReady = cameraReady && gCameraManager.startCapture();
                gCameraDebugUI.setCameraReady(cameraReady);
                Serial.println(cameraReady ? "Camera debug started" : "Camera debug start failed");
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
            if (gMusicManager.state() == MusicPlaybackState::STOPPED ||
                gMusicManager.state() == MusicPlaybackState::ERROR ||
                gMusicManager.trackCount() == 0) {
                gMusicManager.scan();
            }
            updateMusicUiData();
            gMusicUI.show();
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
                M5.Lcd.setBrightness(24);
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
            case VoiceState::LISTENING:  appState.setEmotion(FaceEmotion::LISTENING); break;
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
    M5.Lcd.setBrightness(255);
    drawLcdDarkBackground();
    M5.Lcd.setTextColor(UiTheme::TEXT, UiTheme::BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setCursor(12, 8);
    M5.Lcd.println("CoreS3 AI Pet // Boot");
    esp_reset_reason_t resetReason = esp_reset_reason();
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
    gPomodoroUI.setCompleteCallback(handlePomodoroComplete);

    bootStep("WifiManager...", 6);
    gWifiManager.begin();
    bootStep(gWifiManager.isConfigured() ? "WiFi configured" : "WiFi not configured", 6);

    bootStep("StorageManager...", 7);
    bool sdOk = gStorageManager.begin();
    gMusicManager.begin(&gStorageManager);
    bootStep(sdOk ? "SD ready" : "SD not found (will retry)", 7);

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
    gXiaoZhiClient.setTranscriptCallback(handleXiaoZhiTranscript);
    bootStep("XiaoZhiClient OK", 11);

    bootStep("PowerManager...", 12);
    gPowerManager.begin();
    gPowerManager.setLowBatteryCallback(lowBatteryHandler);
    bootStep("PowerManager OK", 12);

    bootStep("EventBus...", 13);
    EventBus::instance().subscribe(EventType::FACE_DETECTED, handleEvent);
    EventBus::instance().subscribe(EventType::FACE_LOST, handleEvent);

    bootStep("Creating tasks...", 14);
    createTaskChecked(uiTask, "UI", UI_TASK_STACK_SIZE, UI_TASK_PRIORITY, &uiTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(touchTask, "Touch", TOUCH_TASK_STACK_SIZE, TOUCH_TASK_PRIORITY, &touchTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(cameraTask, "Camera", CAMERA_TASK_STACK_SIZE, CAMERA_TASK_PRIORITY, &cameraTaskHandle, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(visionTask, "Vision", VISION_TASK_STACK_SIZE, VISION_TASK_PRIORITY, &visionTaskHandle, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(aiTask, "AI", AI_TASK_STACK_SIZE, AI_TASK_PRIORITY, &aiTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(powerTask, "Power", POWER_TASK_STACK_SIZE, POWER_TASK_PRIORITY, &powerTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    createTaskChecked(networkTask, "Network", NETWORK_TASK_STACK_SIZE, NETWORK_TASK_PRIORITY, &networkTaskHandle, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    Serial.println("Setup complete!");
}

void loop() {
    static unsigned long lastHeartbeat = 0;
    unsigned long now = millis();
    processPomodoroCompletion(now);
    updatePomodoroMelody(now);
    processAiVisionVoiceFallback();
    processPomodoroOpenRequest();

    if (SERIAL_DIAGNOSTIC_HEARTBEAT && millis() - lastHeartbeat >= SERIAL_HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = millis();
        Serial.printf("Alive: heap=%u, psram=%u, state=%d, wifi=%d, camera=%d\n",
                      ESP.getFreeHeap(), ESP.getFreePsram(),
                      static_cast<int>(AppState::instance().getState()),
                      gWifiManager.isConnected() ? 1 : 0,
                      gCameraManager.isRunning() ? 1 : 0);
    }

    AppStateEnum state = AppState::instance().getState();
    if (state == AppStateEnum::POMODORO) {
        gImuOrientation.update();
        PomoOrientation stable = gImuOrientation.getStable();
        gPomodoroUI.setOrientation(stable);
    }

    if (state == AppStateEnum::FACE) {
        gImuOrientation.update();
        if (gImuOrientation.isShaking()) {
            AppState::instance().setEmotion(FaceEmotion::SICK);
            gFaceUI.setTemporaryGaze(0.0f, 0.3f, 2000);
            gSickActive = true;
            gSickUntil = now + SICK_EMOTION_DURATION_MS;
            Serial.println("Shake detected -> SICK");
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
    }

    if (XIAOZHI_REAL_ACTIVATION && state == AppStateEnum::AI && !gXiaoZhiClient.isActivated() && gWifiManager.isConnected()) {
        if (now - gLastActivationCheck >= 5000) {
            gLastActivationCheck = now;
            gNeedActivationCheck = true;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
