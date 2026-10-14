#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <android_native_app_glue.h>
#include "CNFGAndroid.h"
#include "audio.h"

#define CNFG_IMPLEMENTATION
#include "CNFG.h"

static int app_suspended;
static int touch_active;
static char permission_notice[96];
static JNIEnv *app_env;

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
            if (AndroidHasPermissions("RECORD_AUDIO")) {
                permission_notice[0] = '\0';
                audio_start_recording(app_env);
            } else {
                AndroidRequestAppPermissions("RECORD_AUDIO");
                snprintf(permission_notice, sizeof(permission_notice),
                         "Allow microphone access, then tap Record");
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
    CNFGGetTextExtents(label, &text_width, &text_height, 2);
    CNFGPenX = (width - text_width) / 2;
    CNFGPenY = (top + bottom - text_height) / 2;
    CNFGDrawText(label, 2);
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
        if (AndroidHasPermissions("RECORD_AUDIO")) {
            permission_notice[0] = '\0';
        }

        short width;
        short height;
        CNFGGetDimensions(&width, &height);
        CNFGClearFrame();
        CNFGColor(0xffffffff);
        draw_centered_text("NullNoise", 5, height * 6 / 100);
        CNFGColor(0x9aabb8ff);
        draw_centered_text(
            audio_noise_suppressor_available()
                ? "Android noise suppression is available"
                : "RNNoise will reduce noise after recording",
            2, height * 16 / 100);
        draw_centered_text(permission_notice[0] ? permission_notice
                                                : audio_get_status(),
                            2, height * 25 / 100);
        draw_centered_text(audio_has_recording()
                               ? audio_get_latest_recording_name()
                               : "No recording saved yet",
                           2, height * 34 / 100);
        draw_centered_text(audio_get_playback_name()[0]
                               ? audio_get_playback_name()
                               : "Record a clip to begin",
                           2, height * 42 / 100);

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
