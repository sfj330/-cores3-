# CoreS3 AI 桌面宠物

这是一个基于 M5Stack CoreS3 的桌面宠物演示固件。当前版本整合了表情脸、触摸与 IMU 交互、Wi-Fi 状态、相机预览与拍照、番茄钟、SD 音乐播放、小智语音交互，以及通过 XiaoZhi MCP 工具触发的 AI Vision 请求。

英文说明见 [README.md](README.md)。

## 当前状态

- 表情界面：支持 11 种表情、眨眼、视线平滑、临时视线覆盖、表情粒子特效、点击提示音、NTP 同步后的时间感知待机动作，以及安全的二轴舵机联动。
- 表情交互：轻微摇晃会触发 `SURPRISED`，连续摇晃可能触发 `SICK`，顺时针和逆时针扭转会触发左右好奇动作，休眠后可通过点击或摇晃唤醒。
- 设置页：从表情页上滑进入，提供运行时亮度和音量预设调节。
- Camera Debug：支持 320 x 240 预览、FPS 与状态叠层、Back 按钮、JPEG 拍照到 SD，以及拍照前基于启发式人脸区域的居中修正。
- 相机稳定性：Camera 与 AI Vision 前台启动不再阻塞触摸/状态切换回调；用户主动打开时会清除相机失败冷却，初始化前释放 CoreS3 内部 I2C，初始化期间短暂停止触摸任务，失败后会干净反初始化并重试一次。
- AI Vision：通过小智服务下发的 vision endpoint 对相机画面进行描述，不是本地嵌入式视觉大模型。
- 人脸检测：当前 Arduino 构建使用轻量级肤色启发式算法做粗略人脸区域估计与舵机居中，不是机器学习人脸检测，也不是人脸识别。表情页采用短时检测窗口，不让相机长期常驻。
- 系统页：基于共享运行时状态快照，固定展示 Wi-Fi、SD、Audio、Control、Memory 五行状态，并在副标题汇总 XiaoZhi 与 Vision 状态。
- 番茄钟：通过 IMU 方向选择四个预设，支持屏幕旋转、计时控制和完成反馈。
- 音乐播放：扫描 SD 卡 `/music` 目录，最多播放 16 个 MP3 或 PCM WAV 文件。音量使用统一的 `quiet`、`normal`、`loud` 运行时档位，顺时针扭转还能切下一首。
- 舵机互动：表情点击、待机动作、小智表情反应、小智舵机命令和跳舞彩蛋共用同一套安全运动控制器与限速逻辑。表情和小智姿态保持水平 90 度、垂直 140 度的机械中位。
- 舵机降级：PCA9685 连续 3 次扫描不到后会自动禁用舵机驱动，避免未接底座时持续轮询 PortA。
- 好感系统：Bond 页面显示分数、等级、心情和最近互动。好感分值会写入 NVS，重启后可恢复。
- 小智 AI：已实现 OTA 激活与配置、TLS WebSocket、Opus 麦克风上传、Opus TTS 播放、MCP 握手与工具处理，并支持设备状态查询、页面切换、亮度音量调节和休眠唤醒控制。
- 电源：电池电压通过 `M5.Power.getBatteryVoltage()` 读取。Brownout 后会进入安全模式，降低表情页自动相机、舵机、音频负载。

## 硬件

- M5Stack CoreS3，ESP32-S3
- USB 数据线，用于编译、烧录和串口监视
- FAT/FAT32 格式的 MicroSD 或 TF 卡，用于保存照片和 MP3/WAV 音乐
- 可选底座硬件：电池、PCA9685、二轴舵机

本固件使用的舵机接线：

```text
CoreS3 PortA -> PCA9685 I2C
PCA9685 地址: 0x40
水平舵机: PCA9685 通道 0
垂直舵机: PCA9685 通道 1
舵机电源: 外部舵机电源模块，并与 CoreS3 / PCA9685 共地
表情 / 小智中位: 水平 90 度，垂直 140 度
```

本固件使用的 CoreS3 SD SPI 引脚：

```text
SCK  36
MISO 35
MOSI 37
CS   4
```

## 软件要求

- PlatformIO Core
- Git
- Windows、Linux 或 macOS 主机

项目配置为：

- PlatformIO board: `m5stack-cores3`
- Framework: Arduino
- MCU: ESP32-S3
- C++ standard: C++17
- PSRAM: 通过 `board_build.arduino.memory_type = qio_qspi` 启用 QSPI PSRAM

## 目录结构

```text
src/
|- main.cpp
|- app/        # 状态机、手势、好感、事件总线、共享系统状态快照
|- ai/         # 小智激活、WebSocket、Opus 音频、MCP
|- audio/      # SD MP3/WAV 音乐播放器
|- config/     # 固件常量和 Wi-Fi 密钥模板
|- network/    # Wi-Fi 管理和 AI Vision HTTP 客户端
|- power/      # 休眠状态与电池电压读取
|- servo/      # PCA9685 驱动与共享舵机运动层
|- storage/    # SD 探测、照片路径、文件写入
|- ui/         # 表情、菜单、相机、番茄钟、系统、音乐、好感、设置 UI
`- vision/     # 相机管理、启发式人脸检测、追踪器、IMU 方向
```

## Wi-Fi 配置

真实 Wi-Fi 凭据不要提交到 Git。

```powershell
Copy-Item src\config\wifi_secrets.example.h src\config\wifi_secrets.h
```

编辑 `src/config/wifi_secrets.h`：

```cpp
#pragma once
constexpr const char* WIFI_SSID = "你的WiFi名称";
constexpr const char* WIFI_PASSWORD = "你的WiFi密码";
```

`src/config/wifi_secrets.h` 已被 Git 忽略。

## 编译

```powershell
pio run
```

如果 Windows 上没有把 `pio` 加到 `PATH`，可以使用完整路径，例如：

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

- 表情页：右滑进入菜单，左滑或双击进入小智 AI，下滑进入 Bond，上滑进入设置页，点击上/下/左/右会切换表情与头部姿态，摇晃和扭转会触发 IMU 互动，长按进入休眠。
- AI 页：单击切换监听，右滑或长按返回表情页。
- Bond 页：Back、上滑或左滑返回表情页。
- 菜单页：点击图标进入 Wi-Fi、Camera、Timer、Music 或 System。Back 返回表情页。
- 设置页：点击 Brightness 和 Volume 下的 `Low`、`Mid`、`High` 应用运行时预设。Back、左滑或下滑返回表情页。
- Camera Debug：`SHOT` 保存 `/photos/IMG_####.jpg`。如果启发式检测器找到了较可信的人脸区域，固件会在拍照前短暂尝试舵机居中。Back 或左滑返回菜单。
- AI Vision：Back 或左滑关闭预览并返回小智 AI。
- 番茄钟：旋转设备选择预设，Start、Pause、Reset 控制计时，Back 返回菜单。
- 音乐页：Play/Pause、Stop、Next 控制 `/music` 中的 MP3/WAV 文件，顺时针扭转也可切到下一首。
- 系统页：显示 Wi-Fi、SD、Audio、Control、Memory 五行状态，副标题汇总 XiaoZhi 与 Vision 就绪情况。Back 返回菜单。
- 休眠页：点击或摇晃可唤醒并回到表情页。

## SD 卡内容

固件会创建并使用以下路径：

```text
/photos/IMG_####.jpg
/music/*.mp3
/music/*.wav
```

音乐播放支持 MP3 文件，以及 8-bit 或 16-bit、单声道或双声道的 PCM WAV 文件。MP3 的单声道或双声道输出都会先混成单声道，再送入板载扬声器。扫描时最多读取前 16 个音频文件，并按文件名排序。

## 小智 AI

核心实现位于 `src/ai/xiaozhi_client.h` 和 `src/ai/xiaozhi_client.cpp`。

流程如下：

1. 从表情页进入 AI 页。
2. 等待 Wi-Fi 后，请求小智 OTA 接口获取激活与配置数据。
3. 使用返回的 URL 和 token 建立 TLS WebSocket。
4. 发送带 Opus 音频参数并启用 MCP 的 `hello`。
5. 响应 MCP 初始化请求。
6. MCP 就绪后开始监听。
7. 采集 16 kHz 单声道 PCM，编码成 Opus，并通过 WebSocket binary frame 上传。
8. 解码服务端返回的 Opus TTS，并通过 CoreS3 扬声器播放。

CoreS3 麦克风和扬声器按半双工处理：

- 监听时停止扬声器并启动麦克风。
- 说话时停止麦克风并启动扬声器。
- TTS 结束后，如果 AI 页仍请求监听，会重新开始监听。

当前暴露的 MCP 工具：

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

AI Vision 依赖小智服务在 MCP 初始化中提供的 vision endpoint 和 token，不是本地嵌入式视觉流水线。

`self.servo.control` 支持 `center`、`left`、`right`、`up`、`down`、`nod`、`shake`、`dance`、`release`。Transcript fallback 也识别这些常见中文动作表达。`dance` 会先启动舵机动作，再尝试配合 `/music` 中的音频播放；音乐或 SD 异常不会取消舵机动作。

`self.device.status` 支持 `detail=brief|full`。`brief` 固定返回两句自然中文，先讲 Wi-Fi 和 SD，再讲 Music、AI 和 Vision。`full` 会额外补充 heap 与 PSRAM 信息。

`self.device.control` 支持可选的 `page`、`brightness`、`volume`、`sleep` 参数。页面支持 `face`、`menu`、`wifi`、`system`、`camera`、`music`、`pomodoro`、`ai`。亮度预设支持 `dim`、`normal`、`bright`。音量预设支持 `quiet`、`normal`、`loud`。休眠控制支持 `wake` 和 `sleep`。唤醒后会恢复最近一次非睡眠亮度偏好，而不是直接写死满亮。

Transcript fallback 还支持常见中文指令，例如系统状态查询、打开系统页/音乐页/相机页/番茄钟、回到主页、调亮、调暗、调大声、调小声、睡觉和唤醒。

## 已知限制

- 当前的人脸检测器只是基于肤色区域的启发式 blob 检测，可用于粗略居中，但不是严格意义上的机器学习人脸检测，更不是人脸识别。
- 拍照前的人脸追踪仍然是围绕启发式框的开环舵机修正，适合演示，不属于稳健的计算机视觉方案。
- AI Vision 依赖小智服务端提供的 vision endpoint。
- CoreS3 内置麦克风和扬声器是半双工，当前没有实现全双工打断和回声消除。
- 亮度和音量档位都是运行时设置，不做持久化，重启后恢复为 `normal` + `normal`。
- SD 卡识别受卡格式、接触和供电影响。固件会按 25、10、4、1 MHz 逐级重试。
- 舵机表情和小智控制是带限速的开环姿态控制，不是 PID 云台控制或自主底座运动。
- Bond 中的最近互动等细节仍是运行时状态，只有主好感分值会持久化。

## 故障排查

- 烧录失败：长按 RESET 约 3 秒后重新烧录，烧录完成后再按一次 RESET。
- Wi-Fi 显示 `Not configured`：从示例创建 `src/config/wifi_secrets.h`。
- Camera 页黑屏或显示 `Camera not ready`：查看串口是否有 `Foreground camera start failed` 或 `esp_camera_init FAILED`。当前固件会释放内部 I2C、相机初始化期间暂停触摸任务，并失败重试一次；如果仍失败，优先检查相机总线、PSRAM 和供电稳定性。
- 小智 OTA 返回 `400`：检查 `XIAOZHI_BOARD_TYPE`、`XIAOZHI_FIRMWARE_VERSION` 和串口中的 OTA identity。
- 小智 WebSocket 在 MCP 后断开：确认串口中依次出现 `initialize`、`tools/list` 和 `sent listen start`。
- 没有 AI 回复：确认 Wi-Fi 已连接，并检查 `WS connected`、`session_id`、`sent listen start`、`stt`、`tts` 日志。
- AI Vision 提示 endpoint missing：当前小智会话没有下发 vision capability。
- SD 未识别：格式化为 FAT/FAT32，并查看串口中的 `SD.begin failed`、`CARD_NONE`、`Root open failed` 或 `Probe write failed`。
- 舵机日志显示 `PCA9685 not found` 或 `disabled`：检查 PortA 接线、PCA9685 地址跳线、外部舵机供电和共地。连续 3 次扫描失败后，固件会停止继续轮询缺失的 PCA9685。
- PCA9685 灯短暂熄灭或舵机在快速双轴动作下嗡鸣：使用更强的外部舵机电源，缩短或加粗供电线，确认共地，并避免机械撞限位。
- MP3 无声或播放几秒后重启：检查串口中的曲目路径、MP3 采样率/声道信息和 speaker 队列失败日志。如果下次启动显示 `BROWNOUT` 或 `POWERON`，优先按供电掉压处理；如果显示 `WDT` 或 `PANIC`，再继续排查软件崩溃。
