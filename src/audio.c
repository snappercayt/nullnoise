#define _POSIX_C_SOURCE 200809L

#include "audio.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "android_app.h"
#include "rnnoise.h"
#include "wav.h"

#define LOG_TAG "NullNoise"
#define SAMPLE_RATE 48000
#define FRAME_SAMPLES 480
#define RECORD_BUFFER_SAMPLES 960
#define PLAY_BUFFER_SAMPLES 960
#define TARGET_PEAK 0.89f
#define MAX_NORMALIZE_GAIN 4.0f

static int playback_first_write_logged;
static _Atomic enum audio_state state = AUDIO_IDLE;
static int noise_suppressor_available;
static int recording_uses_noise_suppressor;
static int audio_record_started;
static char status_text[128] = "Checking microphone support";
static char recording_path[1024];
static char recording_name[128];
static char original_path[1024];
static char original_name[128];
static char playback_path[1024];
static char playback_name[128];
static char enhanced_path[1024];
static char enhanced_name[128];
static FILE *recording_file;
static jobject audio_record;
static jobject noise_suppressor;
static jobject audio_track;
static jshortArray sample_array;
static jshortArray playback_array;
static FILE *playback_file;
static short playback_samples[PLAY_BUFFER_SAMPLES];
static int playback_sample_count;
static int playback_sample_offset;
static int playback_eof;
static int64_t playback_samples_written;
static int64_t playback_position_samples;
static int64_t playback_duration_samples;
static jmethodID playback_head_method;
static pthread_t enhancement_thread;
static pthread_mutex_t enhancement_mutex = PTHREAD_MUTEX_INITIALIZER;
static char enhancement_status[128];
static char status_snapshot[128];
static char name_snapshot[128];

static int make_enhanced_path(void);
static void stop_playback(JNIEnv *env);

static void set_status(const char *text)
{
    pthread_mutex_lock(&enhancement_mutex);
    snprintf(status_text, sizeof(status_text), "%s", text);
    pthread_mutex_unlock(&enhancement_mutex);
}

static void set_enhancement_status(const char *text)
{
    pthread_mutex_lock(&enhancement_mutex);
    snprintf(enhancement_status, sizeof(enhancement_status), "%s", text);
    pthread_mutex_unlock(&enhancement_mutex);
}

static int clear_exception(JNIEnv *env, const char *operation)
{
    if (!(*env)->ExceptionCheck(env)) {
        return 0;
    }

    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Audio API failed: %s",
                        operation);
    (*env)->ExceptionClear(env);
    return 1;
}

static jclass find_class(JNIEnv *env, const char *name)
{
    jclass cls = (*env)->FindClass(env, name);
    if (clear_exception(env, name) || cls == NULL) {
        return NULL;
    }
    return cls;
}

static void release_global(JNIEnv *env, jobject *object)
{
    if (*object != NULL) {
        (*env)->DeleteGlobalRef(env, *object);
        *object = NULL;
    }
}

static void release_noise_suppressor(JNIEnv *env)
{
    if (noise_suppressor == NULL) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, noise_suppressor);
    if (!clear_exception(env, "NoiseSuppressor.getClass") && cls != NULL) {
        jmethodID release = (*env)->GetMethodID(env, cls, "release", "()V");
        if (!clear_exception(env, "NoiseSuppressor.release") && release != NULL) {
            (*env)->CallVoidMethod(env, noise_suppressor, release);
            clear_exception(env, "release NoiseSuppressor");
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    release_global(env, &noise_suppressor);
}

static int create_audio_record(JNIEnv *env)
{
    jclass cls = find_class(env, "android/media/AudioRecord");
    if (cls == NULL) {
        return 0;
    }

    jmethodID get_min_buffer = (*env)->GetStaticMethodID(
        env, cls, "getMinBufferSize", "(III)I");
    if (clear_exception(env, "AudioRecord.getMinBufferSize") ||
        get_min_buffer == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jint min_buffer = (*env)->CallStaticIntMethod(env, cls, get_min_buffer,
                                                  SAMPLE_RATE, 16, 2);
    if (clear_exception(env, "AudioRecord.getMinBufferSize") || min_buffer < 0) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jmethodID constructor =
        (*env)->GetMethodID(env, cls, "<init>", "(IIIII)V");
    if (clear_exception(env, "AudioRecord constructor") || constructor == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jobject local_record = (*env)->NewObject(
        env, cls, constructor, 7, SAMPLE_RATE, 16, 2,
        (jint)(min_buffer > RECORD_BUFFER_SAMPLES * 4
                   ? min_buffer
                   : RECORD_BUFFER_SAMPLES * 4));
    if (clear_exception(env, "create AudioRecord") || local_record == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jmethodID get_state = (*env)->GetMethodID(env, cls, "getState", "()I");
    if (clear_exception(env, "AudioRecord.getState") || get_state == NULL) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }
    jint initialized = (*env)->CallIntMethod(env, local_record, get_state);
    if (clear_exception(env, "AudioRecord.getState") || initialized != 1) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    jmethodID get_session =
        (*env)->GetMethodID(env, cls, "getAudioSessionId", "()I");
    if (clear_exception(env, "AudioRecord.getAudioSessionId") ||
        get_session == NULL) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }
    jint session = (*env)->CallIntMethod(env, local_record, get_session);
    if (clear_exception(env, "AudioRecord.getAudioSessionId")) {
        (*env)->DeleteLocalRef(env, local_record);
        (*env)->DeleteLocalRef(env, cls);
        return 0;
    }

    recording_uses_noise_suppressor = 0;
    if (noise_suppressor_available) {
        jclass ns_cls = find_class(env, "android/media/audiofx/NoiseSuppressor");
        if (ns_cls != NULL) {
            jmethodID create =
                (*env)->GetStaticMethodID(env, ns_cls, "create",
                                          "(I)Landroid/media/audiofx/"
                                          "NoiseSuppressor;");
            jobject local_ns = NULL;
            if (!clear_exception(env, "NoiseSuppressor.create") && create) {
                local_ns = (*env)->CallStaticObjectMethod(env, ns_cls, create,
                                                          session);
                clear_exception(env, "NoiseSuppressor.create");
            }
            if (local_ns != NULL) {
                jmethodID set_enabled = (*env)->GetMethodID(
                    env, ns_cls, "setEnabled", "(Z)I");
                if (!clear_exception(env, "NoiseSuppressor.setEnabled") &&
                    set_enabled != NULL) {
                    (*env)->CallIntMethod(env, local_ns, set_enabled, JNI_TRUE);
                    if (!clear_exception(env, "enable NoiseSuppressor")) {
                        jmethodID get_enabled = (*env)->GetMethodID(
                            env, ns_cls, "getEnabled", "()Z");
                        if (!clear_exception(env, "NoiseSuppressor.getEnabled") &&
                            get_enabled != NULL) {
                            jboolean enabled =
                                (*env)->CallBooleanMethod(env, local_ns,
                                                          get_enabled);
                            if (!clear_exception(env,
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

void audio_initialize(JNIEnv *env)
{
    jclass cls = find_class(env, "android/media/audiofx/NoiseSuppressor");
    if (cls == NULL) {
        noise_suppressor_available = 0;
        set_status("Android noise suppression unavailable");
    } else {
        jmethodID method =
            (*env)->GetStaticMethodID(env, cls, "isAvailable", "()Z");
        if (clear_exception(env, "NoiseSuppressor.isAvailable") ||
            method == NULL) {
            noise_suppressor_available = 0;
            set_status("Could not check Android noise suppression");
        } else {
            noise_suppressor_available =
                (*env)->CallStaticBooleanMethod(env, cls, method) == JNI_TRUE;
            if (clear_exception(env, "NoiseSuppressor.isAvailable")) {
                noise_suppressor_available = 0;
                set_status("Could not check Android noise suppression");
            } else {
                set_status(noise_suppressor_available
                               ? "Android noise suppression is available"
                               : "Using RNNoise after recording");
            }
        }
        (*env)->DeleteLocalRef(env, cls);
    }

    const char *directory = GetAndroidApp()->activity->internalDataPath;
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Cannot open app audio directory");
        set_status("Cannot open app audio storage");
        return;
    }

    struct dirent *entry;
    time_t newest = 0;
    while ((entry = readdir(dir)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length < 7 || strcmp(entry->d_name + length - 4, ".wav") != 0 ||
            (strstr(entry->d_name, "_NS.wav") == NULL &&
             strstr(entry->d_name, "_RAW.wav") == NULL)) {
            continue;
        }

        char path[sizeof(original_path)];
        int written = snprintf(path, sizeof(path), "%s/%s", directory,
                               entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            continue;
        }

        struct stat info;
        if (stat(path, &info) == 0 && info.st_mtime > newest) {
            newest = info.st_mtime;
            snprintf(original_path, sizeof(original_path), "%s", path);
            snprintf(original_name, sizeof(original_name), "%s", entry->d_name);
        }
    }
    closedir(dir);

    if (original_path[0] != '\0') {
        snprintf(playback_path, sizeof(playback_path), "%s", original_path);
        snprintf(playback_name, sizeof(playback_name), "%s", original_name);
        make_enhanced_path();
        struct stat enhanced_info;
        if (stat(enhanced_path, &enhanced_info) == 0) {
            pthread_mutex_lock(&enhancement_mutex);
            snprintf(playback_path, sizeof(playback_path), "%s", enhanced_path);
            snprintf(playback_name, sizeof(playback_name), "%s", enhanced_name);
            pthread_mutex_unlock(&enhancement_mutex);
        }
        FILE *selected_file = fopen(playback_path, "rb");
        if (selected_file != NULL) {
            if (fseek(selected_file, 0, SEEK_END) == 0) {
                long size = ftell(selected_file);
                if (size >= WAV_HEADER_SIZE &&
                    (size - WAV_HEADER_SIZE) % sizeof(int16_t) == 0) {
                    playback_duration_samples =
                        (size - WAV_HEADER_SIZE) / sizeof(int16_t);
                }
            }
            fclose(selected_file);
        }
    }
}

enum audio_state audio_get_state(void)
{
    return state;
}

int audio_noise_suppressor_available(void)
{
    return noise_suppressor_available;
}

int audio_recording_uses_noise_suppressor(void)
{
    return recording_uses_noise_suppressor;
}

const char *audio_get_status(void)
{
    pthread_mutex_lock(&enhancement_mutex);
    snprintf(status_snapshot, sizeof(status_snapshot), "%s",
             state == AUDIO_PROCESSING
                 ? (enhancement_status[0] ? enhancement_status
                                           : "Enhancing audio")
                 : status_text);
    pthread_mutex_unlock(&enhancement_mutex);
    return status_snapshot;
}

const char *audio_get_latest_recording_name(void)
{
    pthread_mutex_lock(&enhancement_mutex);
    snprintf(name_snapshot, sizeof(name_snapshot), "%s", original_name);
    pthread_mutex_unlock(&enhancement_mutex);
    return name_snapshot;
}

const char *audio_get_playback_name(void)
{
    pthread_mutex_lock(&enhancement_mutex);
    snprintf(name_snapshot, sizeof(name_snapshot), "%s", playback_name);
    pthread_mutex_unlock(&enhancement_mutex);
    return name_snapshot;
}

int audio_has_recording(void)
{
    return original_path[0] != '\0';
}

int64_t audio_get_playback_position_samples(void)
{
    return playback_position_samples;
}

int64_t audio_get_playback_duration_samples(void)
{
    return playback_duration_samples;
}

int audio_start_recording(JNIEnv *env)
{
    if (state != AUDIO_IDLE) {
        set_status("Stop the current action first");
        return 0;
    }
    if (!create_audio_record(env)) {
        set_status("Could not start microphone");
        return 0;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        set_status("Could not create recording name");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }
    struct tm local_time;
    if (localtime_r(&now.tv_sec, &local_time) == NULL) {
        set_status("Could not create recording name");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }

    char base_name[64];
    if (strftime(base_name, sizeof(base_name), "recording_%Y%m%d_%H%M%S",
                 &local_time) == 0) {
        set_status("Could not create recording name");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }
    const char *suffix =
        recording_uses_noise_suppressor ? "_NS.wav" : "_RAW.wav";
    snprintf(recording_name, sizeof(recording_name), "%s_%03ld%s", base_name,
             now.tv_nsec / 1000000, suffix);
    int path_size = snprintf(recording_path, sizeof(recording_path), "%s/%s",
                              GetAndroidApp()->activity->internalDataPath,
                              recording_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(recording_path)) {
        set_status("Recording path is too long");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }

    recording_file = fopen(recording_path, "wb+");
    if (recording_file == NULL || !write_wav_header(recording_file, 0)) {
        if (recording_file != NULL) {
            fclose(recording_file);
            recording_file = NULL;
            unlink(recording_path);
        }
        set_status("Could not save recording");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }

    sample_array = (*env)->NewShortArray(env, RECORD_BUFFER_SAMPLES);
    if (clear_exception(env, "allocate recording buffer") ||
        sample_array == NULL) {
        fclose(recording_file);
        recording_file = NULL;
        unlink(recording_path);
        set_status("Could not allocate recording buffer");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }

    jclass cls = (*env)->GetObjectClass(env, audio_record);
    jmethodID start = (*env)->GetMethodID(env, cls, "startRecording", "()V");
    if (clear_exception(env, "AudioRecord.startRecording") || start == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        fclose(recording_file);
        recording_file = NULL;
        unlink(recording_path);
        (*env)->DeleteLocalRef(env, sample_array);
        sample_array = NULL;
        set_status("Could not start microphone");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }
    (*env)->CallVoidMethod(env, audio_record, start);
    int start_failed = clear_exception(env, "start recording");
    (*env)->DeleteLocalRef(env, cls);
    if (start_failed) {
        fclose(recording_file);
        recording_file = NULL;
        unlink(recording_path);
        (*env)->DeleteLocalRef(env, sample_array);
        sample_array = NULL;
        set_status("Microphone could not start");
        release_global(env, &audio_record);
        release_noise_suppressor(env);
        return 0;
    }

    audio_record_started = 1;
    state = AUDIO_RECORDING;
    set_status(recording_uses_noise_suppressor
                   ? "Recording with Android noise suppression"
                   : "Recording without NS; RNNoise will run on stop");
    return 1;
}

void audio_tick(JNIEnv *env)
{
    if (state == AUDIO_RECORDING && audio_record != NULL &&
        recording_file != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_record);
        jmethodID read = (*env)->GetMethodID(env, cls, "read", "([SIII)I");
        if (clear_exception(env, "AudioRecord.read") || read == NULL) {
            (*env)->DeleteLocalRef(env, cls);
            set_status("Could not read microphone audio");
            return;
        }
        jint count = (*env)->CallIntMethod(env, audio_record, read,
                                           sample_array, 0,
                                           RECORD_BUFFER_SAMPLES, 1);
        int read_failed = clear_exception(env, "read microphone audio");
        (*env)->DeleteLocalRef(env, cls);
        if (read_failed || count < 0) {
            set_status("Microphone read failed; tap Stop to save");
            return;
        }
        if (count > 0) {
            jshort samples[RECORD_BUFFER_SAMPLES];
            (*env)->GetShortArrayRegion(env, sample_array, 0, count, samples);
            if (clear_exception(env, "copy microphone audio") ||
                fwrite(samples, sizeof(samples[0]), (size_t)count,
                       recording_file) != (size_t)count) {
                set_status("Could not write recording; tap Stop to save");
                return;
            }
        }
    } else if (state == AUDIO_PLAYING && audio_track != NULL &&
               playback_file != NULL) {
        jint head = (*env)->CallIntMethod(env, audio_track,
                                          playback_head_method);
        if (clear_exception(env, "read playback head position")) {
            audio_stop_playback(env);
            set_status("Could not read playback position");
            return;
        }
        playback_position_samples = (uint32_t)head;
        if (playback_position_samples > playback_duration_samples) {
            playback_position_samples = playback_duration_samples;
        }
        if (playback_sample_offset == playback_sample_count && !playback_eof) {
            playback_sample_count = (int)fread(
                playback_samples, sizeof(playback_samples[0]),
                PLAY_BUFFER_SAMPLES, playback_file);
            playback_sample_offset = 0;
            if (playback_sample_count < PLAY_BUFFER_SAMPLES) {
                playback_eof = feof(playback_file);
            }
            if (ferror(playback_file)) {
                __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                    "Playback file read failed");
                set_status("Could not read saved audio");
                return;
            }
            (*env)->SetShortArrayRegion(env, playback_array, 0,
                                        playback_sample_count,
                                        playback_samples);
            if (clear_exception(env, "copy playback audio")) {
                return;
            }
        }
        if (playback_sample_offset < playback_sample_count) {
            jclass cls = (*env)->GetObjectClass(env, audio_track);
            jmethodID write = (*env)->GetMethodID(env, cls, "write",
                                                   "([SIII)I");
            if (clear_exception(env, "AudioTrack.write") || write == NULL) {
                (*env)->DeleteLocalRef(env, cls);
                set_status("Could not play saved audio");
                return;
            }
            jint written = (*env)->CallIntMethod(
                env, audio_track, write, playback_array, playback_sample_offset,
                playback_sample_count - playback_sample_offset, 1);
            int write_failed = clear_exception(env, "play audio");
            (*env)->DeleteLocalRef(env, cls);
            if (write_failed || written < 0) {
                __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                    "AudioTrack.write failed: result=%d",
                                    written);
                set_status("Audio playback failed");
                return;
            }
            if (written > 0 && !playback_first_write_logged) {
                __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                                    "First AudioTrack write succeeded: %d samples",
                                    written);
                playback_first_write_logged = 1;
            }
            playback_sample_offset += written;
            playback_samples_written += written;
        }

        if (playback_eof && playback_sample_offset == playback_sample_count) {
            if ((uint32_t)head >= (uint32_t)playback_samples_written) {
                __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                                    "Playback reached end of stream");
                playback_position_samples = playback_duration_samples;
                stop_playback(env);
                set_status("Playback finished");
            }
        }
    }
}

static void stop_playback(JNIEnv *env)
{
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "Stopping playback after writing %lld samples",
                        (long long)playback_samples_written);
    if (audio_track != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_track);
        const char *methods[] = {"pause", "flush", "release"};
        for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
            jmethodID method =
                (*env)->GetMethodID(env, cls, methods[i], "()V");
            if (!clear_exception(env, methods[i]) && method != NULL) {
                (*env)->CallVoidMethod(env, audio_track, method);
                clear_exception(env, methods[i]);
            }
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    release_global(env, &audio_track);
    playback_head_method = NULL;
    if (playback_file != NULL) {
        fclose(playback_file);
        playback_file = NULL;
    }
    if (playback_array != NULL) {
        (*env)->DeleteLocalRef(env, playback_array);
        playback_array = NULL;
    }
    playback_sample_count = 0;
    playback_sample_offset = 0;
    playback_eof = 0;
    playback_samples_written = 0;
    state = AUDIO_IDLE;
}

int audio_stop_recording(JNIEnv *env)
{
    if (state != AUDIO_RECORDING) {
        return 0;
    }

    if (audio_record_started && audio_record != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_record);
        jmethodID stop = (*env)->GetMethodID(env, cls, "stop", "()V");
        if (!clear_exception(env, "AudioRecord.stop") && stop != NULL) {
            (*env)->CallVoidMethod(env, audio_record, stop);
            clear_exception(env, "stop recording");
        }
        (*env)->DeleteLocalRef(env, cls);
    }
    audio_record_started = 0;

    release_noise_suppressor(env);
    if (audio_record != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_record);
        jmethodID release = (*env)->GetMethodID(env, cls, "release", "()V");
        if (!clear_exception(env, "AudioRecord.release") && release != NULL) {
            (*env)->CallVoidMethod(env, audio_record, release);
            clear_exception(env, "release recorder");
        }
        (*env)->DeleteLocalRef(env, cls);
        release_global(env, &audio_record);
    }
    if (sample_array != NULL) {
        (*env)->DeleteLocalRef(env, sample_array);
        sample_array = NULL;
    }

    int saved = recording_file != NULL &&
                fflush(recording_file) == 0 &&
                fseek(recording_file, 0, SEEK_END) == 0;
    long end = saved ? ftell(recording_file) : -1;
    uint64_t data_size =
        end >= WAV_HEADER_SIZE ? (uint64_t)(end - WAV_HEADER_SIZE) : 0;
    saved = saved && data_size > 0 && data_size % sizeof(int16_t) == 0 &&
            data_size <= UINT32_MAX - 36;
    uint32_t samples = saved ? (uint32_t)(data_size / sizeof(int16_t)) : 0;
    if (saved) {
        saved = write_wav_header(recording_file, samples);
    }
    if (recording_file != NULL) {
        if (fclose(recording_file) != 0) {
            saved = 0;
        }
        recording_file = NULL;
    }

    state = AUDIO_IDLE;
    recording_uses_noise_suppressor =
        recording_uses_noise_suppressor && saved;
    if (!saved) {
        unlink(recording_path);
        set_status("Recording could not be saved");
        return 0;
    }

    snprintf(original_path, sizeof(original_path), "%s", recording_path);
    snprintf(original_name, sizeof(original_name), "%s", recording_name);
    snprintf(playback_path, sizeof(playback_path), "%s", recording_path);
    snprintf(playback_name, sizeof(playback_name), "%s", recording_name);
    playback_position_samples = 0;
    playback_duration_samples = samples;
    set_status(recording_uses_noise_suppressor
                   ? "Saved with Android noise suppression"
                   : "Saved; applying RNNoise");
    if (!recording_uses_noise_suppressor) {
        return audio_enhance_latest();
    }
    return 1;
}

static int start_playback(JNIEnv *env)
{
    if (playback_path[0] == '\0') {
        set_status("Record a clip first");
        return 0;
    }
    playback_file = fopen(playback_path, "rb");
    if (playback_file == NULL || fseek(playback_file, 0, SEEK_END) != 0) {
        if (playback_file != NULL) {
            fclose(playback_file);
            playback_file = NULL;
        }
        set_status("Could not open saved audio");
        return 0;
    }
    long file_size = ftell(playback_file);
    if (file_size < WAV_HEADER_SIZE ||
        (file_size - WAV_HEADER_SIZE) % sizeof(int16_t) != 0 ||
        fseek(playback_file, WAV_HEADER_SIZE, SEEK_SET) != 0) {
        fclose(playback_file);
        playback_file = NULL;
        set_status("Saved audio is not a valid WAV recording");
        return 0;
    }
    playback_position_samples = 0;
    playback_duration_samples =
        (file_size - WAV_HEADER_SIZE) / sizeof(int16_t);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "Starting playback for %s", playback_name);

    jclass cls = find_class(env, "android/media/AudioTrack");
    if (cls == NULL) {
        fclose(playback_file);
        playback_file = NULL;
        set_status("Android audio playback is unavailable");
        return 0;
    }
    jmethodID constructor =
        (*env)->GetMethodID(env, cls, "<init>", "(IIIIII)V");
    if (clear_exception(env, "AudioTrack constructor") || constructor == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        fclose(playback_file);
        playback_file = NULL;
        set_status("Could not initialize audio playback");
        return 0;
    }
    jobject local_track = (*env)->NewObject(
        env, cls, constructor, 3, SAMPLE_RATE, 4, 2,
        PLAY_BUFFER_SAMPLES * 4, 1);
    if (clear_exception(env, "create AudioTrack") || local_track == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        fclose(playback_file);
        playback_file = NULL;
        set_status("Could not initialize audio playback");
        return 0;
    }
    jmethodID get_state = (*env)->GetMethodID(env, cls, "getState", "()I");
    int initialized = 0;
    if (!clear_exception(env, "AudioTrack.getState") && get_state != NULL) {
        initialized =
            (*env)->CallIntMethod(env, local_track, get_state) == 1;
        clear_exception(env, "check AudioTrack state");
    }
    (*env)->DeleteLocalRef(env, cls);
    if (!initialized) {
        (*env)->DeleteLocalRef(env, local_track);
        fclose(playback_file);
        playback_file = NULL;
        set_status("Could not initialize audio playback");
        return 0;
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "AudioTrack initialized: rate=%d buffer=%d mode=stream",
                        SAMPLE_RATE, PLAY_BUFFER_SAMPLES * 4);
    audio_track = (*env)->NewGlobalRef(env, local_track);
    (*env)->DeleteLocalRef(env, local_track);
    playback_array = (*env)->NewShortArray(env, PLAY_BUFFER_SAMPLES);
    if (clear_exception(env, "allocate playback buffer") ||
        audio_track == NULL || playback_array == NULL) {
        stop_playback(env);
        set_status("Could not allocate playback buffer");
        return 0;
    }

    cls = (*env)->GetObjectClass(env, audio_track);
    jmethodID play = (*env)->GetMethodID(env, cls, "play", "()V");
    int started = !clear_exception(env, "AudioTrack.play") && play != NULL;
    if (started) {
        playback_head_method = (*env)->GetMethodID(
            env, cls, "getPlaybackHeadPosition", "()I");
        started =
            !clear_exception(env, "AudioTrack.getPlaybackHeadPosition") &&
            playback_head_method != NULL;
    }
    if (started) {
        (*env)->CallVoidMethod(env, audio_track, play);
        started = !clear_exception(env, "start playback");
    }
    (*env)->DeleteLocalRef(env, cls);
    if (!started) {
        stop_playback(env);
        set_status("Could not start playback");
        return 0;
    }
    playback_first_write_logged = 0;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "AudioTrack playback started");
    state = AUDIO_PLAYING;
    set_status("Playing saved recording");
    return 1;
}

int audio_toggle_playback(JNIEnv *env)
{
    if (state == AUDIO_PLAYING) {
        jclass cls = (*env)->GetObjectClass(env, audio_track);
        jmethodID pause = (*env)->GetMethodID(env, cls, "pause", "()V");
        int ok = !clear_exception(env, "AudioTrack.pause") && pause != NULL;
        if (ok) {
            (*env)->CallVoidMethod(env, audio_track, pause);
            ok = !clear_exception(env, "pause playback");
        }
        (*env)->DeleteLocalRef(env, cls);
        if (!ok) {
            set_status("Could not pause playback");
            return 0;
        }
        state = AUDIO_PAUSED;
        set_status("Playback paused");
        return 1;
    }
    if (state == AUDIO_PAUSED && audio_track != NULL) {
        jclass cls = (*env)->GetObjectClass(env, audio_track);
        jmethodID play = (*env)->GetMethodID(env, cls, "play", "()V");
        int ok = !clear_exception(env, "AudioTrack.play") && play != NULL;
        if (ok) {
            (*env)->CallVoidMethod(env, audio_track, play);
            ok = !clear_exception(env, "resume playback");
        }
        (*env)->DeleteLocalRef(env, cls);
        if (!ok) {
            set_status("Could not resume playback");
            return 0;
        }
        state = AUDIO_PLAYING;
        set_status("Playing saved recording");
        return 1;
    }
    if (state == AUDIO_IDLE) {
        return start_playback(env);
    }
    set_status("Stop the current action first");
    return 0;
}

int audio_stop_playback(JNIEnv *env)
{
    if (state != AUDIO_PLAYING && state != AUDIO_PAUSED) {
        return 0;
    }
    stop_playback(env);
    playback_position_samples = 0;
    set_status("Playback stopped");
    return 1;
}

int audio_toggle_version(void)
{
    if (state != AUDIO_IDLE || original_path[0] == '\0' ||
        !make_enhanced_path()) {
        return 0;
    }
    pthread_mutex_lock(&enhancement_mutex);
    if (strcmp(playback_path, enhanced_path) == 0) {
        snprintf(playback_path, sizeof(playback_path), "%s", original_path);
        snprintf(playback_name, sizeof(playback_name), "%s", original_name);
    } else {
        struct stat info;
        if (stat(enhanced_path, &info) != 0) {
            pthread_mutex_unlock(&enhancement_mutex);
            set_status("Enhance the clip before selecting it");
            return 0;
        }
        snprintf(playback_path, sizeof(playback_path), "%s", enhanced_path);
        snprintf(playback_name, sizeof(playback_name), "%s", enhanced_name);
    }
    pthread_mutex_unlock(&enhancement_mutex);
    playback_position_samples = 0;
    set_status("Selected recording version");
    return 1;
}

static int make_enhanced_path(void)
{
    size_t name_length = strlen(original_name);
    const char *suffix = strstr(original_name, "_NS.wav") != NULL
                             ? "_NS.wav"
                             : "_RAW.wav";
    size_t suffix_length = strlen(suffix);
    if (name_length <= suffix_length ||
        strcmp(original_name + name_length - suffix_length, suffix) != 0) {
        return 0;
    }
    size_t base_length = name_length - suffix_length;
    int name_size = snprintf(enhanced_name, sizeof(enhanced_name),
                             "%.*s_NN.wav", (int)base_length, original_name);
    int path_size = snprintf(enhanced_path, sizeof(enhanced_path), "%s/%s",
                             GetAndroidApp()->activity->internalDataPath,
                             enhanced_name);
    return name_size > 0 && (size_t)name_size < sizeof(enhanced_name) &&
           path_size > 0 && (size_t)path_size < sizeof(enhanced_path);
}

static int process_recording(void)
{
    FILE *input = fopen(original_path, "rb");
    if (input == NULL || fseek(input, WAV_HEADER_SIZE, SEEK_SET) != 0) {
        if (input != NULL) {
            fclose(input);
        }
        set_enhancement_status("Could not open recording for enhancement");
        return 0;
    }
    struct stat output_info;
    if (stat(enhanced_path, &output_info) == 0) {
        fclose(input);
        set_enhancement_status("Enhanced file already exists");
        return 0;
    }
    FILE *output = fopen(enhanced_path, "wb+");
    if (output == NULL) {
        fclose(input);
        set_enhancement_status("Could not create enhanced recording");
        return 0;
    }
    unsigned char blank_header[WAV_HEADER_SIZE] = {0};
    if (fwrite(blank_header, 1, sizeof(blank_header), output) !=
        sizeof(blank_header)) {
        fclose(input);
        fclose(output);
        unlink(enhanced_path);
        set_enhancement_status("Could not write enhanced recording");
        return 0;
    }

    DenoiseState *denoise = rnnoise_create(NULL);
    if (denoise == NULL) {
        fclose(input);
        fclose(output);
        unlink(enhanced_path);
        set_enhancement_status("Could not initialize RNNoise");
        return 0;
    }

    int16_t input_pcm[FRAME_SAMPLES];
    int16_t output_pcm[FRAME_SAMPLES];
    float input_frame[FRAME_SAMPLES];
    float output_frame[FRAME_SAMPLES];
    uint32_t sample_count = 0;
    float peak = 0.0f;
    int ok = 1;
    for (;;) {
        size_t count = fread(input_pcm, sizeof(input_pcm[0]), FRAME_SAMPLES,
                             input);
        if (count == 0) {
            if (ferror(input)) {
                ok = 0;
            }
            break;
        }
        for (size_t i = 0; i < FRAME_SAMPLES; ++i) {
            input_frame[i] = i < count ? (float)input_pcm[i] : 0.0f;
        }
        rnnoise_process_frame(denoise, output_frame, input_frame);
        for (size_t i = 0; i < count; ++i) {
            float value = output_frame[i];
            if (value > peak) {
                peak = value;
            } else if (-value > peak) {
                peak = -value;
            }
            if (value > 32767.0f) {
                value = 32767.0f;
            } else if (value < -32768.0f) {
                value = -32768.0f;
            }
            output_pcm[i] = (int16_t)value;
        }
        if (count > (UINT32_MAX - 36) / sizeof(int16_t) - sample_count ||
            fwrite(output_pcm, sizeof(output_pcm[0]), count, output) != count) {
            ok = 0;
            break;
        }
        sample_count += (uint32_t)count;
    }
    rnnoise_destroy(denoise);

    if (ok && fflush(output) == 0 && fseek(output, WAV_HEADER_SIZE, SEEK_SET) == 0) {
        float gain = peak > 0.0f ? TARGET_PEAK * 32767.0f / peak : 1.0f;
        if (gain > MAX_NORMALIZE_GAIN) {
            gain = MAX_NORMALIZE_GAIN;
        }
        uint32_t remaining = sample_count;
        while (remaining > 0) {
            size_t count = remaining > FRAME_SAMPLES ? FRAME_SAMPLES : remaining;
            if (fread(output_pcm, sizeof(output_pcm[0]), count, output) != count) {
                ok = 0;
                break;
            }
            for (size_t i = 0; i < count; ++i) {
                float value = output_pcm[i] * gain;
                if (value > 32767.0f) {
                    value = 32767.0f;
                } else if (value < -32768.0f) {
                    value = -32768.0f;
                }
                output_pcm[i] = (int16_t)value;
            }
            if (fseek(output, -(long)(count * sizeof(output_pcm[0])), SEEK_CUR) !=
                    0 ||
                fwrite(output_pcm, sizeof(output_pcm[0]), count, output) !=
                    count ||
                fseek(output, 0, SEEK_CUR) != 0) {
                ok = 0;
                break;
            }
            remaining -= (uint32_t)count;
        }
    } else {
        ok = 0;
    }
    if (ok) {
        ok = write_wav_header(output, sample_count);
    }
    if (fclose(input) != 0) {
        ok = 0;
    }
    if (fclose(output) != 0) {
        ok = 0;
    }

    if (!ok) {
        unlink(enhanced_path);
        set_enhancement_status("RNNoise enhancement failed");
        return 0;
    }
    pthread_mutex_lock(&enhancement_mutex);
    snprintf(playback_path, sizeof(playback_path), "%s", enhanced_path);
    snprintf(playback_name, sizeof(playback_name), "%s", enhanced_name);
    snprintf(enhancement_status, sizeof(enhancement_status), "Saved %s",
             enhanced_name);
    pthread_mutex_unlock(&enhancement_mutex);
    return 1;
}

static void *enhancement_worker(void *unused)
{
    (void)unused;
    int ok = process_recording();
    pthread_mutex_lock(&enhancement_mutex);
    state = AUDIO_IDLE;
    snprintf(status_text, sizeof(status_text), "%s",
             ok ? "Noise reduced and loudness normalized"
                : enhancement_status);
    pthread_mutex_unlock(&enhancement_mutex);
    return NULL;
}

int audio_enhance_latest(void)
{
    if (state != AUDIO_IDLE) {
        set_status("Wait for the current action to finish");
        return 0;
    }
    if (original_path[0] == '\0') {
        set_status("Record a clip before using NullNoise");
        return 0;
    }
    if (!make_enhanced_path()) {
        set_status("Could not name enhanced recording");
        return 0;
    }

    pthread_mutex_lock(&enhancement_mutex);
    enhancement_status[0] = '\0';
    state = AUDIO_PROCESSING;
    pthread_mutex_unlock(&enhancement_mutex);

    int result = pthread_create(&enhancement_thread, NULL, enhancement_worker,
                                NULL);
    if (result != 0) {
        pthread_mutex_lock(&enhancement_mutex);
        state = AUDIO_IDLE;
        pthread_mutex_unlock(&enhancement_mutex);
        set_status("Could not start RNNoise");
        return 0;
    }
    pthread_detach(enhancement_thread);
    return 1;
}
