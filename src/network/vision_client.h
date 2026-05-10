#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

class VisionClient {
public:
    bool describeImage(const char* url, const char* token,
                       const uint8_t* jpegData, size_t jpegLength,
                       String& description, String& status);

private:
    bool parseResponse(const String& body, String& description);
};
