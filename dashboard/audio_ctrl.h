#ifndef AUDIO_CTRL_H
#define AUDIO_CTRL_H

#include <pthread.h>
#include <stdint.h>

#define MAX_AUDIO 64
#define AUDIO_PATH_LEN 256

typedef struct {
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
    uint32_t duration_ms;
    uint32_t data_offset;
} audio_info_t;

extern char audio_files[MAX_AUDIO][AUDIO_PATH_LEN];
extern audio_info_t audio_info[MAX_AUDIO];
extern int audio_count;

extern volatile int audio_playing;
extern volatile int audio_current_idx;
extern volatile int last_played_idx;
extern volatile int last_played_idx;
extern volatile int audio_elapsed_ms;
extern volatile int audio_total_ms;

extern pthread_t audio_thread_id;
extern volatile int audio_thread_running;

extern volatile int pending_audio_idx;
extern volatile int pending_auto_next;

int audio_scan(const char *path);
void audio_play(int idx);
void audio_stop(void);
void audio_rescan(void);
void audio_ctrl_tick(void);

#endif
