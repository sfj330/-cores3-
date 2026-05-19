#include "audio/music_manager.h"
#include "config/app_config.h"
#include <M5CoreS3.h>
#include <SD.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3a.h>
#include <AudioOutput.h>
#include <cstring>

namespace {
constexpr int MUSIC_SPEAKER_CHANNEL = 6;
constexpr uint32_t MUSIC_MP3_FILE_BUFFER_BYTES = 4096;
uint8_t gMusicRuntimeVolume = MUSIC_SPEAKER_VOLUME;

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

bool hasMp3Extension(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".mp3");
}

bool hasAudioExtension(const String& name) {
    return hasWavExtension(name) || hasMp3Extension(name);
}

class M5SpeakerAudioOutput : public AudioOutput {
public:
    M5SpeakerAudioOutput() {
        hertz = 44100;
        channels = 2;
        bps = 16;
        gainF2P6 = 64;
    }

    bool SetRate(int hz) override {
        if (hz > 0 && hz != hertz) {
            Serial.printf("Music MP3: sample rate %d Hz\n", hz);
            hertz = hz;
        }
        return true;
    }

    bool SetChannels(int chan) override {
        uint8_t safeChannels = chan <= 1 ? 1 : 2;
        if (safeChannels != channels) {
            Serial.printf("Music MP3: channels %u -> mono output\n", safeChannels);
            channels = safeChannels;
        }
        return true;
    }

    bool SetBitsPerSample(int bits) override {
        bps = bits > 0 ? bits : 16;
        return true;
    }

    bool begin() override {
        if (M5.Speaker.isRunning()) {
            M5.Speaker.stop(MUSIC_SPEAKER_CHANNEL);
            M5.Speaker.setVolume(gMusicRuntimeVolume);
            vTaskDelay(pdMS_TO_TICKS(30));
            resetBuffers();
            Serial.printf("Music MP3: output begin (reusing speaker) rate=%u volume=%u\n",
                          hertz, static_cast<unsigned>(gMusicRuntimeVolume));
            return true;
        }
        auto cfg = M5.Speaker.config();
        cfg.sample_rate = hertz > 0 ? hertz : 44100;
        cfg.stereo = false;
        cfg.dma_buf_count = 8;
        cfg.dma_buf_len = 256;
        cfg.task_priority = 5;
        M5.Speaker.config(cfg);
        if (!M5.Speaker.begin()) {
            Serial.println("Music MP3: speaker begin failed");
            return false;
        }
        M5.Speaker.stop(MUSIC_SPEAKER_CHANNEL);
        M5.Speaker.setVolume(gMusicRuntimeVolume);
        vTaskDelay(pdMS_TO_TICKS(30));
        resetBuffers();
        Serial.printf("Music MP3: output begin (fresh) rate=%u volume=%u\n",
                      hertz, static_cast<unsigned>(gMusicRuntimeVolume));
        return true;
    }

    bool ConsumeSample(int16_t sample[2]) override {
        if (frameCount_ >= BUFFER_FRAMES && !flushBuffered(false)) {
            return false;
        }

        int32_t mixed = sample[LEFTCHANNEL];
        if (channels > 1) {
            mixed = (static_cast<int32_t>(sample[LEFTCHANNEL]) +
                     static_cast<int32_t>(sample[RIGHTCHANNEL])) / 2;
        }
        buffer_[writeBufferIndex_][frameCount_] = Amplify(static_cast<int16_t>(mixed));
        frameCount_++;

        if (frameCount_ >= BUFFER_FRAMES) {
            return flushBuffered(false);
        }
        return true;
    }

    bool stop() override {
        flushBuffered(true);
        while (queuedBuffers_ > 0 && M5.Speaker.isPlaying(MUSIC_SPEAKER_CHANNEL)) {
            refreshQueuedBuffers();
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        resetBuffers();
        Serial.println("Music MP3: output stopped");
        return true;
    }

    void flush() override {
        flushBuffered(true);
        while (queuedBuffers_ > 0 && M5.Speaker.isPlaying(MUSIC_SPEAKER_CHANNEL)) {
            refreshQueuedBuffers();
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

private:
    void resetBuffers() {
        frameCount_ = 0;
        writeBufferIndex_ = 0;
        queuedBuffers_ = 0;
        queueFailCount_ = 0;
    }

    void refreshQueuedBuffers() {
        size_t playing = M5.Speaker.isPlaying(MUSIC_SPEAKER_CHANNEL);
        if (playing > BUFFER_COUNT) playing = BUFFER_COUNT;
        queuedBuffers_ = static_cast<uint8_t>(playing);
    }

    bool flushBuffered(bool waitForQueue) {
        if (frameCount_ == 0) return true;
        refreshQueuedBuffers();
        while (true) {
            if (queuedBuffers_ >= MAX_QUEUED_BUFFERS) {
                queueFailCount_++;
                if (queueFailCount_ == 1 || (queueFailCount_ % 25) == 0) {
                    Serial.printf("Music MP3: speaker queue full rate=%u frames=%u wait=%d queued=%u fail=%u\n",
                                  hertz,
                                  frameCount_,
                                  waitForQueue ? 1 : 0,
                                  queuedBuffers_,
                                  queueFailCount_);
                }
                if (!waitForQueue) {
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                refreshQueuedBuffers();
                continue;
            }

            bool queued = M5.Speaker.playRaw(buffer_[writeBufferIndex_], frameCount_, hertz,
                                            false, 1, MUSIC_SPEAKER_CHANNEL, false);
            if (queued) {
                writeBufferIndex_ = (writeBufferIndex_ + 1) % BUFFER_COUNT;
                frameCount_ = 0;
                queueFailCount_ = 0;
                refreshQueuedBuffers();
                return true;
            }
            queueFailCount_++;
            if (queueFailCount_ == 1 || (queueFailCount_ % 25) == 0) {
                Serial.printf("Music MP3: speaker queue busy rate=%u frames=%u wait=%d fail=%u\n",
                              hertz, frameCount_, waitForQueue ? 1 : 0, queueFailCount_);
            }
            if (!waitForQueue) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    static constexpr uint16_t BUFFER_FRAMES = 576;
    static constexpr uint8_t BUFFER_COUNT = 3;
    static constexpr uint8_t MAX_QUEUED_BUFFERS = 2;
    int16_t buffer_[BUFFER_COUNT][BUFFER_FRAMES] = {};
    uint16_t frameCount_ = 0;
    uint8_t writeBufferIndex_ = 0;
    uint8_t queuedBuffers_ = 0;
    uint16_t queueFailCount_ = 0;
};
}

bool MusicManager::begin(StorageManager* storage) {
    storage_ = storage;
    return true;
}

bool MusicManager::ensureTask() {
    if (taskHandle_ == nullptr) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            taskThunk, "Music", 16384, this, 1, &taskHandle_, 0);
        if (ok != pdPASS) {
            setStatus("Music task failed", MusicPlaybackState::ERROR);
            return false;
        }
        Serial.println("Music: task started stack=16384");
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
            if (hasAudioExtension(name)) {
                String path = name.startsWith("/") ? name : String(MUSIC_DIR) + "/" + name;
                int slash = path.lastIndexOf('/');
                trackPaths_[trackCount_] = path;
                trackNames_[trackCount_] = slash >= 0 ? path.substring(slash + 1) : path;
                trackTypes_[trackCount_] = hasMp3Extension(name) ? TrackType::MP3 : TrackType::WAV;
                trackCount_++;
            }
        }
        entry.close();
    }
    dir.close();
    sortTracks();

    if (trackCount_ == 0) {
        setStatus("No audio files", MusicPlaybackState::ERROR);
        Serial.println("Music: no audio files found");
        return false;
    }

    if (currentIndex_ < 0 || currentIndex_ >= trackCount_) {
        currentIndex_ = 0;
    }
    setStatus(String(trackCount_) + " audio found", MusicPlaybackState::STOPPED);
    Serial.printf("Music: scanned %d audio files\n", trackCount_);
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
    return trackCount_ > 0 ? trackNames_[0] : String("No audio file");
}

String MusicManager::statusText() const {
    return status_;
}

MusicPlaybackState MusicManager::state() const {
    return state_;
}

void MusicManager::setVolumePreset(uint8_t volume) {
    volumePreset_ = volume;
    gMusicRuntimeVolume = volume;
    if (M5.Speaker.isRunning()) {
        M5.Speaker.setVolume(volumePreset_);
    }
}

uint8_t MusicManager::volumePreset() const {
    return volumePreset_;
}

bool MusicManager::play(int index) {
    if (trackCount_ == 0 && !scan()) {
        return false;
    }
    if (index < 0 || index >= trackCount_) {
        setStatus("Track index error", MusicPlaybackState::ERROR);
        return false;
    }
    if (!ensureTask()) {
        return false;
    }
    requestedIndex_ = index;
    paused_ = false;
    stopRequested_ = true;
    playRequested_ = true;
    setStatus("Starting", MusicPlaybackState::PLAYING);
    Serial.printf("Music: play request index=%d title=%s\n", index, trackNames_[index].c_str());
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
    Serial.println("Music: stop requested");
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

bool MusicManager::waitForIdle(uint32_t timeoutMs) const {
    unsigned long deadline = millis() + timeoutMs;
    while (playbackActive_) {
        if (timeoutMs != 0 && static_cast<long>(millis() - deadline) >= 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

void MusicManager::releaseSpeaker() {
    M5.Speaker.stop(MUSIC_CHANNEL);
    M5.Speaker.stop(MUSIC_SPEAKER_CHANNEL);
    if (M5.Speaker.isRunning()) {
        M5.Speaker.end();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
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

    playbackActive_ = true;
    Serial.printf("Music: play file path=%s type=%s\n",
                  trackPaths_[index].c_str(),
                  trackTypes_[index] == TrackType::MP3 ? "MP3" : "WAV");

    if (trackTypes_[index] == TrackType::MP3) {
        playMp3File(index);
        playbackActive_ = false;
        return;
    }

    File file = SD.open(trackPaths_[index], FILE_READ);
    if (!file) {
        setStatus("Open failed", MusicPlaybackState::ERROR);
        Serial.printf("Music: open failed path=%s\n", trackPaths_[index].c_str());
        playbackActive_ = false;
        return;
    }

    playWavFile(index, file);
    playbackActive_ = false;
}

void MusicManager::playWavFile(int index, File& file) {
    if (index < 0 || index >= trackCount_) {
        file.close();
        setStatus("Track index error", MusicPlaybackState::ERROR);
        return;
    }

    WavInfo info;
    String error;
    if (!parseWav(file, info, error)) {
        file.close();
        setStatus(error, MusicPlaybackState::ERROR);
        Serial.printf("Music WAV: parse failed path=%s error=%s\n",
                      trackPaths_[index].c_str(), error.c_str());
        return;
    }

    if (!beginSpeaker()) {
        file.close();
        setStatus("Speaker failed", MusicPlaybackState::ERROR);
        Serial.println("Music WAV: speaker begin failed");
        return;
    }

    currentIndex_ = index;
    setStatus("Playing", MusicPlaybackState::PLAYING);
    M5.Speaker.stop(MUSIC_CHANNEL);
    file.seek(info.dataOffset);
    Serial.printf("Music WAV: playing path=%s rate=%lu channels=%u bits=%u\n",
                  trackPaths_[index].c_str(),
                  static_cast<unsigned long>(info.sampleRate),
                  static_cast<unsigned>(info.channels),
                  static_cast<unsigned>(info.bitsPerSample));

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
            Serial.printf("Music WAV: read failed path=%s\n", trackPaths_[index].c_str());
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
            Serial.printf("Music WAV: speaker queue failed path=%s bytes=%d rate=%lu stereo=%d\n",
                          trackPaths_[index].c_str(),
                          bytesRead,
                          static_cast<unsigned long>(info.sampleRate),
                          stereo ? 1 : 0);
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
        Serial.printf("Music WAV: stopped path=%s replaced=%d\n",
                      trackPaths_[index].c_str(), playRequested_ ? 1 : 0);
        return;
    }

    setStatus("Finishing", MusicPlaybackState::PLAYING);
    while (M5.Speaker.isPlaying(MUSIC_CHANNEL) && !stopRequested_ && !playRequested_) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!stopRequested_ && !playRequested_) {
        setStatus("Done", MusicPlaybackState::STOPPED);
        Serial.printf("Music WAV: done path=%s\n", trackPaths_[index].c_str());
    }
}

void MusicManager::playMp3File(int index) {
    if (index < 0 || index >= trackCount_) {
        setStatus("Track index error", MusicPlaybackState::ERROR);
        return;
    }

    M5.Speaker.stop(MUSIC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(50));

    AudioFileSourceSD source;
    if (!source.open(trackPaths_[index].c_str())) {
        setStatus("Open failed", MusicPlaybackState::ERROR);
        Serial.printf("Music MP3: open failed path=%s\n", trackPaths_[index].c_str());
        return;
    }

    AudioFileSourceID3 taggedSource(&source);
    AudioFileSourceBuffer bufferedSource(&taggedSource, MUSIC_MP3_FILE_BUFFER_BYTES);
    M5SpeakerAudioOutput output;
    AudioGeneratorMP3a mp3;
    if (!mp3.begin(&bufferedSource, &output)) {
        source.close();
        setStatus("MP3 decode failed", MusicPlaybackState::ERROR);
        Serial.printf("Music MP3: decoder begin failed path=%s\n", trackPaths_[index].c_str());
        return;
    }

    currentIndex_ = index;
    setStatus("Playing", MusicPlaybackState::PLAYING);
    Serial.printf("Music MP3: playing path=%s\n", trackPaths_[index].c_str());

    while (mp3.isRunning() && !stopRequested_ && !playRequested_) {
        while (paused_ && !stopRequested_ && !playRequested_) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (stopRequested_ || playRequested_) break;
        if (!mp3.loop()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (stopRequested_ || playRequested_) {
        mp3.stop();
        source.close();
        M5.Speaker.stop(MUSIC_CHANNEL);
        if (!playRequested_) {
            setStatus("Stopped", MusicPlaybackState::STOPPED);
        }
        Serial.printf("Music MP3: stopped path=%s replaced=%d\n",
                      trackPaths_[index].c_str(), playRequested_ ? 1 : 0);
        return;
    }

    output.flush();
    mp3.stop();
    source.close();

    setStatus("Finishing", MusicPlaybackState::PLAYING);
    while (M5.Speaker.isPlaying(MUSIC_CHANNEL) && !stopRequested_ && !playRequested_) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!stopRequested_ && !playRequested_) {
        setStatus("Done", MusicPlaybackState::STOPPED);
        Serial.printf("Music MP3: done path=%s\n", trackPaths_[index].c_str());
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
    M5.Speaker.stop(MUSIC_CHANNEL);
    if (M5.Speaker.isRunning()) {
        M5.Speaker.setVolume(volumePreset_);
        vTaskDelay(pdMS_TO_TICKS(30));
        return true;
    }
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = 44100;
    cfg.stereo = false;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.task_priority = 5;
    M5.Speaker.config(cfg);
    if (!M5.Speaker.begin()) {
        Serial.println("Music: speaker begin failed");
        return false;
    }
    M5.Speaker.setVolume(volumePreset_);
    vTaskDelay(pdMS_TO_TICKS(30));
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
                TrackType tmpType = trackTypes_[i];
                trackNames_[i] = trackNames_[j];
                trackPaths_[i] = trackPaths_[j];
                trackTypes_[i] = trackTypes_[j];
                trackNames_[j] = tmpName;
                trackPaths_[j] = tmpPath;
                trackTypes_[j] = tmpType;
            }
        }
    }
}
