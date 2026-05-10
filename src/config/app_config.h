#pragma once

#include <cstdint>

// ==================== Display ====================
constexpr int DISPLAY_WIDTH = 320;
constexpr int DISPLAY_HEIGHT = 240;

// ==================== Face UI ====================
constexpr int FACE_CENTER_X = DISPLAY_WIDTH / 2;
constexpr int FACE_CENTER_Y = DISPLAY_HEIGHT / 2;

// Eye dimensions
constexpr int EYE_WIDTH = 40;
constexpr int EYE_HEIGHT = 48;
constexpr int EYE_SPACING = 60;         // Distance between eye centers
constexpr int PUPIL_RADIUS = 8;
constexpr int EYE_Y_OFFSET = -20;       // Eyes above center

// Eye gaze tracking
constexpr int GAZE_MAX_OFFSET = 12;     // Max pixel offset from eye center
constexpr float GAZE_SMOOTH_ALPHA = 0.2f;

// Eyebrow dimensions
constexpr int BROW_WIDTH = 36;
constexpr int BROW_HEIGHT = 6;
constexpr int BROW_Y_OFFSET = -38;      // Above eyes

// Mouth dimensions
constexpr int MOUTH_WIDTH = 30;
constexpr int MOUTH_HEIGHT = 10;
constexpr int MOUTH_Y_OFFSET = 30;      // Below eyes

// Blink timing (ms)
constexpr int BLINK_INTERVAL_MIN = 2000;
constexpr int BLINK_INTERVAL_MAX = 5000;
constexpr int BLINK_DURATION = 150;

// Speaking mouth animation
constexpr int SPEAK_ANIM_INTERVAL = 180; // ms between mouth shape changes
constexpr int MOUTH_OPEN_MAX = 18;
constexpr int MOUTH_OPEN_MIN = 4;

// ==================== Touch / Gesture ====================
constexpr int TAP_THRESHOLD_MS = 200;           // Max touch duration for a tap
constexpr int LONG_PRESS_THRESHOLD_MS = 800;    // Min hold for long press
constexpr int DOUBLE_TAP_WINDOW_MS = 300;       // Max gap between two taps
constexpr int SWIPE_THRESHOLD_PX = 60;          // Min distance for swipe
constexpr int RELAXED_SWIPE_THRESHOLD_PX = 42;  // Practical CoreS3 swipe distance
constexpr int MOVE_THRESHOLD_PX = 20;           // Max movement for tap detection
constexpr bool GESTURE_DEBUG_LOG = false;

constexpr int TOUCH_SAMPLING_HZ = 50;

// ==================== Camera ====================
constexpr int CAMERA_FRAME_WIDTH = 320;
constexpr int CAMERA_FRAME_HEIGHT = 240;
constexpr int CAMERA_FPS = 15;
constexpr bool CAMERA_INIT_ON_BOOT = false;    // Keep boot path stable; init camera on demand.
constexpr int CAMERA_JPEG_QUALITY = 85;

// ==================== Face Detection ====================
constexpr int DETECTION_FRAME_WIDTH = CAMERA_FRAME_WIDTH;
constexpr int DETECTION_FRAME_HEIGHT = CAMERA_FRAME_HEIGHT;
constexpr int DETECTION_FPS = 5;
constexpr bool FACE_DETECTION_ENABLED_ON_BOOT = false;
constexpr int FACE_TRACKING_TIMEOUT_MS = 3000;  // No face -> NORMAL after this
constexpr unsigned long SICK_EMOTION_DURATION_MS = 2000;

// ==================== Wi-Fi ====================
constexpr int WIFI_RECONNECT_INTERVAL_MS = 5000;

// ==================== SD Card ====================
constexpr int SD_SPI_SCK_PIN = 36;
constexpr int SD_SPI_MISO_PIN = 35;
constexpr int SD_SPI_MOSI_PIN = 37;
constexpr int SD_SPI_CS_PIN = 4;
constexpr int SD_SPI_FREQ = 25000000;
constexpr const char* PHOTO_DIR = "/photos";
constexpr const char* MUSIC_DIR = "/music";

// ==================== Pomodoro ====================
constexpr int POMODORO_FOCUS_MINUTES = 25;
constexpr int POMODORO_BREAK_MINUTES = 5;
constexpr int POMODORO_RING_MS = 3000;          // Notification duration

// ==================== Power ====================
constexpr int POWER_TASK_INTERVAL_MS = 1000;    // 1 Hz monitoring
constexpr float BATTERY_FULL_VOLTAGE = 4.2f;
constexpr float BATTERY_EMPTY_VOLTAGE = 3.3f;
constexpr float BATTERY_LOW_THRESHOLD = 3.5f;   // Go SLEEPY below this

// ==================== AI ====================
constexpr bool XIAOZHI_REAL_ACTIVATION = true;
constexpr const char* XIAOZHI_APP_NAME = "xiaozhi";
constexpr const char* XIAOZHI_FIRMWARE_VERSION = "2.2.6";
constexpr const char* XIAOZHI_BOARD_TYPE = "m5stack-core-s3";
constexpr const char* XIAOZHI_CHIP_ID = "esp32s3";
constexpr const char* AI_SERVER_URL = "wss://api.xiaozhi.me/v1";
constexpr int AI_AUDIO_SAMPLE_RATE = 16000;

// ==================== Servo (reserved) ====================
constexpr int PCA9685_I2C_ADDR = 0x40;
constexpr int SERVO_PAN_MIN = 0;
constexpr int SERVO_PAN_MAX = 180;
constexpr int SERVO_TILT_MIN = 0;
constexpr int SERVO_TILT_MAX = 180;

// ==================== Task Priorities ====================
constexpr bool SERIAL_DIAGNOSTIC_HEARTBEAT = false;
constexpr int SERIAL_HEARTBEAT_INTERVAL_MS = 5000;

constexpr int UI_TASK_PRIORITY = 3;
constexpr int TOUCH_TASK_PRIORITY = 2;
constexpr int CAMERA_TASK_PRIORITY = 2;
constexpr int VISION_TASK_PRIORITY = 1;
constexpr int AI_TASK_PRIORITY = 2;
constexpr int POWER_TASK_PRIORITY = 1;
constexpr int NETWORK_TASK_PRIORITY = 1;

constexpr int UI_TASK_STACK_SIZE = 8192;
constexpr int TOUCH_TASK_STACK_SIZE = 4096;
constexpr int CAMERA_TASK_STACK_SIZE = 16384;
constexpr int VISION_TASK_STACK_SIZE = 8192;
constexpr int AI_TASK_STACK_SIZE = 16384;
constexpr int POWER_TASK_STACK_SIZE = 4096;
constexpr int NETWORK_TASK_STACK_SIZE = 4096;
