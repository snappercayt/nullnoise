#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <android/log.h>
#include <android_native_app_glue.h>
#include "CNFGAndroid.h"
#include "audio.h"

#define CNFG_IMPLEMENTATION
#include "CNFG.h"

static int app_suspended;
static int touch_active;
static int permission_request_pending;
static int permission_result_ready;
static char permission_notice[96];
static JNIEnv *app_env;

static int microphone_permission_granted(JNIEnv *env)
{
    jclass context = (*env)->FindClass(env, "android/content/Context");
    if ((*env)->ExceptionCheck(env) || context == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "NullNoise",
                            "Could not find Android Context class");
        (*env)->ExceptionClear(env);
        return -1;
    }
    jmethodID check_permission = (*env)->GetMethodID(
        env, context, "checkSelfPermission", "(Ljava/lang/String;)I");
    if ((*env)->ExceptionCheck(env) || check_permission == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "NullNoise",
                            "Could not find Context.checkSelfPermission");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, context);
        return -1;
    }
    jstring permission =
        (*env)->NewStringUTF(env, "android.permission.RECORD_AUDIO");
    if ((*env)->ExceptionCheck(env) || permission == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "NullNoise",
                            "Could not create microphone permission name");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, context);
        return -1;
    }

    jint result = (*env)->CallIntMethod(env, gapp->activity->clazz,
                                        check_permission, permission);
    int failed = (*env)->ExceptionCheck(env);
    if (failed) {
        __android_log_print(ANDROID_LOG_ERROR, "NullNoise",
                            "Could not check microphone permission");
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, permission);
    (*env)->DeleteLocalRef(env, context);
    return failed ? -1 : result == 0;
}

static int request_microphone_permission(JNIEnv *env)
{
    int failed = 0;
    jclass activity_class = NULL;
    jclass string_class = NULL;
    jstring permission = NULL;
    jobjectArray permissions = NULL;

    activity_class = (*env)->FindClass(env, "android/app/Activity");
    if ((*env)->ExceptionCheck(env) || activity_class == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "NullNoise",
                            "Could not find Android Activity class");
        (*env)->ExceptionClear(env);
        failed = 1;
        goto cleanup;
    }
    jmethodID request = (*env)->GetMethodID(
        env, activity_class, "requestPermissions", "([Ljava/lang/String;I)V");
    if ((*env)->ExceptionCheck(env) || request == NULL) {
        failed = 1;
        goto cleanup;
    }
    string_class = (*env)->FindClass(env, "java/lang/String");
    if ((*env)->ExceptionCheck(env) || string_class == NULL) {
        failed = 1;
        goto cleanup;
    }
    permission =
        (*env)->NewStringUTF(env, "android.permission.RECORD_AUDIO");
    if ((*env)->ExceptionCheck(env) || permission == NULL) {
        failed = 1;
        goto cleanup;
    }
    permissions = (*env)->NewObjectArray(env, 1, string_class, permission);
    if ((*env)->ExceptionCheck(env) || permissions == NULL) {
        failed = 1;
        goto cleanup;
    }
    (*env)->CallVoidMethod(env, gapp->activity->clazz, request, permissions, 0);
    failed = (*env)->ExceptionCheck(env);

cleanup:
    if (failed) {
        __android_log_print(ANDROID_LOG_ERROR, "NullNoise",
                            "Could not request microphone permission");
        (*env)->ExceptionClear(env);
    }
    if (permissions != NULL) {
        (*env)->DeleteLocalRef(env, permissions);
    }
    if (permission != NULL) {
        (*env)->DeleteLocalRef(env, permission);
    }
    if (string_class != NULL) {
        (*env)->DeleteLocalRef(env, string_class);
    }
    if (activity_class != NULL) {
        (*env)->DeleteLocalRef(env, activity_class);
    }
    return !failed;
}

static int is_inside_button(int x, int y, short width, short height, int index)
{
    int button_top = height * (52 + index * 11) / 100;
    int button_bottom = height * (61 + index * 11) / 100;
    return x >= width / 10 && x <= width * 9 / 10 && y >= button_top &&
           y <= button_bottom;
}

static void handle_tap(int x, int y)
{
    short width;
    short height;
    CNFGGetDimensions(&width, &height);

    if (is_inside_button(x, y, width, height, 0)) {
        enum audio_state state = audio_get_state();
        if (state == AUDIO_RECORDING) {
            audio_stop_recording(app_env);
        } else if (state == AUDIO_PLAYING || state == AUDIO_PAUSED) {
            audio_stop_playback(app_env);
            state = AUDIO_IDLE;
        }
        if (state == AUDIO_IDLE) {
            int granted = microphone_permission_granted(app_env);
            if (granted == 1) {
                permission_notice[0] = '\0';
                audio_start_recording(app_env);
            } else if (granted == 0) {
                if (request_microphone_permission(app_env)) {
                    permission_request_pending = 1;
                    snprintf(permission_notice, sizeof(permission_notice),
                             "Allow microphone access");
                } else {
                    snprintf(permission_notice, sizeof(permission_notice),
                             "Could not request microphone access");
                }
            } else {
                snprintf(permission_notice, sizeof(permission_notice),
                         "Could not check microphone permission");
            }
        }
    } else if (is_inside_button(x, y, width, height, 1)) {
        enum audio_state state = audio_get_state();
        if (state == AUDIO_IDLE || state == AUDIO_PLAYING ||
            state == AUDIO_PAUSED) {
            audio_toggle_playback(app_env);
        }
    } else if (is_inside_button(x, y, width, height, 2)) {
        if (audio_get_state() == AUDIO_PLAYING ||
            audio_get_state() == AUDIO_PAUSED) {
            audio_stop_playback(app_env);
        }
        audio_toggle_version();
    } else if (is_inside_button(x, y, width, height, 3)) {
        audio_enhance_latest();
    }
}

void HandleKey(int keycode, int down)
{
    (void)keycode;
    (void)down;
}

void HandleButton(int x, int y, int button, int down)
{
    (void)button;
    if (down) {
        touch_active = 1;
    } else if (touch_active) {
        touch_active = 0;
        handle_tap(x, y);
    }
}

void HandleMotion(int x, int y, int pointer)
{
    (void)pointer;
    (void)x;
    (void)y;
}

int HandleDestroy(void)
{
    return 0;
}

void HandleResume(void)
{
    app_suspended = 0;
    if (permission_request_pending) {
        permission_result_ready = 1;
    }
}

void HandleSuspend(void)
{
    app_suspended = 1;
}

static void draw_centered_text(const char *text, short scale, short y)
{
    short width;
    short height;
    int text_width;
    int text_height;

    CNFGGetDimensions(&width, &height);
    CNFGGetTextExtents(text, &text_width, &text_height, scale);
    while (scale > 2 && text_width > width - 32) {
        --scale;
        CNFGGetTextExtents(text, &text_width, &text_height, scale);
    }
    CNFGPenX = (width - text_width) / 2;
    CNFGPenY = y;
    CNFGDrawText(text, scale);
}

static void draw_button(short width, short height, int index,
                        const char *label, uint32_t color)
{
    short left = width / 10;
    short right = width * 9 / 10;
    short top = height * (52 + index * 11) / 100;
    short bottom = height * (61 + index * 11) / 100;

    CNFGColor(color);
    CNFGTackRectangle(left, top, right, bottom);
    CNFGColor(0xffffffff);
    int text_width;
    int text_height;
    short scale = 8;
    CNFGGetTextExtents(label, &text_width, &text_height, scale);
    while (scale > 2 && text_width > right - left - 32) {
        --scale;
        CNFGGetTextExtents(label, &text_width, &text_height, scale);
    }
    CNFGPenX = (width - text_width) / 2;
    CNFGPenY = (top + bottom - text_height) / 2;
    CNFGDrawText(label, scale);
}

static JNIEnv *attach_to_vm(JavaVM *vm, int *attached_here)
{
    JNIEnv *env = NULL;
    jint result = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    *attached_here = 0;
    if (result == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            return NULL;
        }
        *attached_here = 1;
    } else if (result != JNI_OK) {
        return NULL;
    }
    return env;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    CNFGBGColor = 0x101820ff;
    CNFGSetupFullscreen("NullNoise", 0);

    int attached_here;
    JavaVM *vm = gapp->activity->vm;
    JNIEnv *env = attach_to_vm(vm, &attached_here);
    if (env == NULL) {
        return 1;
    }
    app_env = env;
    audio_initialize(env);

    while (CNFGHandleInput()) {
        if (app_suspended) {
            if (audio_get_state() == AUDIO_RECORDING) {
                audio_stop_recording(env);
            } else if (audio_get_state() == AUDIO_PLAYING ||
                       audio_get_state() == AUDIO_PAUSED) {
                audio_stop_playback(env);
            }
            usleep(50000);
            continue;
        }

        audio_tick(env);
        if (permission_request_pending && permission_result_ready) {
            permission_request_pending = 0;
            permission_result_ready = 0;
            int granted = microphone_permission_granted(env);
            snprintf(permission_notice, sizeof(permission_notice), "%s",
                     granted == 1
                         ? "Microphone allowed - tap Record"
                         : granted == 0
                               ? "Microphone denied - tap Record to retry"
                               : "Could not check microphone permission");
        }

        short width;
        short height;
        CNFGGetDimensions(&width, &height);
        CNFGClearFrame();
        CNFGColor(0xffffffff);
        draw_centered_text("NullNoise", 8, height * 6 / 100);
        CNFGColor(0x9aabb8ff);
        draw_centered_text(
            audio_noise_suppressor_available()
                ? "Android noise suppression is available"
                : "RNNoise will reduce noise after recording",
            4, height * 17 / 100);
        draw_centered_text(permission_notice[0] ? permission_notice
                                                : audio_get_status(),
                            4, height * 26 / 100);
        draw_centered_text(audio_has_recording()
                               ? audio_get_latest_recording_name()
                               : "No recording saved yet",
                           3, height * 35 / 100);
        draw_centered_text(audio_get_playback_name()[0]
                               ? audio_get_playback_name()
                               : "Record a clip to begin",
                           3, height * 43 / 100);

        enum audio_state state = audio_get_state();
        draw_button(width, height, 0,
                    state == AUDIO_RECORDING ? "STOP RECORDING" : "RECORD",
                    state == AUDIO_RECORDING ? 0xc44536ff : 0x287d6aff);
        draw_button(width, height, 1,
                    state == AUDIO_PLAYING
                        ? "PAUSE PLAYBACK"
                        : state == AUDIO_PAUSED ? "RESUME PLAYBACK" : "PLAY",
                    0x355c7dff);
        draw_button(width, height, 2, "ORIGINAL / ENHANCED", 0x545b66ff);
        draw_button(width, height, 3,
                    state == AUDIO_PROCESSING ? "PROCESSING..."
                                              : "NULLNOISE",
                    0x7952a0ff);
        CNFGSwapBuffers();
        usleep(16000);
    }

    if (audio_get_state() == AUDIO_RECORDING) {
        audio_stop_recording(env);
    } else if (audio_get_state() == AUDIO_PLAYING ||
               audio_get_state() == AUDIO_PAUSED) {
        audio_stop_playback(env);
    }
    if (attached_here) {
        (*vm)->DetachCurrentThread(vm);
    }
    app_env = NULL;
    return 0;
}
