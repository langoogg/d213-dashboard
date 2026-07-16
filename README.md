# D213 Automotive Dashboard

**ArtInChip D211 RISC-V (600MHz, 128MB DDR3) — LVGL v8 Real-Time Dashboard**

[![Platform](https://img.shields.io/badge/platform-D211%20RISC--V-blue)]()
[![OS](https://img.shields.io/badge/os-Buildroot%20Linux%205.10-green)]()
[![UI](https://img.shields.io/badge/ui-LVGL%20v8.3.10-orange)]()
[![Language](https://img.shields.io/badge/language-C%20100%25-555555)]()
[![Status](https://img.shields.io/badge/status-v6.4%20stable-brightgreen)]()
[![License](https://img.shields.io/badge/license-Apache--2.0-lightgrey)]()

A full-stack embedded dashboard system for automotive instrument clusters, built from scratch on a RISC-V SoC. Features 4-mode UI, MPP hardware video decoding, ALSA audio playback, touch gesture recognition, and FIFO-based remote control.

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                      UI Layer (LVGL v8)                   │
│   dashboard.c — 4-mode rendering engine, post_draw_cb     │
│   ui_config.h — layout macros, button hotspots            │
├──────────────────────────────────────────────────────────┤
│                    Display Layer (Framebuffer)             │
│   /dev/fb0 — 1024×600 BGRA8888, virtual 1024×1200        │
│   single-buffer local refresh, FBIOPAN page flip          │
├──────────────────────┬───────────────────────────────────┤
│   Media Layer (MPP)   │   Media Layer (ALSA)              │
│   video_ctrl.c        │   audio_ctrl.c                    │
│   aic_player H.264    │   snd_pcm WAV playback            │
│   FBIOPAN双缓冲翻页     │   pthread异步播放                 │
├──────────────────────┴───────────────────────────────────┤
│                   Driver Layer (Linux Kernel)              │
│   aic-mpp / gt9xx / snd-usb-audio / rt2800usb / mmc       │
├──────────────────────────────────────────────────────────┤
│                    Hardware Layer                          │
│   ArtInChip D211 SoC (RISC-V 64bit, 600MHz, 128MB DDR3)   │
│   1024×600 LCD + GT9xx Touch + RT5572 WiFi + SD Card      │
└──────────────────────────────────────────────────────────┘
```

## Hardware

| Component | Specification |
|-----------|--------------|
| SoC | ArtInChip D211, RISC-V 64bit (RV64IMAFDC), 600MHz single-core |
| RAM | 128MB DDR3 (on-chip) |
| Display | 1024×600 BGRA8888, virtual 1024×1200 (double-buffered) |
| Touch | Goodix GT9xx I2C3 (`/dev/input/event0`) |
| Audio | aicSoundCard (ALSA, `/dev/snd/pcmC0D0p`) |
| WiFi | RT5572 USB (rt2800usb, wlan0) |
| Storage | SPI NAND + 64GB SD card (`/mnt/sdcard`) |
| Dev Board | D213ECV-DEMO-V4-0 |

> **TODO**: Add wiring diagram / pinout table for display, touch, and audio connections.

## Features

### Mode 0: Dashboard `[*]`
- [x] 4 analog gauges (speed, RPM, fuel, coolant temp)
- [x] Central info display (digital speed, clock, fuel economy, odometer)
- [x] 3-page navigation with touch arrows
- [x] 7-speed DSG transmission physics model
- [x] 2.4s self-test sweep animation (30FPS)
- [x] Virtual buttons: turn signals, gear selector, throttle
- [x] Ignition state machine: OFF → SELF_TEST → START

### Mode 1: Music Player `[*]`
- [x] WAV file scanning from SD card
- [x] ALSA PCM playback with real-time header parsing
- [x] Playback controls: `<<` prev / `▶` play/pause / `>>` next
- [x] Real-time progress bar with elapsed/total time display
- [x] Auto-advance to next track
- [x] Supports 8/16/24/32bit, 1-8 channels, arbitrary sample rates
- [ ] MP3/FLAC software decode support

### Mode 2: Video Player `[*]`
- [x] MPP hardware-accelerated H.264 decoding (aic_player API)
- [x] FBIOPAN double-buffer page flipping for seamless video overlay
- [x] Touch bottom 1/6 area to exit playback
- [x] 120-second timeout protection
- [x] First-frame timeout with graceful LVGL recovery
- [ ] True pause/resume (aic_player lacks API)
- [ ] Video progress bar (MPP lacks duration API)

### Mode 3: WiFi Panel `[*]`
- [x] SSID, IP address, signal strength (dBm)
- [x] Connection rate and frequency band display
- [x] 2Hz auto-refresh
- [ ] Hotspot mode support

### System `[*]`
- [x] FIFO command pipe (`/tmp/dash_fifo`) for remote control
- [x] Touch state machine: IDLE → ACTIVE → RELEASE → HANDLED
- [x] SD card mount-timing retry (3 attempts, 1s interval)
- [x] Boot-time stdout real-time logging (non-buffered)
- [ ] OTA firmware update
- [ ] CAN bus integration

## Build & Deploy

### Prerequisites

| Dependency | Version |
|------------|---------|
| RISC-V cross compiler | `riscv64-unknown-linux-gnu-gcc` V2.10.1 |
| LVGL | v8.3.10 |
| Linux kernel | 5.10.44 (Buildroot) |
| Target libraries | `libmedia_player.so`, `libasound.so`, `libmpp_decoder.so` |

### Compile

```bash
# On Ubuntu 18.04 VM (cross-compile for RISC-V)
cd ~/lv_sim
make -f Makefile.d213 -j2
# Output: dashboard_d213 (~503KB, dynamically linked)
```

### Deploy to Board

```bash
# Start HTTP server on VM
cd ~/lv_sim
python3 -m http.server 8888 &

# Download and install on board via ADB
adb shell "killall dashboard_d213; sleep 1; \
  wget -q -O /usr/local/bin/dashboard_d213 http://192.168.0.72:8888/dashboard_d213 && \
  sync && reboot"

# Verify
adb shell "md5sum /usr/local/bin/dashboard_d213"
```

> **⚠️ Always `sync` before `reboot`** — the binary may not be written to flash otherwise.

### Auto-Start

The board runs Buildroot Linux with BusyBox ash. The dashboard starts automatically via:

```bash
# /etc/init.d/S00lvgl
dashboard_d213 < /dev/zero
```

> **Note**: stdin must be `/dev/zero` (FIFO reads from `/tmp/dash_fifo`, not stdin).

## Project Structure

```
~/lv_sim/
├── main_fb.c              ~640 lines    Entry point, framebuffer init, touch, FIFO, main loop
├── Makefile.d213            30 lines    Cross-compile Makefile (dynamic linking)
├── ui_config.h              40 lines    Layout macros, state machine constants
├── lv_conf.h                            LVGL 32bit color depth, 256KB memory pool
├── dashboard/
│   ├── dashboard.h/c                  4-mode rendering engine, post_draw_cb, PLAY/STOP, status lines
│   ├── video_ctrl.h/c                 aic_player, FBIOPAN page flip, first-frame timeout, touch-to-exit
│   ├── audio_ctrl.h/c                 ALSA snd_pcm, WAV parser, pthread playback, retry logic
│   ├── wifi_ctrl.h/c                  WiFi scan, reconnect, signal monitoring
│   └── vehicle_physics.h/c            7-speed DSG physics model
└── lvgl/                              LVGL v8.3.10
```

## Directory Structure (Recommended Refactoring)

> **TODO**: Migrate to this layered structure for better maintainability.

```
d213-dashboard/
├── drivers/                # Hardware abstraction
│   ├── display_fb.c        # Framebuffer init, mmap, FBIOPAN
│   ├── touch_evdev.c       # evdev touch event parser
│   └── audio_alsa.c        # ALSA PCM wrapper
├── ui/                     # LVGL rendering
│   ├── ui_main.c           # Main UI loop
│   ├── ui_dashboard.c      # Mode 0: gauges, dials
│   ├── ui_music.c          # Mode 1: playlist, controls
│   ├── ui_video.c          # Mode 2: video player UI
│   └── ui_wifi.c           # Mode 3: WiFi panel
├── media/                  # Multimedia decoders
│   ├── video_mpp.c         # MPP H.264 decoder wrapper
│   └── audio_wav.c         # WAV file parser
├── core/                   # System core
│   ├── main.c              # Entry point, main loop
│   ├── state_machine.c     # Touch & mode state machine
│   └── fifo_cmd.c          # FIFO command dispatcher
├── configs/                # Configuration
│   ├── ui_config.h         # Layout, colors, hotspots
│   └── lv_conf.h           # LVGL config
├── scripts/                # Build & deploy scripts
│   └── deploy.sh
├── docs/                   # Documentation
├── Makefile                # Build system
└── README.md
```

## Performance

| Metric | Value |
|--------|-------|
| Binary size | 503 KB (stripped) |
| Runtime RSS | ~3.9 MB |
| Idle FPS | 54 |
| Video decode FPS | 93 (1280×720 H.264) |
| Boot time | < 3 seconds |
| Free memory | 74 MB / 119 MB |
| Code size | ~15,000 lines C |

## FIFO Commands

The dashboard accepts real-time commands via `/tmp/dash_fifo`:

```bash
# ── Global ──
echo q > /tmp/dash_fifo    # Mode 0 (Dashboard)
echo w > /tmp/dash_fifo    # Mode 1 (Music)
echo e > /tmp/dash_fifo    # Mode 2 (Video)
echo r > /tmp/dash_fifo    # Mode 3 (WiFi)
echo d > /tmp/dash_fifo    # Toggle ignition
echo h > /tmp/dash_fifo    # Shift gear (P→N→D→P)
echo y > /tmp/dash_fifo    # Throttle ON
echo n > /tmp/dash_fifo    # Throttle OFF
echo 1 > /tmp/dash_fifo    # Dashboard page 1
echo 2 > /tmp/dash_fifo    # Dashboard page 2
echo 3 > /tmp/dash_fifo    # Dashboard page 3
echo [ > /tmp/dash_fifo    # Left turn signal
echo ] > /tmp/dash_fifo    # Right turn signal

# ── Mode 1 (Music) ──
echo b > /tmp/dash_fifo    # Play/Stop toggle
echo z > /tmp/dash_fifo    # Previous track (wrap to last)
echo x > /tmp/dash_fifo    # Next track (wrap to first)
echo p > /tmp/dash_fifo    # Stop
echo c > /tmp/dash_fifo    # Rescan music directory

# ── Mode 2 (Video) ──
echo p > /tmp/dash_fifo    # Stop video
echo c > /tmp/dash_fifo    # Rescan video directory
```

## Known Limitations

| Issue | Root Cause | Workaround |
|-------|-----------|------------|
| No true video pause | aic_player lacks pause/resume API | Stop and restart |
| Audio: WAV only | No software MP3/FLAC decoder integrated | Convert to WAV first |
| No video progress bar | MPP lacks duration API | Architecture constraint |
| Chinese filenames garbled | Montserrat font lacks CJK glyphs | Rename files to ASCII |
| No SD card hot-plug | No inotify on /mnt/sdcard | Reboot after card swap |

## Bug Fixes (v6.0 → v6.4)

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| SD card empty list at boot | Scan runs before SD mount | 3-retry with 1s sleep |
| Log not visible via ADB | stdout fully buffered | `setbuf(stdout, NULL)` |
| Video never displays | `g_fb_yres = 0` never assigned | `g_fb_yres = vinfo.yres` |
| Video stops after 2s | `g_first_video_frame` always 0 | Check `video_playing` instead |
| Can't exit video playback | No touch-to-exit logic | `goto syn_report_end` on bottom 1/6 touch |

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

## Demo

> **TODO**: Add screen recordings or GIFs for each mode:
> - [ ] Boot animation (2.4s self-test sweep)
> - [ ] Mode 0 → Mode 3 switching
> - [ ] Music playback with progress bar
> - [ ] Video playback with touch-to-exit

## Contributing

This project follows [GitHub Flow](https://guides.github.com/introduction/flow/).

1. Fork the repository
2. Create a feature branch (`git checkout -b feat/your-feature`)
3. Make your changes with clear commit messages
4. Push and open a Pull Request

### Commit Convention

```
<type>: <short description>

<type>: feat | fix | docs | refactor | test | chore
```

## License

[Apache-2.0](LICENSE) — Copyright (c) 2026 langoogg

---

**Acknowledgments**: [ArtInChip](https://www.artinchip.com/) for the D21x SDK, [LVGL](https://lvgl.io/) for the embedded GUI framework, and the RISC-V open-source community.
