# 舵机掉电 + MP3 无声 + CoreS3 掉电修复计划

## 问题分析

### 问题 1：舵机测试页面进入/退出时舵机掉电

**根因**：
1. **进入舵机测试**时，`stateChangeHandler(SERVO_TEST)` 调用 `gServoController.setPanTilt(90, 90)` — 这是**瞬间跳转**，没有速度限制。如果之前舵机在 `SERVO_TILT_CENTER_DEG=140` 的位置，瞬间跳到 90°，50° 的瞬间位移会产生巨大电流脉冲。
2. **退出舵机测试**时，`stateChangeHandler` 调用 `gServoController.center()` — 这也是瞬间跳转到 `SERVO_PAN_CENTER_DEG=90, SERVO_TILT_CENTER_DEG=140`。如果舵机在测试页面的 90° 位置，瞬间跳 50° 到 140°，同样大电流。
3. `SERVO_TEST_TRANSITION_SPEED_DPS=22.0f` 和 `SERVO_SAFE_CENTER_SPEED_DPS=22.0f` 已经定义了但**从未使用**。
4. `SERVO_TEST_INPUT_ENABLE_DELAY_MS=1400` 也定义了但未使用 — 进入测试页面后 IMU 立即驱动舵机，没有给舵机到达初始位置的缓冲时间。

**修复**：
- 进入 SERVO_TEST：用 `gServoMotionController` 的 `lookOffset`/`center` 以 `SERVO_TEST_TRANSITION_SPEED_DPS` 慢速过渡到测试初始位置，而非直接 `setPanTilt`
- 退出 SERVO_TEST：用 `gServoMotionController.center(SERVO_SAFE_CENTER_SPEED_DPS)` 慢速回中，而非直接 `gServoController.center()`
- 进入测试页面后延迟 `SERVO_TEST_INPUT_ENABLE_DELAY_MS` 再启用 IMU 输入

### 问题 2：MP3 播放没有声音

**根因**：
1. **扬声器通道冲突**：XiaoZhi AI 语音使用 I2S 扬声器通道，`M5.Speaker` 是共享资源。当 AI 语音停止后，`M5.Speaker` 的内部状态可能没有正确恢复到音乐播放所需的配置。
2. **MP3 Output 的 `begin()` 中重新配置了 Speaker**：`M5SpeakerAudioOutput::begin()` 调用 `M5.Speaker.config()` + `M5.Speaker.begin()`，这可能与 XiaoZhi 的 I2S 配置冲突。
3. **采样率不匹配**：`beginSpeaker()` 设置 `sample_rate=48000`，但 MP3 Output 的默认 `hertz=44100`。两者不一致。
4. **`M5.Speaker.playRaw()` 队列满**：如果 AI 语音刚停止，Speaker 内部队列可能还有残留数据，导致 `playRaw` 返回 false。
5. **音量可能被 AI 语音修改**：XiaoZhi 的 OPUS 解码可能修改了 Speaker 音量。

**修复**：
- 在 `playMp3File()` 开始前，确保先 `M5.Speaker.stop()` 所有相关通道
- 统一 `beginSpeaker()` 和 `M5SpeakerAudioOutput` 的采样率配置
- 在 `M5SpeakerAudioOutput::begin()` 中添加短暂延时让 Speaker 内部状态稳定
- 播放前显式设置音量

### 问题 3：CoreS3 整体掉电（Brownout）

**根因**：
1. **舵机大电流瞬态**：SG90 舵机在快速转动时瞬时电流可达 500mA-1A。两个舵机同时快速转动可能瞬间拉低 3.3V/5V 总线，触发 ESP32-S3 的 Brownout 检测器（默认阈值约 2.7V）。
2. **跳舞时舵机+音乐+AI 同时运行**：Dance 序列以 44°/s 运行两个舵机，同时可能播放 MP3（解码+DAC），同时 XiaoZhi WS 保持连接 — 三者叠加的功耗接近 USB 供电极限。
3. **舵机测试页面进入/退出的瞬间跳转**：50° 瞬移产生的电流尖峰最大。

**修复**：
- 所有舵机运动都通过 `ServoMotionController` 的速度限制，**永远不直接调用 `ServoController::setPanTilt()` 做大角度跳转**
- 降低跳舞速度 `SERVO_DANCE_MOTION_SPEED_DPS` 从 44 降到 36
- 降低 `SERVO_FACE_MOTION_SPEED_DPS` 从 34 降到 28
- 在跳舞开始前停止 AI 音频采集（已有 `pauseForForegroundTool`，但需要确保调用）
- 在舵机测试页面进入/退出使用慢速过渡

## 具体修改步骤

### Step 1: 修复舵机测试页面进入/退出 — 慢速过渡

**文件**: `main.cpp`

1. **进入 SERVO_TEST** (`stateChangeHandler`):
   - 不再直接 `gServoController.setPanTilt(90, 90)`
   - 改为：先 `gServoController.begin()` 确保 PCA9685 就绪，然后通过 `gServoMotionController` 以 `SERVO_TEST_TRANSITION_SPEED_DPS` 慢速移到测试初始位置
   - 设置 `gServoTestReadyForUpdates = false`，启动一个延时计时器 `gServoTestInputEnableAt = now + SERVO_TEST_INPUT_ENABLE_DELAY_MS`

2. **退出 SERVO_TEST** (`stateChangeHandler`):
   - 不再直接 `gServoController.center()`
   - 改为：通过 `gServoMotionController.center(SERVO_SAFE_CENTER_SPEED_DPS)` 慢速回中

3. **IMU 输入延时启用** (`updateServoTestFromImu`):
   - 检查 `millis() >= gServoTestInputEnableAt` 才开始 IMU 驱动

### Step 2: 修复 MP3 无声

**文件**: `music_manager.cpp`

1. **`playMp3File()` 开始前**：
   - 调用 `M5.Speaker.stop(MUSIC_CHANNEL)` 清理残留
   - 添加短暂延时 `vTaskDelay(pdMS_TO_TICKS(50))` 让 Speaker 内部稳定

2. **`M5SpeakerAudioOutput::begin()`**：
   - 统一采样率为 44100（与 MP3 解码器输出一致）
   - 在 `M5.Speaker.setVolume()` 后添加 `vTaskDelay(pdMS_TO_TICKS(30))` 让 DAC 稳定

3. **`beginSpeaker()`**：
   - 采样率改为 44100（匹配 MP3 解码器）
   - 添加 `M5.Speaker.stop(MUSIC_CHANNEL)` 清理

### Step 3: 降低舵机速度，防止 Brownout

**文件**: `config/app_config.h`

1. `SERVO_FACE_MOTION_SPEED_DPS`: 34 → 28
2. `SERVO_DANCE_MOTION_SPEED_DPS`: 44 → 36
3. `SERVO_TRACKING_MOTION_SPEED_DPS`: 35 → 28
4. `SERVO_TEST_MAX_SPEED_DPS`: 45 → 36

### Step 4: 跳舞时确保 AI 音频暂停

**文件**: `main.cpp`

1. 在 `processServoControl()` 的 DANCE 分支中，调用 `gXiaoZhiClient.pauseForForegroundTool()` 暂停 AI 音频
2. 在 `updateDanceLifecycle()` 跳舞结束时，如果之前在 AI 页面，调用 `gXiaoZhiClient.resumeFromForegroundTool()`

### Step 5: 消除所有直接 `ServoController::setPanTilt` 大角度跳转

**文件**: `main.cpp`

1. 检查所有 `gServoController.setPanTilt()` 和 `gServoController.center()` 调用
2. 替换为通过 `gServoMotionController` 的受速度限制的方法
3. 唯一允许直接 `setPanTilt` 的场景：小角度微调（<5°）或舵机刚初始化时

### Step 6: 构建验证

- `pio run` 构建通过
- 确认 RAM/Flash 在合理范围内

## 风险评估

- **速度降低**：舵机响应会稍慢，但更安全，不会掉电
- **MP3 修复**：增加的延时极短（<100ms），不影响用户体验
- **慢速过渡**：进入/退出舵机测试页面会有约 1-2 秒的过渡动画，反而更自然
