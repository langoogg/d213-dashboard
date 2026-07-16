# Migration Guide: Flat → Layered Structure

## Before (Current)

```
~/lv_sim/
├── main_fb.c              640 lines — everything mixed
├── dashboard/
│   ├── dashboard.c        700 lines — rendering
│   ├── video_ctrl.c       380 lines — video
│   ├── audio_ctrl.c       300 lines — audio
│   ├── wifi_ctrl.c        200 lines — wifi
│   └── vehicle_physics.c  100 lines — physics
├── Makefile.d213
├── ui_config.h
└── lv_conf.h
```

## After (Target)

```
d213-dashboard/
├── drivers/
│   ├── display_fb.c       Framebuffer init, mmap, FBIOPAN
│   ├── touch_evdev.c      evdev touch parser, state machine
│   └── audio_alsa.c       ALSA PCM open/config/play
├── ui/
│   ├── ui_main.c          LVGL init, main loop, mode dispatch
│   ├── ui_dashboard.c     Mode 0 — gauges, dials, self-test
│   ├── ui_music.c         Mode 1 — playlist, controls, progress
│   ├── ui_video.c         Mode 2 — video playback UI
│   └── ui_wifi.c          Mode 3 — WiFi panel
├── media/
│   ├── video_mpp.c        MPP decoder wrapper
│   └── audio_wav.c        WAV file parser
├── core/
│   ├── main.c             Entry point
│   ├── state_machine.c    Touch & mode state machine
│   └── fifo_cmd.c         FIFO command dispatcher
├── configs/
│   ├── ui_config.h
│   └── lv_conf.h
├── scripts/
│   └── deploy.sh
├── docs/
├── Makefile
└── README.md
```

## Migration Steps

### Step 1: Create directory structure

```bash
cd ~/lv_sim
mkdir -p drivers ui media core configs scripts docs
```

### Step 2: Move driver code from main_fb.c

```bash
# Extract Framebuffer init (~80 lines) → drivers/display_fb.c
# Extract touch evdev parser (~120 lines) → drivers/touch_evdev.c
# Extract ALSA layer from audio_ctrl.c → drivers/audio_alsa.c
```

### Step 3: Split dashboard.c by mode

```bash
# dashboard.c → ui/ui_dashboard.c + ui/ui_music.c + ui/ui_video.c + ui/ui_wifi.c
# Keep shared drawing utilities in ui/ui_main.c
```

### Step 4: Extract media wrappers

```bash
# video_ctrl.c MPP calls → media/video_mpp.c
# audio_ctrl.c WAV parser → media/audio_wav.c
```

### Step 5: Create core/main.c as thin entry point

```c
// core/main.c — delegates to modules, ~30 lines
int main(void) {
    display_init();
    touch_init();
    lvgl_init();
    fifo_init();
    while (1) {
        fifo_poll();
        touch_process();
        lv_timer_handler();
        media_tick();
    }
}
```

### Step 6: Update Makefile

Replace `Makefile.d213` with the new layered `Makefile` (see Makefile in this directory).

### Step 7: Verify builds

```bash
make           # Cross-compile for board
make sim       # Build PC simulator
make clean     # Clean all
```

## File Mapping Table

| Old File | Old Lines | → | New File(s) | Notes |
|----------|-----------|----|-------------|-------|
| main_fb.c | 640 | → | core/main.c + drivers/display_fb.c + drivers/touch_evdev.c + core/fifo_cmd.c | Split by concern |
| dashboard.c | 700 | → | ui/ui_dashboard.c + ui/ui_music.c + ui/ui_video.c + ui/ui_wifi.c | Split by mode |
| video_ctrl.c | 380 | → | drivers/display_fb.c(FBIOPAN) + media/video_mpp.c | Separate MPP from display |
| audio_ctrl.c | 300 | → | drivers/audio_alsa.c + media/audio_wav.c | Separate ALSA from parser |
| wifi_ctrl.c | 200 | → | ui/ui_wifi.c | Move to UI layer |
| vehicle_physics.c | 100 | → | ui/ui_dashboard.c (inline) | Dashboard-specific |
| Makefile.d213 | 30 | → | Makefile | New build system |
| ui_config.h | 40 | → | configs/ui_config.h | Move to configs/ |
