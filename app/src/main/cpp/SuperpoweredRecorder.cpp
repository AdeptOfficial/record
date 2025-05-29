#include <jni.h>
#include <unistd.h>
#include <cmath>
#include <android/log.h>
#include <SuperpoweredSimple.h>
#include <SuperpoweredRecorder.h>
#include <Superpowered.h>
#include "OpenSource/SuperpoweredAndroidAudioIO.h"
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include <atomic>
#include <mutex>
#include <sys/stat.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "SuperpoweredRecord", __VA_ARGS__)

// Globals
static SuperpoweredAndroidAudioIO* audioIO = nullptr;
static Superpowered::Recorder* recorder = nullptr;
static std::string destinationPath;
static std::atomic<bool> isRecording(false);
static std::once_flag initFlag;
static std::mutex recorderMutex;
static int numChannels = 1;
static unsigned int sampleRate = 44100;
static float* floatBuffer = nullptr;
static unsigned int floatBufferSize = 0;

// Audio Input Callback
static bool audioCallback(
        void* /*clientData*/,
        short int* audioInputOutput,
        int numberOfFrames,
        int /*samplerate*/
) {
    if (!isRecording.load() || !audioInputOutput || numberOfFrames <= 0) return false;

    Superpowered::Recorder* localRecorder;
    {
        std::lock_guard<std::mutex> lock(recorderMutex);
        localRecorder = recorder;
    }

    if (!localRecorder) return false;

    const unsigned int numSamples = numberOfFrames * numChannels;

    if (floatBufferSize < numSamples) {
        delete[] floatBuffer;
        floatBuffer = new float[numSamples];
        floatBufferSize = numSamples;
        LOG("🆕 Allocated float buffer of %u samples", numSamples);
    }

    Superpowered::ShortIntToFloat(audioInputOutput, floatBuffer, numSamples);
    localRecorder->recordInterleaved(floatBuffer, numberOfFrames);

    float rms = 0.0f;
    for (unsigned int i = 0; i < numSamples; ++i) rms += floatBuffer[i] * floatBuffer[i];
    rms = sqrt(rms / numSamples);
    LOG(rms > 0.001f ? "🎧 Sound detected (RMS: %.5f)" : "🤫 Silence detected (RMS: %.5f)", rms);

    return false;
}

extern "C" {

JNIEXPORT void JNICALL
Java_dev_adeptproductions_recorder_audio_NativeBridge_startSuperpoweredRecording(
        JNIEnv* env, jobject, jstring path, jint sampleRateIn
) {
    std::lock_guard<std::mutex> lock(recorderMutex);

    std::call_once(initFlag, []() {
        Superpowered::Initialize("ExampleLicenseKey-WillExpire-OnNextUpdate");
        LOG("🔧 Superpowered initialized.");
    });

    const char* nativePath = env->GetStringUTFChars(path, nullptr);
    destinationPath = nativePath ? std::string(nativePath) : "";
    env->ReleaseStringUTFChars(path, nativePath);

    if (destinationPath.empty()) {
        LOG("❌ Invalid file path.");
        return;
    }

    sampleRate = static_cast<unsigned int>(sampleRateIn);
    numChannels = 1;

    LOG("🎙️ Preparing to record to: %s", destinationPath.c_str());
    recorder = new Superpowered::Recorder(nullptr, numChannels == 2);

    if (!recorder->prepare(destinationPath.c_str(), sampleRate, false, numChannels)) {
        LOG("❌ Recorder prepare() failed.");
        delete recorder;
        recorder = nullptr;
        return;
    } else {
        LOG("✅ Recorder prepare() succeeded.");
    }

    LOG("🚀 Recorder will start writing once audio callback provides data.");

    audioIO = new SuperpoweredAndroidAudioIO(
            sampleRate, 512, true, false, audioCallback, nullptr, -1, -1
    );

    if (audioIO) {
        LOG("✅ AudioIO started.");
    } else {
        LOG("❌ AudioIO failed.");
    }

    isRecording.store(true);
    LOG("🔴 Recording active.");
}

// Stop recording
JNIEXPORT void JNICALL
Java_dev_adeptproductions_recorder_audio_NativeBridge_stopSuperpoweredRecording(
        JNIEnv*, jobject
) {
    Superpowered::Recorder* localRecorder;
    SuperpoweredAndroidAudioIO* localAudioIO;

    {
        std::lock_guard<std::mutex> lock(recorderMutex);
        localRecorder = recorder;
        recorder = nullptr;
        localAudioIO = audioIO;
        audioIO = nullptr;
        isRecording.store(false);
    }

    if (localRecorder) {
        LOG("🛑 Stopping recorder...");
        localRecorder->stop();

        // Wait for recorder to finish writing
        int waitCount = 0;
        while (!localRecorder->isFinished() && waitCount++ < 5000) usleep(1000);  // max 5s

        if (!localRecorder->isFinished()) {
            LOG("⚠️ Recorder did not finish in time.");
        } else {
            LOG("✅ Recorder finished writing.");
        }

        // Confirm final file size
        struct stat st;
        if (stat(destinationPath.c_str(), &st) == 0) {
            if (st.st_size > 0) {
                LOG("📦 File written: %s (%lld bytes)", destinationPath.c_str(), (long long)st.st_size);
            } else {
                LOG("⚠️ File found but is empty: %s", destinationPath.c_str());
            }
        } else {
            LOG("❌ File not found after stop: %s", destinationPath.c_str());
        }

        delete localRecorder;
    }

    if (localAudioIO) {
        delete localAudioIO;
        LOG("🧼 AudioIO cleaned up.");
    }

    if (floatBuffer) {
        delete[] floatBuffer;
        floatBuffer = nullptr;
        floatBufferSize = 0;
        LOG("🧽 Float buffer released.");
    }

    destinationPath.clear();
    LOG("✅ Cleanup complete.");
}

}
