#include "dashboard.h"
#include "wifi_ctrl.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

volatile wifi_ctrl_t g_wifi_ctrl = {0};

void __attribute__((noinline)) wifi_ctrl_init(void) {
    write(2, "WIFI_CTRL_INIT ok\n", 18);
    memset(&g_wifi_ctrl, 0, sizeof(g_wifi_ctrl));
    g_wifi_ctrl.state = WIFI_IDLE;
}

void __attribute__((noinline)) wifi_ctrl_trigger_scan(void) {
    if (g_wifi_ctrl.state == WIFI_SCANNING) return;
    g_wifi_ctrl.ap_count = 0;
    g_wifi_ctrl.list_populated = 0;
    g_wifi_ctrl.state = WIFI_SCANNING;
    g_wifi_ctrl.scan_start_ms = lv_tick_get();
    g_wifi_ctrl.scan_before_connected = g_wifi_status.is_connected;
    g_wifi_ctrl.need_reconnect = 0;
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c",
              "iwlist wlan0 scan 2>/dev/null > /tmp/wifi_scan.txt",
              (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        g_wifi_ctrl.scan_pid = pid;
    } else {
        g_wifi_ctrl.state = WIFI_IDLE;
    }
}

static int __attribute__((noinline)) cmp_signal(const void *a, const void *b) {
    return ((const ap_info_t*)b)->signal_dbm - ((const ap_info_t*)a)->signal_dbm;
}

static void __attribute__((noinline)) wifi_parse_scan_result(void) {
    FILE *fp = fopen("/tmp/wifi_scan.txt", "r");
    if (!fp) { g_wifi_ctrl.state = WIFI_IDLE; return; }
    char line[512];
    int in_cell = 0;
    ap_info_t ap = {0};
    g_wifi_ctrl.ap_count = 0;
    while (fgets(line, sizeof(line), fp) && g_wifi_ctrl.ap_count < MAX_AP) {
        if (strstr(line, "Cell ")) {
            if (in_cell && strlen(ap.ssid) > 0)
                g_wifi_ctrl.ap_list[g_wifi_ctrl.ap_count++] = ap;
            in_cell = 1;
            memset(&ap, 0, sizeof(ap));
            ap.signal_dbm = -100;
            ap.encrypted = 1;
            continue;
        }
        if (!in_cell) continue;
        if (strstr(line, "Address: ")) sscanf(line, "Address: %17s", ap.bssid);
        if (strstr(line, "ESSID:")) {
            char *p = strstr(line, "\"");
            if (p) {
                char *q = strrchr(p + 1, '"');
                if (q) { *q = 0; strncpy(ap.ssid, p + 1, AP_SSID_LEN - 1); }
            }
        }
        if (strstr(line, "Encryption key:")) {
            if (strstr(line, "Encryption key:off")) ap.encrypted = 0;
            else if (strstr(line, "Encryption key:on")) ap.encrypted = 1;
        }
        if (strstr(line, "Quality=")) {
            int qual, max_qual;
            if (sscanf(line, "Quality=%d/%d", &qual, &max_qual) == 2 && max_qual > 0)
                ap.signal_dbm = (qual * 100) / max_qual - 100;
        }
        if (strstr(line, "Channel:")) sscanf(line, "Channel:%d", &ap.channel);
    }
    if (in_cell && strlen(ap.ssid) > 0 && g_wifi_ctrl.ap_count < MAX_AP)
        g_wifi_ctrl.ap_list[g_wifi_ctrl.ap_count++] = ap;
    fclose(fp);
    if (g_wifi_ctrl.ap_count > 1)
        qsort(g_wifi_ctrl.ap_list, g_wifi_ctrl.ap_count, sizeof(ap_info_t), cmp_signal);
    g_wifi_ctrl.state = WIFI_SCAN_DONE;
    g_wifi_ctrl.need_reconnect = 1;
    g_wifi_ctrl.list_populated = 0;
}

void __attribute__((noinline)) wifi_ctrl_tick(void) {
    if (g_wifi_ctrl.state == WIFI_SCANNING && g_wifi_ctrl.scan_pid > 0) {
        int status;
        pid_t ret = waitpid(g_wifi_ctrl.scan_pid, &status, WNOHANG);
        if (ret > 0) {
            g_wifi_ctrl.scan_pid = 0;
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                wifi_parse_scan_result();
            else
                g_wifi_ctrl.state = WIFI_IDLE;
            return;
        }
        if (lv_tick_elaps(g_wifi_ctrl.scan_start_ms) > 10000) {
            kill(g_wifi_ctrl.scan_pid, SIGKILL);
            waitpid(g_wifi_ctrl.scan_pid, NULL, WNOHANG);
            g_wifi_ctrl.scan_pid = 0;
            g_wifi_ctrl.state = WIFI_IDLE;
        }
    }
    if (g_wifi_ctrl.state == WIFI_CONNECTING) {
        if (g_wifi_status.is_connected) {
            g_wifi_ctrl.state = WIFI_CONNECTED;
        } else if (lv_tick_elaps(g_wifi_ctrl.connect_start_ms) > 15000) {
            g_wifi_ctrl.state = WIFI_FAILED;
            snprintf(g_wifi_ctrl.last_error, sizeof(g_wifi_ctrl.last_error), "timeout (15s)");
        }
    }
}

void __attribute__((noinline)) wifi_do_reconnect(void) {
    if (!g_wifi_ctrl.scan_before_connected) {
        system("ifconfig wlan0 up 2>/dev/null");
        g_wifi_ctrl.state = WIFI_IDLE;
        return;
    }
    if (access("/etc/wpa.conf", F_OK) == 0) {
        system("killall wpa_supplicant 2>/dev/null");
        system("ifconfig wlan0 up 2>/dev/null");
        system("wpa_supplicant -i wlan0 -c /etc/wpa.conf -B 2>/dev/null");
        system("sleep 2 && udhcpc -i wlan0 -n -t 5 2>/dev/null &");
        g_wifi_ctrl.state = WIFI_CONNECTING;
        g_wifi_ctrl.connect_start_ms = lv_tick_get();
    } else {
        system("ifconfig wlan0 up 2>/dev/null");
        g_wifi_ctrl.state = WIFI_IDLE;
    }
}

void __attribute__((noinline)) wifi_connect_ap(const char *ssid, const char *password) {
    FILE *fp = fopen("/etc/wpa.conf", "w");
    if (!fp) {
        g_wifi_ctrl.state = WIFI_FAILED;
        snprintf(g_wifi_ctrl.last_error, sizeof(g_wifi_ctrl.last_error), "wpa.conf open fail");
        return;
    }
    fprintf(fp, "network={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n}\n", ssid, password);
    fclose(fp);
    g_wifi_ctrl.need_reconnect = 0;
    system("(killall wpa_supplicant 2>/dev/null; ifconfig wlan0 up; wpa_supplicant -B -c /etc/wpa.conf; sleep 2; udhcpc -n -t 5) &");
    g_wifi_ctrl.state = WIFI_CONNECTING;
    g_wifi_ctrl.connect_start_ms = lv_tick_get();
}

int wifi_ctrl_get_ap_count(void) { return g_wifi_ctrl.ap_count; }

const ap_info_t* wifi_ctrl_get_ap(int idx) {
    if (idx < 0 || idx >= g_wifi_ctrl.ap_count) return NULL;
    return &g_wifi_ctrl.ap_list[idx];
}
