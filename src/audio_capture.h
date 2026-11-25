#ifndef NULLNOISE_AUDIO_CAPTURE_H
#define NULLNOISE_AUDIO_CAPTURE_H

#include <jni.h>
#include <stdint.h>
#include <stdio.h>

/* One capture API, two thin backends selected at compile time.
 *
 * The engine calls these unconditionally; only audio_capture.c knows which
 * backend is active. The backend owns all capture-specific JNI state, sample
 * counting, and platform release logic.
 */

/* Checks platform support once at startup and returns the status text to
 * display. Does not start capture. */
const char *audio_capture_initialize(JNIEnv *env);

/* Creates platform capture objects (AudioRecord + optional NoiseSuppressor)
 * before a filename is chosen. No-op on the miniaudio backend. */
int audio_capture_prepare(JNIEnv *env);

/* Begins writing raw mono PCM into output (after its WAV header). */
int audio_capture_start(JNIEnv *env, FILE *output);

/* Per-frame work from the engine tick. Reads AudioRecord on the default
 * backend; no-op on the miniaudio backend, which has its own writer thread. */
void audio_capture_tick(JNIEnv *env);

/* Stops capture, releases platform objects, and reports results. */
int audio_capture_stop(JNIEnv *env, uint64_t *sample_count,
                       uint64_t *overrun_count);

/* Releases partially prepared capture objects after a failed start. */
void audio_capture_release(JNIEnv *env);

/* Short message from the backend after a failed operation, or NULL. */
const char *audio_capture_last_error(void);

/* Status text shown while this backend is recording. */
const char *audio_capture_recording_status(void);

/* Live count used by the recording timeline. */
uint64_t audio_capture_samples_written(void);

/* Cached capability flags. */
int audio_capture_noise_suppressor_available(void);
int audio_capture_uses_noise_suppressor(void);

#endif
