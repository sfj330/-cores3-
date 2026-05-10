# CLAUDE.md

This repository uses the same working guidance for Claude Code and Codex. Treat [AGENTS.md](AGENTS.md) as the source of truth for the current architecture, implemented features, known limitations, and development rules.

Important current facts:

- This is active M5Stack CoreS3 / ESP32-S3 firmware using PlatformIO Arduino, not a planning-only project.
- Implemented pages are Face, Menu, Wi-Fi Info, Camera Debug, AI Vision, Pomodoro, Music, System Info, AI, and Sleep.
- XiaoZhi voice integration is real in this firmware path: OTA/config request, TLS WebSocket, Opus mic upload, Opus TTS playback, MCP bootstrap, and selected MCP tools are implemented.
- AI Vision depends on a XiaoZhi-provided vision endpoint and token.
- Local face detection is intentionally disabled; do not add or describe fake detection.
- Wi-Fi secrets belong only in `src/config/wifi_secrets.h`, which is ignored by Git.
- Build with `pio run`.

When modifying the project, read `AGENTS.md`, `platformio.ini`, and the relevant `src/` files first.
