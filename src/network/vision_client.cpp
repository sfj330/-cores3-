#include "network/vision_client.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

bool VisionClient::describeImage(const char* url, const char* token,
                                 const uint8_t* jpegData, size_t jpegLength,
                                 String& description, String& status) {
    description = "";
    status = "";

    if (WiFi.status() != WL_CONNECTED) {
        status = "Wi-Fi not connected";
        return false;
    }
    if (url == nullptr || url[0] == '\0') {
        status = "Vision endpoint missing";
        return false;
    }
    if (jpegData == nullptr || jpegLength == 0) {
        status = "JPEG empty";
        return false;
    }

    HTTPClient http;
    bool began = false;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String endpoint(url);

    if (endpoint.startsWith("https://")) {
        secureClient.setInsecure();
        began = http.begin(secureClient, endpoint);
    } else {
        began = http.begin(plainClient, endpoint);
    }

    if (!began) {
        status = "Vision HTTP begin failed";
        return false;
    }

    http.setTimeout(15000);
    http.addHeader("Content-Type", "image/jpeg");
    if (token != nullptr && token[0] != '\0') {
        String bearer = String("Bearer ") + token;
        http.addHeader("Authorization", bearer);
        http.addHeader("X-Token", token);
    }

    int code = http.sendRequest("POST", const_cast<uint8_t*>(jpegData), jpegLength);
    String body = http.getString();
    http.end();

    if (code < 200 || code >= 300) {
        status = "Vision HTTP " + String(code);
        if (body.length() > 0) {
            Serial.printf("VisionClient: HTTP %d body=%s\n", code, body.c_str());
        }
        return false;
    }

    if (!parseResponse(body, description)) {
        description = body;
    }
    description.trim();
    if (description.length() == 0) {
        status = "Vision returned empty result";
        return false;
    }
    if (description.length() > 120) {
        description = description.substring(0, 120);
    }

    status = "Vision OK";
    Serial.printf("VisionClient: result=%s\n", description.c_str());
    return true;
}

bool VisionClient::parseResponse(const String& body, String& description) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        return false;
    }

    if (!doc["description"].isNull()) {
        description = doc["description"].as<String>();
        return true;
    }
    if (!doc["text"].isNull()) {
        description = doc["text"].as<String>();
        return true;
    }
    if (!doc["result"].isNull()) {
        description = doc["result"].as<String>();
        return true;
    }
    if (!doc["message"].isNull()) {
        description = doc["message"].as<String>();
        return true;
    }
    if (doc["data"].is<JsonObjectConst>()) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        if (!data["description"].isNull()) {
            description = data["description"].as<String>();
            return true;
        }
        if (!data["text"].isNull()) {
            description = data["text"].as<String>();
            return true;
        }
        if (!data["result"].isNull()) {
            description = data["result"].as<String>();
            return true;
        }
    }
    return false;
}
