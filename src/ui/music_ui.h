#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include "audio/music_manager.h"
#include "config/app_config.h"

enum class MusicHitZone {
    MUSIC_HIT_NONE,
    MUSIC_HIT_BACK,
    MUSIC_HIT_PLAY,
    MUSIC_HIT_STOP,
    MUSIC_HIT_NEXT
};

class MusicUI {
public:
    MusicUI();
    void begin();
    void show();
    void hide();
    void update();
    void markDirty();

    void setState(const String& title, const String& status, int index, int count,
                  MusicPlaybackState playbackState);

    MusicHitZone hitTest(int x, int y) const;

private:
    void drawBackButton();
    void drawMusicIcon();
    void drawControls();

    M5Canvas canvas_;
    bool spriteReady_ = false;
    bool visible_ = false;
    bool dirty_ = true;

    String title_ = "No audio file";
    String status_ = "No music";
    int index_ = -1;
    int count_ = 0;
    MusicPlaybackState playbackState_ = MusicPlaybackState::STOPPED;

    static constexpr int BACK_X = 5;
    static constexpr int BACK_Y = 5;
    static constexpr int BACK_W = 74;
    static constexpr int BACK_H = 26;

    static constexpr int BTN_Y = DISPLAY_HEIGHT - 45;
    static constexpr int BTN_W = 54;
    static constexpr int BTN_H = 34;
    static constexpr int PLAY_X = 70;
    static constexpr int STOP_X = 133;
    static constexpr int NEXT_X = 196;
};
