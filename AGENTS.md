# AGENTS.md

This file provides guidance to Codex/Coding agents when working with this repository.

## Project Overview

**CoreS3 AI Desktop Pet Interaction System** is active embedded firmware for M5Stack CoreS3 / ESP32-S3. It is used as a National College Embedded Competition desktop pet demo. The CoreS3 acts as the pet head/brain, with optional base hardware for battery, PCA9685 servo control, and two-axis movement.

The current design separates the face UI from camera/AI backends: the screen renders the pet expression, while camera preview, AI voice, AI vision, storage, power, and timer features run as state-driven modules.

## Project Status

Active demo firmware. The project is no longer planning-only.

Implemented:

- Animated face UI with 11 emotions: `NORMAL`, `HAPPY`, `CURIOUS`, `LISTENING`, `THINKING`, `SPEAKING`, `SURPRISED`, `SLEEPY`, `TRACKING`, `SHY`, `SICK`.
- Touch gestures: tap, double tap, left/right/up/down swipe, long press.
- Five-icon menu with Wi-Fi, Camera, Timer, Music, and System pages.
- Settings page opened by up swipe from the Face page.
- Camera preview and JPEG capture to SD.
- Camera foreground startup stabilization: startup is deferred out of touch/state callbacks, user-opened camera clears failure cooldown, CoreS3 internal I2C is released before init, touch is suspended during camera init, and a failed foreground open gets one clean deinit/retry.
- AI Vision preview and JPEG description request through a XiaoZhi-provided vision endpoint.
- Lightweight skin-color heuristic face detector for rough face-area estimation and photo/servo centering. It is not ML face detection or recognition.
- Face-page vision runtime that runs local heuristic detection in short bursts instead of keeping camera capture permanently active.
- IMU orientation based Pomodoro timer with four presets and screen rotation.
- SD MP3/WAV music player. MP3 output is mixed to mono for the CoreS3 speaker at a conservative default volume.
- Shared safe servo motion layer for Face touch/expression reactions, XiaoZhi pet reactions, XiaoZhi `self.servo.control` commands, and the dance demo. Face and XiaoZhi expression poses keep the mechanical center at pan 90 degrees and tilt 140 degrees.
- PCA9685 servo driver self-disables after three missing-device scans so an unplugged base does not keep polling PortA forever.
- Bond/affinity page opened by down swipe from Face. The main affinity score persists in NVS, while recent detail text is runtime-only.
- XiaoZhi OTA activation/config, TLS WebSocket, Opus mic upload, Opus TTS playback, MCP handshake, and MCP tools.
- Wi-Fi auto reconnect, SD retry/fallback, PMU battery voltage reading, brownout safe mode, and basic power/sleep handling.

Removed or intentionally not implemented:

- Servo Test page and `servo_test_ui.*` have been removed. Do not restore the menu entry unless the user explicitly asks for it.
- Real ML local face detection and face recognition are not implemented. Do not describe the skin-color heuristic as face recognition.
- PID gimbal tracking or autonomous base behavior. Current servo support is safe open-loop pose control plus heuristic centering hooks.
- Full-duplex audio interruption or echo cancellation.

## State Machine

- `FACE` - default expression page.
- `MENU` - five-icon app menu.
- `WIFI_INFO` - Wi-Fi status page.
- `CAMERA_DEBUG` - camera preview and SD photo capture.
- `AI_VISION` - camera preview for XiaoZhi vision requests.
- `POMODORO` - IMU selected timer.
- `MUSIC` - SD MP3/WAV player.
- `SYSTEM_INFO` - heap, PSRAM, power, and vision status page.
- `AFFINITY` - Bond page with score, level, mood, and recent interaction.
- `AI` - XiaoZhi voice interaction page.
- `SETTINGS` - runtime brightness and volume page.
- `SLEEP` - dim screen sleep page.

## Code Structure

```text
src/
|- main.cpp
|- app/
|  |- app_state.h/.cpp
|  |- affinity_manager.h/.cpp
|  |- gesture_manager.h/.cpp
|  |- system_status.h/.cpp
|  `- event_bus.h/.cpp
|- ai/
|  |- xiaozhi_client.h/.cpp
|  `- voice_state.h
|- audio/
|  `- music_manager.h/.cpp
|- config/
|  |- app_config.h
|  `- wifi_secrets.example.h
|- network/
|  |- wifi_manager.h/.cpp
|  `- vision_client.h/.cpp
|- power/
|  `- power_manager.h/.cpp
|- servo/
|  |- servo_controller.h/.cpp
|  `- servo_motion_controller.h/.cpp
|- storage/
|  `- storage_manager.h/.cpp
|- ui/
|  |- face_ui.h/.cpp
|  |- menu_ui.h/.cpp
|  |- camera_debug_ui.h/.cpp
|  |- pomodoro_ui.h/.cpp
|  |- info_ui.h/.cpp
|  |- music_ui.h/.cpp
|  |- affinity_ui.h/.cpp
|  |- settings_ui.h/.cpp
|  `- ui_theme.h
`- vision/
   |- camera_manager.h/.cpp
   |- face_detector.h/.cpp
   |- face_tracker.h/.cpp
   |- face_tracking_controller.h/.cpp
   `- imu_orientation.h/.cpp
```

## FreeRTOS Tasks

| Task | Function | Frequency |
|------|----------|-----------|
| UI | Draw active screen with PSRAM canvas where possible | 20 FPS |
| Touch | Read touch and emit gestures | 50 Hz |
| Camera | Push camera frames to Camera Debug / AI Vision UI when capture is running | 15 FPS |
| Vision | Heuristic detection/tracking loop when face vision is enabled | 5 FPS |
| AI | Process XiaoZhi WebSocket, activation, audio channel requests | 50 Hz tick |
| Power | Battery/sleep status update | 1 Hz |
| Network | Wi-Fi reconnect and menu status update | 5 s interval |
| Music | Background MP3/WAV streaming task inside `MusicManager` | event-driven |

## Gesture Routing

- Face: right swipe -> Menu; left swipe or double tap -> AI; down swipe -> Bond; up swipe -> Settings; tap top -> HAPPY + nod; tap bottom -> SHY + look down; tap left/right -> CURIOUS with gaze and pan servo; long press -> Sleep.
- AI: single tap toggles listening; right swipe or long press -> Face.
- Bond: Back, up swipe, or left swipe -> Face.
- Settings: Back, left swipe, or down swipe -> Face.
- Menu: tap app icon -> Wi-Fi, Camera, Timer, Music, or System; Back -> Face; left swipe -> Face.
- Camera Debug: SHOT -> optionally center a heuristic face region, then save JPEG to `/photos`; Back or left swipe -> Menu.
- AI Vision: Back or left swipe -> close preview and return to AI.
- Pomodoro: IMU orientation selects preset before start; Start/Pause/Reset buttons control timer; Back or left swipe -> Menu.
- Music: Play/Pause, Stop, Next, Back; left swipe -> Menu; clockwise twist skips to next track.
- Sleep: tap, double tap, or shake -> Face.

## XiaoZhi AI Notes

- OTA endpoint is `https://api.tenclass.net/xiaozhi/ota/`.
- Firmware identity comes from `XIAOZHI_BOARD_TYPE` and `XIAOZHI_FIRMWARE_VERSION` in `src/config/app_config.h`.
- The client sends a WebSocket `hello` with Opus audio parameters and MCP enabled.
- Audio upload is 16 kHz mono Opus, 60 ms frames, 24 kbps target bitrate.
- Returned TTS Opus is decoded at the server-provided sample rate and played through M5Unified Speaker.
- CoreS3 mic and speaker are handled as half-duplex.
- MCP tools currently exposed:
  - `self.camera.open`
  - `self.camera.close`
  - `self.camera.capture_photo`
  - `self.vision.describe_scene`
  - `self.pomodoro.open`
  - `self.music.control`
  - `self.pet.react`
  - `self.servo.control`
  - `self.device.status`
  - `self.device.control`
- AI Vision only works when the XiaoZhi MCP initialize params provide a vision URL/token.
- `self.servo.control` supports `center`, `left`, `right`, `up`, `down`, `nod`, `shake`, `dance`, and `release`. It must not switch pages or reopen the XiaoZhi audio channel. `dance` starts servo motion first, then uses `/music` when available; music or SD failure must not cancel the servo motion.

## Face Detection Notes

Do not fake real face recognition.

`FaceDetector` currently uses a lightweight skin-color heuristic backend. It can report rough face-like blobs for expression effects and open-loop servo centering, but it is not an ESP-DL/ESP-WHO ML model and must not be described as identity recognition.

The Face page runs detection only in short windows:

- Boot burst when not in brownout safe mode.
- Touch, swipe, double tap, shake, twist, and AI-return bursts.
- Occasional short idle rescans.

Camera Debug, AI Vision, and photo capture may open the camera immediately because those are explicit foreground camera features.

## Stability Notes

- Brownout reset activates a safe-mode window that suppresses automatic Face-page camera bursts and startup chime.
- Low battery handling stops Face-page vision capture and switches the face to `SLEEPY` without forcing deep sleep.
- Foreground camera startup owns camera reinitialization and may suspend the touch task briefly to avoid CoreS3 internal I2C contention.
- Servo companion motion pauses while Face-page vision burst capture is active to reduce current spikes.
- PCA9685 scanning stops permanently after three not-found attempts for the current boot.

## Development Rules

- Preserve user changes. Check `git status --short` before editing.
- Keep Wi-Fi credentials in `src/config/wifi_secrets.h`; never commit real SSID/password files.
- Build with `pio run` unless the user explicitly asks not to compile.
- Keep display rendering centralized through UI classes and PSRAM `M5Canvas` where possible.
- Keep camera framebuffer lifetime explicit: release frames after use and never use data after returning/freeing the framebuffer.
- SD card handling must tolerate missing/flaky cards without rebooting. The retry sequence is 25, 10, 4, and 1 MHz.
- Stop music before exclusive speaker/mic use by XiaoZhi AI or Pomodoro completion melody.
- Do not claim face recognition, closed-loop PID gimbal tracking, or autonomous base behavior.
- Bond recent interaction detail is runtime-only even though the main affinity score persists.

## GitHub Hygiene

- Commit `src/config/wifi_secrets.example.h`, not `src/config/wifi_secrets.h`.
- Do not commit build output, monitor logs, `.pio`, local editor database files, or local secrets.
- The GitHub Actions workflow should run PlatformIO, not the default CMake starter workflow.
- README files should describe current firmware behavior, not older planning-stage design.
