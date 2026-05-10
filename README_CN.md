# CoreS3 AI 桌面宠物交互系统

这是一个基于 M5Stack CoreS3 的嵌入式桌宠演示固件。当前版本已经整合表情 UI、触摸手势、Wi-Fi 状态、Camera Debug、SD 拍照、IMU 番茄钟、SD 音乐播放、小智 AI 语音交互，以及通过小智 MCP 工具触发的 AI Vision 画面识别流程。

英文说明见 [README.md](README.md)。

## 当前状态

- 表情界面：11 种表情、眨眼、视线平滑、临时视线覆盖，摇晃机身会触发 `SICK` 难受表情。
- 功能菜单：五个入口，分别是 Wi-Fi、Camera、Timer、Music、System。
- Camera Debug：320 x 240 预览、FPS/状态叠层、Back、SHOT，支持拍照保存到 SD。
- 番茄钟：IMU 四方向选择预设，屏幕跟随旋转，支持开始/暂停/重置，结束后播放提示音并回到表情页显示临时表情。
- 音乐播放：扫描 SD 卡 `/music` 目录，最多播放 16 个 PCM WAV 文件。
- 小智 AI：已实现 OTA 激活/配置请求、TLS WebSocket、Opus 麦克风上传、Opus TTS 播放、MCP 握手和工具调用处理。
- AI Vision：通过小智服务下发的 vision endpoint，将 CoreS3 拍到的 JPEG 发送给服务端进行画面描述。
- 人脸检测：当前 Arduino/PlatformIO 版本有意关闭，不启用假检测或肤色检测。

## 硬件

- M5Stack CoreS3，ESP32-S3
- USB 数据线，用于编译烧录和串口监视
- FAT/FAT32 格式 MicroSD/TF 卡，用于保存照片和 WAV 音乐
- 可选底座硬件：电池、PCA9685、二轴舵机

本固件使用的 CoreS3 SD SPI 引脚：

```text
SCK  36
MISO 35
MOSI 37
CS   4
```

## 软件环境

- PlatformIO Core
- Git
- Windows、Linux 或 macOS 主机

项目配置：

- PlatformIO board：`m5stack-cores3`
- Framework：Arduino
- MCU：ESP32-S3
- C++ 标准：C++17
- PSRAM：通过 `board_build.arduino.memory_type = qio_qspi` 启用 QSPI PSRAM

## 目录结构

```text
src/
├── main.cpp
├── app/        # 状态机、手势、事件总线
├── ai/         # 小智激活、WebSocket、Opus 音频、MCP
├── audio/      # SD WAV 音乐播放器
├── config/     # 固件参数和 Wi-Fi 密码模板
├── network/    # Wi-Fi 管理和 AI Vision HTTP 客户端
├── power/      # 电池/休眠占位逻辑
├── storage/    # SD 探测、照片路径、文件写入
├── ui/         # 表情、菜单、相机、番茄钟、信息、音乐界面
└── vision/     # 相机管理、关闭的人脸检测接口、追踪器、IMU 方向
```

## Wi-Fi 配置

真实 Wi-Fi 密码不提交到 Git。

```powershell
Copy-Item src\config\wifi_secrets.example.h src\config\wifi_secrets.h
```

编辑 `src/config/wifi_secrets.h`：

```cpp
#pragma once
constexpr const char* WIFI_SSID = "你的WiFi名称";
constexpr const char* WIFI_PASSWORD = "你的WiFi密码";
```

`src/config/wifi_secrets.h` 已被 `.gitignore` 忽略。

## 编译

```powershell
pio run
```

如果 Windows 上没有把 `pio` 加到 `PATH`，可以使用 PlatformIO 的完整路径，例如：

```powershell
C:\Users\<you>\.platformio\penv\Scripts\platformio.exe run
```

## 烧录

```powershell
pio run -t upload
```

如果自动识别端口失败：

```powershell
pio run -t upload --upload-port COM5
```

如果烧录无法开始，长按 RESET 约 3 秒进入下载模式。烧录完成后，再按一次 RESET 运行固件。

## 串口监视

```powershell
pio device monitor -b 115200
```

指定端口：

```powershell
pio device monitor -p COM5 -b 115200
```

## 操作方式

- 表情页：右滑进入菜单；左滑或双击进入小智 AI；点击上/下/左/右切换表情和视线；摇晃机身显示 `SICK`；长按进入休眠。
- AI 页：单击切换监听；右滑或长按返回表情页。
- 菜单页：点击图标进入 Wi-Fi、Camera、Timer、Music 或 System；Back 返回表情页。
- Camera Debug：SHOT 保存 `/photos/IMG_####.jpg`；Back 或左滑返回菜单。
- AI Vision：Back 或左滑关闭预览并返回小智 AI。
- 番茄钟：旋转设备选择预设；Start/Pause 和 Reset 控制计时；Back 返回菜单。
- 音乐页：Play/Pause、Stop、Next 控制 `/music` 里的 WAV 文件。

## SD 卡内容

固件会使用以下路径：

```text
/photos/IMG_####.jpg
/music/*.wav
```

音乐播放当前支持 8-bit 或 16-bit、单声道或双声道的 PCM WAV 文件。扫描时最多读取前 16 个 WAV，并按文件名排序。

## 小智 AI

核心代码在 `src/ai/xiaozhi_client.h` 和 `src/ai/xiaozhi_client.cpp`。

流程：

1. 从表情页进入 AI 页。
2. 等待 Wi-Fi 连接后，请求小智 OTA 接口获取激活/配置数据。
3. 使用返回的 URL/token 建立 TLS WebSocket。
4. 发送带 Opus 音频参数和 MCP 能力的 `hello`。
5. 响应 MCP 初始化请求。
6. MCP 就绪后发送 `listen start`。
7. 采集 16 kHz 单声道 PCM，编码成 Opus，通过 WebSocket binary frame 上传。
8. 解码服务端返回的 Opus TTS，并通过 CoreS3 扬声器播放。

CoreS3 内置麦克风和扬声器按半双工处理：

- 监听时停止扬声器，启动麦克风。
- 说话时停止麦克风，启动扬声器。
- TTS 结束后，如果 AI 页仍在监听状态，会重新开始监听。

当前暴露的 MCP 工具：

```text
self.camera.open
self.camera.close
self.vision.describe_scene
self.pomodoro.open
```

AI Vision 依赖小智服务在 MCP 初始化中提供的 vision endpoint 和 token。它不是本地嵌入式人脸检测。

## 已知限制

- 真实人脸检测仍然关闭。ESP-DL/ESP-WHO 需要先验证 ESP-IDF 或 Arduino-as-IDF-component 集成路径。
- AI Vision 依赖小智服务端提供 vision endpoint。
- CoreS3 内置麦克风/扬声器是半双工，当前没有实现全双工打断和回声消除。
- 电池电压读取仍是占位实现，需要结合最终 PMU API/硬件路径验证。
- SD 卡识别受卡格式、接触和供电影响。固件会按 25、10、4、1 MHz 逐级重试。
- PCA9685 舵机控制目前只保留配置，当前固件尚未实现底座舵机控制。

## 故障排查

- 烧录失败：长按 RESET 约 3 秒后重新烧录，烧录完成后再按一次 RESET。
- Wi-Fi 显示 `Not configured`：从示例创建 `src/config/wifi_secrets.h`。
- 小智 OTA 返回 `400`：检查 `XIAOZHI_BOARD_TYPE`、`XIAOZHI_FIRMWARE_VERSION` 和串口里的 OTA identity。
- 小智 WebSocket 在 MCP 后断开：确认串口中依次出现 `initialize`、`tools/list` 和 `sent listen start`。
- 没有 AI 回复：确认 Wi-Fi 已连接，并检查 `WS connected`、`session_id`、`sent listen start`、`stt`、`tts` 日志。
- AI Vision 提示 endpoint missing：当前小智会话没有下发 vision capability。
- SD 未识别：格式化为 FAT/FAT32，重新插入，查看串口中的 `SD.begin failed`、`CARD_NONE`、`Root open failed` 或 `Probe write failed`。
