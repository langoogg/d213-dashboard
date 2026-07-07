#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include "audio_ctrl.h"

char audio_files[MAX_AUDIO][AUDIO_PATH_LEN];
audio_info_t audio_info[MAX_AUDIO];
int audio_count = 0;

volatile int audio_playing = 0;
volatile int audio_current_idx = -1;
volatile int last_played_idx = -1;
volatile int audio_elapsed_ms = 0;
volatile int audio_total_ms = 0;

pthread_t audio_thread_id = 0;
volatile int audio_thread_running = 0;
volatile int pending_auto_next = -1;

static int is_wav_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".wav") == 0);
}

static int parse_wav_header(const char *path, audio_info_t *info)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t buf[12];
    if (fread(buf, 1, 12, f) != 12) { fclose(f); return -1; }
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        fclose(f); return -1;
    }

    uint32_t offset = 12;
    info->data_offset = 0;
    info->sample_rate = 0;
    info->channels = 0;
    info->bits_per_sample = 0;
    info->duration_ms = 0;

    while (!feof(f)) {
        uint8_t chunk[8];
        if (fread(chunk, 1, 8, f) != 8) break;

        uint32_t chunk_size = chunk[4] | (chunk[5] << 8)
                            | (chunk[6] << 16) | (chunk[7] << 24);

        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16) { fclose(f); return -1; }
            uint8_t fmt[16];
            if (fread(fmt, 1, 16, f) != 16) { fclose(f); return -1; }

            uint16_t audio_format = fmt[0] | (fmt[1] << 8);
            if (audio_format != 1) { fclose(f); return -1; }

            info->channels = fmt[2] | (fmt[3] << 8);
            info->sample_rate = fmt[4] | (fmt[5] << 8)
                              | (fmt[6] << 16) | (fmt[7] << 24);
            info->bits_per_sample = fmt[14] | (fmt[15] << 8);

            if (chunk_size > 16) {
                if (fseek(f, chunk_size - 16, SEEK_CUR) != 0) {
                    fclose(f); return -1;
                }
            }
        }
        else if (memcmp(chunk, "data", 4) == 0) {
            info->data_offset = offset + 8;
            uint32_t byte_rate = info->sample_rate * info->channels
                               * info->bits_per_sample / 8;
            if (byte_rate > 0) {
                info->duration_ms = (uint64_t)chunk_size * 1000ULL / byte_rate;
            }
            fclose(f);
            return 0;
        }
        else {
            if (fseek(f, chunk_size + (chunk_size & 1), SEEK_CUR) != 0) {
                fclose(f); return -1;
            }
        }
        offset += 8 + chunk_size + (chunk_size & 1);
    }

    fclose(f);
    return -1;
}

int audio_scan(const char *path)
{
    DIR *dir = NULL;
    for (int retry = 0; retry < 3; retry++) {
        dir = opendir(path);
        if (dir) break;
        printf("[AUDIO] Cannot open directory: %s (attempt %d/3)\n", path, retry + 1);
        sleep(1);
    }
    if (!dir) {
        printf("[AUDIO] Failed to open directory after 3 attempts: %s\n", path);
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type != DT_REG) continue;
        if (!is_wav_file(ent->d_name)) continue;
        if (audio_count >= MAX_AUDIO) break;

        char full_path[AUDIO_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        audio_info_t info;
        if (parse_wav_header(full_path, &info) == 0) {
            strncpy(audio_files[audio_count], full_path, AUDIO_PATH_LEN - 1);
            audio_files[audio_count][AUDIO_PATH_LEN - 1] = '\0';
            audio_info[audio_count] = info;
            audio_count++;
            printf("[AUDIO] %s: %dHz %dbit ch=%d dur=%dms\n",
                   ent->d_name, info.sample_rate, info.bits_per_sample,
                   info.channels, info.duration_ms);
        } else {
            printf("[AUDIO] Skip invalid WAV: %s\n", ent->d_name);
        }
    }

    closedir(dir);
    printf("[AUDIO] Scanned %d files from %s\n", audio_count, path);
    return audio_count;
}

static void *alsa_play_thread(void *arg)
{
    int idx = audio_current_idx;
    if (idx < 0 || idx >= audio_count) {
        audio_thread_running = 0;
        return NULL;
    }

    FILE *f = fopen(audio_files[idx], "rb");
    if (!f) {
        audio_thread_running = 0;
        return NULL;
    }

    if (fseek(f, audio_info[idx].data_offset, SEEK_SET) != 0) {
        fclose(f);
        audio_thread_running = 0;
        return NULL;
    }

    snd_pcm_t *pcm;
    snd_pcm_format_t format;
    switch (audio_info[idx].bits_per_sample) {
        case 8:  format = SND_PCM_FORMAT_U8; break;
        case 16: format = SND_PCM_FORMAT_S16_LE; break;
        case 24: format = SND_PCM_FORMAT_S24_LE; break;
        case 32: format = SND_PCM_FORMAT_S32_LE; break;
        default: fclose(f); audio_thread_running = 0; return NULL;
    }

    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        printf("[AUDIO] PCM open failed\n");
        fclose(f);
        audio_playing = 0;
        audio_thread_running = 0;
        return NULL;
    }

    snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED,
                       audio_info[idx].channels,
                       audio_info[idx].sample_rate,
                       1, 500000);

    int bytes_per_frame = audio_info[idx].channels
                        * audio_info[idx].bits_per_sample / 8;
    uint8_t buf[4096];

    while (1) {
        __sync_synchronize();
        if (!audio_playing || idx != audio_current_idx) break;

        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) {
            if (!feof(f)) {
                printf("[AUDIO] Read error on %s\n", audio_files[idx]);
            }
            break;
        }

        snd_pcm_sframes_t frames = n / bytes_per_frame;
        snd_pcm_sframes_t ret = snd_pcm_writei(pcm, buf, frames);
        if (ret < 0) {
            snd_pcm_recover(pcm, ret, 0);
            continue;
        }

        audio_elapsed_ms += (int)((1ULL * frames * 1000ULL)
                                  / audio_info[idx].sample_rate);
    }

    if (!audio_playing) {
        snd_pcm_drop(pcm);
    } else {
        snd_pcm_drain(pcm);
    }
    snd_pcm_close(pcm);
    fclose(f);

    if (idx == audio_current_idx && audio_playing) {
        audio_playing = 0;
        audio_elapsed_ms = 0;
        audio_thread_running = 0;
        __sync_synchronize();

        if (idx + 1 < audio_count) {
            pending_auto_next = idx + 1;
            printf("[AUDIO] Auto-advance to track %d\n", idx + 1);
        }
        return NULL;
    }

    audio_thread_running = 0;
    return NULL;
}

void audio_play(int idx)
{
    if (idx < 0 || idx >= audio_count) return;

    audio_stop();

    __sync_synchronize();

    audio_current_idx = idx;
    last_played_idx = idx;
    audio_elapsed_ms = 0;
    audio_total_ms = audio_info[idx].duration_ms;
    audio_playing = 1;
    audio_thread_running = 1;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    size_t stack_size = 64 * 1024;
#ifdef PTHREAD_STACK_MIN
    if (stack_size < PTHREAD_STACK_MIN) stack_size = PTHREAD_STACK_MIN;
#endif
    pthread_attr_setstacksize(&attr, stack_size);

    int ret = pthread_create(&audio_thread_id, &attr, alsa_play_thread, NULL);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        printf("[AUDIO] pthread_create failed: %d\n", ret);
        audio_thread_running = 0;
        audio_playing = 0;
        audio_current_idx = -1;
        audio_total_ms = 0;
    } else {
        printf("[AUDIO] Playing: %s\n", audio_files[idx]);
    }
}

void audio_stop(void)
{
    last_played_idx = audio_current_idx;
    audio_playing = 0;
    __sync_synchronize();
    pending_auto_next = -1;

    if (audio_thread_running && audio_thread_id != 0) {
        pthread_join(audio_thread_id, NULL);
        audio_thread_id = 0;
        audio_thread_running = 0;
    }

    audio_elapsed_ms = 0;
    audio_total_ms = 0;
    audio_current_idx = -1;
}

void audio_rescan(void)
{
    audio_stop();
    audio_count = 0;
    audio_scan("/mnt/sdcard/music/");
}

void audio_ctrl_tick(void)
{
    int next = pending_auto_next;
    __sync_synchronize();
    if (next >= 0 && next < audio_count) {
        pending_auto_next = -1;
        __sync_synchronize();
        audio_play(next);
        return;
    }

    int pending = pending_audio_idx;
    __sync_synchronize();
    if (pending >= 0 && pending < audio_count) {
        pending_audio_idx = -1;
        __sync_synchronize();
        audio_play(pending);
    }
}
