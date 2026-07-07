# D213 Automotive Dashboard

**ArtInChip D211 RISC-V (600MHz, 128MB DDR3) — LVGL v8 Real-Time Dashboard**

[![Platform](https://img.shields.io/badge/platform-D211%20RISC--V-blue)]()
[![OS](https://img.shields.io/badge/os-Buildroot%20Linux%205.10-green)]()
[![UI](https://img.shields.io/badge/ui-LVGL%20v8.3.10-orange)]()
[![Status](https://img.shields.io/badge/status-v6.3%20stable-brightgreen)]()

A full-stack embedded dashboard system for automotive instrument clusters, built from scratch on a RISC-V SoC. Features 4-mode UI, MPP hardware video decoding, ALSA audio playback, touch gesture recognition, and FIFO-based remote control.

## Hardware

| Component | Specification |
|-----------|--------------|
| SoC | ArtInChip D211, RISC-V 64bit (RV64IMAFDC), 600MHz single-core |
| RAM | 128MB DDR3 (on-chip) |
| Display | 1024×600 BGRA8888, virtual 1024×1200 (double-buffered) |
| Touch | Goodix GT9xx I2C3 |
| Audio | aicSoundCard (ALSA) |
| WiFi | RT5572 USB (rt2800usb) |
| Storage | SPI NAND + 64GB SD card |

## Features

### Mode 0: Dashboard
- 4 analog gauges (speed, RPM, fuel, coolant temp)
- Central info display (digital speed, clock, fuel economy, odometer)
- 3-page navigation with touch arrows
- 7-speed DSG transmission physics model
- 2.4s self-test sweep animation (30FPS)
- Virtual buttons: turn signals (L/R), gear selector (P/N/D), throttle (+/-)
- Ignition state machine: OFF → SELF_TEST → START

### Mode 1: Music Player
- WAV file scanning from SD card
- ALSA PCM playback with real-time WAV header parsing
- Playback controls: << prev / ▶ play/pause / >> next
- Real-time progress bar with elapsed/total time display
- Auto-advance to next track
- Supports 8/16/24/32bit, 1-8 channels, arbitrary sample rates

### Mode 2: Video Player
- MPP hardware-accelerated H.264 decoding (aic_player API)
- FBIOPAN double-buffer page flipping for seamless video overlay
- Touch bottom 1/6 area to exit playback
- 120-second timeout protection
- First-frame timeout with graceful LVGL recovery

### Mode 3: WiFi Panel
- SSID, IP address, signal strength (dBm)
- Connection rate and frequency band display
- 2Hz auto-refresh

### System
- FIFO command pipe (`/tmp/dash_fifo`) for remote control
- V23 touch state machine: IDLE → PENDING → ACTIVE → HANDLED
- Gesture detection with direction threshold (horizontal swipe = mode switch)
- SD card mount-timing retry (3 attempts, 1s interval)
- Boot-time stdout real-time logging (non-buffered)

## Architecture

```
main_fb.c          — Entry point, framebuffer init, touch driver, FIFO, main loop
├── dashboard.c    — 4-mode rendering engine, post_draw_cb, virtual buttons
├── video_ctrl.c   — MPP hardware decoder, FBIOPAN page flip, timeout watchdog
├── audio_ctrl.c   — ALSA PCM backend, WAV parser, pthread playback thread
├── wifi_ctrl.c    — WiFi scanning, AP list, signal monitoring
├── vehicle_physics.c — 7-speed DSG physics model
└── ui_config.h    — Layout macros, button hotzones, state machine constants
```

## Build & Deploy

```bash
# Compile (Ubuntu 18.04 VM, riscv64 cross-toolchain)
cd ~/lv_sim
make -f Makefile.d213 -j2

# Binary: ~503KB, dynamically linked
# Deploy to board via ADB
python3 -m http.server 8888 &
adb shell "wget -O /usr/local/bin/dashboard_d213 http://192.168.0.72:8888/dashboard_d213 && sync && reboot"
```

### Build Requirements
- `riscv64-unknown-linux-gnu-gcc` V2.10.1
- `-O2`, dynamic linking, `rpath=/usr/local/lib`
- Dependencies: `libmedia_player.so`, `libasound.so`, `libmpp_decoder.so`

## Performance

| Metric | Value |
|--------|-------|
| Binary size | 503 KB (stripped) |
| Runtime RSS | ~3.9 MB |
| Idle FPS | 54 |
| Video decode FPS | 93 (1280×720 H.264) |
| Boot time | < 3 seconds |
| Free memory | 74 MB / 119 MB |

## Known Limitations

- No true video pause (aic_player lacks pause/resume API)
- Audio supports WAV only (software decoder needed for MP3/FLAC)
- No video progress bar (MPP lacks duration API, architecture constraint)
- Touch uses hardcoded coordinates (no LVGL widget interaction)
- Chinese filenames display as mojibake (montserrat font lacks CJK)
- No SD card hot-plug detection

## FIFO Commands

```bash
# Mode switch: q=0, w=1, e=2, r=3
echo w > /tmp/dash_fifo    # Switch to Music mode
echo e > /tmp/dash_fifo    # Switch to Video mode

# Dashboard: 1/2/3=page, d=ignition, h=gear, y/n=throttle, [/]=turn signals

# Music: b=play/stop, z=prev, x=next, p=stop, c=rescan
echo b > /tmp/dash_fifo    # Play/Stop

# Video: b=play, p=stop, c=rescan
echo p > /tmp/dash_fifo    # Stop video
```

## Project Timeline

| Date | Milestone |
|------|-----------|
| Jun 26 | Video playback via aic_player + dynamic linking |
| Jun 27 | ALSA audio module + FBIOPAN double-buffer fix |
| Jun 30 | Audio v2-v3 (dynamic buttons, auto-next, last_played_idx) |
| Jul 1  | Touch driver, virtual buttons, video list, code audit |
| Jul 2-3 | V12-V23 touch state machine + hotspot alignment |
| Jul 4  | v6.1 audio/video UI overhaul, v6.3 fullscreen touch-to-exit |
| Jul 7  | v6.4 cleanup (dead code removal, volatile fix) |

## License

This project is my personal work. The ArtInChip SDK components (lvgl-ui test files) retain their original Apache-2.0 license.
