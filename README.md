# CoreS3 AI Desktop Pet

Firmware for a M5Stack CoreS3 based desktop pet demo. It combines an animated expression face, touch gestures, Wi-Fi status, camera preview and photo capture, IMU driven Pomodoro interaction, SD card music playback, XiaoZhi voice interaction, and an optional AI vision flow exposed through XiaoZhi MCP tools.

Chinese documentation is available in [README_CN.md](README_CN.md).

## Current Status

- Face UI: 11 expressions, blinking, gaze smoothing, temporary gaze overrides, shake-triggered `SICK`, and safe two-axis servo reactions for expression/touch interactions.
- Menu UI: six app icons for Wi-Fi, Camera, Timer, Music, System, and Servo Test pages.
- Camera Debug: 320 x 240 preview, FPS/status overlay, Back button, JPEG capture to SD, and a real-detector face-centering hook before capture.
- Pomodoro: four presets selected by IMU orientation, screen rotation, timer controls, completion melody, and temporary completion emotion on the face page.
- Music: scans `/music` on the SD card and plays up to 16 PCM WAV files.
- Servo Test: PCA9685 control through CoreS3 PortA, with IMU X/Y tilt mapped to horizontal and vertical servos in a 10-170 degree test range.
- Servo interaction: Face taps, expression changes, XiaoZhi pet reactions, and XiaoZhi servo commands can move/center/release the two-axis head with shared rate limiting.
- XiaoZhi AI: OTA activation/config request, TLS WebSocket, Opus microphone upload, Opus TTS playback, and MCP handshake/tool handling.
- AI Vision: camera preview and image-description requests through the vision endpoint provided by the XiaoZhi service.
- Face detection: intentionally disabled in this Arduino/PlatformIO build. Photo face tracking has the control framework, but no fake face detector or skin-color detector is active.

## Hardware

- M5Stack CoreS3, ESP32-S3
- USB cable for build/upload/serial monitor
- MicroSD/TF card formatted as FAT/FAT32 for photos and WAV music
- Optional base hardware for battery, PCA9685, and two-axis servos

Servo Test wiring used by this firmware:

```text
CoreS3 PortA -> PCA9685 I2C
PCA9685 address: 0x40
Horizontal servo: PCA9685 channel 0
Vertical servo:   PCA9685 channel 1
Servo power: external servo power module, with common ground to CoreS3/PCA9685
```

CoreS3 SD SPI pins used by this firmware:

```text
SCK  36
MISO 35
MOSI 37
CS   4
```

## Software Requirements

- PlatformIO Core
- Git
- Windows, Linux, or macOS host

The project is configured for:

- PlatformIO board: `m5stack-cores3`
- Framework: Arduino
- MCU: ESP32-S3
- C++ standard: C++17
- PSRAM: QSPI PSRAM enabled by `board_build.arduino.memory_type = qio_qspi`

## Repository Layout

```text
src/
├── main.cpp
├── app/        # State machine, gestures, event bus
├── ai/         # XiaoZhi activation, WebSocket, Opus audio, MCP
├── audio/      # SD WAV music player
├── config/     # Firmware constants and Wi-Fi secret template
├── network/    # Wi-Fi manager and AI vision HTTP client
├── power/      # Battery/sleep placeholder logic
├── storage/    # SD card probing, photo paths, file writes
├── ui/         # Face, menu, camera, Pomodoro, info, music UI
└── vision/     # Camera manager, disabled detector, tracker, IMU orientation
```

Additional servo modules:

- `src/servo/servo_controller.*`: PCA9685 driver and safe angle mapping.
- `src/servo/servo_motion_controller.*`: shared motion targets, rate limiting, expression poses, nod/shake sequences, and PWM release control.
- `src/vision/face_tracking_controller.*`: converts real `FaceResult` bounding boxes into small pan/tilt corrections for photo centering.

## Wi-Fi Setup

Real credentials are not committed.

```powershell
Copy-Item src\config\wifi_secrets.example.h src\config\wifi_secrets.h
```

Edit `src/config/wifi_secrets.h`:

```cpp
#pragma once
constexpr const char* WIFI_SSID = "your_ssid";
constexpr const char* WIFI_PASSWORD = "your_password";
```

`src/config/wifi_secrets.h` is ignored by Git.

## Build

```powershell
pio run
```

If `pio` is not on `PATH` on Windows, use the full PlatformIO executable path, for example:

```powershell
C:\Users\<you>\.platformio\penv\Scripts\platformio.exe run
```

## Upload

```powershell
pio run -t upload
```

If automatic port detection fails:

```powershell
pio run -t upload --upload-port COM5
```

If upload does not start, long-press RESET for about 3 seconds to enter download mode. After upload finishes, press RESET once to run the firmware.

## Serial Monitor

```powershell
pio device monitor -b 115200
```

With an explicit port:

```powershell
pio device monitor -p COM5 -b 115200
```

## Controls

- Face page: right swipe opens Menu; left swipe or double tap enters XiaoZhi AI; tap top/bottom/left/right changes expression, gaze, and servo head pose; shaking the device shows `SICK`; long press enters Sleep.
- AI page: single tap toggles listening; right swipe or long press returns to Face.
- Menu page: tap an icon to open Wi-Fi, Camera, Timer, Music, System, or Servo; Back returns to Face.
- Camera Debug: SHOT saves `/photos/IMG_####.jpg`; if a real face detector backend is available, the firmware briefly tries to center the face with the servos before capture; Back or left swipe returns to Menu.
- AI Vision: Back or left swipe closes preview and returns to XiaoZhi AI.
- Pomodoro: rotate the device to select a preset; Start/Pause and Reset control the timer; Back returns to Menu.
- Music: Play/Pause, Stop, and Next operate on WAV files found in `/music`.
- Servo Test: entering the page centers both servos; tilt CoreS3 left/right for the horizontal servo and forward/back for the vertical servo; tap to re-center; long press releases PWM; Back or left swipe returns to Menu.

## SD Card Content

The firmware creates and uses:

```text
/photos/IMG_####.jpg
/music/*.wav
```

Music playback currently supports PCM WAV files with 8-bit or 16-bit samples and one or two channels. The first 16 WAV files are scanned and sorted by filename.

## XiaoZhi AI

The implementation is in `src/ai/xiaozhi_client.h` and `src/ai/xiaozhi_client.cpp`.

The flow is:

1. Enter the AI page from the Face page.
2. Wait for Wi-Fi, then send an OTA request to get activation/config data.
3. Open a TLS WebSocket using the returned URL/token.
4. Send a `hello` message with Opus audio parameters and MCP enabled.
5. Reply to MCP bootstrap requests.
6. Start listening after MCP is ready.
7. Capture 16 kHz mono PCM, encode it as Opus, and send WebSocket binary frames.
8. Decode returned Opus TTS and play it through the CoreS3 speaker.

CoreS3 microphone and speaker are treated as half-duplex:

- Listening stops speaker playback and starts microphone capture.
- Speaking stops microphone capture and starts speaker playback.
- After TTS stops, listening restarts when the AI page still requests it.

Exposed MCP tools:

```text
self.camera.open
self.camera.close
self.camera.capture_photo
self.vision.describe_scene
self.pomodoro.open
self.music.control
self.pet.react
self.servo.control
```

AI Vision depends on the vision endpoint and token delivered by the XiaoZhi service. It is not a local embedded face detector.

`self.servo.control` accepts `action` values `center`, `left`, `right`, `up`, `down`, `nod`, `shake`, and `release`. Transcript fallback also recognizes common Chinese phrases such as looking left/right/up/down, returning to center, nodding, shaking the head, and releasing servos.

## Known Limitations

- Real face detection is disabled. ESP-DL/ESP-WHO integration still needs a verified ESP-IDF or Arduino-as-IDF-component path.
- Photo face tracking only becomes closed-loop when `FaceDetector::detect()` returns real face boxes; the current Arduino build reports face tracking unavailable and still saves photos normally.
- AI Vision requires a XiaoZhi-provided vision endpoint.
- CoreS3 built-in microphone and speaker are half-duplex; full-duplex interruption and echo cancellation are not implemented.
- Battery voltage reading is still a placeholder until the final PMU API/hardware path is validated.
- SD card behavior depends on card format, contact, and power stability. The firmware retries SD init at 25, 10, 4, and 1 MHz.
- Servo expression/XiaoZhi control is open-loop pose control with rate limiting. It is not PID gimbal control or autonomous base motion.

## Troubleshooting

- Upload fails: long-press RESET for about 3 seconds, upload again, then press RESET once.
- Wi-Fi shows `Not configured`: create `src/config/wifi_secrets.h` from the example.
- XiaoZhi OTA returns `400`: check `XIAOZHI_BOARD_TYPE`, `XIAOZHI_FIRMWARE_VERSION`, and OTA identity logs.
- XiaoZhi disconnects after MCP: confirm `initialize`, `tools/list`, and `sent listen start` appear in order.
- No AI reply: confirm Wi-Fi is connected and serial logs show `WS connected`, `session_id`, `sent listen start`, `stt`, and `tts`.
- AI Vision says endpoint missing: the XiaoZhi session did not provide vision capability data.
- SD not found: format the card as FAT/FAT32 and check logs for `SD.begin failed`, `CARD_NONE`, `Root open failed`, or `Probe write failed`.
- Servo page says `PCA9685 not found @0x40`: check PortA wiring, PCA9685 address jumpers, external servo power, and common ground.
- PCA9685 LED briefly turns off or servos buzz under fast two-axis motion: use a stronger external servo supply, shorten/thicken power wiring, verify common ground, and avoid mechanical end stops.
