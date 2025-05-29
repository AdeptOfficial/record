#include <jni.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NativeLog", __VA_ARGS__)

extern "C"
JNIEXPORT void JNICALL
Java_dev_adeptproductions_recorder_audio_NativeBridge_testNativeCall(JNIEnv *, jobject) {
LOGI("✅ Native function called from Kotlin.");
}
