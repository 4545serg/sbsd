#ifndef PROJECT_NOISE_AUDIO_H
#define PROJECT_NOISE_AUDIO_H

#include <stdbool.h>

/* MP3 playback through minimp3 (ARM9 decode) and Maxmod PCM streaming. */
bool audio_init(void);
void audio_exit(void);
bool audio_play(const char *path);
void audio_stop(void);
void audio_toggle_pause(void);
void audio_update(void); /* Call once per main-loop iteration. */
void audio_set_volume(int volume_0_127);
bool audio_is_playing(void);
bool audio_is_paused(void);
bool audio_finished(void);

#endif
