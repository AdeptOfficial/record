#include <jni.h>
#include <oboe/Oboe.h>
#include <vector>
#include <cmath>
#include <fstream>
#include <cstdint>

enum LoopState {
    Idle,
    Recording,
    Playing,
    Overdubbing,
    Stopped
};

class AudioEngine : public oboe::AudioStreamDataCallback {
public:
    void setDeviceId(int32_t deviceId) {
        this->deviceId = deviceId;
    }

    void start() {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setSharingMode(oboe::SharingMode::Exclusive);
        builder.setFormat(oboe::AudioFormat::Float);
        builder.setChannelCount(1);
        builder.setDataCallback(this);
        builder.setDeviceId(deviceId);

        oboe::Result result = builder.openStream(&outputStream);
        if (result != oboe::Result::OK || !outputStream) {
            outputStream = nullptr;
            return;
        }

        oboe::AudioStreamBuilder inputBuilder;
        inputBuilder.setDirection(oboe::Direction::Input);
        inputBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        inputBuilder.setSharingMode(oboe::SharingMode::Exclusive);
        inputBuilder.setFormat(oboe::AudioFormat::Float);
        inputBuilder.setChannelCount(1);
        inputBuilder.setDeviceId(deviceId);

        result = inputBuilder.openStream(&inputStream);
        if (result != oboe::Result::OK || !inputStream) {
            outputStream->close();
            outputStream = nullptr;
            inputStream = nullptr;
            return;
        }

        outputStream->requestStart();
        inputStream->requestStart();
    }

    void stop() {
        if (outputStream) {
            outputStream->stop();
            outputStream->close();
            outputStream = nullptr;
        }
        if (inputStream) {
            inputStream->stop();
            inputStream->close();
            inputStream = nullptr;
        }
        loopLayers.clear();
        redoStack.clear();
        playhead = 0;
        currentState = Idle;
    }

    void setLoopState(LoopState state) {
        if (currentState == Idle && state == Recording) {
            loopLayers.clear();
            redoStack.clear();
            loopLayers.emplace_back();
            playhead = 0;
        }
        if (currentState == Playing && state == Overdubbing) {
            loopLayers.emplace_back();
            playhead = 0;
        }
        currentState = state;
    }
    
    void undo() {
        if (!loopLayers.empty()) {
            redoStack.push_back(loopLayers.back());
            loopLayers.pop_back();
        }
    }

    void redo() {
        if (!redoStack.empty()) {
            loopLayers.push_back(redoStack.back());
            redoStack.pop_back();
        }
    }

    float getInstantaneousLevel() {
        return lastInputLevel;
    }

    bool exportToFile(const char* filePath) {
        if (loopLayers.empty() || loopLayers.front().empty()) {
            return false;
        }

        size_t loopLength = loopLayers.front().size();
        std::vector<float> mixedAudio(loopLength, 0.0f);

        for (const auto& layer : loopLayers) {
            if (layer.empty()) continue;
            for (size_t i = 0; i < loopLength; ++i) {
                mixedAudio[i] += layer[i % layer.size()];
            }
        }

        // Normalize to prevent clipping
        float maxSample = 0.0f;
        for (float sample : mixedAudio) {
            if (std::abs(sample) > maxSample) {
                maxSample = std::abs(sample);
            }
        }
        if (maxSample > 1.0f) {
            for (float& sample : mixedAudio) {
                sample /= maxSample;
            }
        }

        // Convert to 16-bit PCM
        std::vector<int16_t> pcmData(loopLength);
        for (size_t i = 0; i < loopLength; ++i) {
            float clamped = std::max(-1.0f, std::min(1.0f, mixedAudio[i]));
            pcmData[i] = static_cast<int16_t>(clamped * 32767.0f);
        }

        // Write WAV file
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        const int32_t sampleRate = 48000;
        const int16_t numChannels = 1;
        const int16_t bitsPerSample = 16;
        const int32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
        const int16_t blockAlign = numChannels * bitsPerSample / 8;
        const int32_t dataSize = pcmData.size() * sizeof(int16_t);
        const int32_t chunkSize = 36 + dataSize;

        // RIFF header
        file.write("RIFF", 4);
        file.write(reinterpret_cast<const char*>(&chunkSize), 4);
        file.write("WAVE", 4);

        // fmt subchunk
        file.write("fmt ", 4);
        int32_t subchunk1Size = 16;
        file.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
        int16_t audioFormat = 1; // PCM
        file.write(reinterpret_cast<const char*>(&audioFormat), 2);
        file.write(reinterpret_cast<const char*>(&numChannels), 2);
        file.write(reinterpret_cast<const char*>(&sampleRate), 4);
        file.write(reinterpret_cast<const char*>(&byteRate), 4);
        file.write(reinterpret_cast<const char*>(&blockAlign), 2);
        file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

        // data subchunk
        file.write("data", 4);
        file.write(reinterpret_cast<const char*>(&dataSize), 4);
        file.write(reinterpret_cast<const char*>(pcmData.data()), dataSize);

        file.close();
        return true;
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) override {
        std::vector<float> inputBuffer(numFrames, 0.0f);
        if (inputStream) {
            inputStream->read(inputBuffer.data(), numFrames, 0);
        }

        // Calculate RMS of input buffer
        float sum = 0.0f;
        for (int i = 0; i < numFrames; ++i) {
            sum += inputBuffer[i] * inputBuffer[i];
        }
        lastInputLevel = sqrt(sum / numFrames);


        if (currentState == Recording || currentState == Overdubbing) {
            loopLayers.back().insert(loopLayers.back().end(), inputBuffer.begin(), inputBuffer.end());
        }

        memset(audioData, 0, numFrames * sizeof(float));
        
        if (currentState == Playing || currentState == Overdubbing) {
            if (!loopLayers.empty()) {
                for (const auto& layer : loopLayers) {
                     if (layer.empty()) continue;
                    for (int i = 0; i < numFrames; ++i) {
                        reinterpret_cast<float *>(audioData)[i] += layer[(playhead + i) % layer.size()];
                    }
                }
            }
        }
        
        if (currentState == Overdubbing) {
             for (int i = 0; i < numFrames; ++i) {
                reinterpret_cast<float *>(audioData)[i] += inputBuffer[i];
            }
        }


        if ((currentState == Playing || currentState == Overdubbing) &&
            !loopLayers.empty() && !loopLayers.front().empty()) {
            playhead = (playhead + numFrames) % loopLayers.front().size();
        }

        return oboe::DataCallbackResult::Continue;
    }

private:
    oboe::AudioStream *inputStream = nullptr;
    oboe::AudioStream *outputStream = nullptr;
    std::vector<std::vector<float>> loopLayers;
    std::vector<std::vector<float>> redoStack;
    int playhead = 0;
    LoopState currentState = Idle;
    int32_t deviceId = oboe::kUnspecified;
    float lastInputLevel = 0.0f;
};

extern "C" {
JNIEXPORT jlong JNICALL
Java_com_example_record_audio_AudioEngine_native_1create(JNIEnv *env, jobject thiz) {
    return reinterpret_cast<jlong>(new AudioEngine());
}

JNIEXPORT void JNICALL
Java_com_example_record_audio_AudioEngine_native_1start(JNIEnv *env, jobject thiz, jlong engine_handle) {
    reinterpret_cast<AudioEngine *>(engine_handle)->start();
}

JNIEXPORT void JNICALL
Java_com_example_record_audio_AudioEngine_native_1stop(JNIEnv *env, jobject thiz, jlong engine_handle) {
    reinterpret_cast<AudioEngine *>(engine_handle)->stop();
    delete reinterpret_cast<AudioEngine *>(engine_handle);
}

JNIEXPORT void JNICALL
Java_com_example_record_audio_AudioEngine_native_1set_1loop_1state(JNIEnv *env, jobject thiz, jlong engine_handle, jint state) {
    reinterpret_cast<AudioEngine *>(engine_handle)->setLoopState(static_cast<LoopState>(state));
}

JNIEXPORT void JNICALL
Java_com_example_record_audio_AudioEngine_native_1set_1device_1id(JNIEnv *env, jobject thiz, jlong engine_handle, jint device_id) {
    reinterpret_cast<AudioEngine *>(engine_handle)->setDeviceId(device_id);
}

JNIEXPORT void JNICALL
Java_com_example_record_audio_AudioEngine_native_1undo(JNIEnv *env, jobject thiz, jlong engine_handle) {
    reinterpret_cast<AudioEngine *>(engine_handle)->undo();
}

JNIEXPORT void JNICALL
Java_com_example_record_audio_AudioEngine_native_1redo(JNIEnv *env, jobject thiz, jlong engine_handle) {
    reinterpret_cast<AudioEngine *>(engine_handle)->redo();
}

JNIEXPORT jfloat JNICALL
Java_com_example_record_audio_AudioEngine_native_1get_1input_1level(JNIEnv *env, jobject thiz, jlong engine_handle) {
    return reinterpret_cast<AudioEngine *>(engine_handle)->getInstantaneousLevel();
}

JNIEXPORT jboolean JNICALL
Java_com_example_record_audio_AudioEngine_native_1export_1to_1file(JNIEnv *env, jobject thiz, jlong engine_handle, jstring file_path) {
    const char* path = env->GetStringUTFChars(file_path, nullptr);
    bool result = reinterpret_cast<AudioEngine *>(engine_handle)->exportToFile(path);
    env->ReleaseStringUTFChars(file_path, path);
    return result;
}
}
