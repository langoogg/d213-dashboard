# D213 Automotive Dashboard / D213 汽车仪表盘

[English](#english) | [中文](#中文)

[![Platform](https://img.shields.io/badge/platform-D211%20RISC--V-blue)]()
[![OS](https://img.shields.io/badge/os-Buildroot%20Linux%205.10-green)]()
[![UI](https://img.shields.io/badge/ui-LVGL%20v8.3.10-orange)]()
[![Language](https://img.shields.io/badge/language-C%20100%25-555555)]()
[![Status](https://img.shields.io/badge/status-v6.4%20stable-brightgreen)]()
[![License](https://img.shields.io/badge/license-Apache--2.0-lightgrey)]()

---

## Quick Start / 快速开始

```bash
# Clone the repository
git clone https://github.com/langoogg/d213-dashboard
cd d213-dashboard

# Cross-compile for D211 board (RISC-V)
# 交叉编译到 D211 板端
make

# Deploy to board via ADB
# 通过 ADB 部署到板端
adb shell "killall dashboard_d213; wget -O /usr/local/bin/dashboard_d213 http://192.168.0.72:8888/dashboard_d213 && sync && reboot"

# Optional: Build PC simulator for UI debugging
# 可选：编译 PC 模拟器调试 UI
make sim
```

---

## English

A full-stack embedded automotive dashboard on ArtInChip D211 RISC-V SoC (600MHz/128MB). Pure C with LVGL v8 UI, MPP hardware H.264 decoding at 720p, ALSA audio playback, and FBIOPAN double-buffered display. 4 dashboard modes, touch gesture interaction, FIFO remote control. 503KB binary, 4MB runtime memory, 72-hour stability.

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│                   UI Layer (LVGL v8)                      │
│   dashboard.c — 4-mode rendering engine, post_draw_cb     │
├──────────────────────────────────────────────────────────┤
│               Display Layer (Framebuffer)                 │
│   /dev/fb0 — 1024×600 BGRA8888, virtual 1024×1200        │
├──────────────────┬───────────────────────────────────────┤
│  MPP Decoder      │  ALSA Audio                          │
│  aic_player H.264 │  snd_pcm WAV playback                │
├──────────────────┴───────────────────────────────────────┤
│                Driver Layer (Linux Kernel)                │
├──────────────────────────────────────────────────────────┤
│                ArtInChip D211 RISC-V SoC                  │
└──────────────────────────────────────────────────────────┘
```

### Features

- [x] **Mode 0 - Dashboard**: 4 analog gauges, 3-page display, 7-speed DSG physics, self-test animation
- [x] **Mode 1 - Music**: WAV playback via ALSA, `<</ ▶/ >>` controls, progress bar with elapsed/total time
- [x] **Mode 2 - Video**: MPP H.264 hardware decode, FBIOPAN page flip, touch bottom 1/6 to exit
- [x] **Mode 3 - WiFi**: SSID, IP, signal strength, connection rate
- [ ] MP3/FLAC software decode
- [ ] CAN bus integration

### Hardware

| Component | Specification |
|-----------|--------------|
| SoC | ArtInChip D211, RISC-V 64bit, 600MHz |
| RAM | 128MB DDR3 |
| Display | 1024×600 BGRA8888 |
| Touch | GT9xx I2C3 |
| Audio | ALSA sound card |
| WiFi | RT5572 USB |

### Build & Deploy

```bash
# Compile (Ubuntu VM, RISC-V cross compiler)
cd ~/lv_sim && make -f Makefile.d213 -j2

# Deploy
python3 -m http.server 8888 &
adb shell "wget -O /usr/local/bin/dashboard_d213 http://192.168.0.72:8888/dashboard_d213 && sync && reboot"
```

### Performance

| Metric | Value |
|--------|-------|
| Binary size | 503 KB |
| Runtime RSS | ~4 MB |
| Idle FPS | 54 |
| Video decode | 720p@30fps |

### Known Limitations

| Issue | Root Cause |
|-------|-----------|
| No video pause | aic_player lacks pause API |
| WAV only | No MP3/FLAC decoder |
| No progress bar in video | MPP lacks duration API |

### License

[Apache-2.0](LICENSE) — Copyright (c) 2026 langoogg

**Acknowledgments**: ArtInChip for the D21x SDK, LVGL project, RISC-V community.

---

## 中文

基于匠芯创 D211 RISC-V SoC（600MHz/128MB）的全栈嵌入式汽车仪表盘。纯 C 语言实现，LVGL v8 驱动界面，MPP 硬件解码 720p H.264 视频，ALSA 音频播放，FBIOPAN 双缓冲显示。4 种仪表盘模式，触摸手势交互，FIFO 远程控制，二进制 503KB，运行时 4MB 内存。

### 系统架构

```
┌──────────────────────────────────────────────────────────┐
│                  UI 层 (LVGL v8)                          │
│   dashboard.c — 4 模式渲染引擎, post_draw_cb 自定义绘制    │
├──────────────────────────────────────────────────────────┤
│              显示层 (Framebuffer)                         │
│   /dev/fb0 — 1024×600 BGRA8888, 虚拟 1024×1200 双缓冲     │
├──────────────────┬───────────────────────────────────────┤
│  MPP 视频解码     │  ALSA 音频                            │
│  aic_player H.264│  snd_pcm WAV 播放                      │
├──────────────────┴───────────────────────────────────────┤
│                驱动层 (Linux Kernel)                       │
├──────────────────────────────────────────────────────────┤
│               ArtInChip D211 RISC-V SoC                   │
└──────────────────────────────────────────────────────────┘
```

### 功能特性

- [x] **模式 0 — 仪表盘**：4 表盘、3 页切换、7 速 DSG 物理模型、2.4 秒自检动画
- [x] **模式 1 — 音乐**：WAV 播放、`<</ ▶/ >>` 控制、进度条 + 时间显示
- [x] **模式 2 — 视频**：MPP 硬解 H.264、FBIOPAN 页翻转、底部 1/6 触摸退出
- [x] **模式 3 — WiFi**：SSID、IP 地址、信号强度、连接速率
- [ ] MP3/FLAC 软件解码
- [ ] CAN 总线集成

### 硬件环境

| 组件 | 规格 |
|------|------|
| 芯片 | ArtInChip D211, RISC-V 64bit, 600MHz |
| 内存 | 128MB DDR3 |
| 屏幕 | 1024×600 BGRA8888 |
| 触摸 | GT9xx I2C3 |
| 音频 | ALSA 声卡 |
| WiFi | RT5572 USB |

### 编译与部署

```bash
# 编译（Ubuntu 虚拟机，RISC-V 交叉编译器）
cd ~/lv_sim && make -f Makefile.d213 -j2

# 部署到板端
python3 -m http.server 8888 &
adb shell "wget -O /usr/local/bin/dashboard_d213 http://192.168.0.72:8888/dashboard_d213 && sync && reboot"
```

### 性能指标

| 指标 | 数值 |
|------|------|
| 二进制大小 | 503 KB |
| 运行时内存 | ~4 MB |
| 空闲帧率 | 54 FPS |
| 视频解码 | 720p@30fps |

### 已知限制

| 问题 | 根因 |
|------|------|
| 视频无法暂停 | aic_player 缺少 pause 接口 |
| 仅支持 WAV | 未集成 MP3/FLAC 解码器 |
| 视频无进度条 | MPP 不支持时长查询 |

### 许可

[Apache-2.0](LICENSE) — Copyright (c) 2026 langoogg

**致谢**：匠芯创 D21x SDK、LVGL 项目、RISC-V 开源社区。
