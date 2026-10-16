#define _POSIX_C_SOURCE 200809L

#include "audio_capture.h"

#include <android/log.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "miniaudio.h"

#define CAPTURE_SAMPLE_RATE 48000
#define CAPTURE_CHANNELS 1
#define CAPTURE_BUFFER_FRAMES (CAPTURE_SAMPLE_RATE * 4)
#define LOG_TAG "NullNoise"

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

int audio_capture_start(FILE *output)
{
    if (output == NULL) {
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
        return 0;
    }
    return 1;
}

int audio_capture_stop(uint64_t *sample_count, uint64_t *overrun_count)
{
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

uint64_t audio_capture_samples_written(void)
{
    return atomic_load_explicit(&capture.samples_written, memory_order_relaxed);
}
