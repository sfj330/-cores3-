#include "audio/music_manager.h"
#include "config/app_config.h"
#include <M5CoreS3.h>
#include <SD.h>
#include <cstring>

namespace {
bool readExact(File& file, void* dst, size_t len) {
    return file.read(static_cast<uint8_t*>(dst), len) == static_cast<int>(len);
}

bool readU16(File& file, uint16_t& value) {
    uint8_t b[2];
    if (!readExact(file, b, sizeof(b))) return false;
    value = static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
    return true;
}

bool readU32(File& file, uint32_t& value) {
    uint8_t b[4];
    if (!readExact(file, b, sizeof(b))) return false;
    value = static_cast<uint32_t>(b[0]) |
            (static_cast<uint32_t>(b[1]) << 8) |
            (static_cast<uint32_t>(b[2]) << 16) |
            (static_cast<uint32_t>(b[3]) << 24);
    return true;
}

bool hasWavExtension(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".wav");
}
}

bool MusicManager::begin(StorageManager* storage) {
    storage_ = storage;
    if (taskHandle_ == nullptr) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            taskThunk, "Music", 8192, this, 1, &taskHandle_, 0);
        if (ok != pdPASS) {
            setStatus("Music task failed", MusicPlaybackState::ERROR);
            return false;
        }
    }
    return true;
}

bool MusicManager::scan() {
    trackCount_ = 0;

    if (!storage_ || !storage_->ensureReady()) {
        setStatus(storage_ ? storage_->statusText() : String("SD not ready"), MusicPlaybackState::ERROR);
        return false;
    }

    if (!SD.exists(MUSIC_DIR)) {
        setStatus("No /music folder", MusicPlaybackState::ERROR);
        return false;
    }

    File dir = SD.open(MUSIC_DIR);
    if (!dir || !dir.isDirectory()) {
        setStatus("Music folder error", MusicPlaybackState::ERROR);
        if (dir) dir.close();
        return false;
    }

    while (trackCount_ < MAX_TRACKS) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (hasWavExtension(name)) {
                String path = name.startsWith("/") ? name : String(MUSIC_DIR) + "/" + name;
                int slash = path.lastIndexOf('/');
                trackPaths_[trackCount_] = path;
                trackNames_[trackCount_] = slash >= 0 ? path.substring(slash + 1) : path;
                trackCount_++;
            }
        }
        entry.close();
    }
    dir.close();
    sortTracks();

    if (trackCount_ == 0) {
        setStatus("No WAV files", MusicPlaybackState::ERROR);
        return false;
    }

    if (currentIndex_ < 0 || currentIndex_ >= trackCount_) {
        currentIndex_ = 0;
    }
    setStatus(String(trackCount_) + " WAV found", MusicPlaybackState::STOPPED);
    return true;
}

int MusicManager::trackCount() const {
    return trackCount_;
}

int MusicManager::currentIndex() const {
    return currentIndex_;
}

String MusicManager::trackName(int index) const {
    return (index >= 0 && index < trackCount_) ? trackNames_[index] : String();
}

String MusicManager::currentTitle() const {
    if (currentIndex_ >= 0 && currentIndex_ < trackCount_) {
        return trackNames_[currentIndex_];
    }
    return trackCount_ > 0 ? trackNames_[0] : String("No WAV file");
}

String MusicManager::statusText() const {
    return status_;
}

MusicPlaybackState MusicManager::state() const {
    return state_;
}

bool MusicManager::play(int index) {
    if (trackCount_ == 0 && !scan()) {
        return false;
    }
    if (index < 0 || index >= trackCount_) {
        setStatus("Track index error", MusicPlaybackState::ERROR);
        return false;
    }
    requestedIndex_ = index;
    paused_ = false;
    stopRequested_ = true;
    playRequested_ = true;
    setStatus("Starting", MusicPlaybackState::PLAYING);
    return true;
}

void MusicManager::togglePause() {
    if (state_ == MusicPlaybackState::PLAYING) {
        paused_ = true;
        setStatus("Paused", MusicPlaybackState::PAUSED);
        return;
    }
    if (state_ == MusicPlaybackState::PAUSED) {
        paused_ = false;
        setStatus("Playing", MusicPlaybackState::PLAYING);
        return;
    }
    int index = currentIndex_ >= 0 ? currentIndex_ : 0;
    play(index);
}

void MusicManager::stop() {
    playRequested_ = false;
    stopRequested_ = true;
    paused_ = false;
    M5.Speaker.stop(MUSIC_CHANNEL);
    setStatus("Stopped", MusicPlaybackState::STOPPED);
}

bool MusicManager::next() {
    if (trackCount_ == 0 && !scan()) {
        return false;
    }
    int nextIndex = currentIndex_ + 1;
    if (nextIndex < 0 || nextIndex >= trackCount_) {
        nextIndex = 0;
    }
    return play(nextIndex);
}

void MusicManager::taskThunk(void* arg) {
    static_cast<MusicManager*>(arg)->taskLoop();
}

void MusicManager::taskLoop() {
    while (true) {
        if (playRequested_) {
            int index = requestedIndex_;
            playRequested_ = false;
            stopRequested_ = false;
            playFile(index);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void MusicManager::playFile(int index) {
    if (index < 0 || index >= trackCount_) {
        setStatus("Track index error", MusicPlaybackState::ERROR);
        return;
    }

    File file = SD.open(trackPaths_[index], FILE_READ);
    if (!file) {
        setStatus("Open failed", MusicPlaybackState::ERROR);
        return;
    }

    WavInfo info;
    String error;
    if (!parseWav(file, info, error)) {
        file.close();
        setStatus(error, MusicPlaybackState::ERROR);
        return;
    }

    if (!beginSpeaker()) {
        file.close();
        setStatus("Speaker failed", MusicPlaybackState::ERROR);
        return;
    }

    currentIndex_ = index;
    setStatus("Playing", MusicPlaybackState::PLAYING);
    M5.Speaker.stop(MUSIC_CHANNEL);
    file.seek(info.dataOffset);

    uint32_t remaining = info.dataSize;
    int bufferIndex = 0;
    bool stereo = info.channels > 1;
    while (remaining > 0 && !stopRequested_ && !playRequested_) {
        while (paused_ && !stopRequested_ && !playRequested_) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (stopRequested_ || playRequested_) break;

        size_t bytesToRead = remaining > BUFFER_BYTES ? BUFFER_BYTES : remaining;
        if (info.blockAlign > 1 && bytesToRead > info.blockAlign) {
            bytesToRead -= bytesToRead % info.blockAlign;
        }
        if (bytesToRead == 0) break;

        int bytesRead = file.read(buffers_[bufferIndex], bytesToRead);
        if (bytesRead <= 0) {
            setStatus("Read failed", MusicPlaybackState::ERROR);
            break;
        }
        remaining -= bytesRead;

        bool queued = false;
        if (info.bitsPerSample == 16) {
            queued = M5.Speaker.playRaw(reinterpret_cast<int16_t*>(buffers_[bufferIndex]),
                                        bytesRead / 2, info.sampleRate, stereo, 1,
                                        MUSIC_CHANNEL, false);
        } else {
            queued = M5.Speaker.playRaw(buffers_[bufferIndex], bytesRead, info.sampleRate,
                                        stereo, 1, MUSIC_CHANNEL, false);
        }
        if (!queued) {
            setStatus("Speaker queue failed", MusicPlaybackState::ERROR);
            break;
        }

        bufferIndex = (bufferIndex + 1) % BUFFER_COUNT;
    }

    file.close();

    if (stopRequested_ || playRequested_) {
        M5.Speaker.stop(MUSIC_CHANNEL);
        if (!playRequested_) {
            setStatus("Stopped", MusicPlaybackState::STOPPED);
        }
        return;
    }

    setStatus("Finishing", MusicPlaybackState::PLAYING);
    while (M5.Speaker.isPlaying(MUSIC_CHANNEL) && !stopRequested_ && !playRequested_) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!stopRequested_ && !playRequested_) {
        setStatus("Done", MusicPlaybackState::STOPPED);
    }
}

bool MusicManager::parseWav(File& file, WavInfo& info, String& error) {
    char riff[4];
    char wave[4];
    uint32_t riffSize = 0;

    file.seek(0);
    if (!readExact(file, riff, sizeof(riff)) || !readU32(file, riffSize) ||
        !readExact(file, wave, sizeof(wave)) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
        error = "Not a WAV file";
        return false;
    }
    (void)riffSize;

    bool haveFmt = false;
    bool haveData = false;
    while (file.available()) {
        char id[4];
        uint32_t chunkSize = 0;
        if (!readExact(file, id, sizeof(id)) || !readU32(file, chunkSize)) break;
        uint32_t chunkData = file.position();
        uint32_t nextChunk = chunkData + chunkSize + (chunkSize & 1);

        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t format = 0;
            uint32_t byteRate = 0;
            if (!readU16(file, format) ||
                !readU16(file, info.channels) ||
                !readU32(file, info.sampleRate) ||
                !readU32(file, byteRate) ||
                !readU16(file, info.blockAlign) ||
                !readU16(file, info.bitsPerSample)) {
                error = "Bad WAV header";
                return false;
            }
            (void)byteRate;
            if (format != 1 || (info.bitsPerSample != 8 && info.bitsPerSample != 16) ||
                info.channels == 0 || info.channels > 2 || info.sampleRate == 0) {
                error = "Unsupported WAV";
                return false;
            }
            haveFmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            info.dataOffset = chunkData;
            info.dataSize = chunkSize;
            haveData = true;
        }

        if (haveFmt && haveData) {
            return true;
        }
        file.seek(nextChunk);
    }

    error = "WAV data missing";
    return false;
}

bool MusicManager::beginSpeaker() {
    if (!M5.Speaker.isRunning()) {
        auto cfg = M5.Speaker.config();
        cfg.sample_rate = 48000;
        cfg.stereo = false;
        cfg.dma_buf_count = 8;
        cfg.dma_buf_len = 256;
        cfg.task_priority = 5;
        M5.Speaker.config(cfg);
        if (!M5.Speaker.begin()) {
            return false;
        }
    }
    M5.Speaker.setVolume(160);
    return true;
}

void MusicManager::setStatus(const String& status, MusicPlaybackState state) {
    status_ = status;
    state_ = state;
}

void MusicManager::sortTracks() {
    for (int i = 0; i < trackCount_ - 1; ++i) {
        for (int j = i + 1; j < trackCount_; ++j) {
            if (trackNames_[j].compareTo(trackNames_[i]) < 0) {
                String tmpName = trackNames_[i];
                String tmpPath = trackPaths_[i];
                trackNames_[i] = trackNames_[j];
                trackPaths_[i] = trackPaths_[j];
                trackNames_[j] = tmpName;
                trackPaths_[j] = tmpPath;
            }
        }
    }
}
