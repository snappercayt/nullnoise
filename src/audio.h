#ifndef NULLNOISE_AUDIO_H
#define NULLNOISE_AUDIO_H

#include <jni.h>

enum audio_state {
    AUDIO_IDLE,
    AUDIO_RECORDING,
    AUDIO_PLAYING,
    AUDIO_PAUSED,
    AUDIO_PROCESSING
};

void audio_initialize(JNIEnv *env);
enum audio_state audio_get_state(void);
int audio_noise_suppressor_available(void);
int audio_recording_uses_noise_suppressor(void);
const char *audio_get_status(void);
const char *audio_get_latest_recording_name(void);
const char *audio_get_playback_name(void);
int audio_has_recording(void);
int audio_start_recording(JNIEnv *env);
int audio_stop_recording(JNIEnv *env);
int audio_toggle_playback(JNIEnv *env);
int audio_stop_playback(JNIEnv *env);
int audio_toggle_version(void);
void audio_tick(JNIEnv *env);
int audio_enhance_latest(void);

#endif
