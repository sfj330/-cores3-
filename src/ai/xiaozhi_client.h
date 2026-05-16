#pragma once

#include <Arduino.h>
#include <functional>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <opus.h>
#include "ai/voice_state.h"

class XiaoZhiClient {
public:
    using McpToolCallback = std::function<void(const String& toolName, JsonObjectConst arguments, int callId)>;
    using TranscriptCallback = std::function<void(const String& text)>;

    XiaoZhiClient();
    bool begin();
    bool end();

    bool requestActivationCode();
    bool checkActivation();

    bool isConnected() const;
    bool isWsConnected() const;
    bool isActivated() const;
    bool hasActivationCode() const;
    String getActivationCode() const;
    String getActivationMessage() const;
    String getWebSocketUrl() const;
    String getToken() const;
    String getLastError() const;
    String getFirmwareIdentity() const;
    String getVisionUrl() const;
    String getVisionToken() const;
    bool hasVisionEndpoint() const;
    bool isPreparingListening() const;
    bool isListeningStarted() const;

    bool startListening();
    bool stopListening();
    bool openAudioChannel();
    void closeAudioChannel();
    void pauseForForegroundTool();
    void resumeFromForegroundTool();
    bool isAudioChannelOpen() const;

    void setStateCallback(std::function<void(VoiceState)> cb);
    void setMcpToolCallback(McpToolCallback cb);
    void setTranscriptCallback(TranscriptCallback cb);
    VoiceState getState() const;
    void process();
    void queueMcpToolTextResult(int id, const String& text, bool isError);

private:
    void setState(VoiceState state);
    String getMacAddress();
    String getDeviceId();
    String buildUserAgent() const;
    String buildSystemInfoJson() const;

    void wsEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleTextMessage(const char* data, size_t len);
    void handleBinaryMessage(const uint8_t* data, size_t len);
    void parseServerHello(const char* data, size_t len);
    String getHelloMessage();
    void handleMcpMessage(JsonObjectConst payload);

private:
    void sendMcpResult(int id, const String& resultJson);
    void sendMcpError(int id, const String& message);
    void sendMcpToolTextResult(int id, const String& text, bool isError);
    void sendMcpToolsList(int id);
    void parseMcpInitializeParams(JsonObjectConst params);
    bool tryStartListeningStream();

    bool initAudio();
    void deinitAudio();
    bool startMic();
    void stopMic();
    bool startSpeaker();
    void stopSpeaker();
    void audioCaptureTask();
    bool sendAudioFrame(const int16_t* pcmData, size_t sampleCount);
    void sendListenStart();
    void sendListenStop();
    void sendAbort();
    bool initOpusCodec();
    void deinitOpusCodec();
    void flushOutgoingAudio();

    static constexpr int OPUS_MAX_PACKET_BYTES = 1500;
    static constexpr int OPUS_MAX_DECODE_SAMPLES = 5760;
    static constexpr int PLAYBACK_BUFFER_COUNT = 3;

    struct OpusTxPacket {
        uint16_t length = 0;
        uint8_t data[OPUS_MAX_PACKET_BYTES] = {};
    };

    bool initialized_ = false;
    bool connected_ = false;
    bool activated_ = false;
    bool hasActivationCode_ = false;
    bool wsConnected_ = false;
    bool audioChannelOpen_ = false;
    bool audioInitialized_ = false;
    bool serverHelloReceived_ = false;
    bool opusReady_ = false;
    bool openingAudioChannel_ = false;
    bool micActive_ = false;
    bool speakerActive_ = false;
    bool listenRequested_ = false;
    bool listenStarted_ = false;
    bool mcpReady_ = false;
    volatile bool pendingStartListening_ = false;
    volatile bool pendingStopListening_ = false;
    VoiceState state_ = VoiceState::IDLE;
    std::function<void(VoiceState)> callback_;
    McpToolCallback mcpToolCallback_;

    struct PendingMcpResult {
        int id = 0;
        String text;
        bool isError = false;
        bool valid = false;
    };
    static constexpr int MAX_PENDING_MCP = 4;
    PendingMcpResult pendingMcpResults_[MAX_PENDING_MCP];
    TranscriptCallback transcriptCallback_;

    String activationCode_;
    String activationMessage_;
    String webSocketUrl_;
    String token_;
    String visionUrl_;
    String visionToken_;
    String sessionId_;
    int lastHttpCode_ = 0;
    String lastError_;
    String deviceId_;
    String clientId_;
    unsigned long lastActivationAttemptMs_ = 0;
    unsigned long lastActivationErrorMs_ = 0;
    unsigned long lastAudioOpenAttemptMs_ = 0;

    WebSocketsClient webSocket_;
    int protocolVersion_ = 1;
    int serverSampleRate_ = 24000;
    int serverFrameDuration_ = 60;

    int16_t* pcmCaptureBuf_ = nullptr;
    int16_t* pcmPlaybackBufs_[PLAYBACK_BUFFER_COUNT] = {};
    size_t playbackBufferIndex_ = 0;
    uint8_t* opusEncodeBuf_ = nullptr;
    OpusEncoder* opusEncoder_ = nullptr;
    OpusDecoder* opusDecoder_ = nullptr;
    QueueHandle_t opusTxQueue_ = nullptr;

    TaskHandle_t audioCaptureTaskHandle_ = nullptr;
    volatile bool audioCaptureRunning_ = false;

    static constexpr const char* OTA_URL = "https://api.tenclass.net/xiaozhi/ota/";
    static constexpr unsigned long ACTIVATION_ERROR_COOLDOWN_MS = 30000;
    static constexpr unsigned long AUDIO_OPEN_RETRY_COOLDOWN_MS = 10000;
    static constexpr int AUDIO_SAMPLE_RATE = 16000;
    static constexpr int AUDIO_CHANNELS = 1;
    static constexpr int AUDIO_FRAME_DURATION_MS = 60;
    static constexpr int AUDIO_FRAME_SAMPLES = AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_MS / 1000;
    static constexpr int OPUS_BITRATE = 24000;
    static constexpr int OPUS_COMPLEXITY = 1;
};
