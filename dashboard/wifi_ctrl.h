#ifndef WIFI_CTRL_H
#define WIFI_CTRL_H

#include "lvgl/lvgl.h"
#include <stdint.h>

#define MAX_AP 64
#define AP_SSID_LEN 64

typedef enum {
    WIFI_IDLE = 0,
    WIFI_SCANNING,
    WIFI_SCAN_DONE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
} wifi_state_t;

typedef struct {
    char ssid[AP_SSID_LEN];
    int signal_dbm;
    int channel;
    int encrypted;
    char bssid[18];
} ap_info_t;

typedef struct {
    wifi_state_t state;
    ap_info_t ap_list[MAX_AP];
    int ap_count;
    uint32_t scan_start_ms;
    uint32_t connect_start_ms;
    int scan_pid;
    int scan_before_connected;
    int need_reconnect;
    int list_populated;
    char last_error[64];
} wifi_ctrl_t;

extern volatile wifi_ctrl_t g_wifi_ctrl;

void wifi_ctrl_init(void);
extern void wifi_ui_update(void);
void wifi_ui_create(lv_obj_t *parent);
void wifi_ui_destroy(void);
void wifi_ctrl_trigger_scan(void);
void wifi_ctrl_tick(void);
void wifi_do_reconnect(void);
void wifi_connect_ap(const char *ssid, const char *password);
int  wifi_ctrl_get_ap_count(void);
const ap_info_t* wifi_ctrl_get_ap(int idx);

#endif
