#include <unistd.h>
#include <stdio.h>

#include <jni.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#define CNFG_IMPLEMENTATION
#include "CNFG.h"

#define LOG_TAG "NullNoise"

enum noise_suppressor_status {
    NOISE_SUPPRESSOR_CHECK_FAILED = -1,
    NOISE_SUPPRESSOR_UNAVAILABLE,
    NOISE_SUPPRESSOR_AVAILABLE
};

static int app_suspended;
static enum noise_suppressor_status noise_suppressor_status =
    NOISE_SUPPRESSOR_CHECK_FAILED;

static void log_jni_exception(JNIEnv *env, const char *operation)
{
    if ((*env)->ExceptionCheck(env)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Android NoiseSuppressor check failed during %s",
                            operation);
        (*env)->ExceptionClear(env);
    }
}

static enum noise_suppressor_status check_noise_suppressor(void)
{
    JavaVM *vm = gapp->activity->vm;
    JNIEnv *env = NULL;
    jint env_status = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    int attached_here = 0;

    if (env_status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                "Could not attach to the Android VM");
            return NOISE_SUPPRESSOR_CHECK_FAILED;
        }
        attached_here = 1;
    } else if (env_status != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not access the Android VM environment");
        return NOISE_SUPPRESSOR_CHECK_FAILED;
    }

    jclass suppressor_class =
        (*env)->FindClass(env, "android/media/audiofx/NoiseSuppressor");
    log_jni_exception(env, "finding NoiseSuppressor");
    if (suppressor_class == NULL) {
        if (attached_here) {
            (*vm)->DetachCurrentThread(vm);
        }
        return NOISE_SUPPRESSOR_CHECK_FAILED;
    }

    jmethodID is_available = (*env)->GetStaticMethodID(
        env, suppressor_class, "isAvailable", "()Z");
    log_jni_exception(env, "looking up isAvailable");
    if (is_available == NULL) {
        (*env)->DeleteLocalRef(env, suppressor_class);
        if (attached_here) {
            (*vm)->DetachCurrentThread(vm);
        }
        return NOISE_SUPPRESSOR_CHECK_FAILED;
    }

    jboolean available =
        (*env)->CallStaticBooleanMethod(env, suppressor_class, is_available);
    int call_failed = (*env)->ExceptionCheck(env);
    if (call_failed) {
        log_jni_exception(env, "calling isAvailable");
    }

    (*env)->DeleteLocalRef(env, suppressor_class);
    if (attached_here) {
        (*vm)->DetachCurrentThread(vm);
    }

    if (call_failed) {
        return NOISE_SUPPRESSOR_CHECK_FAILED;
    }
    return available ? NOISE_SUPPRESSOR_AVAILABLE
                     : NOISE_SUPPRESSOR_UNAVAILABLE;
}

void HandleKey(int keycode, int down)
{
    (void)keycode;
    (void)down;
}

void HandleButton(int x, int y, int button, int down)
{
    (void)x;
    (void)y;
    (void)button;
    (void)down;
}

void HandleMotion(int x, int y, int pointer)
{
    (void)x;
    (void)y;
    (void)pointer;
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
    CNFGPenY = (height - text_height) / 2 + y;
    CNFGDrawText(text, scale);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    CNFGBGColor = 0x101820ff;
    CNFGSetupFullscreen("NullNoise", 0);
    noise_suppressor_status = check_noise_suppressor();

    while (CNFGHandleInput()) {
        if (app_suspended) {
            usleep(50000);
            continue;
        }

        CNFGClearFrame();
        CNFGColor(0xffffffff);
        draw_centered_text("NullNoise", 6, -28);
        draw_centered_text("Voice recorder and enhancer", 2, 12);
        CNFGColor(0x9aabb8ff);
        if (noise_suppressor_status == NOISE_SUPPRESSOR_AVAILABLE) {
            draw_centered_text("Android noise suppression: available", 2, 42);
            draw_centered_text("Recorder integration is not implemented yet", 2, 68);
        } else if (noise_suppressor_status == NOISE_SUPPRESSOR_UNAVAILABLE) {
            draw_centered_text("Android noise suppression: unavailable", 2, 42);
            draw_centered_text("RNNoise fallback is not implemented yet", 2, 68);
        } else {
            draw_centered_text("Could not check Android noise suppression", 2, 42);
            draw_centered_text("Recorder integration is not implemented yet", 2, 68);
        }
        CNFGSwapBuffers();
        usleep(16000);
    }

    return 0;
}
