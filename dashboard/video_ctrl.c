/* video_ctrl.c - D213 video player using ArtInChip aic_player API */
#include "video_ctrl.h"
#include "lvgl/lvgl.h"
#include "aic_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>
#include "ui_config.h"
#include "dashboard.h"
volatile uint32_t g_video_start_tick = 0;
volatile int g_video_state = VIDEO_STATE_IDLE;



#define VIDEO_TIMEOUT_MS    (120 * 1000)
#define PREPARE_POLL_MS     10
#define PREPARE_POLL_COUNT  20
#define MPP_SETTLE_MS       30

extern int fb_fd;
extern int g_fb_yres;
extern unsigned char *fbp;

char video_files[MAX_VIDEO][MAX_PATH_LEN];
int  video_count      = 0;
int  video_sel_idx    = 0;
int  video_page_start = 0;
volatile int video_playing = 0;
int  video_paused     = 0;
pid_t video_pid       = 0;
FILE *mplayer_cmd_fp  = NULL;

static int video_stopping = 0;
static struct aic_player *g_player = NULL;
static void (*saved_flush_cb)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *) = NULL;
static int lvgl_paused = 0;
static unsigned long video_start_ms = 0;

static const char *video_dirs[] = {
    "/mnt/sdcard/videos", "/mnt/sdcard",
    "/media/sdcard", "/mnt", NULL
};

static const char *video_exts[] = {
    ".mp4", ".avi", ".mkv", ".mov", ".flv",
    ".wmv", ".mpeg", ".mpg", ".ts", ".m4v", NULL
};

/* ===== Helper: monotonic timestamp ===== */
static unsigned long get_tick_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }

/* ===== Helper: dummy flush ===== */
static void dummy_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{ lv_disp_flush_ready(drv); }

/* ===== fb0 page flip ===== */
static int fb_pan_to(int yoffset) {
    if (fb_fd < 0) {
        fprintf(stderr, "[VIDEO] fb_pan_to: fb_fd invalid\n");
        return -1;
    }
    struct fb_var_screeninfo vi;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("[VIDEO] FBIOGET_VSCREENINFO");
        return -1;
    }
    if (g_fb_yres != vi.yres) {
        fprintf(stderr, "[VIDEO] g_fb_yres(%d) != vi.yres(%d)\n", g_fb_yres, vi.yres);
        return -1;
    }
    vi.yoffset = yoffset;
    if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vi) == 0) {
        printf("[VIDEO] FBIOPAN_DISPLAY yoffset=%d OK\n", yoffset);
        return 0;
    }
    usleep(5000);
    vi.activate = FB_ACTIVATE_VBL;
    if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vi) == 0) {
        printf("[VIDEO] FBIOPAN_DISPLAY yoffset=%d with VBL OK\n", yoffset);
        return 0;
    }
    perror("[VIDEO] FBIOPAN_DISPLAY failed");
    return -1;
}

/* ===== pause / resume LVGL ===== */
static void pause_lvgl(void) {
    if (lvgl_paused) { printf("[VIDEO] pause_lvgl: already paused\n"); return; }
    if (fb_fd < 0) { fprintf(stderr, "[VIDEO] pause_lvgl: fb_fd invalid\n"); return; }
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) { fprintf(stderr, "[VIDEO] pause_lvgl: no display\n"); return; }
    if (!saved_flush_cb) {
        saved_flush_cb = disp->driver->flush_cb;
        disp->driver->flush_cb = dummy_flush_cb;
    }
    if (fb_pan_to(g_fb_yres) != 0) {
        fprintf(stderr, "[VIDEO] pause_lvgl: fb_pan_to failed, video may not be visible\n");
    }
    lvgl_paused = 1;
    printf("[VIDEO] LVGL paused\n");
}

static void resume_lvgl(void) {
    if (!lvgl_paused) { printf("[VIDEO] resume_lvgl: not paused\n"); return; }
    int pan_ok = 0;
    if (fb_fd >= 0) {
        int retry = 2;
        while (retry-- > 0) {
            if (fb_pan_to(0) == 0) { pan_ok = 1; break; }
            if (retry > 0) usleep(10000);
        }
    }
    if (!pan_ok && fbp != NULL) {
        fprintf(stderr, "[VIDEO] resume_lvgl: pan failed, clearing front page\n");
        size_t page_size = 1024 * g_fb_yres * 4;
        memset(fbp, 0, page_size);
        msync(fbp, page_size, MS_SYNC);
    }
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && saved_flush_cb) {
        disp->driver->flush_cb = saved_flush_cb;
        saved_flush_cb = NULL;
    }
    lv_refr_now(NULL);
    lvgl_paused = 0;
    printf("[VIDEO] LVGL resumed (pan_ok=%d)\n", pan_ok);
}

/* ===== Event callback ===== */
static s32 video_event_cb(void *app_data, s32 event, s32 data1, s32 data2)
{
    (void)data1; (void)data2;
    (void)app_data;
    switch (event) {
    case 0:
        printf("[VIDEO] Playback finished\n");
        video_playing = 0;
        video_paused = 0;
        video_pid = 0;
        video_start_ms = 0;
        resume_lvgl();
        lv_obj_invalidate(lv_scr_act());
        break;
    }
    return 0;
}

/* ===== Init ===== */
void video_ctrl_init(void)
{
    printf("[VIDEO] aic_player init\n");
}

/* ===== File scanning ===== */
static int has_valid_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int i = 0; video_exts[i]; i++)
        if (strcasecmp(dot, video_exts[i]) == 0) return 1;
    return 0;
}

static int cmp_filename(const void *a, const void *b)
{ return strcasecmp((const char *)a, (const char *)b); }

void video_scan_dir(void)
{
    int retry;
    for (retry = 0; retry < 3; retry++) {
        video_count = 0; video_sel_idx = 0; video_page_start = 0;
        for (int d = 0; video_dirs[d] && video_count < MAX_VIDEO; d++) {
            DIR *dir = opendir(video_dirs[d]);
            if (!dir) continue;
            struct dirent *ent;
            while ((ent = readdir(dir)) && video_count < MAX_VIDEO) {
                if (!has_valid_ext(ent->d_name)) continue;
                snprintf(video_files[video_count], MAX_PATH_LEN,
                         "%s/%s", video_dirs[d], ent->d_name);
                video_count++;
            }
            closedir(dir);
            if (video_count > 0) break;
        }
        if (video_count > 0) break;
        printf("[VIDEO] Scan attempt %d found 0 files, retrying...\n", retry + 1);
        sleep(1);
    }
    if (video_count > 1)
        qsort(video_files, video_count, MAX_PATH_LEN, cmp_filename);
    printf("[VIDEO] Scanned %d files (attempts: %d)\n", video_count, retry + 1);
}

int video_get_page_count(void) { return (video_count + 9) / 10; }

void video_select_up(void)
{
    if (!video_count) return;
    video_sel_idx = (video_sel_idx > 0) ? video_sel_idx - 1 : video_count - 1;
    if (video_sel_idx < video_page_start)
        video_page_start = (video_sel_idx / 10) * 10;
}

void video_select_down(void)
{
    if (!video_count) return;
    video_sel_idx = (video_sel_idx < video_count - 1) ? video_sel_idx + 1 : 0;
    if (video_sel_idx >= video_page_start + 10)
        video_page_start = (video_sel_idx / 10) * 10;
}

void video_prev_page(void)
{
    if (video_page_start >= 10) video_page_start -= 10;
    else video_page_start = 0;
    if (video_sel_idx < video_page_start) video_sel_idx = video_page_start;
}

void video_next_page(void)
{
    int total = video_get_page_count();
    if (video_page_start / 10 < total - 1) {
        video_page_start += 10;
        if (video_sel_idx < video_page_start) video_sel_idx = video_page_start;
    }
}

const char* video_get_current_file(void)
{
    return (video_sel_idx >= 0 && video_sel_idx < video_count)
           ? video_files[video_sel_idx] : NULL;
}

/* ===== Playback control ===== */
void video_play(int idx)
{
    if (idx < 0 || idx >= video_count) {
        printf("[VIDEO] Invalid index: %d\n", idx);
        return;
    }

    if (video_playing) video_stop();

    const char *uri = video_files[idx];
    printf("[VIDEO] Playing: %s\n", uri);

    g_player = aic_player_create(NULL);
    if (!g_player) {
        printf("[VIDEO] ERROR: aic_player_create failed\n");
        return;
    }

    aic_player_set_event_callback(g_player, NULL, video_event_cb);

    if (aic_player_set_uri(g_player, (char *)uri) != 0) {
        printf("[VIDEO] ERROR: set_uri failed\n");
        aic_player_destroy(g_player);
        g_player = NULL;
        return;
    }

    if (aic_player_prepare_async(g_player) != 0) {
        printf("[VIDEO] ERROR: prepare_async failed\n");
        aic_player_destroy(g_player);
        g_player = NULL;
        return;
    }

    for (int i = 0; i < PREPARE_POLL_COUNT; i++) {
        usleep(PREPARE_POLL_MS * 1000);
    }

    if (aic_player_start(g_player) != 0) {
        printf("[VIDEO] ERROR: start failed\n");
        aic_player_destroy(g_player);
        g_player = NULL;
        return;
    }

    if (aic_player_play(g_player) != 0) {
        printf("[VIDEO] ERROR: play failed\n");
        aic_player_destroy(g_player);
        g_player = NULL;
        return;
    }

    pause_lvgl();
    /* ---- 重置首帧状态机 ---- */
    g_video_start_tick = lv_tick_get();
    g_video_state = VIDEO_STATE_STARTING;
    g_video_error = 0;


    video_playing = 1;
    video_paused = 0;
    video_stopping = 0;
    video_pid = 1;
    video_start_ms = get_tick_ms();
    printf("[VIDEO] video_play(%d) started\n", idx);
}

void video_pause_toggle(void)
{
    video_stop();
}

void video_stop(void)
{
    if (!video_playing && !video_paused) return;
    if (g_player) {
        aic_player_stop(g_player);
        aic_player_destroy(g_player);
        g_player = NULL;
    }
    usleep(MPP_SETTLE_MS * 1000);
    resume_lvgl();
    video_playing = 0;
    video_paused = 0;
    video_pid = 0;
    video_stopping = 0;
    video_start_ms = 0;
    lv_obj_invalidate(lv_scr_act());
    printf("[VIDEO] video_stop() done\n");
}

void video_ctrl_tick(void)
{
    /* ---- 首帧启动超时保护 ---- */
    if (g_video_state == VIDEO_STATE_STARTING) {
        uint32_t elapsed = lv_tick_get() - g_video_start_tick;
        if (video_playing) {
            g_video_state = VIDEO_STATE_PLAYING;
        } else if (elapsed > VIDEO_FIRST_FRAME_TIMEOUT_MS) {
            g_video_state = VIDEO_STATE_IDLE;
            g_video_error = 1;
            video_stop();
            lv_obj_invalidate(lv_scr_act());
            #ifdef DEBUG
            printf("[VIDEO] First frame timeout (%d ms), stopped\n",
                   VIDEO_FIRST_FRAME_TIMEOUT_MS);
            #endif
        }
    }

    if (!video_playing) return;
    if (video_start_ms > 0 && (get_tick_ms() - video_start_ms) > VIDEO_TIMEOUT_MS) {
        printf("[VIDEO] timeout (%d ms), force stop\n", VIDEO_TIMEOUT_MS);
        video_stop();
    }
}

void video_handle_fifo_cmd(const char *cmd)
{
    if (strcmp(cmd, "video_stop") == 0) video_stop();
}
