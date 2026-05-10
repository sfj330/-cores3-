#include "music_ui.h"
#include "ui/ui_theme.h"
#include <M5CoreS3.h>

MusicUI::MusicUI() : canvas_(&M5.Lcd) {}

void MusicUI::begin() {
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    spriteReady_ = canvas_.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT) != nullptr;
    if (spriteReady_) {
        canvas_.fillSprite(UiTheme::BG);
    }
}

void MusicUI::show() {
    visible_ = true;
    dirty_ = true;
}

void MusicUI::hide() {
    visible_ = false;
}

void MusicUI::markDirty() {
    dirty_ = true;
}

void MusicUI::setState(const String& title, const String& status, int index, int count,
                       MusicPlaybackState playbackState) {
    if (title != title_ || status != status_ || index != index_ ||
        count != count_ || playbackState != playbackState_) {
        title_ = title;
        status_ = status;
        index_ = index;
        count_ = count;
        playbackState_ = playbackState;
        dirty_ = true;
    }
}

MusicHitZone MusicUI::hitTest(int x, int y) const {
    if (x >= BACK_X && x < BACK_X + BACK_W && y >= BACK_Y && y < BACK_Y + BACK_H) {
        return MusicHitZone::MUSIC_HIT_BACK;
    }
    if (y >= BTN_Y && y < BTN_Y + BTN_H) {
        if (x >= PLAY_X && x < PLAY_X + BTN_W) return MusicHitZone::MUSIC_HIT_PLAY;
        if (x >= STOP_X && x < STOP_X + BTN_W) return MusicHitZone::MUSIC_HIT_STOP;
        if (x >= NEXT_X && x < NEXT_X + BTN_W) return MusicHitZone::MUSIC_HIT_NEXT;
    }
    return MusicHitZone::MUSIC_HIT_NONE;
}

void MusicUI::update() {
    if (!visible_ || !spriteReady_ || !dirty_) return;

    canvas_.fillSprite(UiTheme::BG);
    UiTheme::drawTitle(canvas_, "Music", "SD WAV player", UiTheme::BLUE);
    drawBackButton();
    drawMusicIcon();

    canvas_.setTextDatum(TC_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::TEXT_DIM, UiTheme::BG);
    String countText = count_ > 0 ? String(index_ + 1) + "/" + String(count_) : String("0/0");
    canvas_.drawString(countText, DISPLAY_WIDTH / 2, 88);

    canvas_.setTextColor(UiTheme::TEXT, UiTheme::BG);
    String title = title_;
    if (title.length() > 28) {
        title = title.substring(0, 25) + "...";
    }
    canvas_.drawString(title, DISPLAY_WIDTH / 2, 112);

    uint16_t statusColor = UiTheme::TEXT_DIM;
    if (playbackState_ == MusicPlaybackState::PLAYING) statusColor = UiTheme::GREEN;
    if (playbackState_ == MusicPlaybackState::PAUSED) statusColor = UiTheme::AMBER;
    if (playbackState_ == MusicPlaybackState::ERROR) statusColor = UiTheme::RED;
    UiTheme::drawStatusPill(canvas_, DISPLAY_WIDTH / 2 - 70, 137, 140,
                            status_.c_str(), statusColor, UiTheme::BG);
    canvas_.setTextDatum(TL_DATUM);

    drawControls();
    canvas_.pushSprite(0, 0);
    dirty_ = false;
}

void MusicUI::drawBackButton() {
    UiTheme::drawBackButton(canvas_, BACK_X, BACK_Y, BACK_W, BACK_H);
}

void MusicUI::drawMusicIcon() {
    int x = DISPLAY_WIDTH / 2 - 29;
    int y = 42;
    int size = 58;
    canvas_.fillRoundRect(x, y, size, size, 15, UiTheme::BLUE);
    canvas_.drawRoundRect(x, y, size, size, 15, UiTheme::TEXT);
    canvas_.fillRoundRect(x + 31, y + 12, 8, 31, 3, UiTheme::BG);
    canvas_.drawFastHLine(x + 36, y + 12, 17, UiTheme::BG);
    canvas_.drawFastVLine(x + 52, y + 12, 11, UiTheme::BG);
    canvas_.fillCircle(x + 23, y + 45, 9, UiTheme::BG);
    canvas_.fillCircle(x + 45, y + 36, 9, UiTheme::BG);
    canvas_.fillCircle(x + 23, y + 45, 4, UiTheme::BLUE);
    canvas_.fillCircle(x + 45, y + 36, 4, UiTheme::BLUE);
}

void MusicUI::drawControls() {
    bool paused = playbackState_ == MusicPlaybackState::PAUSED;
    bool playing = playbackState_ == MusicPlaybackState::PLAYING;
    uint16_t playColor = (playing || paused) ? UiTheme::AMBER : UiTheme::GREEN;

    canvas_.fillRoundRect(PLAY_X, BTN_Y, BTN_W, BTN_H, 8, playColor);
    canvas_.drawRoundRect(PLAY_X, BTN_Y, BTN_W, BTN_H, 8, UiTheme::TEXT);
    canvas_.setTextDatum(MC_DATUM);
    canvas_.setTextSize(1);
    canvas_.setTextColor(UiTheme::BG, playColor);
    canvas_.drawString(playing ? "PAUSE" : "PLAY", PLAY_X + BTN_W / 2, BTN_Y + BTN_H / 2 + 1);

    canvas_.fillRoundRect(STOP_X, BTN_Y, BTN_W, BTN_H, 8, UiTheme::PANEL);
    canvas_.drawRoundRect(STOP_X, BTN_Y, BTN_W, BTN_H, 8, UiTheme::RED);
    canvas_.setTextColor(UiTheme::RED, UiTheme::PANEL);
    canvas_.drawString("STOP", STOP_X + BTN_W / 2, BTN_Y + BTN_H / 2 + 1);

    canvas_.fillRoundRect(NEXT_X, BTN_Y, BTN_W, BTN_H, 8, UiTheme::PANEL);
    canvas_.drawRoundRect(NEXT_X, BTN_Y, BTN_W, BTN_H, 8, UiTheme::BLUE);
    canvas_.setTextColor(UiTheme::BLUE, UiTheme::PANEL);
    canvas_.drawString("NEXT", NEXT_X + BTN_W / 2, BTN_Y + BTN_H / 2 + 1);
    canvas_.setTextDatum(TL_DATUM);
}
