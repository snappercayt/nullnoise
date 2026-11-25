/* Minimal JNI declarations for host compile checks only.
 *
 * This is NOT a JNI implementation. It exists so the Android-facing C files
 * can be compiled with -Wall -Wextra -Werror on a host, catching syntax,
 * type, and #if-branch errors before the NDK build. Never shipped in the APK.
 */
#ifndef NULLNOISE_HOST_JNI_H
#define NULLNOISE_HOST_JNI_H

#include <stdint.h>

typedef int32_t jint;
typedef uint8_t jboolean;
typedef int16_t jshort;
typedef int64_t jlong;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

typedef void *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jthrowable;
typedef jobject jarray;
typedef jarray jshortArray;
typedef jarray jintArray;
typedef void *jmethodID;
typedef void *jfieldID;

#define JNI_TRUE 1
#define JNI_FALSE 0

struct JNINativeInterface_;
typedef const struct JNINativeInterface_ *JNIEnv;

struct JNINativeInterface_ {
    jboolean (*ExceptionCheck)(JNIEnv *env);
    void (*ExceptionClear)(JNIEnv *env);
    jclass (*FindClass)(JNIEnv *env, const char *name);
    jclass (*GetObjectClass)(JNIEnv *env, jobject object);
    jmethodID (*GetMethodID)(JNIEnv *env, jclass cls, const char *name,
                             const char *signature);
    jmethodID (*GetStaticMethodID)(JNIEnv *env, jclass cls, const char *name,
                                   const char *signature);
    jobject (*NewGlobalRef)(JNIEnv *env, jobject object);
    void (*DeleteGlobalRef)(JNIEnv *env, jobject object);
    void (*DeleteLocalRef)(JNIEnv *env, jobject object);
    jint (*CallStaticIntMethod)(JNIEnv *env, jclass cls, jmethodID method, ...);
    jboolean (*CallStaticBooleanMethod)(JNIEnv *env, jclass cls,
                                        jmethodID method, ...);
    jobject (*CallStaticObjectMethod)(JNIEnv *env, jclass cls,
                                      jmethodID method, ...);
    jint (*CallIntMethod)(JNIEnv *env, jobject object, jmethodID method, ...);
    jboolean (*CallBooleanMethod)(JNIEnv *env, jobject object,
                                  jmethodID method, ...);
    jobject (*CallObjectMethod)(JNIEnv *env, jobject object, jmethodID method,
                                ...);
    void (*CallVoidMethod)(JNIEnv *env, jobject object, jmethodID method, ...);
    jobject (*NewObject)(JNIEnv *env, jclass cls, jmethodID method, ...);
    jshortArray (*NewShortArray)(JNIEnv *env, jsize length);
    void (*GetShortArrayRegion)(JNIEnv *env, jshortArray array, jsize start,
                                jsize length, jshort *buffer);
    void (*SetShortArrayRegion)(JNIEnv *env, jshortArray array, jsize start,
                                jsize length, const jshort *buffer);
    jstring (*NewStringUTF)(JNIEnv *env, const char *bytes);
};

#endif
