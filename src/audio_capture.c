#define _POSIX_C_SOURCE 200809L

#ifndef NULLNOISE_MINIAUDIO_CAPTURE
#define NULLNOISE_MINIAUDIO_CAPTURE 0
#endif

#include "audio_capture.h"

#include <android/log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if NULLNOISE_MINIAUDIO_CAPTURE
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>

#include "../third_party/raylib/src/external/miniaudio.h"
#endif

#define LOG_TAG "NullNoise"
#define SAMPLE_RATE 48000
#define RECORD_BUFFER_SAMPLES 960

static int noise_suppressor_available;
static int recording_uses_noise_suppressor;
static const char *capture_error;

#if !NULLNOISE_MINIAUDIO_CAPTURE
static int clear_capture_exception(JNIEnv *env, const char *operation)
{
    if (!(*env)->ExceptionCheck(env)) {
        return 0;
    }
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Audio API failed: %s",
                        operation);
    (*env)->ExceptionClear(env);
    return 1;
}

static jclass find_capture_class(JNIEnv *env, const char *name)
{
    jclass cls = (*env)->FindClass(env, name);
    if (clear_capture_exception(env, name) || cls == NULL) {
        return NULL;
    }
    return cls;
}

static void release_capture_global(JNIEnv *env, jobject *object)
{
    if (*object != NULL) {
        (*env)->DeleteGlobalRef(env, *object);
        *object = NULL;
    }
}
#endif

#if NULLNOISE_MINIAUDIO_CAPTURE

/* ------------------------------------------------------------------------- */
/* Backend: miniaudio capture with Android's voice-recognition input preset. */
/* ------------------------------------------------------------------------- */

#define CAPTURE_SAMPLE_RATE 48000
#define CAPTURE_CHANNELS 1
#define CAPTURE_BUFFER_FRAMES (CAPTURE_SAMPLE_RATE * 4)

typedef struct {
    ma_device device;
    ma_pcm_rb queue;
    pthread_t writer_thread;
    FILE *output;
    atomic_bool stopping;
    atomic_bool failed;
    atomic_uint_fast64_t samples_written;
    atomic_uint overrun_frames;
    int queue_initialized;
    int device_initialized;
    int thread_started;
} audio_capture;

static audio_capture capture;

static void capture_data_callback(ma_device *device, void *output,
                                  const void *input, ma_uint32 frame_count)
{
    (void)output;
    audio_capture *session = device->pUserData;
    if (session == NULL ||
        atomic_load_explicit(&session->stopping, memory_order_relaxed)) {
        return;
    }
    if (input == NULL) {
        atomic_fetch_add_explicit(&session->overrun_frames, frame_count,
                                  memory_order_relaxed);
        atomic_store_explicit(&session->failed, true, memory_order_relaxed);
        return;
    }

    const int16_t *samples = input;
    ma_uint32 remaining = frame_count;
    while (remaining > 0) {
        ma_uint32 available = remaining;
        void *destination = NULL;
        ma_result result = ma_pcm_rb_acquire_write(
            &session->queue, &available, &destination);
        if (result != MA_SUCCESS) {
            atomic_store_explicit(&session->failed, true, memory_order_relaxed);
            break;
        }
        if (available == 0 || destination == NULL) {
            atomic_fetch_add_explicit(&session->overrun_frames, remaining,
                                      memory_order_relaxed);
            break;
        }
        memcpy(destination, samples, available * sizeof(*samples));
        if (ma_pcm_rb_commit_write(&session->queue, available) != MA_SUCCESS) {
            atomic_store_explicit(&session->failed, true, memory_order_relaxed);
            break;
        }
        samples += available;
        remaining -= available;
    }
}

static void *capture_writer(void *unused)
{
    (void)unused;
    for (;;) {
        ma_uint32 available = CAPTURE_BUFFER_FRAMES;
        void *source = NULL;
        ma_result result =
            ma_pcm_rb_acquire_read(&capture.queue, &available, &source);
        if (result != MA_SUCCESS) {
            atomic_store_explicit(&capture.failed, true, memory_order_relaxed);
            break;
        }
        if (available > 0 && source != NULL) {
            size_t written = fwrite(source, sizeof(int16_t), available,
                                    capture.output);
            if (written != available) {
                atomic_store_explicit(&capture.failed, true,
                                      memory_order_relaxed);
            } else {
                atomic_fetch_add_explicit(&capture.samples_written, written,
                                          memory_order_relaxed);
            }
            if (ma_pcm_rb_commit_read(&capture.queue, available) !=
                MA_SUCCESS) {
                atomic_store_explicit(&capture.failed, true,
                                      memory_order_relaxed);
                break;
            }
        } else if (atomic_load_explicit(&capture.stopping,
                                        memory_order_acquire)) {
            break;
        } else {
            const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
            nanosleep(&delay, NULL);
        }
    }
    return NULL;
}

static void cleanup_capture(void)
{
    if (capture.device_initialized) {
        ma_device_uninit(&capture.device);
        capture.device_initialized = 0;
    }
    if (capture.queue_initialized) {
        ma_pcm_rb_uninit(&capture.queue);
        capture.queue_initialized = 0;
    }
}

const char *audio_capture_initialize(JNIEnv *env)
{
    (void)env;
    noise_suppressor_available = 0;
    capture_error = NULL;
    return "Miniaudio voice-recognition capture ready";
}

int audio_capture_prepare(JNIEnv *env)
{
    (void)env;
    recording_uses_noise_suppressor = 0;
    return 1;
}

int audio_capture_start(JNIEnv *env, FILE *output)
{
    (void)env;
    capture_error = NULL;
    if (output == NULL) {
        capture_error = "Could not start miniaudio microphone capture";
        return 0;
    }
    memset(&capture, 0, sizeof(capture));
    capture.output = output;
    atomic_init(&capture.stopping, false);
    atomic_init(&capture.failed, false);
    atomic_init(&capture.samples_written, 0);
    atomic_init(&capture.overrun_frames, 0);

    ma_result result = ma_pcm_rb_init(ma_format_s16, CAPTURE_CHANNELS,
                                      CAPTURE_BUFFER_FRAMES, NULL, NULL,
                                      &capture.queue);
    if (result != MA_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Miniaudio PCM queue initialization failed: %d",
                            result);
        capture_error = "Could not start miniaudio microphone capture";
        return 0;
    }
    capture.queue_initialized = 1;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = CAPTURE_CHANNELS;
    config.sampleRate = CAPTURE_SAMPLE_RATE;
    config.dataCallback = capture_data_callback;
    config.pUserData = &capture;
    config.aaudio.inputPreset = ma_aaudio_input_preset_voice_recognition;
    config.opensl.recordingPreset = ma_opensl_recording_preset_voice_recognition;
    result = ma_device_init(NULL, &config, &capture.device);
    if (result != MA_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Miniaudio capture device initialization failed: %d",
                            result);
        cleanup_capture();
        capture_error = "Could not start miniaudio microphone capture";
        return 0;
    }
    capture.device_initialized = 1;

    int thread_result =
        pthread_create(&capture.writer_thread, NULL, capture_writer, NULL);
    if (thread_result != 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Miniaudio writer thread creation failed: %d",
                            thread_result);
        cleanup_capture();
        capture_error = "Could not start miniaudio microphone capture";
        return 0;
    }
    capture.thread_started = 1;

    result = ma_device_start(&capture.device);
    if (result != MA_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Miniaudio capture device start failed: %d", result);
        atomic_store_explicit(&capture.stopping, true, memory_order_release);
        pthread_join(capture.writer_thread, NULL);
        capture.thread_started = 0;
        cleanup_capture();
        capture_error = "Could not start miniaudio microphone capture";
        return 0;
    }
    return 1;
}

void audio_capture_tick(JNIEnv *env)
{
    (void)env;
}

int audio_capture_stop(JNIEnv *env, uint64_t *sample_count,
                       uint64_t *overrun_count)
{
    (void)env;
    if (capture.device_initialized) {
        ma_result result = ma_device_stop(&capture.device);
        if (result != MA_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                "Miniaudio capture device stop failed: %d",
                                result);
            atomic_store_explicit(&capture.failed, true,
                                  memory_order_relaxed);
        }
    }
    atomic_store_explicit(&capture.stopping, true, memory_order_release);
    if (capture.thread_started) {
        if (pthread_join(capture.writer_thread, NULL) != 0) {
            atomic_store_explicit(&capture.failed, true,
                                  memory_order_relaxed);
        }
        capture.thread_started = 0;
    }

    int ok = !atomic_load_explicit(&capture.failed, memory_order_relaxed);
    uint64_t samples =
        atomic_load_explicit(&capture.samples_written, memory_order_relaxed);
    uint64_t overruns =
        atomic_load_explicit(&capture.overrun_frames, memory_order_relaxed);
    if (overruns > 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Miniaudio capture dropped %llu samples",
                            (unsigned long long)overruns);
    }
    if (!ok) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Miniaudio capture or writer reported a failure");
        capture_error = "Miniaudio capture failed; recording discarded";
    }
    if (sample_count != NULL) {
        *sample_count = samples;
    }
    if (overrun_count != NULL) {
        *overrun_count = overruns;
    }
    cleanup_capture();
    capture.output = NULL;
    return ok && overruns == 0;
}

void audio_capture_release(JNIEnv *env)
{
    (void)env;
}

const char *audio_capture_recording_status(void)
{
    return "Recording with Android voice-recognition preset";
}

uint64_t audio_capture_samples_written(void)
{
    return atomic_load_explicit(&capture.samples_written, memory_order_relaxed);
}

#else

/* ------------------------------------------------------------------------- */
/* Backend: AudioRecord with the optional Android NoiseSuppressor effect.    */
/* ------------------------------------------------------------------------- */

static jobject audio_record;
static jobject noise_suppressor;
static jshortArray sample_array;
static int audio_record_started;
static FILE *output_file;
static uint64_t samples_written;

static void release_noise_suppressor(JNIEnv *env)
{
    if (noise_suppressor == NULL) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, noise_suppressor);
    if (!clear_capture_exception(env, "NoiseSuppressor.getClass") &&
        cls != NULL) {
        jmethodID release = (*env)->GetMethodID(env, cls, "release", "()V");
        if (!clear_capture_exception(env, "NoiseSuppressor.release") &&
            release != NULL) {
            (*env)->CallVoidMethod(env, noise_suppressor, release);
            clear_capture_exception(env, "release NoiseSuppressor");
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    release_capture_global(env, &noise_suppressor);
}

static int create_audio_record(JNIEnv *env)
{
    jclass cls = find_capture_class(env, "android/media/AudioRecord");
    if (cls == NULL) {
        return 0;
    }

    jmethodID get_min_buffer = (*env)->GetStaticMethodID(
        env, cls, "getMinBufferSize", "(III)I");
    if (clear_capture_exception(env, "AudioRecord.getMinBufferSize") ||
        get_min_buffer == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jint min_buffer = (*env)->CallStaticIntMethod(env, cls, get_min_buffer,
                                                  SAMPLE_RATE, 16, 2);
    if (clear_capture_exception(env, "AudioRecord.getMinBufferSize") ||
        min_buffer < 0) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jmethodID constructor =
        (*env)->GetMethodID(env, cls, "<init>", "(IIIII)V");
    if (clear_capture_exception(env, "AudioRecord constructor") ||
        constructor == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jobject local_record = (*env)->NewObject(
        env, cls, constructor, 7, SAMPLE_RATE, 16, 2,
        (jint)(min_buffer > RECORD_BUFFER_SAMPLES * 4
                   ? min_buffer
                   : RECORD_BUFFER_SAMPLES * 4));
    if (clear_capture_exception(env, "create AudioRecord") ||
        local_record == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jmethodID get_state = (*env)->GetMethodID(env, cls, "getState", "()I");
    if (clear_capture_exception(env, "AudioRecord.getState") ||
        get_state == NULL) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }
    jint initialized = (*env)->CallIntMethod(env, local_record, get_state);
    if (clear_capture_exception(env, "AudioRecord.getState") ||
        initialized != 1) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jmethodID get_session =
        (*env)->GetMethodID(env, cls, "getAudioSessionId", "()I");
    if (clear_capture_exception(env, "AudioRecord.getAudioSessionId") ||
        get_session == NULL) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }
    jint session = (*env)->CallIntMethod(env, local_record, get_session);
    if (clear_capture_exception(env, "AudioRecord.getAudioSessionId")) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    recording_uses_noise_suppressor = 0;
    if (noise_suppressor_available) {
        jclass ns_cls = find_capture_class(env, "android/media/audiofx/NoiseSuppressor");
        if (ns_cls != NULL) {
            jmethodID create =
                (*env)->GetStaticMethodID(env, ns_cls, "create",
                                          "(I)Landroid/media/audiofx/"
                                          "NoiseSuppressor;");
            jobject local_ns = NULL;
            if (!clear_capture_exception(env, "NoiseSuppressor.create") &&
                create) {
                local_ns = (*env)->CallStaticObjectMethod(env, ns_cls, create,
                                                          session);
                clear_capture_exception(env, "NoiseSuppressor.create");
            }
            if (local_ns != NULL) {
                jmethodID set_enabled = (*env)->GetMethodID(
                    env, ns_cls, "setEnabled", "(Z)I");
                if (!clear_capture_exception(env, "NoiseSuppressor.setEnabled") &&
                    set_enabled != NULL) {
                    (*env)->CallIntMethod(env, local_ns, set_enabled, JNI_TRUE);
                    if (!clear_capture_exception(env, "enable NoiseSuppressor")) {
                        jmethodID get_enabled = (*env)->GetMethodID(
                            env, ns_cls, "getEnabled", "()Z");
                        if (!clear_capture_exception(env,
                                                    "NoiseSuppressor.getEnabled") &&
                            get_enabled != NULL) {
                            jboolean enabled =
                                (*env)->CallBooleanMethod(env, local_ns,
                                                          get_enabled);
                            if (!clear_capture_exception(env,
                                                         "check NoiseSuppressor") &&
                                enabled) {
                                noise_suppressor =
                                    (*env)->NewGlobalRef(env, local_ns);
                                recording_uses_noise_suppressor =
                                    noise_suppressor != NULL;
                            }
                        }
                    }
                }
                (*env)->DeleteLocalRef(env, local_ns);
            }
            (*env)->DeleteLocalRef(env, ns_cls);
        }
    }

    audio_record = (*env)->NewGlobalRef(env, local_record);
    (*env)->DeleteLocalRef(env, local_record);
    (*env)->DeleteLocalRef(env, cls);
    if (audio_record == NULL) {
        release_noise_suppressor(env);
        recording_uses_noise_suppressor = 0;
        return 0;
    }
    return 1;
}

const char *audio_capture_initialize(JNIEnv *env)
{
    jclass cls = find_capture_class(env, "android/media/audiofx/NoiseSuppressor");
    if (cls == NULL) {
        noise_suppressor_available = 0;
        return "Android noise suppression unavailable";
    }

    jmethodID method =
        (*env)->GetStaticMethodID(env, cls, "isAvailable", "()Z");
    if (clear_capture_exception(env, "NoiseSuppressor.isAvailable") ||
        method == NULL) {
        noise_suppressor_available = 0;
        (*env)->DeleteLocalRef(env, cls);
        return "Could not check Android noise suppression";
    }
    noise_suppressor_available =
        (*env)->CallStaticBooleanMethod(env, cls, method) == JNI_TRUE;
    (*env)->DeleteLocalRef(env, cls);
    if (clear_capture_exception(env, "NoiseSuppressor.isAvailable")) {
        noise_suppressor_available = 0;
        return "Could not check Android noise suppression";
    }
    return noise_suppressor_available ? "Android noise suppression is available"
                                      : "Using RNNoise after recording";
}

int audio_capture_prepare(JNIEnv *env)
{
    capture_error = NULL;
    return create_audio_record(env);
}

int audio_capture_start(JNIEnv *env, FILE *output)
{
    if (output == NULL || audio_record == NULL) {
        capture_error = "Could not start microphone";
        return 0;
    }
    output_file = output;
    samples_written = 0;

    sample_array = (*env)->NewShortArray(env, RECORD_BUFFER_SAMPLES);
    if (clear_capture_exception(env, "allocate recording buffer") ||
        sample_array == NULL) {
        capture_error = "Could not allocate recording buffer";
        return 0;
    }

    jclass cls = (*env)->GetObjectClass(env, audio_record);
    jmethodID start = (*env)->GetMethodID(env, cls, "startRecording", "()V");
    if (clear_capture_exception(env, "AudioRecord.startRecording") ||
        start == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        capture_error = "Could not start microphone";
        return 0;
    }
    (*env)->CallVoidMethod(env, audio_record, start);
    int start_failed = clear_capture_exception(env, "start recording");
    (*env)->DeleteLocalRef(env, cls);
    if (start_failed) {
        capture_error = "Microphone could not start";
        return 0;
    }
    audio_record_started = 1;
    return 1;
}

void audio_capture_tick(JNIEnv *env)
{
    /* Called by the engine only while AUDIO_RECORDING. */
    if (audio_record == NULL || output_file == NULL ||
        !audio_record_started) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, audio_record);
    jmethodID read = (*env)->GetMethodID(env, cls, "read", "([SIII)I");
    if (clear_capture_exception(env, "AudioRecord.read") || read == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        capture_error = "Could not read microphone audio";
        return;
    }
    jint count = (*env)->CallIntMethod(env, audio_record, read, sample_array, 0,
                                       RECORD_BUFFER_SAMPLES, 1);
    int read_failed = clear_capture_exception(env, "read microphone audio");
    (*env)->DeleteLocalRef(env, cls);
    if (read_failed || count < 0) {
        capture_error = "Microphone read failed; tap Stop to save";
        return;
    }
    if (count > 0) {
        jshort samples[RECORD_BUFFER_SAMPLES];
        (*env)->GetShortArrayRegion(env, sample_array, 0, count, samples);
        if (clear_capture_exception(env, "copy microphone audio") ||
            fwrite(samples, sizeof(samples[0]), (size_t)count, output_file) !=
                (size_t)count) {
            capture_error = "Could not write recording; tap Stop to save";
            return;
        }
        samples_written += (uint64_t)count;
    }
}

int audio_capture_stop(JNIEnv *env, uint64_t *sample_count,
                       uint64_t *overrun_count)
{
    if (audio_record_started && audio_record != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_record);
        jmethodID stop = (*env)->GetMethodID(env, cls, "stop", "()V");
        if (!clear_capture_exception(env, "AudioRecord.stop") &&
            stop != NULL) {
            (*env)->CallVoidMethod(env, audio_record, stop);
            clear_capture_exception(env, "stop recording");
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    audio_record_started = 0;

    release_noise_suppressor(env);
    if (audio_record != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_record);
        jmethodID release = (*env)->GetMethodID(env, cls, "release", "()V");
        if (!clear_capture_exception(env, "AudioRecord.release") &&
            release != NULL) {
            (*env)->CallVoidMethod(env, audio_record, release);
            clear_capture_exception(env, "release recorder");
        }
        (*env)->DeleteLocalRef(env, cls);
        release_capture_global(env, &audio_record);
    }
    if (sample_array != NULL) {
        (*env)->DeleteLocalRef(env, sample_array);
        sample_array = NULL;
    }
    if (sample_count != NULL) {
        *sample_count = samples_written;
    }
    if (overrun_count != NULL) {
        *overrun_count = 0;
    }
    output_file = NULL;
    return 1;
}

void audio_capture_release(JNIEnv *env)
{
    audio_record_started = 0;
    release_noise_suppressor(env);
    release_capture_global(env, &audio_record);
    if (sample_array != NULL) {
        (*env)->DeleteLocalRef(env, sample_array);
        sample_array = NULL;
    }
    output_file = NULL;
}

const char *audio_capture_recording_status(void)
{
    return recording_uses_noise_suppressor
               ? "Recording with Android noise suppression"
               : "Recording without NS; RNNoise will run on stop";
}

uint64_t audio_capture_samples_written(void)
{
    return samples_written;
}

#endif

const char *audio_capture_last_error(void)
{
    return capture_error;
}

int audio_capture_noise_suppressor_available(void)
{
    return noise_suppressor_available;
}

int audio_capture_uses_noise_suppressor(void)
{
    return recording_uses_noise_suppressor;
}
