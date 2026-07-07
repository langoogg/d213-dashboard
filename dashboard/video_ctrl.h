#ifndef VIDEO_CTRL_H
#define VIDEO_CTRL_H

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>

#define MAX_VIDEO       64
#define MAX_PATH_LEN    512
#define MPLAYER_FIFO    "/tmp/mplayer_cmd_fifo"
#define MPLAYER_BIN     "/mnt/sdcard/mplayer"
#define MPLAYER_SCRIPT  "/tmp/run_mplayer.sh"

extern char video_files[MAX_VIDEO][MAX_PATH_LEN];
extern int  video_count;
extern int  video_sel_idx;
extern int  video_page_start;
extern volatile int video_playing;
extern int  video_paused;
extern pid_t video_pid;
extern FILE *mplayer_cmd_fp;

void video_ctrl_init(void);
void video_scan_dir(void);
void video_play(int idx);
void video_pause_toggle(void);
void video_stop(void);
void video_ctrl_tick(void);
void video_select_up(void);
void video_select_down(void);
void video_prev_page(void);
void video_next_page(void);
const char* video_get_current_file(void);
int video_get_page_count(void);

#endif

