#include "camera_manager.h"
#include "config/app_config.h"
#include <M5CoreS3.h>
#include <esp_camera.h>
#include <driver/i2c.h>
#include <esp_heap_caps.h>
#include <img_converters.h>
#include <SD.h>

CameraManager::CameraManager() = default;

void CameraManager::setTouchTaskHandle(TaskHandle_t handle) {
    touchTaskHandle_ = handle;
}

bool CameraManager::beginInternal() {
    Serial.printf("Camera: beginInternal() PSRAM free=%u/%u, heap=%u\n",
                  ESP.getFreePsram(), ESP.getPsramSize(), ESP.getFreeHeap());
    if (ESP.getPsramSize() == 0) {
        Serial.println("Camera: no PSRAM!");
        recordFail();
        return false;
    }
    M5.In_I2C.release();
    vTaskDelay(pdMS_TO_TICKS(10));

    camera_config_t cfg = {
        .pin_pwdn     = -1,
        .pin_reset    = -1,
        .pin_xclk     = -1,
        .pin_sscb_sda = 12,
        .pin_sscb_scl = 11,
        .pin_d7       = 47,
        .pin_d6       = 48,
        .pin_d5       = 16,
        .pin_d4       = 15,
        .pin_d3       = 42,
        .pin_d2       = 41,
        .pin_d1       = 40,
        .pin_d0       = 39,
        .pin_vsync    = 46,
        .pin_href     = 38,
        .pin_pclk     = 45,
        .xclk_freq_hz = 10000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size   = FRAMESIZE_QVGA,
        .jpeg_quality = 0,
        .fb_count     = 1,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = I2C_NUM_1,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        Serial.printf("Camera: esp_camera_init FAILED err=0x%x, PSRAM free=%u\n",
                      err, ESP.getFreePsram());
        recordFail();
        return false;
    }
    CoreS3.Camera.sensor = esp_camera_sensor_get();
    Serial.println("Camera: esp_camera_init OK");
    return true;
}

bool CameraManager::begin() {
    if (initialized_) return true;
    if (isInCooldown()) {
        Serial.printf("Camera: in cooldown, %lus left\n",
                      (unsigned long)((FAIL_COOLDOWN_MS - (millis() - lastFailTime_)) / 1000));
        return false;
    }

    bool touchSuspended = false;
    if (touchTaskHandle_ != nullptr) {
        vTaskSuspend(touchTaskHandle_);
        touchSuspended = true;
        Serial.println("Camera: Touch task suspended for I2C");
    }

    bool ok = beginInternal();

    if (touchSuspended && touchTaskHandle_ != nullptr) {
        vTaskResume(touchTaskHandle_);
        Serial.println("Camera: Touch task resumed");
    }

    if (!ok) return false;
    initialized_ = true;
    capturing_ = false;
    resetFailure();
    Serial.println("Camera: initialized OK");
    return true;
}

bool CameraManager::end() {
    stopCapture();
    if (initialized_) {
        esp_camera_deinit();
        initialized_ = false;
        Serial.println("Camera: deinit, restoring In_I2C");
        M5.In_I2C.begin();
    }
    return true;
}

bool CameraManager::startCapture() {
    if (!initialized_) return false;
    if (isInCooldown()) return false;
    capturing_ = true;
    return true;
}

bool CameraManager::stopCapture() {
    capturing_ = false;
    return true;
}

CameraFrame CameraManager::getDisplayFrame() {
    CameraFrame frame;
    if (!initialized_ || !capturing_) return frame;
    if (!ensureMutex()) return frame;
    if (xSemaphoreTake(cameraMutex_, pdMS_TO_TICKS(200)) != pdTRUE) return frame;

    if (CoreS3.Camera.get()) {
        frame.data = CoreS3.Camera.fb->buf;
        frame.width = CoreS3.Camera.fb->width;
        frame.height = CoreS3.Camera.fb->height;
        frame.size = CoreS3.Camera.fb->len;
        frame.valid = true;
        frame.locked = true;
        return frame;
    }
    xSemaphoreGive(cameraMutex_);
    return frame;
}

CameraFrame CameraManager::getDetectionFrame() {
    return getDisplayFrame();
}

void CameraManager::releaseFrame(CameraFrame& frame) {
    if (!frame.locked) {
        frame = CameraFrame{};
        return;
    }
    CoreS3.Camera.free();
    CoreS3.Camera.fb = nullptr;
    frame = CameraFrame{};
    if (cameraMutex_ != nullptr) {
        xSemaphoreGive(cameraMutex_);
    }
}

bool CameraManager::isRunning() const {
    return initialized_ && capturing_;
}

bool CameraManager::isInitialized() const {
    return initialized_;
}

bool CameraManager::isInCooldown() const {
    if (consecutiveFailCount_ < MAX_CONSECUTIVE_FAILS) return false;
    if (millis() - lastFailTime_ < FAIL_COOLDOWN_MS) return true;
    return false;
}

unsigned long CameraManager::lastFailTime() const {
    return lastFailTime_;
}

void CameraManager::resetFailure() {
    consecutiveFailCount_ = 0;
    lastFailTime_ = 0;
}

void CameraManager::recordFail() {
    consecutiveFailCount_++;
    lastFailTime_ = millis();
    Serial.printf("Camera: fail #%d at %lus\n", consecutiveFailCount_, millis() / 1000);
}

bool CameraManager::ensureMutex() {
    if (cameraMutex_ == nullptr) {
        cameraMutex_ = xSemaphoreCreateMutex();
    }
    return cameraMutex_ != nullptr;
}

bool CameraManager::captureJpegToFile(const char* path, String& status) {
    if (!initialized_) {
        status = "Camera not initialized";
        return false;
    }
    if (!ensureMutex()) {
        status = "Mutex create failed";
        return false;
    }
    if (xSemaphoreTake(cameraMutex_, pdMS_TO_TICKS(2000)) != pdTRUE) {
        status = "Camera busy";
        return false;
    }

    bool ok = false;
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        status = "Capture failed";
        recordFail();
        xSemaphoreGive(cameraMutex_);
        return false;
    }

    if (fb->format == PIXFORMAT_JPEG) {
        File file = SD.open(path, FILE_WRITE);
        if (file) {
            size_t written = file.write(fb->buf, fb->len);
            file.close();
            if (written == fb->len) {
                status = String("Saved ") + path;
                ok = true;
            } else {
                status = "Write failed";
            }
        } else {
            status = "SD open failed";
        }
    } else {
        uint8_t* jpgBuf = nullptr;
        size_t jpgLen = 0;
        bool jpegOk = fmt2jpg(fb->buf, fb->len, fb->width, fb->height,
                               fb->format, CAMERA_JPEG_QUALITY, &jpgBuf, &jpgLen);
        if (!jpegOk || jpgBuf == nullptr) {
            status = "JPEG convert failed";
        } else {
            File file = SD.open(path, FILE_WRITE);
            if (file) {
                size_t written = file.write(jpgBuf, jpgLen);
                file.close();
                if (written == jpgLen) {
                    status = String("Saved ") + path;
                    ok = true;
                } else {
                    status = "Write failed";
                }
            } else {
                status = "SD open failed";
            }
            free(jpgBuf);
        }
    }

    esp_camera_fb_return(fb);
    xSemaphoreGive(cameraMutex_);
    if (ok) resetFailure();
    return ok;
}

bool CameraManager::captureJpegToMemory(CameraJpeg& jpeg, String& status) {
    jpeg = CameraJpeg{};
    if (!initialized_) {
        status = "Camera not initialized";
        return false;
    }
    if (!ensureMutex()) {
        status = "Mutex create failed";
        return false;
    }
    if (xSemaphoreTake(cameraMutex_, pdMS_TO_TICKS(2500)) != pdTRUE) {
        status = "Camera busy";
        return false;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        status = "Capture failed";
        recordFail();
        xSemaphoreGive(cameraMutex_);
        return false;
    }

    bool ok = false;
    if (fb->format == PIXFORMAT_JPEG) {
        uint8_t* copy = static_cast<uint8_t*>(heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!copy) {
            copy = static_cast<uint8_t*>(heap_caps_malloc(fb->len, MALLOC_CAP_8BIT));
        }
        if (!copy) {
            status = "JPEG alloc failed";
        } else {
            memcpy(copy, fb->buf, fb->len);
            jpeg.data = copy;
            jpeg.length = fb->len;
            jpeg.valid = true;
            status = "JPEG captured";
            ok = true;
        }
    } else {
        uint8_t* jpgBuf = nullptr;
        size_t jpgLen = 0;
        bool jpegOk = fmt2jpg(fb->buf, fb->len, fb->width, fb->height,
                               fb->format, CAMERA_JPEG_QUALITY, &jpgBuf, &jpgLen);
        if (!jpegOk || jpgBuf == nullptr) {
            status = "JPEG convert failed";
        } else {
            jpeg.data = jpgBuf;
            jpeg.length = jpgLen;
            jpeg.valid = true;
            status = "JPEG captured";
            ok = true;
        }
    }

    esp_camera_fb_return(fb);
    xSemaphoreGive(cameraMutex_);
    if (ok) resetFailure();
    return ok;
}

void CameraManager::releaseJpeg(CameraJpeg& jpeg) {
    if (jpeg.data != nullptr) {
        free(jpeg.data);
    }
    jpeg = CameraJpeg{};
}

void CameraManager::scaleDown(const uint8_t* src, uint8_t* dst,
                               int srcW, int srcH, int dstW, int dstH) {
    float scaleX = (float)srcW / dstW;
    float scaleY = (float)srcH / dstH;
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            int srcX = (int)(x * scaleX);
            int srcY = (int)(y * scaleY);
            int srcIdx = (srcY * srcW + srcX) * 2;
            int dstIdx = (y * dstW + x) * 2;
            dst[dstIdx] = src[srcIdx];
            dst[dstIdx + 1] = src[srcIdx + 1];
        }
    }
}
