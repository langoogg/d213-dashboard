/* main_fb.c — D213 v4: fixed 16ms tick, no GPIO/WiFi */
#include "lvgl/lvgl.h"
#include "ui_config.h"

/* ---- 防御性 TOUCH_LOG ---- */
#ifndef TOUCH_LOG
#ifdef TOUCH_DEBUG
#define TOUCH_LOG(fmt, ...) printf("[TOUCH] " fmt "\n", ##__VA_ARGS__)
#else
#define TOUCH_LOG(fmt, ...) ((void)0)
#endif
#endif

/* ---- 播放错误标志 ---- */
volatile int g_audio_error = 0;
volatile int g_video_error = 0;

/* ---- last_played_idx 已在 audio_ctrl.c 定义，此处仅 extern 引用 ---- */
extern volatile int last_played_idx;

/* ---- 列表滚动偏移（定义在 dashboard.c，FIFO 翻页命令使用） ---- */
extern int audio_list_offset;

#define AUDIO_PLAY_RETURNS_VOID 1

/* ---- audio_play() 兼容层 ---- */
static inline int audio_play_wrapper(int idx) {
    audio_play(idx);
    return 0;
}

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <stdio.h>
#include <errno.h>
#include "dashboard/dashboard.h"
#include "dashboard/vehicle_physics.h"

/* forward declaration */
static inline void handle_key_press(int ki);
#include <stdatomic.h>  /* __atomic_* */

/* ============================================================
 * 全局辅助宏
 * ============================================================ */

/* ---- 安全绝对值 */
#define SAFE_ABS(x) ((x) < 0 ? -(x) : (x))

/* 无效坐标值 */
#define TOUCH_COORD_INVALID (-1)

/* ---- 调试日志（默认关闭） ---- */
#ifdef TOUCH_DEBUG
#define TOUCH_LOG(fmt, ...) printf("[TOUCH] " fmt "\n", ##__VA_ARGS__)
#else
#define TOUCH_LOG(fmt, ...) do {} while(0)
#endif

/* ---- 热区名称条件编译 ---- */
#ifdef TOUCH_DEBUG
#define ZONE_NAME(n) n
#else
#define ZONE_NAME(n) ""
#endif

/* ============================================================
 * 原子油门状态
 * ============================================================ */
static int g_throttle_atomic = 0;

static inline void set_throttle(int val) {
    __atomic_store_n(&g_throttle_atomic, val, __ATOMIC_RELAXED);
}

static inline int get_throttle(void) {
    return __atomic_load_n(&g_throttle_atomic, __ATOMIC_RELAXED);
}

/* ============================================================
 * 触摸状态机
 * ============================================================ */
typedef enum {
    TOUCH_IDLE = 0,
    TOUCH_PENDING,
    TOUCH_ACTIVE,
    TOUCH_HANDLED
} touch_state_t;

static touch_state_t touch_state = TOUCH_IDLE;
static int touch_start_x = TOUCH_COORD_INVALID;
static int touch_start_y = TOUCH_COORD_INVALID;
static int touch_cur_x = 0;
static int touch_cur_y = 0;

/* ============================================================
 * g_mode 安全访问封装
 * ============================================================ */
static inline void set_g_mode(int mode) {
    if (mode < 0 || mode > 3) {
        TOUCH_LOG("Invalid g_mode %d, clamping to 0", mode);
        mode = 0;
    }
    g_mode = mode;
}

/* g_mode <-> g_page 显式映射表 */
static const int mode_page_map[] = {0, 1, 2, 3};
STATIC_ASSERT(sizeof(mode_page_map) / sizeof(mode_page_map[0]) == 4,
              "mode_page_map must have 4 entries");

/* ============================================================
 * 底层动作封装
 * ============================================================ */
static inline void gear_shift(void) {
    handle_key_press(1);
}

static inline void turn_left(void) {
    handle_key_press(3);
}

static inline void turn_right(void) {
    handle_key_press(4);
}

static inline void throttle_on(void) {
    set_throttle(1);
}

static inline void throttle_off(void) {
    set_throttle(0);
}

/* ============================================================
 * 热区动作枚举
 * ============================================================ */
typedef enum {
    ZONE_ARROW_PREV = -2,
    ZONE_ARROW_NEXT = -1,
    ZONE_GEAR       = 1,
    ZONE_L_TURN     = 3,
    ZONE_R_TURN     = 4,
    ZONE_THR_ON     = 5,
    ZONE_THR_OFF    = 6
} zone_action_t;

/* ============================================================
 * 热区配置表
 * ============================================================ */
typedef struct {
    int x0, x1, y0, y1;
    zone_action_t action;
    const char * const name;
} hotzone_t;

static const hotzone_t mode0_zones[] = {
    {ARROW_L_X0, ARROW_L_X1, ARROW_Y0, ARROW_Y1, ZONE_ARROW_PREV, ZONE_NAME("ArrowL")},
    {ARROW_R_X0, ARROW_R_X1, ARROW_Y0, ARROW_Y1, ZONE_ARROW_NEXT, ZONE_NAME("ArrowR")},
    {BTN_L_X0, BTN_L_X1, BTN_ROW1_Y0, BTN_ROW1_Y1, ZONE_L_TURN, ZONE_NAME("L")},
    {BTN_PND_X0, BTN_PND_X1, BTN_ROW1_Y0, BTN_ROW1_Y1, ZONE_GEAR, ZONE_NAME("PND")},
    {BTN_R_X0, BTN_R_X1, BTN_ROW1_Y0, BTN_ROW1_Y1, ZONE_R_TURN, ZONE_NAME("R")},
    {BTN_PLUS_X0, BTN_PLUS_X1, BTN_ROW2_Y0, BTN_ROW2_Y1, ZONE_THR_ON, ZONE_NAME("Plus")},
    {BTN_MINUS_X0, BTN_MINUS_X1, BTN_ROW2_Y0, BTN_ROW2_Y1, ZONE_THR_OFF, ZONE_NAME("Minus")},
};

enum { MODE0_ZONE_COUNT = sizeof(mode0_zones) / sizeof(mode0_zones[0]) };
STATIC_ASSERT(MODE0_ZONE_COUNT > 0, "mode0_zones must not be empty");

/* ============================================================
 * 热区检测
 * ============================================================ */
static int hit_test_zone(int x, int y, const hotzone_t *zone) {
    return (x >= zone->x0 && x <= zone->x1 &&
            y >= zone->y0 && y <= zone->y1);
}

/* ============================================================
 * 热区动作执行
 * ============================================================ */
static int execute_zone_action(const hotzone_t *zone, lv_obj_t *scr) {
    switch (zone->action) {
        case ZONE_ARROW_PREV:
            dashboard_next_page(scr, -1);
            break;
        case ZONE_ARROW_NEXT:
            dashboard_next_page(scr, 1);
            break;
        case ZONE_L_TURN:
            turn_left();
            break;
        case ZONE_R_TURN:
            turn_right();
            break;
        case ZONE_GEAR:
            gear_shift();
            break;
        case ZONE_THR_ON:
            throttle_on();
            break;
        case ZONE_THR_OFF:
            throttle_off();
            break;
        default:
            TOUCH_LOG("Unknown zone action %d", zone->action);
            return 0;
    }

    lv_obj_invalidate(scr);
    touch_state = TOUCH_HANDLED;
    TOUCH_LOG("Zone %s triggered", zone->name);
    return 1;
}


int fb_fd = -1, fifo_fd = -1;
unsigned char *fbp = NULL;
int g_fb_yres = 0;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static uint8_t *fb_mem;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;

/* --- FPS counter --- */
static struct timeval g_last_fps;
static int g_frame_count = 0;

extern void dashboard_start_self_test(lv_obj_t *scr);
extern void dashboard_self_test_tick(void);
extern int g_turn_left, g_turn_right;

static void fb_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cp) {
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    for (int y = 0; y < h; y++) {
        uint8_t *src = (uint8_t *)(cp + y * w);
        uint8_t *dst = fb_mem + (area->y1 + y) * finfo.line_length + area->x1 * 4;
        memcpy(dst, src, w * 4);
    }
    lv_disp_flush_ready(drv);
}

VehiclePhysics phy;
volatile int pending_audio_idx = -1;
volatile int pending_video_idx = -1;
int g_key_pressed_idx = -1;
uint32_t g_key_press_time = 0;
volatile int g_turn_blink = 0;

static void timer_cb(lv_timer_t *t) {
    lv_obj_t *scr = (lv_obj_t *)t->user_data;
    if (g_ign_state == IGN_START && phy.engine_on) {
        phy.throttle = g_throttle;
        phy.gear = g_gear;
        vehicle_physics_update(&phy, 0.02);
        lv_obj_invalidate(scr);
    }
}

static inline void handle_key_press(int ki)
{
    switch (ki) {
        case 0:
            if (g_ign_state == IGN_OFF) g_ign_state = IGN_SELF_TEST;
            else if (g_ign_state == IGN_START) {
                g_ign_state = IGN_OFF; g_gear = 0; g_throttle = 0;
                g_turn_left = 0; g_turn_right = 0; g_turn_blink = 0;
            }
            break;
        case 1:
            if (g_ign_state == IGN_START) g_gear = (g_gear + 1) % 3;
            break;
        case 2:
            if (g_ign_state == IGN_START) g_throttle = !g_throttle;
            else g_throttle = 0;
            break;
        case 3:
            g_turn_left = !g_turn_left;
            if (g_turn_left) g_turn_right = 0;
            g_turn_blink = 1;
            break;
        case 4:
            g_turn_right = !g_turn_right;
            if (g_turn_right) g_turn_left = 0;
            g_turn_blink = 1;
            break;
    }
}

int main(void) {
    setbuf(stdout, NULL); setbuf(stderr, NULL);
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("fb"); return 1; }
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    g_fb_yres = vinfo.yres;
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    fb_mem = mmap(NULL, finfo.smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);
    audio_scan("/mnt/sdcard/music/");
    video_scan_dir();
    if (fb_mem == MAP_FAILED) { perror("mmap"); return 1; }

    lv_init();
    buf1 = malloc(vinfo.xres * vinfo.yres * sizeof(lv_color_t));
    if (!buf1) { return 1; }
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, vinfo.xres * vinfo.yres);

    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = vinfo.xres; dd.ver_res = vinfo.yres;
    dd.flush_cb = fb_flush; dd.draw_buf = &draw_buf;
    lv_obj_t *scr = lv_disp_drv_register(&dd)->act_scr;
    lv_timer_create(timer_cb, 20, scr);

    vehicle_physics_init(&phy); phy.engine_on = 1;
    dashboard_init(scr, &phy);

    unlink("/tmp/dash_fifo"); mkfifo("/tmp/dash_fifo", 0666);
    fifo_fd = open("/tmp/dash_fifo", O_RDONLY | O_NONBLOCK);

    int touch_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);


    dashboard_start_self_test(scr);
    printf("Entering main loop\n");
    static int last_ign = IGN_OFF;

    while (1) {
        static int last_mode = -1;
        if (g_mode != last_mode) {
            g_key_pressed_idx = -1;
            last_mode = g_mode;
        }
        lv_tick_inc(16);
        lv_timer_handler();
        /* ---- 音频/视频 tick ---- */
        audio_ctrl_tick();
        video_ctrl_tick();
        dashboard_self_test_tick();

        /* ---- 消费 pending 播放请求 ---- */
        if (pending_audio_idx >= 0) {
            audio_play(pending_audio_idx);
            pending_audio_idx = -1;
        }
        if (pending_video_idx >= 0) {
            video_play(pending_video_idx);
            pending_video_idx = -1;
        }

        /* --- FPS counter: print every 1 second --- */
        {
            struct timeval now;
            gettimeofday(&now, NULL);
            if (g_last_fps.tv_sec == 0) g_last_fps = now;
            g_frame_count++;
            double elapsed = (now.tv_sec - g_last_fps.tv_sec) +
                             (now.tv_usec - g_last_fps.tv_usec) / 1000000.0;
            if (elapsed >= 1.0) {
                double fps = g_frame_count / elapsed;
                printf("FPS: %.1f  state=%d\n", fps, g_ign_state);
                g_frame_count = 0;
                g_last_fps = now;
            }
        }

        if (g_ign_state == IGN_START && last_ign == IGN_SELF_TEST) {
            phy.rpm = 800; phy.speed = 0;
            g_turn_left = g_turn_right = 0; g_throttle = 0;
        }
        last_ign = g_ign_state;

        if (touch_fd >= 0) {
            struct input_event ev;
            while (read(touch_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_POSITION_X) touch_cur_x = ev.value;
                    else if (ev.code == ABS_MT_POSITION_Y) touch_cur_y = ev.value;
                } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                    if (ev.value == 1) {  /* 按下 */
                        touch_state = TOUCH_PENDING;
                        TOUCH_LOG("Touch down");
                    } else if (ev.value == 0) {  /* 释放 */
                        TOUCH_LOG("Touch up");
                        touch_state = TOUCH_IDLE;
                        touch_start_x = TOUCH_COORD_INVALID;
                        touch_start_y = TOUCH_COORD_INVALID;
                    }
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    if (touch_state == TOUCH_PENDING) {
                        touch_start_x = touch_cur_x;
                        touch_start_y = touch_cur_y;
                        touch_state = TOUCH_ACTIVE;
                    }

                    int zone_hit = 0;
        /* video playback: touch bottom 1/6 to exit */
        if (video_playing && touch_state == TOUCH_ACTIVE) {
            if (touch_cur_y > g_fb_yres * 5 / 6) {
                video_stop();
                zone_hit = 1;
                touch_state = TOUCH_HANDLED;
                lv_obj_invalidate(lv_scr_act());
                goto syn_report_end;
            }
        }


                    /* 滑动检测(所有模式) */
                    if (touch_state == TOUCH_ACTIVE) {
                        int dx = touch_cur_x - touch_start_x;
                        int dy = touch_cur_y - touch_start_y;
                        if (abs(dx) > 100 && abs(dx) > abs(dy)) {
                            int new_mode = (dx > 0) ? (g_mode + 3) % 4 : (g_mode + 1) % 4;
                            g_mode = new_mode;
                            g_page = mode_page_map[g_mode];
                            dashboard_set_page(scr, g_page);
                            lv_obj_invalidate(scr);
                            touch_state = TOUCH_HANDLED;
                            zone_hit = 1;
                        }
                    }

                    /* Mode 0 热区检测 */
                    if (!zone_hit && g_mode == 0 && g_ign_state == IGN_START && (touch_state == TOUCH_ACTIVE || touch_state == TOUCH_HANDLED)) {
                        int i;
                        for (i = 0; i < MODE0_ZONE_COUNT; i++) {
                            if (hit_test_zone(touch_cur_x, touch_cur_y, &mode0_zones[i])) {
                                g_key_pressed_idx = i;
                                g_key_press_time = lv_tick_get();
                                zone_hit = execute_zone_action(&mode0_zones[i], scr);
                                break;
                            }
                        }
                    }

                    /* Mode 1 音频列表 */
                    if (!zone_hit && g_mode == 1 && touch_state == TOUCH_ACTIVE) {
    /* ============================================================
     * Mode 1 音频：列表点击 + 底部控制按钮
     * ============================================================ */
    if (!zone_hit && g_mode == 1 && touch_state == TOUCH_ACTIVE) {
        if (touch_cur_y >= LIST_Y_START && touch_cur_y <= AUDIO_LIST_Y_MAX &&
            touch_cur_x >= LIST_X0 && touch_cur_x <= LIST_X1) {
            int item_idx = (touch_cur_y - LIST_Y_START) / AUDIO_ROW_HEIGHT;
            if (item_idx >= 0 && item_idx < audio_count) {
                pending_audio_idx = item_idx;
                zone_hit = 1;
                touch_state = TOUCH_HANDLED;
                lv_obj_invalidate(scr);
                TOUCH_LOG("Audio list item %d", item_idx);
            }
        } else if (touch_cur_y >= BTN_Y0 && touch_cur_y <= BTN_Y1) {
            if (touch_cur_x >= BTN_PREV_X0 && touch_cur_x <= BTN_PREV_X1) {
                pending_audio_idx = (audio_current_idx > 0) ? audio_current_idx - 1 : audio_count - 1;
                zone_hit = 1;
                touch_state = TOUCH_HANDLED;
                lv_obj_invalidate(scr);
                TOUCH_LOG("Audio prev -> %d", pending_audio_idx);
            } else if (touch_cur_x >= BTN_PLAY_X0 && touch_cur_x <= BTN_PLAY_X1) {
                if (audio_playing) audio_stop();
                else {
                    int target = (last_played_idx >= 0) ? last_played_idx : 0;
                    if (audio_play_wrapper(target) != 0) g_audio_error = 1;
                }
                zone_hit = 1;
                touch_state = TOUCH_HANDLED;
                lv_obj_invalidate(scr);
                TOUCH_LOG("Audio play/stop toggle");
            } else if (touch_cur_x >= BTN_NEXT_X0 && touch_cur_x <= BTN_NEXT_X1) {
                pending_audio_idx = (audio_current_idx < audio_count - 1) ? audio_current_idx + 1 : 0;
                zone_hit = 1;
                touch_state = TOUCH_HANDLED;
                lv_obj_invalidate(scr);
                TOUCH_LOG("Audio next -> %d", pending_audio_idx);
            }
        }
    }
                    }

                    /* Mode 2 视频列表 */
                    if (!zone_hit && g_mode == 2 && touch_state == TOUCH_ACTIVE) {
    /* ============================================================
     * Mode 2 视频：列表点击 + STOP 按钮
     * ============================================================ */
    if (!zone_hit && g_mode == 2 && touch_state == TOUCH_ACTIVE) {
        if (touch_cur_y >= LIST_Y_START && touch_cur_y <= VIDEO_LIST_Y_MAX &&
            touch_cur_x >= LIST_X0 && touch_cur_x <= LIST_X1) {
            int item_idx = (touch_cur_y - LIST_Y_START) / VIDEO_ROW_HEIGHT;
            if (item_idx >= 0 && item_idx < video_count) {
                pending_video_idx = item_idx;
                zone_hit = 1;
                touch_state = TOUCH_HANDLED;
                lv_obj_invalidate(scr);
                TOUCH_LOG("Video list item %d", item_idx);
            }
        } else if (touch_cur_y >= BTN_Y0 && touch_cur_y <= BTN_Y1 &&
                   touch_cur_x >= BTN_STOP_X0 && touch_cur_x <= BTN_STOP_X1) {
            video_stop();
            zone_hit = 1;
            touch_state = TOUCH_HANDLED;
            lv_obj_invalidate(scr);
            TOUCH_LOG("Video stop");
        }
    }
                    }
                }
            }
        }

        if (fifo_fd >= 0) {
            char c;
        if (fifo_fd >= 0) {
            char c;
            while (read(fifo_fd, &c, 1) > 0) {
                int handled = 0;

                /* ---- 全局命令 ---- */
                switch (c) {
                    case 'q': g_mode = 0; handled = 1; break;
                    case 'w': g_mode = 1; handled = 1; break;
                    case 'e': g_mode = 2; handled = 1; break;
                    case 'r': g_mode = 3; handled = 1; break;
                    case 'd':
                        if (g_ign_state == IGN_OFF) {
                            g_ign_state = IGN_START;
                            phy.rpm = 800; phy.speed = 0;
                        } else {
                            g_ign_state = IGN_OFF;
                            phy.rpm = 0; phy.speed = 0;
                            g_gear = 0;
                            g_turn_left = g_turn_right = 0;
                            g_throttle = 0;
                        }
                        handled = 1;
                        break;
                    case 'h':
                        if (g_ign_state == IGN_START) g_gear = (g_gear + 1) % 3;
                        handled = 1;
                        break;
                    case 'y': g_throttle = 1; handled = 1; break;
                    case 'n': g_throttle = 0; handled = 1; break;
                    case '1': g_page = 0; handled = 1; break;
                    case '2': g_page = 1; handled = 1; break;
                    case '3': g_page = 2; handled = 1; break;
                }

                if (handled) {
                    dashboard_set_page(scr, g_page);
                    lv_obj_invalidate(scr);
                    continue;
                }

                /* ---- Mode 1 音频 ---- */
                if (g_mode == 1) {
                    switch (c) {
                        case 'z':
                            pending_audio_idx = (audio_current_idx > 0) ? audio_current_idx - 1 : audio_count - 1;
                            handled = 1;
                            break;
                        case 'x':
                            pending_audio_idx = (audio_current_idx < audio_count - 1) ? audio_current_idx + 1 : 0;
                            handled = 1;
                            break;
                        case 'b':
                            if (audio_playing) audio_stop();
                            else {
                                int target = (last_played_idx >= 0) ? last_played_idx : 0;
                                if (audio_play_wrapper(target) != 0) g_audio_error = 1;
                            }
                            handled = 1;
                            break;
                        case 'p': audio_stop(); handled = 1; break;
                        case 'c': audio_scan("/mnt/sdcard/music/"); handled = 1; break;
                        case 's':
                            if (audio_list_offset + 1 < audio_count) audio_list_offset++;
                            handled = 1;
                            break;
                        case 'a':
                            if (audio_list_offset > 0) audio_list_offset--;
                            handled = 1;
                            break;
                        case 'g':
                            if (audio_list_offset + AUDIO_PAGE_SIZE < audio_count)
                                audio_list_offset += AUDIO_PAGE_SIZE;
                            handled = 1;
                            break;
                        case 't':
                            if (audio_list_offset >= AUDIO_PAGE_SIZE)
                                audio_list_offset -= AUDIO_PAGE_SIZE;
                            handled = 1;
                            break;
                    }
                }

                /* ---- Mode 2 视频 ---- */
                else if (g_mode == 2) {
                    switch (c) {
                        case 'p': video_stop(); handled = 1; break;
                        case 'c': video_scan_dir(); handled = 1; break;
                    }
                }

                if (handled) {
                    lv_obj_invalidate(scr);
                } else {
                    TOUCH_LOG("Unknown FIFO cmd '%c' in mode %d", c, g_mode);
                syn_report_end:
                    ;
                }
            }
        }
        }
    }
    return 0;
}
