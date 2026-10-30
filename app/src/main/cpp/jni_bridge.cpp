#include <jni.h>

#include <cstring>

#include "OboeLooper.h"

// Layout of the status array shared with NativeBridge.kt.
namespace status_index {
constexpr int kState = 0;
constexpr int kLoopFrames = 1;
constexpr int kPositionFrames = 2;
constexpr int kRecordedFrames = 3;
constexpr int kLayers = 4;
constexpr int kSampleRate = 5;
constexpr int kInputPeakBits = 6;
constexpr int kRoundTripFrames = 7;
constexpr int kMaxLoopFrames = 8;
constexpr int kRunning = 9;
constexpr int kXruns = 10;
constexpr int kErrorCount = 11;
constexpr int kLastError = 12;
constexpr int kInputDeviceId = 13;
constexpr int kSize = 14;
}  // namespace status_index

namespace {
OboeLooper* engine(jlong handle) { return reinterpret_cast<OboeLooper*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeCreate(JNIEnv*, jclass, jint sampleRate,
                                                                 jdouble maxLoopSeconds) {
    return reinterpret_cast<jlong>(new OboeLooper(sampleRate, maxLoopSeconds));
}

JNIEXPORT void JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete engine(handle);
}

JNIEXPORT jint JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeStart(JNIEnv*, jclass, jlong handle) {
    return static_cast<jint>(engine(handle)->start());
}

JNIEXPORT void JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeStop(JNIEnv*, jclass, jlong handle) {
    engine(handle)->stop();
}

JNIEXPORT jint JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeSetInputDevice(JNIEnv*, jclass, jlong handle,
                                                                         jint deviceId) {
    return static_cast<jint>(engine(handle)->setInputDevice(deviceId));
}

JNIEXPORT void JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeSetMonitoring(JNIEnv*, jclass, jlong handle,
                                                                        jboolean enabled) {
    engine(handle)->setMonitoring(enabled == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativePost(JNIEnv*, jclass, jlong handle,
                                                               jint command, jlong eventTimeNs) {
    if (command < 0 || command > static_cast<jint>(looper::Command::Clear)) return JNI_FALSE;
    return engine(handle)->post(static_cast<looper::Command>(command), eventTimeNs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeStatus(JNIEnv* env, jclass, jlong handle,
                                                                 jlongArray out) {
    if (env->GetArrayLength(out) < status_index::kSize) return;
    const OboeLooper::Status s = engine(handle)->status();
    jint peakBits;
    std::memcpy(&peakBits, &s.snapshot.inputPeak, sizeof peakBits);
    jlong values[status_index::kSize];
    values[status_index::kState] = static_cast<jlong>(s.snapshot.state);
    values[status_index::kLoopFrames] = s.snapshot.loopFrames;
    values[status_index::kPositionFrames] = s.snapshot.positionFrames;
    values[status_index::kRecordedFrames] = s.snapshot.recordedFrames;
    values[status_index::kLayers] = s.snapshot.layers;
    values[status_index::kSampleRate] = s.snapshot.sampleRate;
    values[status_index::kInputPeakBits] = peakBits;
    values[status_index::kRoundTripFrames] = s.snapshot.roundTripFrames;
    values[status_index::kMaxLoopFrames] = s.snapshot.maxLoopFrames;
    values[status_index::kRunning] = s.running ? 1 : 0;
    values[status_index::kXruns] = s.xruns;
    values[status_index::kErrorCount] = s.errorCount;
    values[status_index::kLastError] = s.lastError;
    values[status_index::kInputDeviceId] = s.inputDeviceId;
    env->SetLongArrayRegion(out, 0, status_index::kSize, values);
}

JNIEXPORT jdouble JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeMaintain(JNIEnv*, jclass, jlong handle) {
    return engine(handle)->maintain();
}

JNIEXPORT jint JNICALL
Java_dev_adeptproductions_record_audio_NativeBridge_nativeExportWav(JNIEnv*, jclass, jlong handle, jint fd) {
    return static_cast<jint>(engine(handle)->exportWav(fd));
}

}  // extern "C"
