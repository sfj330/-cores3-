# CoreS3 AI Desktop Pet

Firmware for a M5Stack CoreS3 based desktop pet demo. It combines an animated face, touch and IMU interaction, Wi-Fi status, camera preview and photo capture, Pomodoro, SD music playback, XiaoZhi voice interaction, and AI Vision requests exposed through XiaoZhi MCP tools.

Chinese documentation is available in [README_CN.md](README_CN.md).

## Current Status

- Face UI: 11 expressions, blinking, gaze smoothing, temporary gaze overrides, expression particles, tap sound feedback, time-aware idle behavior after NTP sync, and safe two-axis servo reactions.
- Face interaction: light shaking triggers `SURPRISED`, repeated shaking can trigger `SICK`, clockwise and counter-clockwise twists trigger curious left/right reactions, and sleep can be exited by tap or shake.
- Settings page: a quick settings page is opened from the Face page by up swipe and provides runtime brightness and volume presets.
- Camera Debug: 320 x 240 preview, FPS and status overlay, Back button, JPEG capture to SD, and heuristic face centering before capture.
- System page: a shared runtime status snapshot is rendered as fixed Wi-Fi, SD, Audio, Control, and Memory rows, with XiaoZhi and Vision summary text in the subtitle.
- Pomodoro: four presets selected by IMU orientation, screen rotation, timer controls, and completion feedback.
- Music: scans `/music` on the SD card and plays up to 16 MP3 or PCM WAV files. Volume uses shared `quiet`, `normal`, and `loud` runtime presets. A clockwise twist can skip to the next track.
- Servo Test: PCA9685 control through CoreS3 PortA, with IMU X/Y tilt mapped to horizontal and vertical servos in a 10 to 170 degree test range. The Servo Test neutral pose is pan 90 degrees and tilt 90 degrees.
- Servo interaction: Face taps, idle motions, XiaoZhi pet reactions, XiaoZhi servo commands, and the dance demo all share the same motion controller and rate limits. Face and XiaoZhi expression poses keep the mechanical neutral at pan 90 degrees and tilt 140 degrees.
- Affinity: the Bond page shows score, level, mood, and recent interaction. The affinity score now persists in NVS across reboot.
- XiaoZhi AI: OTA activation and config, TLS WebSocket, Opus microphone upload, Opus TTS playback, MCP handshake and tools, device status query, and device control for page switching, brightness, volume, and sleep or wake.
- AI Vision: camera preview and image description requests through the vision endpoint provided by the XiaoZhi service.
- Face detection: the current Arduino build uses a lightweight skin-color heuristic for rough face-area estimation and servo centering. It is not a real ML face detector or face recognition system.
- Power: battery voltage now reads through the CoreS3 PMU path exposed by `M5.Power.getBatteryVoltage()`.

## Hardware

- M5Stack CoreS3, ESP32-S3
- USB cable for build, upload, and serial monitor
- MicroSD or TF card formatted as FAT or FAT32 for photos and MP3 or WAV music
- Optional base hardware for battery, PCA9685, and two-axis servos

Servo wiring used by this firmware:

```text
CoreS3 PortA -> PCA9685 I2C
PCA9685 address: 0x40
Horizontal servo: PCA9685 channel 0
Vertical servo:   PCA9685 channel 1
Servo power: external servo power module, with common ground to CoreS3/PCA9685
Servo Test center: horizontal 90 degrees, vertical 90 degrees
Face/XiaoZhi center: horizontal 90 degrees, vertical 140 degrees
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
├── app/        # State machine, gestures, affinity, event bus, shared system status snapshot
├── ai/         # XiaoZhi activation, WebSocket, Opus audio, MCP
├── audio/      # SD MP3/WAV music player
├── config/     # Firmware constants and Wi-Fi secret template
├── network/    # Wi-Fi manager and AI Vision HTTP client
├── power/      # Sleep state and battery voltage reporting
├── servo/      # PCA9685 driver and shared servo motion layer
├── storage/    # SD card probing, photo paths, file writes
├── ui/         # Face, menu, camera, Pomodoro, system, music, affinity, settings UI
└── vision/     # Camera manager, heuristic face detector, tracker, IMU orientation
```

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

- Face page: right swipe opens Menu, left swipe or double tap enters XiaoZhi AI, down swipe opens Bond, up swipe opens Settings, tap top/bottom/left/right changes expression and head pose, shake and twist gestures trigger IMU reactions, and long press enters Sleep.
- AI page: single tap toggles listening, right swipe or long press returns to Face.
- Bond page: Back, up swipe, or left swipe returns to Face.
- Menu page: tap an icon to open Wi-Fi, Camera, Timer, Music, System, or Servo Test. Back returns to Face.
- Settings page: tap `Low`, `Mid`, or `High` under Brightness and Volume to apply runtime presets. Back, left swipe, or down swipe returns to Face.
- Camera Debug: `SHOT` saves `/photos/IMG_####.jpg`. If the heuristic detector finds a plausible face region, the firmware briefly tries to center it with the servos before capture. Back or left swipe returns to Menu.
- AI Vision: Back or left swipe closes preview and returns to XiaoZhi AI.
- Pomodoro: rotate the device to select a preset. Start, Pause, and Reset control the timer. Back returns to Menu.
- Music: Play/Pause, Stop, and Next operate on MP3/WAV files found in `/music`. A clockwise twist can also skip to the next track.
- System: shows Wi-Fi, SD, Audio, Control, and Memory rows, while the subtitle summarizes XiaoZhi and Vision readiness. Back returns to Menu.
- Servo Test: entering the page centers both servos at 90/90 using the shared safe transition speed. Tilt CoreS3 left/right for the horizontal servo and forward/back for the vertical servo. Tap to re-center at 90/90, long press releases PWM, and Back or left swipe returns to Menu and restores the Face/XiaoZhi 90/140 center.
- Sleep: tap or shake wakes the device back to the Face page.

## SD Card Content

The firmware creates and uses:

```text
/photos/IMG_####.jpg
/music/*.mp3
/music/*.wav
```

Music playback supports MP3 files plus PCM WAV files with 8-bit or 16-bit samples and one or two channels. MP3 stereo or mono output is mixed to mono before it is queued to the built-in speaker. The first 16 audio files are scanned and sorted by filename.

## XiaoZhi AI

The implementation is in `src/ai/xiaozhi_client.h` and `src/ai/xiaozhi_client.cpp`.

The flow is:

1. Enter the AI page from the Face page.
2. Wait for Wi-Fi, then send an OTA request to get activation and config data.
3. Open a TLS WebSocket using the returned URL and token.
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
self.device.status
self.device.control
```

AI Vision depends on the vision endpoint and token delivered by the XiaoZhi service. It is not a local embedded vision pipeline.

`self.servo.control` accepts `action` values `center`, `left`, `right`, `up`, `down`, `nod`, `shake`, `dance`, and `release`. Transcript fallback also recognizes common Chinese phrases for these motions. The dance action starts servo motion first, then tries to use `/music` if available. Music or SD failure does not cancel the servo dance.

`self.device.status` accepts `detail=brief|full`. `brief` returns two short Chinese sentences covering Wi-Fi and SD first, then Music, AI, and Vision. `full` appends heap and PSRAM information.

`self.device.control` supports optional `page`, `brightness`, `volume`, and `sleep` fields. Supported pages are `face`, `menu`, `wifi`, `system`, `camera`, `music`, `pomodoro`, and `ai`. Brightness presets are `dim`, `normal`, and `bright`. Volume presets are `quiet`, `normal`, and `loud`. Sleep supports `wake` and `sleep`. Wake restores the last non-sleep brightness preference instead of hard-coding full brightness.

Transcript fallback also recognizes common Chinese commands for status query, opening the System, Music, Camera, and Pomodoro pages, returning to the home page, adjusting brightness, adjusting volume, and sleep or wake requests.

## Known Limitations

- The current face detector is only a heuristic skin-color blob detector. It can assist rough centering, but it is not a real face detector and should not be described as face recognition.
- Photo face tracking is still open-loop servo correction around heuristic boxes. It is useful for demo centering, not robust computer vision.
- AI Vision requires a XiaoZhi-provided vision endpoint.
- CoreS3 built-in microphone and speaker are half-duplex. Full-duplex interruption and echo cancellation are not implemented.
- Brightness and volume presets are runtime-only and reset to `bright` plus `normal` after reboot.
- SD card behavior depends on card format, contact, and power stability. The firmware retries SD init at 25, 10, 4, and 1 MHz.
- Servo expression and XiaoZhi control are open-loop pose control with rate limiting. They are not PID gimbal control or autonomous base motion.
- Bond details such as recent interaction text are still runtime-only even though the main affinity score now persists.

## Troubleshooting

- Upload fails: long-press RESET for about 3 seconds, upload again, then press RESET once.
- Wi-Fi shows `Not configured`: create `src/config/wifi_secrets.h` from the example.
- XiaoZhi OTA returns `400`: check `XIAOZHI_BOARD_TYPE`, `XIAOZHI_FIRMWARE_VERSION`, and OTA identity logs.
- XiaoZhi disconnects after MCP: confirm `initialize`, `tools/list`, and `sent listen start` appear in order.
- No AI reply: confirm Wi-Fi is connected and serial logs show `WS connected`, `session_id`, `sent listen start`, `stt`, and `tts`.
- AI Vision says endpoint missing: the XiaoZhi session did not provide vision capability data.
- SD not found: format the card as FAT/FAT32 and check logs for `SD.begin failed`, `CARD_NONE`, `Root open failed`, or `Probe write failed`.
- Servo page says `PCA9685 not found @0x40`: check PortA wiring, PCA9685 address jumpers, external servo power, and common ground.
- PCA9685 LED briefly turns off or servos buzz under fast two-axis motion: use a stronger external servo supply, shorten or thicken power wiring, verify common ground, and avoid mechanical end stops.
- MP3 is silent or the board resets a few seconds after playback starts: check serial logs for the track path, MP3 sample rate and channel report, and speaker queue failures. If the next boot reports `BROWNOUT` or `POWERON`, treat it as a power drop. If it reports `WDT` or `PANIC`, continue with software crash diagnosis.
