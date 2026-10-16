#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <android/log.h>

#include "android_app.h"
#include "audio.h"
#include "raylib.h"

#define LOG_TAG "NullNoise"
#define SAMPLE_RATE 48000

static const Color COLOR_BACKGROUND = {15, 21, 23, 255};
static const Color COLOR_CARD = {27, 36, 38, 255};
static const Color COLOR_SURFACE = {38, 49, 51, 255};
static const Color COLOR_TEXT = {241, 246, 242, 255};
static const Color COLOR_MUTED = {163, 179, 175, 255};
static const Color COLOR_ACCENT = {142, 225, 193, 255};
static const Color COLOR_ACCENT_TEXT = {14, 31, 25, 255};
static const Color COLOR_RECORD = {190, 69, 64, 255};
static const Color COLOR_PURPLE = {104, 85, 137, 255};
static const Color COLOR_WARNING = {255, 191, 164, 255};

static JNIEnv *app_env;
static struct android_app *android_app;
static void (*raylib_app_command)(struct android_app *, int32_t);
static int app_suspended;
static int permission_request_pending;
static int permission_result_ready;
static char permission_notice[128];
static char previous_exit_notice[160];

static int clear_diagnostic_exception(JNIEnv *env, const char *operation)
{
    if (!(*env)->ExceptionCheck(env)) {
        return 0;
    }
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                        "Previous-run diagnostics unavailable at %s",
                        operation);
    (*env)->ExceptionClear(env);
    return 1;
}

static void load_previous_crash_notice(JNIEnv *env)
{
    jclass context = (*env)->FindClass(env, "android/content/Context");
    if (clear_diagnostic_exception(env, "Context lookup") || context == NULL) {
        return;
    }

    jmethodID get_service = (*env)->GetMethodID(
        env, context, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    if (clear_diagnostic_exception(env, "getSystemService lookup") ||
        get_service == NULL) {
        (*env)->DeleteLocalRef(env, context);
        return;
    }
    jmethodID get_package_name =
        (*env)->GetMethodID(env, context, "getPackageName",
                            "()Ljava/lang/String;");
    if (clear_diagnostic_exception(env, "getPackageName lookup") ||
        get_package_name == NULL) {
        (*env)->DeleteLocalRef(env, context);
        return;
    }

    jstring service_name = (*env)->NewStringUTF(env, "activity");
    if (clear_diagnostic_exception(env, "activity service name") ||
        service_name == NULL) {
        (*env)->DeleteLocalRef(env, context);
        return;
    }
    jobject manager = (*env)->CallObjectMethod(
        env, android_app->activity->clazz, get_service, service_name);
    int failed = clear_diagnostic_exception(env, "get activity service");
    jstring package_name = (*env)->CallObjectMethod(
        env, android_app->activity->clazz, get_package_name);
    failed |= clear_diagnostic_exception(env, "get application package name");
    (*env)->DeleteLocalRef(env, service_name);
    (*env)->DeleteLocalRef(env, context);
    if (failed || manager == NULL || package_name == NULL) {
        if (manager != NULL) {
            (*env)->DeleteLocalRef(env, manager);
        }
        if (package_name != NULL) {
            (*env)->DeleteLocalRef(env, package_name);
        }
        return;
    }

    jclass manager_class = (*env)->GetObjectClass(env, manager);
    if (clear_diagnostic_exception(env, "ActivityManager class") ||
        manager_class == NULL) {
        (*env)->DeleteLocalRef(env, manager);
        (*env)->DeleteLocalRef(env, package_name);
        return;
    }
    jmethodID history = (*env)->GetMethodID(
        env, manager_class, "getHistoricalProcessExitReasons",
        "(Ljava/lang/String;II)Ljava/util/List;");
    if (clear_diagnostic_exception(env, "exit history lookup") ||
        history == NULL) {
        (*env)->DeleteLocalRef(env, manager_class);
        (*env)->DeleteLocalRef(env, manager);
        (*env)->DeleteLocalRef(env, package_name);
        return;
    }

    jobject exits = (*env)->CallObjectMethod(env, manager, history,
                                             package_name, 0, 1);
    failed = clear_diagnostic_exception(env, "read exit history");
    (*env)->DeleteLocalRef(env, manager_class);
    (*env)->DeleteLocalRef(env, manager);
    (*env)->DeleteLocalRef(env, package_name);
    if (failed || exits == NULL) {
        return;
    }

    jclass list_class = (*env)->FindClass(env, "java/util/List");
    if (clear_diagnostic_exception(env, "List class") ||
        list_class == NULL) {
        (*env)->DeleteLocalRef(env, exits);
        return;
    }
    jmethodID list_size = (*env)->GetMethodID(env, list_class, "size", "()I");
    if (clear_diagnostic_exception(env, "List.size lookup") ||
        list_size == NULL) {
        (*env)->DeleteLocalRef(env, list_class);
        (*env)->DeleteLocalRef(env, exits);
        return;
    }
    jmethodID list_get = (*env)->GetMethodID(
        env, list_class, "get", "(I)Ljava/lang/Object;");
    if (clear_diagnostic_exception(env, "List.get lookup") ||
        list_get == NULL) {
        (*env)->DeleteLocalRef(env, list_class);
        (*env)->DeleteLocalRef(env, exits);
        return;
    }
    jint count = (*env)->CallIntMethod(env, exits, list_size);
    failed = clear_diagnostic_exception(env, "exit history size");
    if (failed || count <= 0) {
        (*env)->DeleteLocalRef(env, list_class);
        (*env)->DeleteLocalRef(env, exits);
        return;
    }

    jobject exit_info = (*env)->CallObjectMethod(env, exits, list_get, 0);
    failed = clear_diagnostic_exception(env, "latest exit record");
    (*env)->DeleteLocalRef(env, list_class);
    (*env)->DeleteLocalRef(env, exits);
    if (failed || exit_info == NULL) {
        return;
    }
    jclass exit_class = (*env)->GetObjectClass(env, exit_info);
    if (clear_diagnostic_exception(env, "ApplicationExitInfo class") ||
        exit_class == NULL) {
        (*env)->DeleteLocalRef(env, exit_info);
        return;
    }
    jmethodID get_reason =
        (*env)->GetMethodID(env, exit_class, "getReason", "()I");
    if (clear_diagnostic_exception(env, "exit reason lookup") ||
        get_reason == NULL) {
        (*env)->DeleteLocalRef(env, exit_class);
        (*env)->DeleteLocalRef(env, exit_info);
        return;
    }
    jint reason = (*env)->CallIntMethod(env, exit_info, get_reason);
    failed = clear_diagnostic_exception(env, "read exit reason");
    (*env)->DeleteLocalRef(env, exit_class);
    (*env)->DeleteLocalRef(env, exit_info);
    if (failed) {
        return;
    }

    const char *description = NULL;
    switch (reason) {
    case 2:
        description = "terminated by signal";
        break;
    case 4:
        description = "Java crash";
        break;
    case 5:
        description = "native crash";
        break;
    case 6:
        description = "app not responding";
        break;
    case 7:
        description = "startup failure";
        break;
    default:
        return;
    }
    snprintf(previous_exit_notice, sizeof(previous_exit_notice),
             "Previous run ended: %s", description);
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s",
                        previous_exit_notice);
}

static int microphone_permission_granted(JNIEnv *env)
{
    jclass context = (*env)->FindClass(env, "android/content/Context");
    if ((*env)->ExceptionCheck(env) || context == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not find Android Context class");
        (*env)->ExceptionClear(env);
        return -1;
    }
    jmethodID check_permission = (*env)->GetMethodID(
        env, context, "checkSelfPermission", "(Ljava/lang/String;)I");
    if ((*env)->ExceptionCheck(env) || check_permission == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not find Context.checkSelfPermission");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, context);
        return -1;
    }
    jstring permission =
        (*env)->NewStringUTF(env, "android.permission.RECORD_AUDIO");
    if ((*env)->ExceptionCheck(env) || permission == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not create microphone permission name");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, context);
        return -1;
    }

    jint result = (*env)->CallIntMethod(
        env, android_app->activity->clazz, check_permission, permission);
    int failed = (*env)->ExceptionCheck(env);
    if (failed) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not check microphone permission");
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, permission);
    (*env)->DeleteLocalRef(env, context);
    return failed ? -1 : result == 0;
}

static int request_microphone_permission(JNIEnv *env)
{
    jclass activity = (*env)->FindClass(env, "android/app/Activity");
    if ((*env)->ExceptionCheck(env) || activity == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not find Android Activity class");
        (*env)->ExceptionClear(env);
        return 0;
    }
    jmethodID request = (*env)->GetMethodID(
        env, activity, "requestPermissions", "([Ljava/lang/String;I)V");
    if ((*env)->ExceptionCheck(env) || request == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not resolve Activity.requestPermissions");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activity);
        return 0;
    }
    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    int failed = (*env)->ExceptionCheck(env) || string_class == NULL;
    if (failed) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not resolve requestPermissions");
        (*env)->ExceptionClear(env);
        if (string_class != NULL) {
            (*env)->DeleteLocalRef(env, string_class);
        }
        (*env)->DeleteLocalRef(env, activity);
        return 0;
    }

    jstring permission =
        (*env)->NewStringUTF(env, "android.permission.RECORD_AUDIO");
    jobjectArray permissions = NULL;
    if (!(*env)->ExceptionCheck(env) && permission != NULL) {
        permissions = (*env)->NewObjectArray(env, 1, string_class, permission);
    }
    failed = (*env)->ExceptionCheck(env) || permissions == NULL;
    if (!failed) {
        (*env)->CallVoidMethod(env, android_app->activity->clazz, request,
                               permissions, 0);
        failed = (*env)->ExceptionCheck(env);
    }
    if (failed) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not request microphone permission");
        (*env)->ExceptionClear(env);
    }
    if (permissions != NULL) {
        (*env)->DeleteLocalRef(env, permissions);
    }
    if (permission != NULL) {
        (*env)->DeleteLocalRef(env, permission);
    }
    (*env)->DeleteLocalRef(env, string_class);
    (*env)->DeleteLocalRef(env, activity);
    return !failed;
}

static void stop_audio_for_background(void)
{
    if (app_env == NULL) {
        return;
    }
    if (audio_get_state() == AUDIO_RECORDING) {
        audio_stop_recording(app_env);
    } else if (audio_get_state() == AUDIO_PLAYING ||
               audio_get_state() == AUDIO_PAUSED) {
        audio_stop_playback(app_env);
    }
}

static void on_app_command(struct android_app *app, int32_t command)
{
    if (raylib_app_command != NULL) {
        raylib_app_command(app, command);
    }
    switch (command) {
    case APP_CMD_LOST_FOCUS:
    case APP_CMD_PAUSE:
    case APP_CMD_STOP:
    case APP_CMD_TERM_WINDOW:
        app_suspended = 1;
        stop_audio_for_background();
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_RESUME:
        app_suspended = 0;
        if (permission_request_pending) {
            permission_result_ready = 1;
        }
        break;
    default:
        break;
    }
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

static void draw_text_fitted(const char *text, Rectangle bounds,
                             float font_size, Color color, int centered)
{
    Font font = GetFontDefault();
    float spacing = font_size * 0.035f;
    Vector2 measured = MeasureTextEx(font, text, font_size, spacing);
    while (measured.x > bounds.width && font_size > 10.0f) {
        font_size -= 1.0f;
        spacing = font_size * 0.035f;
        measured = MeasureTextEx(font, text, font_size, spacing);
    }
    float x = centered ? bounds.x + (bounds.width - measured.x) * 0.5f
                       : bounds.x;
    float y = bounds.y + (bounds.height - measured.y) * 0.5f;
    DrawTextEx(font, text, (Vector2){x, y}, font_size, spacing, color);
}

static void draw_button(Rectangle bounds, const char *label, Color fill,
                        Color text, float font_size)
{
    DrawRectangleRounded(bounds, 0.22f, 12, fill);
    draw_text_fitted(label,
                     (Rectangle){bounds.x + 12.0f, bounds.y,
                                 bounds.width - 24.0f, bounds.height},
                     font_size, text, 1);
}

static void format_playback_time(int64_t samples, char *text, size_t size)
{
    int64_t seconds = samples / SAMPLE_RATE;
    snprintf(text, size, "%lld:%02lld", (long long)(seconds / 60),
             (long long)(seconds % 60));
}

static void draw_playback_track(Rectangle bounds, int64_t position,
                                int64_t duration)
{
    float progress = 0.0f;
    if (duration > 0) {
        progress = (float)position / (float)duration;
        if (progress < 0.0f) {
            progress = 0.0f;
        } else if (progress > 1.0f) {
            progress = 1.0f;
        }
    }
    DrawRectangleRounded(bounds, 0.5f, 8, COLOR_SURFACE);
    if (progress > 0.0f) {
        Rectangle fill = bounds;
        fill.width *= progress;
        DrawRectangleRounded(fill, 0.5f, 8, COLOR_ACCENT);
    }
    float knob_x = bounds.x + bounds.width * progress;
    DrawCircleV((Vector2){knob_x, bounds.y + bounds.height * 0.5f},
                bounds.height * 0.7f, COLOR_ACCENT);
}

static int playback_is_enhanced(void)
{
    const char *name = audio_get_playback_name();
    size_t length = strlen(name);
    return length >= 7 && strcmp(name + length - 7, "_NN.wav") == 0;
}

static Rectangle record_button_bounds(float width, float height)
{
    return (Rectangle){width * 0.07f, height * 0.61f, width * 0.86f,
                       height * 0.092f};
}

static Rectangle playback_button_bounds(float width, float height)
{
    return (Rectangle){width * 0.07f, height * 0.49f, width * 0.86f,
                       height * 0.09f};
}

static Rectangle version_button_bounds(float width, float height)
{
    return (Rectangle){width * 0.07f, height * 0.735f, width * 0.41f,
                       height * 0.09f};
}

static Rectangle enhance_button_bounds(float width, float height)
{
    return (Rectangle){width * 0.52f, height * 0.735f, width * 0.41f,
                       height * 0.09f};
}

static void start_recording_or_request_permission(void)
{
    enum audio_state state = audio_get_state();
    if (state == AUDIO_RECORDING) {
        audio_stop_recording(app_env);
        return;
    }
    if (state == AUDIO_PLAYING || state == AUDIO_PAUSED) {
        audio_stop_playback(app_env);
        state = AUDIO_IDLE;
    }
    if (state != AUDIO_IDLE) {
        snprintf(permission_notice, sizeof(permission_notice),
                 "Wait for the current audio task to finish");
        return;
    }

    int granted = microphone_permission_granted(app_env);
    if (granted == 1) {
        permission_notice[0] = '\0';
        audio_start_recording(app_env);
    } else if (granted == 0) {
        if (request_microphone_permission(app_env)) {
            permission_request_pending = 1;
            permission_result_ready = 0;
            snprintf(permission_notice, sizeof(permission_notice),
                     "Allow microphone access to record");
        } else {
            snprintf(permission_notice, sizeof(permission_notice),
                     "Could not request microphone access");
        }
    } else {
        snprintf(permission_notice, sizeof(permission_notice),
                 "Could not check microphone permission");
    }
}

static void handle_touch(float width, float height)
{
    Vector2 point = GetMousePosition();
    Rectangle record = record_button_bounds(width, height);
    Rectangle playback = playback_button_bounds(width, height);
    Rectangle version = version_button_bounds(width, height);
    Rectangle enhance = enhance_button_bounds(width, height);

    if (CheckCollisionPointRec(point, record)) {
        start_recording_or_request_permission();
    } else if (CheckCollisionPointRec(point, playback)) {
        enum audio_state state = audio_get_state();
        if (state == AUDIO_IDLE || state == AUDIO_PLAYING ||
            state == AUDIO_PAUSED) {
            if (state == AUDIO_IDLE && !audio_has_recording()) {
                snprintf(permission_notice, sizeof(permission_notice),
                         "Record a clip before playback");
                return;
            }
            permission_notice[0] = '\0';
            audio_toggle_playback(app_env);
        }
    } else if (CheckCollisionPointRec(point, version)) {
        enum audio_state state = audio_get_state();
        if (state == AUDIO_PLAYING || state == AUDIO_PAUSED) {
            audio_stop_playback(app_env);
            state = AUDIO_IDLE;
        }
        if (!audio_has_recording()) {
            snprintf(permission_notice, sizeof(permission_notice),
                     "Record a clip before switching versions");
        } else if (state != AUDIO_IDLE) {
            snprintf(permission_notice, sizeof(permission_notice),
                     "Wait for the current audio task to finish");
        } else {
            permission_notice[0] = '\0';
            audio_toggle_version();
        }
    } else if (CheckCollisionPointRec(point, enhance)) {
        enum audio_state state = audio_get_state();
        if (state != AUDIO_IDLE) {
            snprintf(permission_notice, sizeof(permission_notice), "%s",
                     state == AUDIO_PROCESSING
                         ? "Enhancement is already running"
                         : "Stop the current audio task first");
        } else if (!audio_has_recording()) {
            snprintf(permission_notice, sizeof(permission_notice),
                     "Record a clip before enhancing audio");
        } else {
            permission_notice[0] = '\0';
            audio_enhance_latest();
        }
    }
}

static void draw_screen(float width, float height)
{
    float scale = width / 390.0f;
    enum audio_state state = audio_get_state();
    int has_recording = audio_has_recording();
    int enhanced = playback_is_enhanced();
    int64_t position = audio_get_playback_position_samples();
    int64_t duration = audio_get_playback_duration_samples();
    char elapsed[24];
    char total[24];
    format_playback_time(position, elapsed, sizeof(elapsed));
    format_playback_time(duration, total, sizeof(total));

    ClearBackground(COLOR_BACKGROUND);
    DrawCircleV((Vector2){width * 0.87f, height * 0.075f}, 28.0f * scale,
                (Color){29, 55, 47, 255});
    DrawCircleV((Vector2){width * 0.87f, height * 0.075f}, 13.0f * scale,
                COLOR_ACCENT);
    draw_text_fitted("NULLNOISE",
                     (Rectangle){width * 0.07f, height * 0.035f,
                                 width * 0.68f, height * 0.05f},
                     25.0f * scale, COLOR_TEXT, 0);
    draw_text_fitted("YOUR VOICE, IN FOCUS",
                     (Rectangle){width * 0.07f, height * 0.092f,
                                 width * 0.72f, height * 0.03f},
                     10.0f * scale, COLOR_MUTED, 0);

    if (previous_exit_notice[0] != '\0') {
        draw_text_fitted(previous_exit_notice,
                         (Rectangle){width * 0.07f, height * 0.135f,
                                     width * 0.86f, height * 0.028f},
                         10.0f * scale, COLOR_WARNING, 0);
    }

    Rectangle card = {width * 0.055f, height * 0.175f, width * 0.89f,
                      height * 0.285f};
    DrawRectangleRounded(card, 0.075f, 14, COLOR_CARD);
    draw_text_fitted("SELECTED RECORDING",
                     (Rectangle){card.x + 18.0f * scale,
                                 card.y + 14.0f * scale,
                                 card.width - 36.0f * scale,
                                 18.0f * scale},
                     10.0f * scale, COLOR_ACCENT, 0);
    const char *clip_name =
        has_recording ? audio_get_playback_name() : "No recording yet";
    draw_text_fitted(clip_name,
                     (Rectangle){card.x + 18.0f * scale,
                                 card.y + 39.0f * scale,
                                 card.width - 36.0f * scale,
                                 32.0f * scale},
                     19.0f * scale, COLOR_TEXT, 0);
    const char *version_label =
        has_recording ? (enhanced ? "ENHANCED COPY" : "ORIGINAL RECORDING")
                      : "Record a clip to see it here";
    draw_text_fitted(version_label,
                     (Rectangle){card.x + 18.0f * scale,
                                 card.y + 76.0f * scale,
                                 card.width - 36.0f * scale,
                                 20.0f * scale},
                     11.0f * scale, COLOR_MUTED, 0);

    Rectangle track = {card.x + 18.0f * scale,
                       card.y + card.height * 0.69f,
                       card.width - 36.0f * scale, 5.0f * scale};
    draw_playback_track(track, position, duration);
    draw_text_fitted(elapsed,
                     (Rectangle){track.x, track.y + 12.0f * scale,
                                 track.width * 0.35f, 18.0f * scale},
                     11.0f * scale, COLOR_MUTED, 0);
    Vector2 total_size =
        MeasureTextEx(GetFontDefault(), total, 11.0f * scale,
                      11.0f * scale * 0.035f);
    draw_text_fitted(total,
                     (Rectangle){track.x + track.width - total_size.x,
                                 track.y + 12.0f * scale, total_size.x,
                                 18.0f * scale},
                     11.0f * scale, COLOR_MUTED, 0);

    const char *play_label =
        state == AUDIO_PLAYING
            ? "PAUSE PLAYBACK"
            : state == AUDIO_PAUSED
                  ? "RESUME PLAYBACK"
                  : has_recording ? (enhanced ? "PLAY ENHANCED CLIP"
                                              : "PLAY ORIGINAL CLIP")
                                  : "RECORD A CLIP FIRST";
    Color play_color = has_recording ? COLOR_ACCENT : COLOR_SURFACE;
    Color play_text = has_recording ? COLOR_ACCENT_TEXT : COLOR_MUTED;
    draw_button(playback_button_bounds(width, height), play_label, play_color,
                play_text, 15.0f * scale);

    const char *record_label =
        state == AUDIO_RECORDING
            ? "STOP AND SAVE"
            : state == AUDIO_PROCESSING ? "PROCESSING AUDIO"
                                        : "START RECORDING";
    Color record_color =
        state == AUDIO_RECORDING ? COLOR_RECORD : COLOR_SURFACE;
    draw_button(record_button_bounds(width, height), record_label,
                record_color, COLOR_TEXT, 15.0f * scale);

    draw_button(version_button_bounds(width, height),
                enhanced ? "USE ORIGINAL" : "USE ENHANCED",
                has_recording ? COLOR_SURFACE : (Color){31, 39, 41, 255},
                has_recording ? COLOR_TEXT : COLOR_MUTED,
                11.0f * scale);
    draw_button(enhance_button_bounds(width, height),
                state == AUDIO_PROCESSING ? "ENHANCING..." : "REDUCE NOISE",
                state == AUDIO_IDLE && has_recording
                    ? COLOR_PURPLE
                    : (Color){47, 43, 55, 255},
                state == AUDIO_IDLE && has_recording ? COLOR_TEXT : COLOR_MUTED,
                11.0f * scale);

    const char *status = permission_notice[0] != '\0'
                             ? permission_notice
                             : audio_get_status();
    draw_text_fitted(status,
                     (Rectangle){width * 0.07f, height * 0.855f,
                                 width * 0.86f, height * 0.035f},
                     11.0f * scale, COLOR_MUTED, 1);
    draw_text_fitted("YOUR AUDIO STAYS ON THIS DEVICE",
                     (Rectangle){width * 0.07f, height * 0.91f,
                                 width * 0.86f, height * 0.03f},
                     9.0f * scale, COLOR_MUTED, 1);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    android_app = GetAndroidApp();
    if (android_app == NULL || android_app->activity == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Raylib did not provide an Android activity");
        return 1;
    }

    InitWindow(0, 0, "NullNoise");
    JavaVM *vm = android_app->activity->vm;
    int attached_here;
    JNIEnv *env = attach_to_vm(vm, &attached_here);
    if (env == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "Could not attach app thread to Java VM");
        CloseWindow();
        return 1;
    }

    app_env = env;
    load_previous_crash_notice(env);
    audio_initialize(env);
    raylib_app_command = android_app->onAppCmd;
    android_app->onAppCmd = on_app_command;
    SetTargetFPS(30);

    while (!WindowShouldClose()) {
        if (!app_suspended) {
            audio_tick(env);
            if (permission_request_pending && permission_result_ready) {
                permission_request_pending = 0;
                permission_result_ready = 0;
                int granted = microphone_permission_granted(env);
                if (granted == 1) {
                    permission_notice[0] = '\0';
                    audio_start_recording(env);
                } else {
                    snprintf(permission_notice, sizeof(permission_notice), "%s",
                             granted == 0
                                 ? "Microphone denied - tap record to retry"
                                 : "Could not check microphone permission");
                }
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                handle_touch((float)GetScreenWidth(),
                             (float)GetScreenHeight());
            }
        }

        BeginDrawing();
        draw_screen((float)GetScreenWidth(), (float)GetScreenHeight());
        EndDrawing();
    }

    stop_audio_for_background();
    CloseWindow();
    app_env = NULL;
    if (attached_here) {
        (*vm)->DetachCurrentThread(vm);
    }
    return 0;
}
