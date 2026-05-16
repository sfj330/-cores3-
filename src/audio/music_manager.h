#pragma once

#include <Arduino.h>
#include <FS.h>
#include "storage/storage_manager.h"

enum class MusicPlaybackState {
    STOPPED,
    PLAYING,
    PAUSED,
    ERROR
};

class MusicManager {
public:
    bool begin(StorageManager* storage);
    bool scan();

    int trackCount() const;
    int currentIndex() const;
    String trackName(int index) const;
    String currentTitle() const;
    String statusText() const;
    MusicPlaybackState state() const;

    bool play(int index);
    void togglePause();
    void stop();
    bool next();

private:
    enum class TrackType : uint8_t {
        WAV,
        MP3
    };

    struct WavInfo {
        uint16_t channels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        uint16_t blockAlign = 0;
        uint32_t dataOffset = 0;
        uint32_t dataSize = 0;
    };

    static void taskThunk(void* arg);
    void taskLoop();
    void playFile(int index);
    void playWavFile(int index, File& file);
    void playMp3File(int index);
    bool parseWav(File& file, WavInfo& info, String& error);
    bool beginSpeaker();
    void setStatus(const String& status, MusicPlaybackState state);
    void sortTracks();

    StorageManager* storage_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;

    static constexpr int MAX_TRACKS = 16;
    static constexpr int BUFFER_COUNT = 3;
    static constexpr size_t BUFFER_BYTES = 8192;
    static constexpr int MUSIC_CHANNEL = 6;

    String trackPaths_[MAX_TRACKS];
    String trackNames_[MAX_TRACKS];
    TrackType trackTypes_[MAX_TRACKS] = {};
    int trackCount_ = 0;
    int currentIndex_ = -1;
    int requestedIndex_ = 0;

    volatile bool playRequested_ = false;
    volatile bool stopRequested_ = false;
    volatile bool paused_ = false;
    volatile MusicPlaybackState state_ = MusicPlaybackState::STOPPED;

    String status_ = "No music";
    alignas(4) uint8_t buffers_[BUFFER_COUNT][BUFFER_BYTES] = {};
};
