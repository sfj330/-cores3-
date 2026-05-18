#include "face_ui.h"
#include "app/app_state.h"
#include "config/app_config.h"
#include "ui/ui_theme.h"
#include <M5CoreS3.h>
#include <cmath>

static constexpr int BIG_EYE_W = 50;
static constexpr int BIG_EYE_H = 58;
static constexpr int BIG_PUPIL_R = 12;
static constexpr int BIG_SPACING = 70;
static constexpr int BIG_GAZE_MAX = 14;
static constexpr int BIG_BROW_W = 44;
static constexpr int BIG_MOUTH_W = 36;

FaceUI::FaceUI() : canvas_(&M5.Lcd) {}

bool FaceUI::begin() {
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    spriteReady_ = canvas_.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT) != nullptr;
    if (!spriteReady_) {
        M5.Lcd.setTextColor(UiTheme::RED, UiTheme::BG);
        M5.Lcd.setCursor(0, 36);
        M5.Lcd.print("FaceUI sprite alloc failed");
        return false;
    }
    canvas_.fillSprite(UiTheme::BG);
    canvas_.pushSprite(0, 0);
    return true;
}

void FaceUI::setGazeOffset(float dx, float dy) {
    targetGazeX_ = dx;
    targetGazeY_ = dy;
}

void FaceUI::setExpression(int emotion) {
    currentEmotion_ = emotion;
}

void FaceUI::setSpeakingMouthOpen(bool open) {
    speakingMouthOpen_ = open;
}

void FaceUI::wake() {
    sleeping_ = false;
}

void FaceUI::setTemporaryGaze(float dx, float dy, unsigned long durationMs) {
    tempGazeX_ = dx;
    tempGazeY_ = dy;
    tempGazeActive_ = true;
    tempGazeEndTime_ = millis() + durationMs;
}

void FaceUI::setStatusText(const char* text, uint16_t color, unsigned long ttlMs) {
    statusText_ = text ? String(text) : String();
    statusColor_ = color;
    statusEndTime_ = ttlMs > 0 ? millis() + ttlMs : 0;
}

void FaceUI::clearStatusText() {
    statusText_ = "";
    statusEndTime_ = 0;
}

void FaceUI::update() {
    if (sleeping_ || !spriteReady_) return;

    updateBlink();
    updateGaze();
    updateSpeakingAnimation();
    drawFace();

    canvas_.pushSprite(0, 0);
}

void FaceUI::updateBlink() {
    unsigned long now = millis();
    if (isBlinking_) {
        if (now - blinkStartTime_ > BLINK_DURATION) {
            isBlinking_ = false;
            nextBlinkTime_ = now + random(BLINK_INTERVAL_MIN, BLINK_INTERVAL_MAX);
        }
    } else if (now >= nextBlinkTime_) {
        isBlinking_ = true;
        blinkStartTime_ = now;
    }
}

void FaceUI::updateGaze() {
    unsigned long now = millis();
    if (tempGazeActive_) {
        if (now >= tempGazeEndTime_) {
            tempGazeActive_ = false;
        } else {
            gazeX_ = gazeX_ * 0.5f + tempGazeX_ * 0.5f;
            gazeY_ = gazeY_ * 0.5f + tempGazeY_ * 0.5f;
            return;
        }
    }
    gazeX_ = gazeX_ * (1.0f - GAZE_SMOOTH_ALPHA) + targetGazeX_ * GAZE_SMOOTH_ALPHA;
    gazeY_ = gazeY_ * (1.0f - GAZE_SMOOTH_ALPHA) + targetGazeY_ * GAZE_SMOOTH_ALPHA;
}

void FaceUI::updateSpeakingAnimation() {
    if (speakingMouthOpen_ || currentEmotion_ == static_cast<int>(FaceEmotion::SPEAKING)) {
        unsigned long now = millis();
        if (now - lastSpeakAnimTime_ > SPEAK_ANIM_INTERVAL) {
            lastSpeakAnimTime_ = now;
            mouthOpenAmount_ = random(MOUTH_OPEN_MIN, MOUTH_OPEN_MAX) / (float)MOUTH_OPEN_MAX;
        }
    } else {
        mouthOpenAmount_ = 0.0f;
    }
}

void FaceUI::drawFace() {
    canvas_.fillSprite(UiTheme::BG);

    int cx = FACE_CENTER_X;
    int cy = FACE_CENTER_Y;

    drawEyes(cx, cy);
    drawEyebrows(cx, cy);
    drawMouth(cx, cy);
    drawCheeks(cx, cy);
    updateParticles();
    drawParticles();
    drawStatusText();
}

void FaceUI::drawStatusText() {
    if (statusEndTime_ > 0 && millis() > statusEndTime_) {
        statusText_ = "";
        statusEndTime_ = 0;
    }
    if (statusText_.length() == 0) return;

    canvas_.setTextDatum(TC_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(statusColor_, UiTheme::BG);
    canvas_.drawString(statusText_, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - 12);
    canvas_.setTextDatum(TL_DATUM);
}

void FaceUI::drawEyes(int cx, int cy) {
    int eyeY = cy + EYE_Y_OFFSET;
    int leftEyeX = cx - BIG_SPACING / 2;
    int rightEyeX = cx + BIG_SPACING / 2;

    int pupilDx = (int)(gazeX_ * BIG_GAZE_MAX);
    int pupilDy = (int)(gazeY_ * BIG_GAZE_MAX);

    int eyeW = BIG_EYE_W;
    int eyeH = BIG_EYE_H;
    int pupilR = BIG_PUPIL_R;

    bool drawArcEyes = false;

    switch (static_cast<FaceEmotion>(currentEmotion_)) {
        case FaceEmotion::HAPPY:
            eyeH = BIG_EYE_H * 2 / 3;
            drawArcEyes = true;
            break;
        case FaceEmotion::CURIOUS:
            eyeW = BIG_EYE_W + 6;
            eyeH = BIG_EYE_H + 6;
            pupilR = BIG_PUPIL_R + 2;
            break;
        case FaceEmotion::LISTENING:
            eyeH = BIG_EYE_H + 4;
            pupilDy -= 3;
            break;
        case FaceEmotion::THINKING:
            pupilDx += 4;
            pupilDy -= 4;
            break;
        case FaceEmotion::SPEAKING:
            eyeW = BIG_EYE_W + 4;
            break;
        case FaceEmotion::SURPRISED:
            eyeW = BIG_EYE_W + 10;
            eyeH = BIG_EYE_H + 10;
            pupilR = BIG_PUPIL_R - 2;
            break;
        case FaceEmotion::SLEEPY:
            eyeH = BIG_EYE_H / 3;
            pupilR = 0;
            break;
        case FaceEmotion::TRACKING:
            pupilR = BIG_PUPIL_R + 1;
            break;
        case FaceEmotion::SHY:
            eyeH = BIG_EYE_H / 2;
            pupilDy += 6;
            pupilR = BIG_PUPIL_R - 1;
            break;
        case FaceEmotion::SICK:
            eyeH = BIG_EYE_H / 2;
            pupilDy += 3;
            pupilR = BIG_PUPIL_R - 2;
            break;
        default:
            break;
    }

    if (isBlinking_) {
        eyeH = 3;
        pupilR = 0;
        drawArcEyes = false;
    }

    canvas_.fillCircle(leftEyeX, eyeY, eyeW / 2, TFT_WHITE);
    canvas_.fillCircle(rightEyeX, eyeY, eyeW / 2, TFT_WHITE);

    if (eyeH < eyeW) {
        int clipH = (eyeW - eyeH) / 2 + 2;
        canvas_.fillRect(leftEyeX - eyeW / 2 - 1, eyeY - eyeW / 2 - 1, eyeW + 2, clipH, UiTheme::BG);
        canvas_.fillRect(leftEyeX - eyeW / 2 - 1, eyeY + eyeW / 2 - clipH + 1, eyeW + 2, clipH, UiTheme::BG);
        canvas_.fillRect(rightEyeX - eyeW / 2 - 1, eyeY - eyeW / 2 - 1, eyeW + 2, clipH, UiTheme::BG);
        canvas_.fillRect(rightEyeX - eyeW / 2 - 1, eyeY + eyeW / 2 - clipH + 1, eyeW + 2, clipH, UiTheme::BG);
    }

    if (pupilR > 0) {
        int lPupilX = leftEyeX + pupilDx;
        int rPupilX = rightEyeX + pupilDx;
        int pupilY = eyeY + pupilDy;

        int maxPupilMove = eyeW / 2 - pupilR - 2;
        lPupilX = constrain(lPupilX, leftEyeX - maxPupilMove, leftEyeX + maxPupilMove);
        rPupilX = constrain(rPupilX, rightEyeX - maxPupilMove, rightEyeX + maxPupilMove);
        pupilY = constrain(pupilY, eyeY - maxPupilMove, eyeY + maxPupilMove);

        canvas_.fillCircle(lPupilX, pupilY, pupilR + 3, 0x4208);
        canvas_.fillCircle(rPupilX, pupilY, pupilR + 3, 0x4208);

        canvas_.fillCircle(lPupilX, pupilY, pupilR, TFT_BLACK);
        canvas_.fillCircle(rPupilX, pupilY, pupilR, TFT_BLACK);

        canvas_.fillCircle(lPupilX - pupilR / 2, pupilY - pupilR / 2, pupilR / 3, TFT_WHITE);
        canvas_.fillCircle(rPupilX - pupilR / 2, pupilY - pupilR / 2, pupilR / 3, TFT_WHITE);

        canvas_.fillCircle(lPupilX + pupilR / 3, pupilY + pupilR / 3, pupilR / 5, 0xD69A);
        canvas_.fillCircle(rPupilX + pupilR / 3, pupilY + pupilR / 3, pupilR / 5, 0xD69A);
    }

    if (drawArcEyes && !isBlinking_) {
        canvas_.fillCircle(leftEyeX, eyeY, eyeW / 2, UiTheme::BG);
        canvas_.fillCircle(rightEyeX, eyeY, eyeW / 2, UiTheme::BG);
        int arcW = eyeW * 3 / 4;
        for (int i = -arcW; i <= arcW; ++i) {
            int yOff = (i * i) / (arcW * 2) + 2;
            canvas_.drawPixel(leftEyeX + i, eyeY - eyeH / 4 + yOff, TFT_WHITE);
            canvas_.drawPixel(rightEyeX + i, eyeY - eyeH / 4 + yOff, TFT_WHITE);
        }
    }

    FaceEmotion em = static_cast<FaceEmotion>(currentEmotion_);
    if ((em == FaceEmotion::SLEEPY || em == FaceEmotion::SHY || em == FaceEmotion::SICK) && !isBlinking_) {
        canvas_.drawFastHLine(leftEyeX - eyeW / 2, eyeY, eyeW, 0x8410);
        canvas_.drawFastHLine(rightEyeX - eyeW / 2, eyeY, eyeW, 0x8410);
    }
}

void FaceUI::drawEyebrows(int cx, int cy) {
    int browY = cy + BROW_Y_OFFSET;
    int leftBrowX = cx - BIG_SPACING / 2;
    int rightBrowX = cx + BIG_SPACING / 2;

    int lx1 = leftBrowX - BIG_BROW_W / 2;
    int lx2 = leftBrowX + BIG_BROW_W / 2;
    int rx1 = rightBrowX - BIG_BROW_W / 2;
    int rx2 = rightBrowX + BIG_BROW_W / 2;

    int ly = browY;
    int ry = browY;
    int thickness = 3;

    switch (static_cast<FaceEmotion>(currentEmotion_)) {
        case FaceEmotion::NORMAL: break;
        case FaceEmotion::HAPPY: ly -= 5; ry -= 5; break;
        case FaceEmotion::CURIOUS: ly -= 8; ry += 2; break;
        case FaceEmotion::LISTENING: ly -= 2; ry -= 2; break;
        case FaceEmotion::THINKING:
            lx1 = leftBrowX; lx2 = leftBrowX - BIG_BROW_W / 2;
            rx1 = rightBrowX; rx2 = rightBrowX + BIG_BROW_W / 2;
            ly += 3; ry += 3;
            break;
        case FaceEmotion::SPEAKING:
            ly += sin(millis() / 200.0f) * 2;
            ry += sin(millis() / 200.0f + 1.0f) * 2;
            break;
        case FaceEmotion::SURPRISED: ly -= 10; ry -= 10; thickness = 4; break;
        case FaceEmotion::SLEEPY: ly += 8; ry += 8; break;
        case FaceEmotion::TRACKING: ly -= 4; ry -= 4; break;
        case FaceEmotion::SHY: ly += 5; ry += 5; break;
        case FaceEmotion::SICK:
            lx2 = leftBrowX + BIG_BROW_W / 2 - 4;
            rx1 = rightBrowX - BIG_BROW_W / 2 + 4;
            ly += 4; ry += 4;
            break;
        default: break;
    }

    for (int t = 0; t < thickness; ++t) {
        canvas_.drawLine(lx1, ly + t, lx2, ly + t, UiTheme::TEXT);
        canvas_.drawLine(rx1, ry + t, rx2, ry + t, UiTheme::TEXT);
    }
}

void FaceUI::drawMouth(int cx, int cy) {
    int mouthY = cy + MOUTH_Y_OFFSET;

    switch (static_cast<FaceEmotion>(currentEmotion_)) {
        case FaceEmotion::NORMAL:
            canvas_.drawFastHLine(cx - BIG_MOUTH_W / 2, mouthY, BIG_MOUTH_W, UiTheme::TEXT);
            break;
        case FaceEmotion::HAPPY:
            {
                int smW = BIG_MOUTH_W / 2;
                for (int i = -smW; i <= smW; ++i) {
                    int yOff = -(i * i) / (smW * 2) + smW / 2;
                    for (int t = 0; t < 3; ++t) {
                        canvas_.drawPixel(cx + i, mouthY + yOff + t, TFT_WHITE);
                    }
                }
            }
            break;
        case FaceEmotion::CURIOUS:
            canvas_.drawCircle(cx, mouthY, 7, TFT_WHITE);
            canvas_.drawCircle(cx, mouthY, 5, TFT_WHITE);
            break;
        case FaceEmotion::LISTENING:
            {
                int smW = BIG_MOUTH_W / 2;
                for (int i = -smW; i <= smW; ++i) {
                    int yOff = (i * i) / (smW * 3) + 2;
                    canvas_.drawPixel(cx + i, mouthY + yOff, TFT_WHITE);
                }
            }
            break;
        case FaceEmotion::THINKING:
            canvas_.fillCircle(cx + 4, mouthY, 5, TFT_WHITE);
            break;
        case FaceEmotion::SPEAKING:
            {
                int openH = (int)(mouthOpenAmount_ * MOUTH_OPEN_MAX);
                if (openH < 2) openH = 2;
                canvas_.fillRoundRect(cx - BIG_MOUTH_W / 4, mouthY - openH / 2,
                                      BIG_MOUTH_W / 2, openH, 4, TFT_WHITE);
            }
            break;
        case FaceEmotion::SURPRISED:
            canvas_.fillCircle(cx, mouthY, BIG_MOUTH_W / 3, TFT_WHITE);
            canvas_.fillCircle(cx, mouthY, BIG_MOUTH_W / 3 - 4, UiTheme::BG);
            break;
        case FaceEmotion::SLEEPY:
            {
                for (int i = -BIG_MOUTH_W / 3; i <= BIG_MOUTH_W / 3; ++i) {
                    int yOff = (int)(sin(i / 3.0f) * 2);
                    canvas_.drawPixel(cx + i, mouthY + yOff, 0x8410);
                }
            }
            break;
        case FaceEmotion::TRACKING:
            canvas_.drawRoundRect(cx - BIG_MOUTH_W / 3, mouthY - 3, BIG_MOUTH_W * 2 / 3, 8, 3, TFT_WHITE);
            break;
        case FaceEmotion::SHY:
            {
                int smW = BIG_MOUTH_W / 3;
                for (int i = -smW; i <= smW; ++i) {
                    int yOff = (i * i) / (smW * 2) + 1;
                    canvas_.drawPixel(cx + i, mouthY + yOff, TFT_WHITE);
                }
            }
            break;
        case FaceEmotion::SICK:
            {
                int smW = BIG_MOUTH_W / 2;
                for (int i = -smW; i <= smW; ++i) {
                    int yOff = (int)(sin(i / 2.5f + millis() / 300.0f) * 3) + 3;
                    canvas_.drawPixel(cx + i, mouthY + yOff, 0xB5E2);
                }
            }
            break;
        default:
            canvas_.drawFastHLine(cx - BIG_MOUTH_W / 2, mouthY, BIG_MOUTH_W, TFT_WHITE);
            break;
    }
}

void FaceUI::drawCheeks(int cx, int cy) {
    int cheekY = cy + EYE_Y_OFFSET + BIG_EYE_H / 2;
    int leftCheekX = cx - BIG_SPACING / 2 - BIG_EYE_W / 2 - 10;
    int rightCheekX = cx + BIG_SPACING / 2 + BIG_EYE_W / 2 + 10;

    FaceEmotion em = static_cast<FaceEmotion>(currentEmotion_);

    if (em == FaceEmotion::HAPPY || em == FaceEmotion::SURPRISED) {
        canvas_.fillCircle(leftCheekX, cheekY, 10, 0x8010);
        canvas_.fillCircle(rightCheekX, cheekY, 10, 0x8010);
    }

    if (em == FaceEmotion::SHY) {
        canvas_.fillCircle(leftCheekX, cheekY, 12, 0xA014);
        canvas_.fillCircle(rightCheekX, cheekY, 12, 0xA014);
    }

    if (em == FaceEmotion::SICK) {
        canvas_.fillCircle(leftCheekX, cheekY, 10, 0x37E0);
        canvas_.fillCircle(rightCheekX, cheekY, 10, 0x37E0);
    }
}

void FaceUI::spawnParticles(int emotion) {
    FaceEmotion em = static_cast<FaceEmotion>(emotion);
    unsigned long now = millis();
    if (now - lastParticleSpawn_ < 400) return;
    lastParticleSpawn_ = now;

    for (int i = 0; i < MAX_PARTICLES; ++i) {
        if (particles_[i].life > 0) continue;

        Particle& p = particles_[i];
        p.maxLife = 1.0f;
        p.life = 1.0f;

        switch (em) {
            case FaceEmotion::HAPPY:
                p.x = FACE_CENTER_X + random(-80, 80);
                p.y = FACE_CENTER_Y + random(-60, 40);
                p.vx = random(-10, 10) / 10.0f;
                p.vy = -random(5, 15) / 10.0f;
                p.color = 0xFFE0;
                p.size = random(2, 5);
                p.shape = 1;
                break;
            case FaceEmotion::SHY:
                p.x = FACE_CENTER_X + random(-60, 60);
                p.y = FACE_CENTER_Y + random(-40, 20);
                p.vx = random(-5, 5) / 10.0f;
                p.vy = -random(3, 10) / 10.0f;
                p.color = 0xF81F;
                p.size = random(3, 6);
                p.shape = 2;
                break;
            case FaceEmotion::SLEEPY:
                p.x = FACE_CENTER_X + 50 + random(0, 30);
                p.y = FACE_CENTER_Y - 20 + random(-10, 10);
                p.vx = random(2, 8) / 10.0f;
                p.vy = -random(5, 12) / 10.0f;
                p.color = 0x7BEF;
                p.size = random(1, 3);
                p.shape = 3;
                p.letter = 'Z';
                break;
            case FaceEmotion::SICK:
                p.x = FACE_CENTER_X + random(-50, 50);
                p.y = FACE_CENTER_Y + 30 + random(0, 20);
                p.vx = random(-8, 8) / 10.0f;
                p.vy = -random(3, 8) / 10.0f;
                p.color = 0x37E0;
                p.size = random(3, 6);
                p.shape = 0;
                break;
            case FaceEmotion::SURPRISED:
                p.x = FACE_CENTER_X + random(-90, 90);
                p.y = FACE_CENTER_Y + random(-70, 50);
                p.vx = random(-15, 15) / 10.0f;
                p.vy = random(-15, 15) / 10.0f;
                p.color = 0xFFFF;
                p.size = random(1, 3);
                p.shape = 1;
                break;
            default:
                return;
        }
        break;
    }
}

void FaceUI::updateParticles() {
    FaceEmotion em = static_cast<FaceEmotion>(currentEmotion_);
    if (em == FaceEmotion::HAPPY || em == FaceEmotion::SHY ||
        em == FaceEmotion::SLEEPY || em == FaceEmotion::SICK ||
        em == FaceEmotion::SURPRISED) {
        spawnParticles(currentEmotion_);
    }

    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle& p = particles_[i];
        if (p.life <= 0) continue;
        p.x += p.vx;
        p.y += p.vy;
        p.life -= 0.02f;
        if (p.life < 0) p.life = 0;
    }
}

void FaceUI::drawParticles() {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle& p = particles_[i];
        if (p.life <= 0) continue;

        uint8_t alpha = (uint8_t)(p.life * 255);
        uint16_t color = p.color;
        if (alpha < 128) {
            uint8_t r = ((color >> 11) & 0x1F) * alpha / 255;
            uint8_t g = ((color >> 5) & 0x3F) * alpha / 255;
            uint8_t b = (color & 0x1F) * alpha / 255;
            color = (r << 11) | (g << 5) | b;
        }

        int px = (int)p.x;
        int py = (int)p.y;

        switch (p.shape) {
            case 0:
                canvas_.fillCircle(px, py, p.size, color);
                break;
            case 1:
                drawStar(px, py, p.size, color);
                break;
            case 2:
                drawHeart(px, py, p.size, color);
                break;
            case 3:
                canvas_.setTextSize(p.size);
                canvas_.setTextColor(color);
                canvas_.drawChar(p.letter, px, py);
                canvas_.setTextSize(1);
                break;
        }
    }
}

void FaceUI::drawStar(int cx, int cy, int r, uint16_t color) {
    for (int i = 0; i < 4; ++i) {
        float angle = i * 3.14159f / 2.0f;
        int ex = cx + (int)(cos(angle) * r);
        int ey = cy + (int)(sin(angle) * r);
        canvas_.drawLine(cx, cy, ex, ey, color);
    }
    canvas_.fillCircle(cx, cy, r > 2 ? 1 : 0, color);
}

void FaceUI::drawHeart(int cx, int cy, int size, uint16_t color) {
    int s = size / 2;
    if (s < 1) s = 1;
    canvas_.fillCircle(cx - s, cy - s, s, color);
    canvas_.fillCircle(cx + s, cy - s, s, color);
    canvas_.fillTriangle(cx - size, cy, cx + size, cy, cx, cy + size, color);
}
