# Code Style Guide — d213-dashboard

## 1. Naming Conventions

| Element | Style | Example |
|---------|-------|---------|
| Function | `snake_case` | `video_play()`, `audio_stop()` |
| Global variable | `g_` prefix | `g_mode`, `g_fb_yres` |
| Static variable | `s_` prefix | `s_pcm_handle` |
| Module variable | module name prefix | `audio_playing`, `video_count` |
| Macro / Constant | `UPPER_CASE` | `SCREEN_WIDTH`, `AUDIO_LIST_Y_MAX` |
| Type / Enum | `snake_case_t` | `touch_state_t` |
| Struct | `snake_case` | `struct audio_ctx` |

### File naming

```
ui_dashboard.c   — snake_case, module prefix
ui_dashboard.h
```

## 2. Header Files

### Include guard

```c
#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

// ... declarations ...

#endif /* UI_DASHBOARD_H */
```

### Include order

```c
#include "configs/ui_config.h"    // Project config always first
#include <stdio.h>                 // Standard library
#include <stdlib.h>
#include "lvgl/lvgl.h"            // External libraries
#include "drivers/display_fb.h"   // Internal headers
```

## 3. Comments — Doxygen Format

```c
/**
 * @brief Play a video file using MPP hardware decoder.
 *
 * Pauses LVGL rendering, opens aic_player, and flips
 * the FBIOPAN display to the back buffer for video output.
 *
 * @param idx  Index into the video_files array (0-based).
 *
 * @note  Caller must ensure video_count > 0 before calling.
 * @note  FBIOPAN recovery is handled by video_ctrl_tick() on timeout.
 */
void video_play(int idx);
```

## 4. Memory Management

### Rules for embedded (128MB RAM)

- **No `malloc` in hot paths.** Allocate at init, never free.
- **Prefer static arrays over dynamic allocation.** `char files[MAX_FILES][MAX_PATH]`.
- **Every `malloc` must have a corresponding `free` in the same module.**
- **Check return value.** `if (!ptr) { LOG_E("OOM"); return -1; }`.
- **LVGL memory pool:** use `lv_mem_alloc()` / `lv_mem_free()` for LVGL objects, not raw `malloc`.

```c
// ✅ OK
static lv_obj_t *scr = NULL;
scr = lv_obj_create(NULL);

// ❌ Avoid
lv_obj_t *scr = lv_mem_alloc(sizeof(lv_obj_t));  // LVGL manages its own memory
```

## 5. LVGL-Specific Rules

- **Use `post_draw_cb` for custom drawing**, not widgets, when performance matters.
- **Always call `lv_obj_invalidate()`** after changing visual state.
- **Touch coordinates use `ui_config.h` macros**, never hardcoded numbers.
- **`lv_timer_handler()` runs in the main loop**, not in a separate thread.

```c
// ✅ OK
btn_area = (lv_area_t){BTN_PLAY_X0, BTN_Y0, BTN_PLAY_X1, BTN_Y1};

// ❌ Avoid
btn_area = (lv_area_t){462, 530, 562, 570};
```

## 6. Error Handling

```c
// Check every system call
DIR *d = opendir(path);
if (!d) {
    LOG_E("Cannot open directory: %s (attempt %d/%d)", path, attempt, max_attempts);
    sleep(1);
    continue;
}

// ALSA: check every step
if (snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
    LOG_E("ALSA open failed");
    return -1;
}
```

## 7. Build System

- `-Wall -Wextra -Werror` for all commits going to `main`.
- `-O2` for release, `-O0 -g` for debug / simulator.
- `-march=rv64imafdc -mabi=lp64d` for RISC-V cross-compile.
