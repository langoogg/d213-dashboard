/* dashboard.c — D213 v4: real-time self-test, dual-clock */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "lvgl/lvgl.h"
#include "dashboard.h"
#include "vehicle_physics.h"
#include "audio_ctrl.h"
#include "video_ctrl.h"
#include "ui_config.h"

#define SELF_TEST_RISE_MS   1200
#define SELF_TEST_TOTAL_MS  2400

ignition_state_t g_ign_state = IGN_OFF;
int g_self_test_done = 0;
int g_page = 0, g_mode = 0, g_gear = 0, g_throttle = 0, g_brake = 0;
int audio_list_offset = 0;
int g_turn_left = 0, g_turn_right = 0;
struct timeval g_self_test_start_tv = {0,0};
float g_self_test_ratio = 0.0f;

/* ========== WiFi status panel global ========== */
wifi_status_t g_wifi_status = {0};

static lv_obj_t *st_scr = NULL;
static lv_timer_t *st_timer = NULL;
static int last_logged_ratio = -1;
static uint32_t last_log_time = 0;
static VehiclePhysics *vehicle = NULL;

/* --- helpers --- */
static void angle_to_xy(double cx, double cy, double r, double deg, int *x, int *y) {
    double rad = deg * M_PI / 180.0;
    *x = (int)(cx + r * cos(rad));
    *y = (int)(cy + r * sin(rad));
}

#define LBL(dc,x,y,t,c) draw_label_v8(dc,x,y,t,c,&lv_font_montserrat_14)
static void draw_label_v8(lv_draw_ctx_t *dc, int x, int y, const char *txt, lv_color_t color, const lv_font_t *font) {
    lv_draw_label_dsc_t dsc; lv_draw_label_dsc_init(&dsc);
    dsc.color = color; dsc.font = font; dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_point_t sz; lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_area_t area; area.x1 = x - sz.x / 2; area.y1 = y - sz.y / 2;
    area.x2 = x + sz.x / 2; area.y2 = y + sz.y / 2;
    lv_draw_label(dc, &dsc, &area, txt, NULL);
}

static void draw_line_v8(lv_draw_ctx_t *dc, int x1, int y1, int x2, int y2, lv_color_t c, int w) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = c; d.width = w; d.round_start = d.round_end = 1;
    lv_point_t p1 = {x1, y1}, p2 = {x2, y2};
    lv_draw_line(dc, &d, &p1, &p2);
}

static void draw_poly_arc(lv_draw_ctx_t *dc, int cx, int cy, int r, int s, int e, lv_color_t color, int w) {
    int sweep = (e - s + 3600) % 3600;
    if (sweep <= 0) sweep = 3600;
    int n = sweep / 25; if (n < 4) n = 4; if (n > 80) n = 80;
    int px = 0, py = 0, first = 1;
    for (int j = 0; j <= n; j++) {
        double ang = (s + j * sweep / n) / 10.0;
        double rad = ang * M_PI / 180.0;
        int x = (int)(cx + r * cos(rad)), y = (int)(cy + r * sin(rad));
        if (!first) draw_line_v8(dc, px, py, x, y, color, w);
        px = x; py = y; first = 0;
    }
}

static void draw_tick(lv_draw_ctx_t *dc, double cx, double cy, double r, double deg, int len, lv_color_t c) {
    int x1, y1, x2, y2;
    angle_to_xy(cx, cy, r - 6, deg, &x1, &y1);
    angle_to_xy(cx, cy, r - 6 - len, deg, &x2, &y2);
    draw_line_v8(dc, x1, y1, x2, y2, c, 1);
}

static void draw_main_gauge(lv_draw_ctx_t *dc, double cx, double cy, double r,
                             double ratio, lv_color_t ac, int max_v, int step) {
    int ri = (int)r;
    draw_poly_arc(dc, (int)cx, (int)cy, ri + 3, 1400, 4000, lv_color_make(100,100,105), 2);
    draw_poly_arc(dc, (int)cx, (int)cy, ri + 1, 1400, 4000, lv_color_make(70,70,75), 1);
    draw_poly_arc(dc, (int)cx, (int)cy, ri, 1400, 4000, lv_color_make(45,45,50), 14);
    int fill = 1400 + (int)(ratio * 2600);
    if (fill > 1400) draw_poly_arc(dc, (int)cx, (int)cy, ri, 1400, fill, ac, 14);
    if (max_v == 8000 && ratio > 0.75) {
        int red_s = 1400 + (int)(6000.0 / max_v * 2600);
        draw_poly_arc(dc, (int)cx, (int)cy, ri, red_s, fill, lv_color_make(255,30,30), 15);
    }
    int n = max_v / step;
    for (int i = 0; i <= n; i++) {
        double qt_deg = 220.0 + (-260.0) * i / n;
        double lv_deg = fmod(-qt_deg + 360.0, 360.0);
        int val = i * step, isRed = (max_v == 8000 && val >= 6000);
        lv_color_t tc = isRed ? lv_color_make(220,40,40) : lv_color_make(150,150,150);
        if (i % 2 == 0 || i == n) {
            char b[8]; snprintf(b, sizeof(b), "%d", val);
            int tx,ty; angle_to_xy(cx, cy, r * 1.35, lv_deg, &tx, &ty);
            LBL(dc, tx, ty, b, isRed ? lv_color_make(220,40,40) : lv_color_make(200,200,200));
        }
        draw_tick(dc, cx, cy, r, lv_deg, i % 2 ? 6 : 10, tc);
    }
}

static void draw_needle(lv_draw_ctx_t *dc, double cx, double cy, double r, double ratio, lv_color_t color) {
    double qt_deg = 220.0 + (-260.0) * ratio;
    double deg = fmod(360.0 - qt_deg, 360.0);
    int tx,ty,blx,bly,brx,bry;
    angle_to_xy(cx, cy, r * 0.78, deg, &tx, &ty);
    angle_to_xy(cx, cy, r * 0.04, deg - 90, &blx, &bly);
    angle_to_xy(cx, cy, r * 0.04, deg + 90, &brx, &bry);
    draw_line_v8(dc, tx, ty, blx, bly, color, 4);
    draw_line_v8(dc, tx, ty, brx, bry, color, 4);
    draw_line_v8(dc, blx, bly, brx, bry, color, 4);
    int cwx,cwy;
    angle_to_xy(cx, cy, r * 0.22, fmod(deg + 180.0, 360.0), &cwx, &cwy);
    draw_line_v8(dc, tx, ty, cwx, cwy, lv_color_make(180,180,180), 3);
    int cap_r = (int)(r * 0.05);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_make(30,30,35); rd.bg_opa = 255; rd.radius = cap_r;
    lv_area_t ca = {(int)cx - cap_r, (int)cy - cap_r, (int)cx + cap_r, (int)cy + cap_r};
    lv_draw_rect(dc, &rd, &ca);
}

static void draw_small_gauge(lv_draw_ctx_t *dc, double cx, double cy, double r,
                              double ratio, lv_color_t ac, const char *lo, const char *hi) {
    int ri = (int)r;
    draw_poly_arc(dc, (int)cx, (int)cy, ri, 1400, 4000, lv_color_make(45,45,50), 6);
    int fill = 1400 + (int)(ratio * 2600);
    if (fill > 1400) draw_poly_arc(dc, (int)cx, (int)cy, ri, 1400, fill, ac, 6);
    for (int i = 0; i <= 8; i++) {
        double qt_deg = 220.0 + (-260.0) * i / 8.0;
        double lv_deg = fmod(-qt_deg + 360.0, 360.0);
        draw_tick(dc, cx, cy, r, lv_deg, (i % 2) ? 3 : 5, lv_color_make(140,140,140));
    }
    int lx,ly,hx,hy;
    angle_to_xy(cx, cy, r + 10, fmod(-220.0+360.0,360.0), &lx, &ly);
    angle_to_xy(cx, cy, r + 10, fmod(40.0,360.0), &hx, &hy);
    LBL(dc, lx-4, ly, (char*)lo, lv_color_make(160,160,160));
    LBL(dc, hx+4, hy, (char*)hi, lv_color_make(160,160,160));
    draw_needle(dc, cx, cy, r, ratio, lv_color_make(200,200,200));
}

/* --- self-test log --- */
static void st_refresh(lv_timer_t *t) { lv_obj_invalidate((lv_obj_t*)t->user_data); }

static void self_test_log(const char *msg, int v1, int v2) {
    uint32_t now = lv_tick_get();
    if (v1 == last_logged_ratio && lv_tick_elaps(last_log_time) < 100) return;
    last_logged_ratio = v1;
    last_log_time = now;
    FILE *fp = fopen("/tmp/selftest.log", "a");
    if (fp) { fprintf(fp, "[%lu] %s v1=%d v2=%d\n", (unsigned long)now, msg, v1, v2); fclose(fp); }
}

/* --- self-test API (real-time clock) --- */
void dashboard_start_self_test(lv_obj_t *scr) {
    st_scr = scr;
    g_ign_state = IGN_SELF_TEST;
    g_self_test_done = 0;
    gettimeofday(&g_self_test_start_tv, NULL);
    g_self_test_ratio = 0.0f;
    last_logged_ratio = -1;
    st_timer = lv_timer_create(st_refresh, 33, scr);
    lv_obj_invalidate(scr);
    self_test_log("START", 0, 0);
}

void dashboard_self_test_tick(void) {
    if (g_ign_state != IGN_SELF_TEST) return;
    struct timeval now_tv;
    gettimeofday(&now_tv, NULL);
    long elapsed_ms = (now_tv.tv_sec - g_self_test_start_tv.tv_sec) * 1000 +
                      (now_tv.tv_usec - g_self_test_start_tv.tv_usec) / 1000;
    if (elapsed_ms > 10000) {
        g_ign_state = IGN_START; g_self_test_done = 1;
        if (vehicle) { vehicle->rpm = 800; vehicle->speed = 0; }
        if (st_timer) { lv_timer_del(st_timer); st_timer = NULL; }
        lv_obj_invalidate(st_scr);
        self_test_log("FORCE_START", (int)elapsed_ms, 0);
        return;
    }
    if (elapsed_ms >= SELF_TEST_TOTAL_MS) {
        g_ign_state = IGN_START; g_self_test_done = 1;
        if (vehicle) { vehicle->rpm = 800; vehicle->speed = 0; }
        if (st_timer) { lv_timer_del(st_timer); st_timer = NULL; }
        lv_obj_invalidate(st_scr);
        self_test_log("TO_START", (int)elapsed_ms, 0);
        return;
    }
    float progress = (float)elapsed_ms / SELF_TEST_RISE_MS;
    float new_ratio = (progress <= 1.0f) ? progress : (2.0f - progress);
    g_self_test_ratio = new_ratio;
}

/* ========== WiFi status update ========== */
void wifi_update_status(void) {
    FILE *fp;
    char line[256];
    int ok_ssid = 0, ok_signal = 0, ok_bitrate = 0, ok_freq = 0;

    if (access("/sys/class/net/wlan0", F_OK) != 0) {
        g_wifi_status.is_connected = 0;
        g_wifi_status.ssid[0] = '\0';
        g_wifi_status.ip[0] = '\0';
        g_wifi_status.signal_dbm = 0;
        g_wifi_status.bit_rate_mbps = 0;
        g_wifi_status.freq_ghz = 0;
        return;
    }

    fp = popen("iwconfig wlan0 2>/dev/null", "r");
    if (!fp) { g_wifi_status.is_connected = 0; return; }

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "ESSID:")) {
            char *p = strstr(line, "\"");
            if (p) {
                char *q = strchr(p + 1, '\"');
                if (q) {
                    *q = '\0';
                    strncpy(g_wifi_status.ssid, p + 1, 32);
                    g_wifi_status.ssid[32] = '\0';
                    ok_ssid = 1;
                }
            }
        }

        if (strstr(line, "Signal level=")) {
            char *p = strstr(line, "Signal level=");
            if (p) {
                sscanf(p, "Signal level=%d dBm", &g_wifi_status.signal_dbm);
            } else {
                sscanf(line, "Signal level: %d dBm", &g_wifi_status.signal_dbm);
            }
            ok_signal = 1;
        } else if (strstr(line, "Quality=")) {
            int qual, max_qual;
            if (sscanf(line, "Quality=%d/%d", &qual, &max_qual) == 2 && max_qual > 0) {
                g_wifi_status.signal_dbm = (int)((float)qual / max_qual * 100 - 100);
                ok_signal = 1;
            }
        }

        if (strstr(line, "Bit Rate=")) {
            char *p = strstr(line, "Bit Rate=");
            if (p) {
                sscanf(p, "Bit Rate=%f Mb/s", &g_wifi_status.bit_rate_mbps);
                ok_bitrate = 1;
            }
        }

        if (strstr(line, "Frequency:")) {
            char *p = strstr(line, "Frequency:");
            if (p) {
                sscanf(p, "Frequency:%f GHz", &g_wifi_status.freq_ghz);
                ok_freq = 1;
            }
        }
    }
    pclose(fp);

    fp = popen("ip -4 addr show wlan0 2>/dev/null", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "inet ")) {
                char *p = strstr(line, "inet ");
                if (p) {
                    p += 5;
                    char *q = strchr(p, '/');
                    if (q) {
                        *q = '\0';
                        strncpy(g_wifi_status.ip, p, 15);
                        g_wifi_status.ip[15] = '\0';
                    }
                }
                break;
            }
        }
        pclose(fp);
    }

    g_wifi_status.is_connected = (ok_ssid && ok_signal && ok_bitrate && ok_freq);

    if (!g_wifi_status.is_connected) {
        g_wifi_status.ssid[0] = '\0';
        g_wifi_status.ip[0] = '\0';
        g_wifi_status.signal_dbm = 0;
        g_wifi_status.bit_rate_mbps = 0;
        g_wifi_status.freq_ghz = 0;
    }
}

/* ========== WiFi panel draw ========== */
static void draw_wifi_panel(lv_draw_ctx_t *dc, int w, int h, int mx, int my) {
    lv_draw_label_dsc_t lbl_dsc;
    lv_draw_label_dsc_init(&lbl_dsc);
    lbl_dsc.color = lv_color_hex(0xFFFFFF);
    lv_area_t area;

    const int panel_left = 50;
    const int panel_top = 40;
    const int line_height_small = 30;
    const int line_height_large = 35;

    lbl_dsc.font = &lv_font_montserrat_28;
    area.x1 = panel_left;
    area.y1 = panel_top;
    area.x2 = panel_left + 200;
    area.y2 = panel_top + line_height_large;
    lv_draw_label(dc, &lbl_dsc, &area, "WiFi", NULL);

    if (g_wifi_status.is_connected) {
        char buf[128];

        lbl_dsc.font = &lv_font_montserrat_24;
        area.y1 = panel_top + 50;
        area.y2 = area.y1 + line_height_large;
        if (strlen(g_wifi_status.ssid) > 0) {
            snprintf(buf, sizeof(buf), "%s", g_wifi_status.ssid);
        } else {
            snprintf(buf, sizeof(buf), "<hidden>");
        }
        lv_draw_label(dc, &lbl_dsc, &area, buf, NULL);

        lbl_dsc.font = &lv_font_montserrat_20;
        area.y1 = panel_top + 95;
        area.y2 = area.y1 + line_height_small;
        snprintf(buf, sizeof(buf), "IP: %s", g_wifi_status.ip);
        lv_draw_label(dc, &lbl_dsc, &area, buf, NULL);

        int bars = (g_wifi_status.signal_dbm + 100) / 25;
        if (bars < 0) bars = 0;
        if (bars > 4) bars = 4;

        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        for (int i = 0; i < 4; i++) {
            rect_dsc.bg_color = (i < bars) ? lv_color_hex(0x00FF00) : lv_color_hex(0x444444);
            lv_area_t bar_area;
            bar_area.x1 = 400 + i * 25;
            bar_area.y1 = 130;
            bar_area.x2 = bar_area.x1 + 15;
            bar_area.y2 = 160 - i * 5;
            lv_draw_rect(dc, &rect_dsc, &bar_area);
        }

        lbl_dsc.font = &lv_font_montserrat_18;
        lbl_dsc.color = lv_color_hex(0xAAAAAA);
        area.x1 = 500;
        area.y1 = 135;
        area.x2 = 600;
        area.y2 = 160;
        snprintf(buf, sizeof(buf), "%d dBm", g_wifi_status.signal_dbm);
        lv_draw_label(dc, &lbl_dsc, &area, buf, NULL);

        lbl_dsc.font = &lv_font_montserrat_18;
        lbl_dsc.color = lv_color_hex(0xCCCCCC);
        area.x1 = panel_left;
        area.y1 = panel_top + 140;
        area.x2 = panel_left + 400;
        area.y2 = area.y1 + line_height_small;
        snprintf(buf, sizeof(buf), "rate: %.1f Mb/s  freq: %.1f GHz",
                 g_wifi_status.bit_rate_mbps, g_wifi_status.freq_ghz);
        lv_draw_label(dc, &lbl_dsc, &area, buf, NULL);

    } else {
        lbl_dsc.font = &lv_font_montserrat_32;
        lbl_dsc.color = lv_color_hex(0xFF4444);
        area.x1 = mx - 120;
        area.y1 = my - 40;
        area.x2 = mx + 120;
        area.y2 = my + 40;
        lv_draw_label(dc, &lbl_dsc, &area, "not connected", NULL);
    }
}

/* ========== POST DRAW ========== */
static void post_draw_cb(lv_event_t *e) {
    lv_draw_ctx_t *dc = lv_event_get_draw_ctx(e);
    int w = 1024, h = 600, mx = 512, my = 300;
    char b[64];

    switch (g_ign_state) {
    case IGN_OFF:
        draw_main_gauge(dc, 205, my-50, 139, 0, lv_color_make(30,140,255), 8000, 1000);
        draw_main_gauge(dc, 819, my-50, 139, 0, lv_color_make(220,50,50), 280, 40);
        draw_small_gauge(dc, 205, 500, 70, 0, lv_color_make(0,150,255), "C", "H");
        draw_small_gauge(dc, 819, 500, 70, 0, lv_color_make(0,200,100), "E", "F");
        break;

    case IGN_SELF_TEST: {
        double r = g_self_test_ratio;
        draw_main_gauge(dc, 205, my-50, 139, r, lv_color_make(30,140,255), 8000, 1000);
        draw_needle(dc, 205, my-50, 139, r, lv_color_make(220,220,220));
        draw_main_gauge(dc, 819, my-50, 139, r, lv_color_make(220,50,50), 280, 40);
        draw_needle(dc, 819, my-50, 139, r, lv_color_make(220,220,220));
        draw_small_gauge(dc, 205, 500, 70, r, lv_color_make(0,150,255), "C", "H");
        draw_small_gauge(dc, 819, 500, 70, r, lv_color_make(0,200,100), "E", "F");
        lv_draw_label_dsc_t ld; lv_draw_label_dsc_init(&ld);
        ld.color = lv_color_make(255,255,255); ld.font = &lv_font_montserrat_14; ld.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t a = {mx-80, my-12, mx+80, my+12};
        lv_draw_label(dc, &ld, &a, "SELF-TEST", NULL);
        break;
    }

    case IGN_START: {
        int spd = vehicle ? (int)vehicle->speed : 0;
        int rpm = vehicle ? (int)vehicle->rpm : 800;
        double fuel = vehicle ? vehicle->fuel_liters : 41.25;
        double ctemp = vehicle ? vehicle->coolant_temp : 65.0;
        double fuel_ratio = fmax(0, fmin(1.0, fuel / 55.0));
        double temp_ratio = fmax(0, fmin(1.0, (ctemp - 50.0) / 80.0));
        double rr = (rpm > 800) ? (double)rpm / 8000.0 : 0;
        double sr2 = (spd > 0) ? (double)spd / 280.0 : 0;
        double cy = 250;

        if (g_mode == 0) {
            draw_main_gauge(dc, 205, my-50, 139, rr, lv_color_make(30,140,255), 8000, 1000);
            draw_needle(dc, 205, my-50, 139, rr, lv_color_make(220,220,220));
            draw_main_gauge(dc, 819, my-50, 139, sr2, lv_color_make(220,50,50), 280, 40);
            draw_needle(dc, 819, my-50, 139, sr2, lv_color_make(220,220,220));
            draw_small_gauge(dc, 205, 500, 70, temp_ratio, lv_color_make(0,150,255), "C", "H");
            draw_small_gauge(dc, 819, 500, 70, fuel_ratio, lv_color_make(0,200,100), "E", "F");
            lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
            rd.bg_color = lv_color_make(12,15,22); rd.bg_opa = 240; rd.radius = 15;
            lv_area_t ca = {mx-78, (int)(cy-78), mx+78, (int)(cy+78)};
            lv_draw_rect(dc, &rd, &ca);
            if (g_page == 0) {
                snprintf(b, sizeof(b), "%d", spd);
                LBL(dc, mx, (int)(cy-16), b, lv_color_make(255,255,255));
                LBL(dc, mx, (int)(cy+2), "km/h", lv_color_make(120,120,130));
                draw_line_v8(dc, mx-31, (int)(cy+14), mx+31, (int)(cy+14), lv_color_make(50,50,55), 1);
                const char *gn = g_gear == 1 ? "N" : (g_gear == 2 ? "" : "P");
                if (g_gear == 2) { snprintf(b, sizeof(b), "S%d", vehicle?vehicle->dsg_gear:1); gn = b; }
                LBL(dc, mx, (int)(cy+24), gn, lv_color_make(230,200,0));
                snprintf(b, sizeof(b), "%.1f km", vehicle ? vehicle->odometer : 0.0);
                LBL(dc, mx, (int)(cy+42), b, lv_color_make(140,140,150));
            } else if (g_page == 1) {
                struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
                int hh = ts.tv_sec / 3600, mm = (ts.tv_sec % 3600) / 60, ss = ts.tv_sec % 60;
                snprintf(b, sizeof(b), "%02d:%02d:%02d", hh, mm, ss);
                LBL(dc, mx, (int)(cy-16), b, lv_color_make(255,255,255));
                LBL(dc, mx, (int)(cy+2), "UPTIME", lv_color_make(120,120,130));
                snprintf(b, sizeof(b), "RPM %4d", rpm);
                LBL(dc, mx, (int)(cy+24), b, lv_color_make(100,200,255));
            } else {
                snprintf(b, sizeof(b), "%.1f L", fuel);
                LBL(dc, mx, (int)(cy-16), b, lv_color_make(0,200,100));
                LBL(dc, mx, (int)(cy+2), "FUEL", lv_color_make(120,120,130));
                draw_line_v8(dc, mx-31, (int)(cy+14), mx+31, (int)(cy+14), lv_color_make(50,50,55), 1);
                snprintf(b, sizeof(b), "T %.0fC", ctemp);
                LBL(dc, mx, (int)(cy+24), b, lv_color_make(160,160,160));
            }
            int ay = 340, al = mx - 60, ar = mx + 60;
            for (int k = 0; k < 3; k++) {
                draw_line_v8(dc, al - k*4, ay, al + 8 - k*4, ay - 8, lv_color_make(180,180,180), 2);
                draw_line_v8(dc, al - k*4, ay, al + 8 - k*4, ay + 8, lv_color_make(180,180,180), 2);
                draw_line_v8(dc, ar + k*4, ay, ar - 8 + k*4, ay - 8, lv_color_make(180,180,180), 2);
                draw_line_v8(dc, ar + k*4, ay, ar - 8 + k*4, ay + 8, lv_color_make(180,180,180), 2);
            }

            /* ===== virtual buttons: 3+2 layout ===== */
            #define VB_W  72
            #define VB_H  32
            #define VB_G  12
            #define VB_R1Y 370
            #define VB_R2Y 420
            #define VB_R1X0 392
            #define VB_R2X0 434
            for (int vi = 0; vi < 5; vi++) {
                int vbx, vby, active = 0;
                char vlbl[8] = "";
                if (vi < 3) {
                    vbx = VB_R1X0 + vi * (VB_W + VB_G);
                    vby = VB_R1Y;
                } else {
                    vbx = VB_R2X0 + (vi - 3) * (VB_W + VB_G);
                    vby = VB_R2Y;
                }
                switch (vi) {
                case 0: snprintf(vlbl, sizeof(vlbl), "%s", "L"); active = g_turn_left; break;
                case 1: snprintf(vlbl, sizeof(vlbl), "%s", g_gear == 0 ? "P" : (g_gear == 1 ? "N" : "D")); break;
                case 2: snprintf(vlbl, sizeof(vlbl), "%s", "R"); active = g_turn_right; break;
                case 3: snprintf(vlbl, sizeof(vlbl), "%s", "+"); active = g_throttle; break;
                case 4: snprintf(vlbl, sizeof(vlbl), "%s", "-"); break;
                }
                lv_draw_rect_dsc_t vrd; lv_draw_rect_dsc_init(&vrd);
                vrd.bg_color = active ? lv_color_make(50, 90, 160) : lv_color_make(32, 34, 44);
                vrd.bg_opa = 255; vrd.radius = 6;
                vrd.border_color = active ? lv_color_make(90, 140, 220) : lv_color_make(60, 62, 72);
                vrd.border_width = 1; vrd.border_opa = 255;
                lv_area_t vba = {vbx, vby, vbx + VB_W, vby + VB_H};
                lv_draw_rect(dc, &vrd, &vba);
                lv_color_t vlc = active ? lv_color_make(255, 255, 255) : lv_color_make(170, 172, 182);
                LBL(dc, vbx + VB_W/2 - 1, vby + VB_H/2 + 1, vlbl, vlc);
            }
            #undef VB_W
            #undef VB_H
            #undef VB_G
            #undef VB_R1Y
            #undef VB_R2Y
            #undef VB_R1X0
            #undef VB_R2X0
    /* ---------- Mode 1: 音频播放界面 ---------- */
    } else if (g_mode == 1) {
        /* 标题栏背景 */
        {
            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = lv_color_hex(0x1a1a2e);
            rect_dsc.radius = 0;
            lv_draw_rect(dc, &rect_dsc, &(lv_area_t){0, 0, 1023, 50});
        /* ---- Mode 1 播放状态行 ---- */
        {
            lv_draw_label_dsc_t status_dsc;
            lv_draw_label_dsc_init(&status_dsc);
            status_dsc.font = &lv_font_montserrat_16;
            status_dsc.align = LV_TEXT_ALIGN_LEFT;

            char status_buf[64];
            lv_area_t status_area = {STATUS_X0, STATUS_Y0, STATUS_X1, STATUS_Y1};

            if (g_audio_error) {
                status_dsc.color = lv_color_hex(0xFF4444);
                lv_snprintf(status_buf, sizeof(status_buf), "⚠ 播放失败");
                g_audio_error = 0;
            } else if (audio_playing) {
                status_dsc.color = lv_color_hex(0x4CAF50);
                int emin = audio_elapsed_ms / 60000;
                int esec = (audio_elapsed_ms % 60000) / 1000;
                int tmin = audio_total_ms / 60000;
                int tsec = (audio_total_ms % 60000) / 1000;
                lv_snprintf(status_buf, sizeof(status_buf), "▶ %02d:%02d / %02d:%02d", emin, esec, tmin, tsec);
            } else {
                status_dsc.color = lv_color_hex(0x888888);
                lv_snprintf(status_buf, sizeof(status_buf), "■ 已停止");
            }

            lv_draw_label(dc, &status_dsc, &status_area, status_buf, NULL);
        }


            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_hex(0xFFFFFF);
            label_dsc.font = &lv_font_montserrat_24;
            label_dsc.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t title_area = {0, 10, 1023, 40};
            lv_draw_label(dc, &label_dsc, &title_area, "AUDIO", NULL);

            label_dsc.font = &lv_font_montserrat_14;
            label_dsc.color = lv_color_hex(0xAAAAAA);
            lv_area_t hint_area = {20, 15, 250, 40};
            lv_draw_label(dc, &label_dsc, &hint_area, "< Slide up to back", NULL);
        }

        /* 音频文件列表 */
        {
            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.font = &lv_font_montserrat_16;
            label_dsc.align = LV_TEXT_ALIGN_LEFT;

            int max_items = (audio_count < 8) ? audio_count : 8;
            for (int i = 0; i < max_items; i++) {
                int y0 = 80 + i * 40;
                int y1 = y0 + 35;

                /* 持续高亮当前播放歌曲（使用 audio_current_idx） */
                if (i == audio_current_idx && audio_current_idx >= 0) {
                    lv_draw_rect_dsc_t rect_dsc;
                    lv_draw_rect_dsc_init(&rect_dsc);
                    rect_dsc.bg_color = lv_color_hex(0x2d3a4f);
                    rect_dsc.radius = 6;
                    lv_draw_rect(dc, &rect_dsc, &(lv_area_t){35, y0 - 2, 600, y1 + 2});
                    label_dsc.color = lv_color_hex(0x4CAF50);
                } else {
                    label_dsc.color = lv_color_hex(0xCCCCCC);
                }

                lv_area_t item_area = {40, y0 + 5, 580, y1};
                char buf[128];
                snprintf(buf, sizeof(buf), "%d. %s", i + 1, audio_files[i]);
                lv_draw_label(dc, &label_dsc, &item_area, buf, NULL);
            }
        }

        /* 底部控制区（纯视觉，无触摸交互） */
        {
            int ctrl_y = 520;
            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = lv_color_hex(0x1a1a2e);
            rect_dsc.radius = 0;
            lv_draw_rect(dc, &rect_dsc, &(lv_area_t){0, ctrl_y, 1023, 599});

            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_hex(0xFFFFFF);
            label_dsc.font = &lv_font_montserrat_16;
            label_dsc.align = LV_TEXT_ALIGN_CENTER;

            lv_draw_rect_dsc_t btn_dsc;
            lv_draw_rect_dsc_init(&btn_dsc);
            btn_dsc.radius = 8;

            btn_dsc.bg_color = lv_color_hex(0x2196F3);
            lv_draw_rect(dc, &btn_dsc, &(lv_area_t){352, ctrl_y + 10, 442, ctrl_y + 50});
            lv_area_t prev_area = {352, ctrl_y + 18, 442, ctrl_y + 45};
            lv_draw_label(dc, &label_dsc, &prev_area, "<<", NULL);

            btn_dsc.bg_color = lv_color_hex(0x4CAF50);
            lv_draw_rect(dc, &btn_dsc, &(lv_area_t){462, ctrl_y + 10, 562, ctrl_y + 50});
            lv_area_t btn_area = {462, ctrl_y + 18, 562, ctrl_y + 45};
            lv_draw_label(dc, &label_dsc, &btn_area, audio_playing ? "STOP" : "PLAY", NULL);

            btn_dsc.bg_color = lv_color_hex(0x2196F3);
            lv_draw_rect(dc, &btn_dsc, &(lv_area_t){582, ctrl_y + 10, 672, ctrl_y + 50});
            lv_area_t next_area = {582, ctrl_y + 18, 672, ctrl_y + 45};
            lv_draw_label(dc, &label_dsc, &next_area, ">>", NULL);
        }

    /* ---------- Mode 2: 视频播放界面 ---------- */
    } else if (g_mode == 2) {
        /* 标题栏背景 */
        {
            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = lv_color_hex(0x1a1a2e);
            rect_dsc.radius = 0;
            lv_draw_rect(dc, &rect_dsc, &(lv_area_t){0, 0, 1023, 50});
        /* ---- Mode 2 视频状态行 ---- */
        {
            lv_draw_label_dsc_t status_dsc;
            lv_draw_label_dsc_init(&status_dsc);
            status_dsc.font = &lv_font_montserrat_16;
            status_dsc.align = LV_TEXT_ALIGN_LEFT;

            char status_buf[64];
            lv_area_t status_area = {STATUS_X0, STATUS_Y0, STATUS_X1, STATUS_Y1};

            if (g_video_error) {
                status_dsc.color = lv_color_hex(0xFF4444);
                lv_snprintf(status_buf, sizeof(status_buf), "⚠ 视频无法播放");
                g_video_error = 0;
            } else if (video_playing) {
                status_dsc.color = lv_color_hex(0x4CAF50);
                lv_snprintf(status_buf, sizeof(status_buf), "▶ 播放中");
            } else {
                status_dsc.color = lv_color_hex(0x888888);
                lv_snprintf(status_buf, sizeof(status_buf), "■ 已停止");
            }

            lv_draw_label(dc, &status_dsc, &status_area, status_buf, NULL);
        }


            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_hex(0xFFFFFF);
            label_dsc.font = &lv_font_montserrat_24;
            label_dsc.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t title_area = {0, 10, 1023, 40};
            lv_draw_label(dc, &label_dsc, &title_area, "VIDEO", NULL);

            label_dsc.font = &lv_font_montserrat_14;
            label_dsc.color = lv_color_hex(0xAAAAAA);
            lv_area_t hint_area = {20, 15, 250, 40};
            lv_draw_label(dc, &label_dsc, &hint_area, "< Slide up to back", NULL);
        }

        /* 视频文件列表 */
        {
            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.font = &lv_font_montserrat_16;
            label_dsc.align = LV_TEXT_ALIGN_LEFT;

            int max_items = (video_count < 7) ? video_count : 7;
            for (int i = 0; i < max_items; i++) {
                int y0 = 80 + i * 50;
                int y1 = y0 + 42;

                /* 暂时高亮被点击的视频（使用 pending_video_idx），无 video_current_idx */
                if (i == pending_video_idx) {
                    lv_draw_rect_dsc_t rect_dsc;
                    lv_draw_rect_dsc_init(&rect_dsc);
                    rect_dsc.bg_color = lv_color_hex(0x2d3a4f);
                    rect_dsc.radius = 6;
                    lv_draw_rect(dc, &rect_dsc, &(lv_area_t){35, y0 - 2, 600, y1 + 2});
                    label_dsc.color = lv_color_hex(0x4CAF50);
                } else {
                    label_dsc.color = lv_color_hex(0xCCCCCC);
                }

                lv_area_t item_area = {50, y0 + 8, 580, y1};
                char buf[128];
                snprintf(buf, sizeof(buf), "%d. %s", i + 1, video_files[i]);
                lv_draw_label(dc, &label_dsc, &item_area, buf, NULL);
            }
        }

        /* 底部控制区（纯视觉，无触摸交互） */
        {
            int ctrl_y = 520;
            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = lv_color_hex(0x1a1a2e);
            rect_dsc.radius = 0;
            lv_draw_rect(dc, &rect_dsc, &(lv_area_t){0, ctrl_y, 1023, 599});

            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_hex(0xFFFFFF);
            label_dsc.font = &lv_font_montserrat_16;
            label_dsc.align = LV_TEXT_ALIGN_CENTER;

            lv_draw_rect_dsc_t btn_dsc;
            lv_draw_rect_dsc_init(&btn_dsc);
            btn_dsc.bg_color = lv_color_hex(0xFF5722);
            btn_dsc.radius = 8;
            lv_draw_rect(dc, &btn_dsc, &(lv_area_t){462, ctrl_y + 10, 562, ctrl_y + 50});
            lv_area_t stop_area = {462, ctrl_y + 18, 562, ctrl_y + 45};
            lv_draw_label(dc, &label_dsc, &stop_area, "STOP", NULL);
        }

    /* ---------- Mode 3: WiFi 面板（保持原有） ---------- */
    } else if (g_mode == 3) {
        draw_wifi_panel(dc, w, h, mx, my);
    }
        break;
    }
    default: break;
    }

    int dot_gap = 20, dot_start_x = mx - 30;
    for (int p = 0; p < 4; p++) {
        lv_draw_rect_dsc_t dd; lv_draw_rect_dsc_init(&dd);
        dd.bg_color = (p == g_mode) ? lv_color_make(255,255,255) : lv_color_make(68,68,68);
        dd.bg_opa = 255; dd.radius = 4;
        int dx = dot_start_x + p * dot_gap;
        lv_area_t a = {dx-4, h-16, dx+4, h-8};
        lv_draw_rect(dc, &dd, &a);
    }
}

void dashboard_init(lv_obj_t *scr, VehiclePhysics *phy) {
    vehicle = phy;
    lv_obj_set_style_bg_color(scr, lv_color_make(8,8,12), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, post_draw_cb, LV_EVENT_DRAW_POST_END, NULL);
}

void dashboard_set_page(lv_obj_t *o, int p) { g_page = p; lv_obj_invalidate(o); }
void dashboard_next_page(lv_obj_t *o, int dir) { g_page = (g_page + dir + 3) % 3; lv_obj_invalidate(o); }
