/* Minimal android_native_app_glue.h types for host compile checks. */
#ifndef NULLNOISE_HOST_NATIVE_APP_GLUE_H
#define NULLNOISE_HOST_NATIVE_APP_GLUE_H

#include <jni.h>

struct ANativeActivity {
    const char *internalDataPath;
    jobject clazz;
};

struct android_app {
    struct ANativeActivity *activity;
};

#endif
