/* ============================================================
 * 编译时断言（兼容 C99 / GNU89）
 * 若断言失败，错误信息为 "size of array 'static_assert_N' is negative"，
 * 请结合下方紧邻注释定位具体断言。
 * ============================================================ */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)
#else
    #define STATIC_ASSERT(expr, msg) \
        typedef char static_assert_##__LINE__[(expr) ? 1 : -1]; \
        static void *__static_assert_used_##__LINE__ = \
            (void *)(sizeof(static_assert_##__LINE__)) /* 强制引用，消除 -Wunused-typedef */
#endif

/* ============================================================
 * Mode 0 3+2 按键触摸热区共享宏（最终生产版）
 * ============================================================ */

/* ---- 分辨率抽象层 ---- */
#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       600
#define SCREEN_CENTER_X     (SCREEN_WIDTH / 2)   /* 512 */

/* ---- 静态断言：分辨率必须为 1024x600 ---- */
STATIC_ASSERT(SCREEN_WIDTH == 1024, "SCREEN_WIDTH must be 1024");
STATIC_ASSERT(SCREEN_HEIGHT == 600, "SCREEN_HEIGHT must be 600");

/* ---- 翻页箭头 ---- */
#define ARROW_Y0            320
#define ARROW_Y1            360
#define ARROW_L_X0          (SCREEN_CENTER_X - 90)
#define ARROW_L_X1          (SCREEN_CENTER_X - 30)
#define ARROW_R_X0          (SCREEN_CENTER_X + 30)
#define ARROW_R_X1          (SCREEN_CENTER_X + 90)

/* ---- Row1 按键 L / PND / R ---- */
#define BTN_ROW1_Y0         370
#define BTN_ROW1_Y1         410
#define BTN_L_X0            392
#define BTN_L_X1            464
#define BTN_PND_X0          476
#define BTN_PND_X1          548
#define BTN_R_X0            560
#define BTN_R_X1            632

/* ---- Row2 按键 + / - ---- */
#define BTN_ROW2_Y0         410
#define BTN_ROW2_Y1         452
#define BTN_PLUS_X0         434
#define BTN_PLUS_X1         506
#define BTN_MINUS_X0        518
#define BTN_MINUS_X1        590

/* ---- 静态断言：Row1/Row2 必须无缝衔接 ---- */
STATIC_ASSERT(BTN_ROW1_Y1 == BTN_ROW2_Y0, "Row1/Row2 must be seamless");

/* ---- 各按钮 X 方向间隙说明 ---- */
/* L (392-464) → 间隙 12px → PND (476-548) → 间隙 12px → R (560-632) */
/* 间隙区域（464-476, 548-560）为无响应区，符合视觉间距设计 */

/* ---- 废弃宏（V12 前） ---- */
/* DEPRECATED: replaced with BTN_* / ARROW_* macros */
/* #define KEY_AREA_Y_TOP   340 */
/* #define KEY_HEIGHT       80 */
/* #define KEY_AREA_Y_BOT   420 */
/* #define KEY_WIDTH        196 */
/* #define MODE0_SLIDE_LIMIT KEY_AREA_Y_TOP */

#include "lvgl/lvgl.h"
#include "vehicle_physics.h"
#include <sys/time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { IGN_OFF = 0, IGN_SELF_TEST = 1, IGN_START = 2 } ignition_state_t;


typedef struct {
    int is_connected;
    char ssid[33];
    char ip[16];
    int signal_dbm;
    double bit_rate_mbps;
    double freq_ghz;
} wifi_status_t;

extern ignition_state_t g_ign_state;
extern int g_self_test_done;
extern int g_page, g_mode, g_gear, g_throttle, g_brake;
extern struct timeval g_self_test_start_tv;
extern float g_self_test_ratio;
extern int audio_list_offset;
extern volatile int pending_video_idx;

extern int fb_fd;
extern unsigned char *fbp;
extern int g_fb_yres;
extern int g_key_pressed_idx;
extern uint32_t g_key_press_time;
extern wifi_status_t g_wifi_status;
extern int g_turn_left;
extern int g_turn_right;
extern volatile int g_turn_blink;
extern VehiclePhysics phy;

static inline void phy_sync(void) {
    phy.engine_on = (g_ign_state == IGN_START);
    phy.gear = g_gear;
    phy.throttle = g_throttle;
}

void dashboard_init(lv_obj_t *scr, VehiclePhysics *phy);
void dashboard_set_page(lv_obj_t *scr, int page);
void dashboard_next_page(lv_obj_t *scr, int dir);
void dashboard_start_self_test(lv_obj_t *scr);
void dashboard_self_test_tick(void);

#ifdef __cplusplus
}
#endif

#include "video_ctrl.h"
#include "audio_ctrl.h"

/* === v6.1 跨模块状态变量 === */
extern volatile int g_audio_error;
extern volatile int g_video_error;
extern volatile int g_first_video_frame;
extern volatile uint32_t g_video_start_tick;
extern volatile int g_video_state;
extern volatile int last_played_idx;
