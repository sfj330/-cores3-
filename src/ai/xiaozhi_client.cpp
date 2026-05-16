#include "xiaozhi_client.h"
#include "config/app_config.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <M5CoreS3.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace {
constexpr bool XIAOZHI_VERBOSE_WS_LOG = false;
}

XiaoZhiClient::XiaoZhiClient() = default;

bool XiaoZhiClient::begin() {
    initialized_ = true;
    connected_ = false;
    activated_ = false;
    hasActivationCode_ = false;
    wsConnected_ = false;
    audioChannelOpen_ = false;
    audioInitialized_ = false;
    serverHelloReceived_ = false;
    opusReady_ = false;
    openingAudioChannel_ = false;
    micActive_ = false;
    speakerActive_ = false;
    listenRequested_ = false;
    listenStarted_ = false;
    mcpReady_ = false;
    pendingStartListening_ = false;
    pendingStopListening_ = false;
    lastHttpCode_ = 0;
    lastError_ = "";
    webSocketUrl_ = "";
    token_ = "";
    visionUrl_ = "";
    visionToken_ = "";
    sessionId_ = "";
    deviceId_ = getMacAddress();
    deviceId_.toLowerCase();
    String macClean = deviceId_;
    macClean.replace(":", "");
    char uuidBuf[37];
    snprintf(uuidBuf, sizeof(uuidBuf),
             "%.8s-%.4s-4%.3s-a%.3s-%.2s%.6s%.4s",
             macClean.c_str(), macClean.c_str() + 8,
             macClean.c_str() + 1, macClean.c_str() + 5,
             macClean.c_str() + 10, macClean.c_str(),
             macClean.c_str() + 6);
    clientId_ = uuidBuf;

    pcmCaptureBuf_ = (int16_t*)heap_caps_malloc(AUDIO_FRAME_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (int i = 0; i < PLAYBACK_BUFFER_COUNT; ++i) {
        pcmPlaybackBufs_[i] = (int16_t*)heap_caps_malloc(OPUS_MAX_DECODE_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    opusEncodeBuf_ = (uint8_t*)heap_caps_malloc(OPUS_MAX_PACKET_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    opusTxQueue_ = xQueueCreate(4, sizeof(OpusTxPacket));

    bool playbackBuffersOk = true;
    for (int i = 0; i < PLAYBACK_BUFFER_COUNT; ++i) {
        playbackBuffersOk = playbackBuffersOk && pcmPlaybackBufs_[i] != nullptr;
    }
    if (!pcmCaptureBuf_ || !playbackBuffersOk || !opusEncodeBuf_ || !opusTxQueue_) {
        Serial.println("XiaoZhi: audio buffer allocation failed");
        lastError_ = "Audio alloc failed";
    }

    Serial.println("XiaoZhiClient OK");
    return true;
}

bool XiaoZhiClient::end() {
    closeAudioChannel();
    deinitAudio();
    deinitOpusCodec();
    if (pcmCaptureBuf_) { heap_caps_free(pcmCaptureBuf_); pcmCaptureBuf_ = nullptr; }
    for (int i = 0; i < PLAYBACK_BUFFER_COUNT; ++i) {
        if (pcmPlaybackBufs_[i]) {
            heap_caps_free(pcmPlaybackBufs_[i]);
            pcmPlaybackBufs_[i] = nullptr;
        }
    }
    if (opusEncodeBuf_) { heap_caps_free(opusEncodeBuf_); opusEncodeBuf_ = nullptr; }
    if (opusTxQueue_) {
        vQueueDelete(opusTxQueue_);
        opusTxQueue_ = nullptr;
    }
    activated_ = false;
    connected_ = false;
    initialized_ = false;
    return true;
}

String XiaoZhiClient::getMacAddress() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

String XiaoZhiClient::getDeviceId() { return deviceId_; }

String XiaoZhiClient::buildUserAgent() const {
    return String(XIAOZHI_BOARD_TYPE) + "/" + XIAOZHI_FIRMWARE_VERSION;
}

String XiaoZhiClient::buildSystemInfoJson() const {
    JsonDocument doc;
    doc["version"] = 2;
    doc["language"] = "zh-CN";
    doc["flash_size"] = ESP.getFlashChipSize();
    doc["minimum_free_heap_size"] = ESP.getMinFreeHeap();
    doc["mac_address"] = deviceId_;
    doc["uuid"] = clientId_;
    doc["chip_model_name"] = XIAOZHI_CHIP_ID;
    esp_chip_info_t chipInfoRaw;
    esp_chip_info(&chipInfoRaw);
    JsonObject chipInfo = doc["chip_info"].to<JsonObject>();
    chipInfo["model"] = static_cast<int>(chipInfoRaw.model);
    chipInfo["cores"] = chipInfoRaw.cores;
    chipInfo["revision"] = chipInfoRaw.revision;
    chipInfo["features"] = chipInfoRaw.features;
    JsonObject app = doc["application"].to<JsonObject>();
    app["name"] = XIAOZHI_APP_NAME;
    app["version"] = XIAOZHI_FIRMWARE_VERSION;
    app["compile_time"] = String(__DATE__) + "T" + String(__TIME__) + "Z";
    app["idf_version"] = ESP.getSdkVersion();
    app["elf_sha256"] = "";
    doc["partition_table"].to<JsonArray>();
    JsonObject ota = doc["ota"].to<JsonObject>();
    ota["label"] = "factory";
    String body;
    serializeJson(doc, body);
    return body;
}

bool XiaoZhiClient::requestActivationCode() {
    if (!XIAOZHI_REAL_ACTIVATION) return false;
    if (!initialized_ || WiFi.status() != WL_CONNECTED) {
        connected_ = false;
        return false;
    }
    unsigned long now = millis();
    if (lastHttpCode_ >= 400 && lastActivationErrorMs_ > 0 &&
        now - lastActivationErrorMs_ < ACTIVATION_ERROR_COOLDOWN_MS) {
        return false;
    }
    lastActivationAttemptMs_ = now;

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, OTA_URL)) return false;

    http.setUserAgent(buildUserAgent());
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Activation-Version", "1");
    http.addHeader("Device-Id", deviceId_);
    http.addHeader("Client-Id", clientId_);
    http.addHeader("Accept-Language", "zh-CN");

    String body = buildSystemInfoJson();
    Serial.printf("XiaoZhi: OTA POST identity=%s\n", buildUserAgent().c_str());

    int code = http.POST(body);
    if (code != 200) {
        String errBody = http.getString();
        Serial.printf("XiaoZhi: OTA failed code=%d body=%s\n", code, errBody.c_str());
        lastHttpCode_ = code;
        lastActivationErrorMs_ = millis();
        lastError_ = "HTTP " + String(code);
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();
    lastHttpCode_ = code;
    lastError_ = "";
    connected_ = true;
    Serial.printf("XiaoZhi: OTA response: %s\n", response.c_str());

    JsonDocument resp;
    DeserializationError err = deserializeJson(resp, response);
    if (err) {
        Serial.printf("XiaoZhi: JSON parse failed: %s\n", err.c_str());
        return false;
    }

    if (resp["activation"].is<JsonObject>()) {
        JsonObject act = resp["activation"];
        if (!act["code"].isNull()) {
            activationCode_ = act["code"].as<String>();
            hasActivationCode_ = true;
            Serial.printf("XiaoZhi: activation code: %s\n", activationCode_.c_str());
        }
        if (!act["message"].isNull()) {
            activationMessage_ = act["message"].as<String>();
        }
    }

    if (resp["websocket"].is<JsonObject>()) {
        JsonObject ws = resp["websocket"];
        if (!ws["url"].isNull()) webSocketUrl_ = ws["url"].as<String>();
        if (!ws["token"].isNull()) token_ = ws["token"].as<String>();
        if (webSocketUrl_.length() > 0 && token_.length() > 0) {
            activated_ = true;
            Serial.println("XiaoZhi: device activated, got WS config");
        }
    }

    return hasActivationCode_ || activated_;
}

bool XiaoZhiClient::checkActivation() {
    if (!XIAOZHI_REAL_ACTIVATION) return false;
    if (!initialized_ || WiFi.status() != WL_CONNECTED) return false;
    if (activated_) return true;
    requestActivationCode();
    return activated_;
}

bool XiaoZhiClient::isConnected() const { return connected_; }
bool XiaoZhiClient::isWsConnected() const { return wsConnected_; }
bool XiaoZhiClient::isActivated() const { return activated_; }
bool XiaoZhiClient::hasActivationCode() const { return hasActivationCode_; }
String XiaoZhiClient::getActivationCode() const { return activationCode_; }
String XiaoZhiClient::getActivationMessage() const { return activationMessage_; }
String XiaoZhiClient::getWebSocketUrl() const { return webSocketUrl_; }
String XiaoZhiClient::getToken() const { return token_; }
String XiaoZhiClient::getLastError() const { return lastError_; }
String XiaoZhiClient::getFirmwareIdentity() const { return buildUserAgent(); }
String XiaoZhiClient::getVisionUrl() const { return visionUrl_; }
String XiaoZhiClient::getVisionToken() const { return visionToken_; }
bool XiaoZhiClient::hasVisionEndpoint() const { return visionUrl_.length() > 0; }
bool XiaoZhiClient::isPreparingListening() const {
    return openingAudioChannel_ ||
           pendingStartListening_ ||
           (listenRequested_ && !listenStarted_);
}
bool XiaoZhiClient::isListeningStarted() const {
    return listenStarted_ && audioCaptureRunning_ && micActive_ && wsConnected_;
}

String XiaoZhiClient::getHelloMessage() {
    JsonDocument doc;
    doc["type"] = "hello";
    doc["version"] = protocolVersion_;
    JsonObject features = doc["features"].to<JsonObject>();
    features["mcp"] = true;
    doc["transport"] = "websocket";
    JsonObject audioParams = doc["audio_params"].to<JsonObject>();
    audioParams["format"] = "opus";
    audioParams["sample_rate"] = AUDIO_SAMPLE_RATE;
    audioParams["channels"] = AUDIO_CHANNELS;
    audioParams["frame_duration"] = AUDIO_FRAME_DURATION_MS;
    String msg;
    serializeJson(doc, msg);
    return msg;
}

bool XiaoZhiClient::openAudioChannel() {
    if (audioChannelOpen_ && wsConnected_ && mcpReady_) {
        Serial.println("XiaoZhi: audio channel already open, resuming listening");
        startListening();
        return true;
    }

    if (openingAudioChannel_) {
        return false;
    }
    unsigned long now = millis();
    if (lastAudioOpenAttemptMs_ > 0 && now - lastAudioOpenAttemptMs_ < AUDIO_OPEN_RETRY_COOLDOWN_MS) {
        lastError_ = "Audio open cooldown";
        return false;
    }
    openingAudioChannel_ = true;
    lastAudioOpenAttemptMs_ = now;

    if (!activated_ || webSocketUrl_.length() == 0) {
        Serial.println("XiaoZhi: cannot open audio channel, not activated");
        lastError_ = "Not activated";
        openingAudioChannel_ = false;
        return false;
    }

    Serial.printf("XiaoZhi: connecting WS to %s\n", webSocketUrl_.c_str());
    wsConnected_ = false;
    serverHelloReceived_ = false;
    audioChannelOpen_ = false;
    listenStarted_ = false;
    mcpReady_ = false;
    sessionId_ = "";
    if (opusTxQueue_) {
        xQueueReset(opusTxQueue_);
    }

    String wsUrl = webSocketUrl_;
    String wsPath = "/";
    int protoEnd = wsUrl.indexOf("://") + 3;
    int pathIdx = wsUrl.indexOf("/", protoEnd);
    if (pathIdx > 0) {
        wsPath = wsUrl.substring(pathIdx);
        wsUrl = wsUrl.substring(0, pathIdx);
    }

    int port = 443;
    bool useSSL = true;
    if (wsUrl.startsWith("ws://")) {
        wsUrl = wsUrl.substring(5);
        useSSL = false;
        port = 80;
    } else if (wsUrl.startsWith("wss://")) {
        wsUrl = wsUrl.substring(6);
    }

    int colonIdx = wsUrl.indexOf(":");
    if (colonIdx > 0) {
        port = wsUrl.substring(colonIdx + 1).toInt();
        wsUrl = wsUrl.substring(0, colonIdx);
    }

    if (useSSL) {
        webSocket_.beginSSL(wsUrl.c_str(), port, wsPath.c_str(), nullptr, "");
    } else {
        webSocket_.begin(wsUrl.c_str(), port, wsPath.c_str(), "");
    }

    String authVal = "Bearer " + token_;
    webSocket_.setAuthorization(authVal.c_str());
    String headers = "Protocol-Version: " + String(protocolVersion_) + "\r\n"
                     "Device-Id: " + deviceId_ + "\r\n"
                     "Client-Id: " + clientId_;
    webSocket_.setExtraHeaders(headers.c_str());

    webSocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        this->wsEvent(type, payload, length);
    });
    webSocket_.setReconnectInterval(5000);
    webSocket_.enableHeartbeat(15000, 3000, 2);

    unsigned long startWait = millis();
    while (!wsConnected_ && millis() - startWait < 10000) {
        webSocket_.loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!wsConnected_) {
        Serial.println("XiaoZhi: WS connect timeout");
        lastError_ = "WS connect timeout";
        setState(VoiceState::ERROR);
        openingAudioChannel_ = false;
        return false;
    }

    String hello = getHelloMessage();
    Serial.printf("XiaoZhi: sending hello: %s\n", hello.c_str());
    webSocket_.sendTXT(hello);

    startWait = millis();
    while (!serverHelloReceived_ && wsConnected_ && millis() - startWait < 10000) {
        webSocket_.loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!serverHelloReceived_) {
        Serial.println("XiaoZhi: server hello timeout");
        lastError_ = "Server hello timeout";
        webSocket_.disconnect();
        wsConnected_ = false;
        setState(VoiceState::ERROR);
        openingAudioChannel_ = false;
        return false;
    }

    if (!audioInitialized_ || !opusReady_) {
        Serial.println("XiaoZhi: audio init incomplete after server hello");
        lastError_ = lastError_.length() > 0 ? lastError_ : String("Audio init failed");
        webSocket_.disconnect();
        wsConnected_ = false;
        audioChannelOpen_ = false;
        openingAudioChannel_ = false;
        setState(VoiceState::ERROR);
        return false;
    }

    audioChannelOpen_ = true;
    openingAudioChannel_ = false;
    lastAudioOpenAttemptMs_ = 0;
    lastError_ = "";
    return true;
}

void XiaoZhiClient::closeAudioChannel() {
    audioCaptureRunning_ = false;
    audioChannelOpen_ = false;
    openingAudioChannel_ = false;
    listenRequested_ = false;
    listenStarted_ = false;
    mcpReady_ = false;
    pendingStartListening_ = false;
    pendingStopListening_ = false;

    if (audioCaptureTaskHandle_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelete(audioCaptureTaskHandle_);
        audioCaptureTaskHandle_ = nullptr;
    }
    if (opusTxQueue_) {
        xQueueReset(opusTxQueue_);
    }

    if (wsConnected_) {
        webSocket_.disconnect();
        wsConnected_ = false;
    }
    deinitAudio();
    deinitOpusCodec();
    serverHelloReceived_ = false;
    listenStarted_ = false;
    mcpReady_ = false;
    lastAudioOpenAttemptMs_ = 0;
    setState(VoiceState::IDLE);
}

void XiaoZhiClient::pauseForForegroundTool() {
    audioCaptureRunning_ = false;
    listenRequested_ = false;
    listenStarted_ = false;
    pendingStartListening_ = false;
    pendingStopListening_ = false;
    stopMic();
    stopSpeaker();
    if (opusTxQueue_) {
        xQueueReset(opusTxQueue_);
    }
    setState(VoiceState::IDLE);
    Serial.println("XiaoZhi: paused for foreground tool");
}

void XiaoZhiClient::resumeFromForegroundTool() {
    if (!audioChannelOpen_ || !wsConnected_) {
        Serial.println("XiaoZhi: cannot resume, channel not open");
        setState(VoiceState::IDLE);
        return;
    }
    startListening();
    Serial.println("XiaoZhi: resumed from foreground tool");
}

bool XiaoZhiClient::isAudioChannelOpen() const {
    return audioChannelOpen_ && wsConnected_;
}

void XiaoZhiClient::wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("XiaoZhi: WS disconnected");
            wsConnected_ = false;
            audioChannelOpen_ = false;
            audioCaptureRunning_ = false;
            listenRequested_ = false;
            listenStarted_ = false;
            mcpReady_ = false;
            if (opusTxQueue_) {
                xQueueReset(opusTxQueue_);
            }
            setState(VoiceState::IDLE);
            break;
        case WStype_CONNECTED:
            Serial.println("XiaoZhi: WS connected");
            wsConnected_ = true;
            break;
        case WStype_TEXT:
            handleTextMessage((const char*)payload, length);
            break;
        case WStype_BIN:
            handleBinaryMessage(payload, length);
            break;
        default:
            break;
    }
}

void XiaoZhiClient::handleTextMessage(const char* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("XiaoZhi: WS JSON parse failed: %s\n", err.c_str());
        return;
    }

    String type = doc["type"] | "";
    if (XIAOZHI_VERBOSE_WS_LOG) {
        Serial.printf("XiaoZhi: WS text: %.*s\n", (int)min((size_t)200, len), data);
    } else if (type != "tts" && type != "response") {
        Serial.printf("XiaoZhi: WS type=%s\n", type.c_str());
    }

    if (type == "hello") {
        parseServerHello(data, len);
    } else if (type == "mcp") {
        if (doc["payload"].is<JsonObject>()) {
            handleMcpMessage(doc["payload"].as<JsonObjectConst>());
        } else {
            Serial.println("XiaoZhi: MCP payload missing");
        }
    } else if (type == "tts") {
        String st = doc["state"] | "";
        if (st == "start") {
            audioCaptureRunning_ = false;
            listenStarted_ = false;
            if (opusTxQueue_) {
                xQueueReset(opusTxQueue_);
            }
            startSpeaker();
            setState(VoiceState::SPEAKING);
        } else if (st == "stop") {
            stopSpeaker();
            tryStartListeningStream();
            if (!listenStarted_) {
                setState(VoiceState::IDLE);
            }
        }
    } else if (type == "stt") {
        String st = doc["state"] | "";
        String text = doc["text"] | "";
        if (text.length() == 0) text = doc["content"] | "";
        if (text.length() == 0) text = doc["transcript"] | "";
        if (text.length() > 0 && transcriptCallback_) {
            transcriptCallback_(text);
        }
        if (st == "stop") {
            setState(VoiceState::THINKING);
            audioCaptureRunning_ = false;
            listenStarted_ = false;
            if (opusTxQueue_) {
                xQueueReset(opusTxQueue_);
            }
        }
    } else if (type == "response") {
        String action = doc["action"] | "";
        if (action == "listen") {
            setState(VoiceState::LISTENING);
            audioCaptureRunning_ = true;
        }
    }
}

void XiaoZhiClient::handleMcpMessage(JsonObjectConst payload) {
    String method = payload["method"] | "";
    if (method.length() == 0 || method.startsWith("notifications")) {
        return;
    }

    if (payload["id"].isNull()) {
        Serial.printf("XiaoZhi: MCP request without id: %s\n", method.c_str());
        return;
    }

    int id = payload["id"].as<int>();
    Serial.printf("XiaoZhi: MCP method=%s id=%d\n", method.c_str(), id);

    if (method == "initialize") {
        JsonDocument result;
        const char* protocolVersion = "2024-11-05";
        if (payload["params"].is<JsonObjectConst>()) {
            JsonObjectConst params = payload["params"].as<JsonObjectConst>();
            parseMcpInitializeParams(params);
            const char* requestedVersion = params["protocolVersion"] | nullptr;
            if (requestedVersion && requestedVersion[0] != '\0') {
                protocolVersion = requestedVersion;
            }
        }

        result["protocolVersion"] = protocolVersion;
        result["capabilities"]["tools"].to<JsonObject>();
        result["serverInfo"]["name"] = "CoreS3 AI Pet";
        result["serverInfo"]["version"] = XIAOZHI_FIRMWARE_VERSION;

        String resultJson;
        serializeJson(result, resultJson);
        sendMcpResult(id, resultJson);
    } else if (method == "tools/list") {
        sendMcpToolsList(id);
        mcpReady_ = true;
        tryStartListeningStream();
    } else if (method == "tools/call") {
        String toolName = "";
        JsonObjectConst arguments;
        if (payload["params"].is<JsonObjectConst>()) {
            JsonObjectConst params = payload["params"].as<JsonObjectConst>();
            toolName = params["name"] | "";
            if (params["arguments"].is<JsonObjectConst>()) {
                arguments = params["arguments"].as<JsonObjectConst>();
            }
        }
        if (toolName.length() == 0) {
            toolName = "unknown";
        }
        if (!mcpToolCallback_) {
            sendMcpToolTextResult(id, "CoreS3 has no MCP tool handler", true);
            return;
        }
        mcpToolCallback_(toolName, arguments, id);
    } else {
        sendMcpError(id, "Method not implemented: " + method);
    }
}

void XiaoZhiClient::sendMcpResult(int id, const String& resultJson) {
    if (!wsConnected_ || sessionId_.length() == 0) return;
    String msg = "{\"session_id\":\"" + sessionId_ +
                 "\",\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":" +
                 String(id) + ",\"result\":" + resultJson + "}}";
    webSocket_.sendTXT(msg);
    Serial.printf("XiaoZhi: sent MCP result id=%d\n", id);
}

void XiaoZhiClient::sendMcpError(int id, const String& message) {
    if (!wsConnected_ || sessionId_.length() == 0) return;

    JsonDocument payload;
    payload["jsonrpc"] = "2.0";
    payload["id"] = id;
    payload["error"]["code"] = -32601;
    payload["error"]["message"] = message;

    String payloadJson;
    serializeJson(payload, payloadJson);
    String msg = "{\"session_id\":\"" + sessionId_ +
                 "\",\"type\":\"mcp\",\"payload\":" + payloadJson + "}";
    webSocket_.sendTXT(msg);
    Serial.printf("XiaoZhi: sent MCP error id=%d %s\n", id, message.c_str());
}

void XiaoZhiClient::sendMcpToolTextResult(int id, const String& text, bool isError) {
    if (!wsConnected_ || sessionId_.length() == 0) return;

    JsonDocument result;
    JsonArray content = result["content"].to<JsonArray>();
    JsonObject item = content.add<JsonObject>();
    item["type"] = "text";
    item["text"] = text;
    if (isError) {
        result["isError"] = true;
    } else {
        result["isError"] = false;
    }

    String resultJson;
    serializeJson(result, resultJson);
    sendMcpResult(id, resultJson);
}

void XiaoZhiClient::queueMcpToolTextResult(int id, const String& text, bool isError) {
    for (int i = 0; i < MAX_PENDING_MCP; ++i) {
        if (!pendingMcpResults_[i].valid) {
            pendingMcpResults_[i].id = id;
            pendingMcpResults_[i].text = text;
            pendingMcpResults_[i].isError = isError;
            pendingMcpResults_[i].valid = true;
            return;
        }
    }
    Serial.printf("XiaoZhi: MCP result queue full, dropping id=%d\n", id);
}

void XiaoZhiClient::sendMcpToolsList(int id) {
    JsonDocument result;
    JsonArray tools = result["tools"].to<JsonArray>();

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.camera.open";
        t["description"] = "打开 CoreS3 摄像头预览。当用户说打开摄像头、让我看看、你能看见吗时调用。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        s["properties"].to<JsonObject>();
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.camera.close";
        t["description"] = "关闭 CoreS3 摄像头预览，返回小智 AI 页面。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        s["properties"].to<JsonObject>();
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.camera.capture_photo";
        t["description"] = "用 CoreS3 摄像头拍一张照片并保存到 SD 卡。当用户说拍张照、保存一张照片、拍照时调用。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        s["properties"].to<JsonObject>();
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.vision.describe_scene";
        t["description"] = "用 CoreS3 摄像头拍照并识别画面。当用户问这是什么、你能看到什么、帮我识别时调用。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        JsonObject p = s["properties"].to<JsonObject>();
        JsonObject prompt = p["prompt"].to<JsonObject>();
        prompt["type"] = "string";
        prompt["description"] = "用户关于画面的可选问题。";
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.pomodoro.open";
        t["description"] = "打开 CoreS3 番茄钟计时器。当用户说打开番茄钟、开始计时、专注模式时调用。支持指定分钟数和是否自动开始。只支持5、15、25、50分钟四个预设。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        JsonObject p = s["properties"].to<JsonObject>();
        JsonObject minutes = p["minutes"].to<JsonObject>();
        minutes["type"] = "integer";
        minutes["description"] = "计时时长（分钟），只支持 5、15、25、50。";
        JsonObject autoStart = p["auto_start"].to<JsonObject>();
        autoStart["type"] = "boolean";
        autoStart["description"] = "是否自动开始计时。默认 false。";
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.music.control";
        t["description"] = "控制 CoreS3 音乐播放。当用户说播放音乐、暂停音乐、停止音乐、下一首时调用。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        JsonObject p = s["properties"].to<JsonObject>();
        JsonObject action = p["action"].to<JsonObject>();
        action["type"] = "string";
        action["description"] = "操作类型：play_pause（播放/暂停）、stop（停止）、next（下一首）";
        JsonArray actionEnum = action["enum"].to<JsonArray>();
        actionEnum.add("play_pause");
        actionEnum.add("stop");
        actionEnum.add("next");
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.pet.react";
        t["description"] = "让 CoreS3 桌宠做出表情反应。当用户说开心一下、害羞一下、卖个萌、装困、惊讶一下时调用。只改变表情和短状态文本，不影响 AI 状态。";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        JsonObject p = s["properties"].to<JsonObject>();
        JsonObject reaction = p["reaction"].to<JsonObject>();
        reaction["type"] = "string";
        reaction["description"] = "表情类型：happy、shy、curious、sleepy、surprised、sick";
        JsonArray reactionEnum = reaction["enum"].to<JsonArray>();
        reactionEnum.add("happy");
        reactionEnum.add("shy");
        reactionEnum.add("curious");
        reactionEnum.add("sleepy");
        reactionEnum.add("surprised");
        reactionEnum.add("sick");
    }

    {
        JsonObject t = tools.add<JsonObject>();
        t["name"] = "self.servo.control";
        t["description"] = "Control the CoreS3 two-axis servo head. Use for look left, look right, look up, look down, center, nod, shake head, or release servos.";
        JsonObject s = t["inputSchema"].to<JsonObject>();
        s["type"] = "object";
        JsonObject p = s["properties"].to<JsonObject>();
        JsonObject action = p["action"].to<JsonObject>();
        action["type"] = "string";
        action["description"] = "Servo action: center, left, right, up, down, nod, shake, release";
        JsonArray actionEnum = action["enum"].to<JsonArray>();
        actionEnum.add("center");
        actionEnum.add("left");
        actionEnum.add("right");
        actionEnum.add("up");
        actionEnum.add("down");
        actionEnum.add("nod");
        actionEnum.add("shake");
        actionEnum.add("release");
    }

    String resultJson;
    serializeJson(result, resultJson);
    sendMcpResult(id, resultJson);
}

void XiaoZhiClient::parseMcpInitializeParams(JsonObjectConst params) {
    if (!params["capabilities"].is<JsonObjectConst>()) {
        return;
    }

    JsonObjectConst capabilities = params["capabilities"].as<JsonObjectConst>();
    if (!capabilities["vision"].is<JsonObjectConst>()) {
        return;
    }

    JsonObjectConst vision = capabilities["vision"].as<JsonObjectConst>();
    String newUrl = vision["url"] | "";
    String newToken = vision["token"] | "";
    if (newUrl.length() > 0) {
        visionUrl_ = newUrl;
        visionToken_ = newToken;
        Serial.printf("XiaoZhi: vision endpoint ready url=%s token=%s\n",
                      visionUrl_.c_str(), visionToken_.length() > 0 ? "yes" : "no");
    }
}

void XiaoZhiClient::handleBinaryMessage(const uint8_t* data, size_t len) {
    if (!opusReady_ || !opusDecoder_ || len == 0) return;
    if (!speakerActive_ && !startSpeaker()) {
        return;
    }

    int16_t* playbackBuf = pcmPlaybackBufs_[playbackBufferIndex_];
    if (!playbackBuf) return;
    playbackBufferIndex_ = (playbackBufferIndex_ + 1) % PLAYBACK_BUFFER_COUNT;

    int decodedSamples = opus_decode(opusDecoder_, data, len, playbackBuf, OPUS_MAX_DECODE_SAMPLES, 0);
    if (decodedSamples <= 0) {
        Serial.printf("XiaoZhi: opus decode failed: %d len=%u\n", decodedSamples, static_cast<unsigned>(len));
        return;
    }

    M5.Speaker.playRaw(playbackBuf, decodedSamples, serverSampleRate_, false, 1, 0, false);
}

void XiaoZhiClient::parseServerHello(const char* data, size_t len) {
    JsonDocument doc;
    deserializeJson(doc, data, len);

    String transport = doc["transport"] | "";
    if (transport != "websocket") {
        Serial.printf("XiaoZhi: unsupported transport: %s\n", transport.c_str());
        return;
    }

    sessionId_ = doc["session_id"] | "";
    Serial.printf("XiaoZhi: session_id=%s\n", sessionId_.c_str());

    if (doc["audio_params"].is<JsonObject>()) {
        JsonObject ap = doc["audio_params"];
        if (!ap["sample_rate"].isNull()) serverSampleRate_ = ap["sample_rate"].as<int>();
        if (!ap["frame_duration"].isNull()) serverFrameDuration_ = ap["frame_duration"].as<int>();
    }

    Serial.printf("XiaoZhi: server audio %dHz %dms\n", serverSampleRate_, serverFrameDuration_);

    serverHelloReceived_ = true;

    if (!audioInitialized_ && !initAudio()) {
        Serial.println("XiaoZhi: audio init failed after server hello");
        return;
    }
    if (!micActive_ && !startMic()) {
        Serial.println("XiaoZhi: mic not available, cannot listen");
        return;
    }

    setState(VoiceState::IDLE);
    audioCaptureRunning_ = false;
}

bool XiaoZhiClient::initAudio() {
    if (audioInitialized_) return true;
    if (!initOpusCodec()) {
        setState(VoiceState::ERROR);
        return false;
    }

    stopSpeaker();
    if (!startMic()) {
        setState(VoiceState::ERROR);
        return false;
    }

    audioInitialized_ = true;
    Serial.println("XiaoZhi: audio I/O initialized (half-duplex mic)");

    xTaskCreatePinnedToCore(
        [](void* arg) { static_cast<XiaoZhiClient*>(arg)->audioCaptureTask(); },
        "AudioCap", 32768, this, 4, &audioCaptureTaskHandle_, 0);

    return true;
}

void XiaoZhiClient::deinitAudio() {
    if (!audioInitialized_) return;
    stopMic();
    stopSpeaker();
    audioInitialized_ = false;
}

bool XiaoZhiClient::startMic() {
    if (micActive_ && M5.Mic.isRunning()) return true;

    stopSpeaker();
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    mic_cfg.stereo = false;
    M5.Mic.config(mic_cfg);
    micActive_ = M5.Mic.begin();
    if (!micActive_) {
        Serial.println("XiaoZhi: mic begin failed");
        lastError_ = "Mic begin failed";
    }
    return micActive_;
}

void XiaoZhiClient::stopMic() {
    if (!micActive_ && !M5.Mic.isRunning()) return;
    audioCaptureRunning_ = false;
    unsigned long start = millis();
    while (M5.Mic.isRecording() && millis() - start < 500) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    M5.Mic.end();
    micActive_ = false;
}

bool XiaoZhiClient::startSpeaker() {
    if (speakerActive_ && M5.Speaker.isRunning()) return true;

    stopMic();
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = 48000;
    spk_cfg.stereo = false;
    spk_cfg.dma_buf_count = 8;
    spk_cfg.dma_buf_len = 256;
    spk_cfg.task_priority = 5;
    M5.Speaker.config(spk_cfg);
    speakerActive_ = M5.Speaker.begin();
    if (speakerActive_) {
        M5.Speaker.setVolume(160);
    }
    if (!speakerActive_) {
        Serial.println("XiaoZhi: speaker begin failed");
        lastError_ = "Speaker begin failed";
    }
    return speakerActive_;
}

void XiaoZhiClient::stopSpeaker() {
    if (!speakerActive_ && !M5.Speaker.isRunning()) return;
    M5.Speaker.end();
    speakerActive_ = false;
}

bool XiaoZhiClient::initOpusCodec() {
    deinitOpusCodec();
    bool playbackBuffersOk = true;
    for (int i = 0; i < PLAYBACK_BUFFER_COUNT; ++i) {
        playbackBuffersOk = playbackBuffersOk && pcmPlaybackBufs_[i] != nullptr;
    }
    if (!opusEncodeBuf_ || !playbackBuffersOk) {
        lastError_ = "No audio buffers";
        return false;
    }

    int err = OPUS_OK;
    opusEncoder_ = opus_encoder_create(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !opusEncoder_) {
        Serial.printf("XiaoZhi: opus encoder create failed: %d\n", err);
        lastError_ = "Opus encoder failed";
        return false;
    }
    opus_encoder_ctl(opusEncoder_, OPUS_SET_BITRATE(OPUS_BITRATE));
    opus_encoder_ctl(opusEncoder_, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY));
    opus_encoder_ctl(opusEncoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    err = OPUS_OK;
    opusDecoder_ = opus_decoder_create(serverSampleRate_, AUDIO_CHANNELS, &err);
    if (err != OPUS_OK || !opusDecoder_) {
        Serial.printf("XiaoZhi: opus decoder create failed: %d\n", err);
        lastError_ = "Opus decoder failed";
        deinitOpusCodec();
        return false;
    }

    opusReady_ = true;
    playbackBufferIndex_ = 0;
    Serial.printf("XiaoZhi: opus ready enc=%dHz dec=%dHz frame=%dms\n",
                  AUDIO_SAMPLE_RATE, serverSampleRate_, AUDIO_FRAME_DURATION_MS);
    return true;
}

void XiaoZhiClient::deinitOpusCodec() {
    if (opusEncoder_) {
        opus_encoder_destroy(opusEncoder_);
        opusEncoder_ = nullptr;
    }
    if (opusDecoder_) {
        opus_decoder_destroy(opusDecoder_);
        opusDecoder_ = nullptr;
    }
    opusReady_ = false;
}

void XiaoZhiClient::audioCaptureTask() {
    Serial.println("XiaoZhi: audio capture task started");
    while (true) {
        if (!audioCaptureRunning_ || !wsConnected_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (micActive_ && M5.Mic.isRunning()) {
            size_t readLen = AUDIO_FRAME_SAMPLES;
            if (M5.Mic.record(pcmCaptureBuf_, readLen, AUDIO_SAMPLE_RATE, false)) {
                sendAudioFrame(pcmCaptureBuf_, readLen);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

bool XiaoZhiClient::sendAudioFrame(const int16_t* pcmData, size_t sampleCount) {
    if (!wsConnected_ || !opusReady_ || !opusEncoder_ || !opusTxQueue_) return false;
    int encoded = opus_encode(opusEncoder_, pcmData, sampleCount, opusEncodeBuf_, OPUS_MAX_PACKET_BYTES);
    if (encoded <= 0) {
        Serial.printf("XiaoZhi: opus encode failed: %d\n", encoded);
        return false;
    }

    OpusTxPacket packet;
    packet.length = static_cast<uint16_t>(encoded);
    memcpy(packet.data, opusEncodeBuf_, encoded);
    if (xQueueSend(opusTxQueue_, &packet, 0) != pdTRUE) {
        return false;
    }
    return true;
}

void XiaoZhiClient::sendListenStart() {
    if (!wsConnected_ || sessionId_.length() == 0) return;
    JsonDocument doc;
    doc["session_id"] = sessionId_;
    doc["type"] = "listen";
    doc["state"] = "start";
    doc["mode"] = "auto";
    String msg;
    serializeJson(doc, msg);
    webSocket_.sendTXT(msg);
    Serial.println("XiaoZhi: sent listen start");
}

void XiaoZhiClient::sendListenStop() {
    if (!wsConnected_ || sessionId_.length() == 0) return;
    JsonDocument doc;
    doc["session_id"] = sessionId_;
    doc["type"] = "listen";
    doc["state"] = "stop";
    String msg;
    serializeJson(doc, msg);
    webSocket_.sendTXT(msg);
    Serial.println("XiaoZhi: sent listen stop");
}

void XiaoZhiClient::sendAbort() {
    if (!wsConnected_ || sessionId_.length() == 0) return;
    JsonDocument doc;
    doc["session_id"] = sessionId_;
    doc["type"] = "abort";
    String msg;
    serializeJson(doc, msg);
    webSocket_.sendTXT(msg);
}

bool XiaoZhiClient::startListening() {
    if (!initialized_) return false;
    lastError_ = "";
    pendingStartListening_ = true;
    if (!audioChannelOpen_ || !mcpReady_ || !wsConnected_) {
        setState(VoiceState::IDLE);
    }
    return true;
}

bool XiaoZhiClient::stopListening() {
    pendingStopListening_ = true;
    setState(VoiceState::IDLE);
    return true;
}

bool XiaoZhiClient::tryStartListeningStream() {
    if (!listenRequested_ || listenStarted_ || !audioChannelOpen_ || !mcpReady_ || !wsConnected_) {
        return false;
    }
    if (!micActive_ && !startMic()) {
        lastError_ = "Mic begin failed";
        setState(VoiceState::ERROR);
        return false;
    }

    sendListenStart();
    listenStarted_ = true;
    audioCaptureRunning_ = micActive_;
    setState(VoiceState::LISTENING);
    return true;
}

void XiaoZhiClient::process() {
    if (pendingStopListening_) {
        pendingStopListening_ = false;
        listenRequested_ = false;
        listenStarted_ = false;
        if (audioChannelOpen_) {
            sendListenStop();
            audioCaptureRunning_ = false;
        }
        if (opusTxQueue_) {
            xQueueReset(opusTxQueue_);
        }
        setState(VoiceState::IDLE);
    }

    if (pendingStartListening_) {
        pendingStartListening_ = false;
        listenRequested_ = true;
        if (activated_ && !audioChannelOpen_) {
            if (!openAudioChannel()) {
                listenRequested_ = false;
                listenStarted_ = false;
                audioCaptureRunning_ = false;
                if (lastError_.length() == 0) {
                    lastError_ = "Audio open failed";
                }
                setState(VoiceState::ERROR);
            }
        }
        if (listenRequested_) {
            tryStartListeningStream();
        }
    }

    if (wsConnected_) {
        webSocket_.loop();
        flushOutgoingAudio();
    }

    for (int i = 0; i < MAX_PENDING_MCP; ++i) {
        if (pendingMcpResults_[i].valid) {
            sendMcpToolTextResult(pendingMcpResults_[i].id,
                                  pendingMcpResults_[i].text,
                                  pendingMcpResults_[i].isError);
            pendingMcpResults_[i].valid = false;
        }
    }
}

void XiaoZhiClient::setStateCallback(std::function<void(VoiceState)> cb) {
    callback_ = std::move(cb);
}

void XiaoZhiClient::setMcpToolCallback(McpToolCallback cb) {
    mcpToolCallback_ = std::move(cb);
}

void XiaoZhiClient::setTranscriptCallback(TranscriptCallback cb) {
    transcriptCallback_ = std::move(cb);
}

VoiceState XiaoZhiClient::getState() const { return state_; }

void XiaoZhiClient::flushOutgoingAudio() {
    if (!wsConnected_ || !opusTxQueue_) return;

    OpusTxPacket packet;
    while (xQueueReceive(opusTxQueue_, &packet, 0) == pdTRUE) {
        if (packet.length > 0) {
            webSocket_.sendBIN(packet.data, packet.length);
        }
    }
}

void XiaoZhiClient::setState(VoiceState state) {
    if (state_ != state) {
        state_ = state;
        if (callback_) callback_(state);
    }
}
