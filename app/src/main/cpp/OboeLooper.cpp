#include "OboeLooper.h"

#include <android/log.h>

#include <chrono>
#include <ctime>
#include <thread>

namespace {

constexpr const char* kTag = "RecordEngine";
constexpr int kReopenAttempts = 3;

int64_t monotonicNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

template <typename Predicate>
bool waitFor(Predicate done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

}  // namespace

// Error callbacks arrive on an Oboe thread that can outlive the engine; the gate lets
// them find out whether there is still an engine to report to.
struct OboeLooper::Gate {
    std::mutex mutex;
    OboeLooper* owner = nullptr;
};

class OboeLooper::Duplex : public oboe::FullDuplexStream, public oboe::AudioStreamErrorCallback {
public:
    Duplex(looper::LooperCore& core, std::shared_ptr<Gate> gate) : core_(core), gate_(std::move(gate)) {}

    oboe::DataCallbackResult onBothStreamsReady(const void* inputData, int numInputFrames,
                                                void* outputData, int numOutputFrames) override {
        core_.process(static_cast<const float*>(inputData), numInputFrames,
                      static_cast<float*>(outputData), numOutputFrames, monotonicNs());
        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
        std::lock_guard<std::mutex> lock(gate_->mutex);
        if (gate_->owner != nullptr) gate_->owner->onStreamError(stream, error);
    }

private:
    looper::LooperCore& core_;
    std::shared_ptr<Gate> gate_;
};

OboeLooper::OboeLooper(int32_t sampleRate, double maxLoopSeconds)
    : core_(sampleRate, maxLoopSeconds), gate_(std::make_shared<Gate>()) {
    gate_->owner = this;
    duplex_ = std::make_shared<Duplex>(core_, gate_);
}

OboeLooper::~OboeLooper() {
    {
        std::lock_guard<std::mutex> lock(gate_->mutex);
        gate_->owner = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    wantRunning_ = false;
    closeLocked();
}

oboe::Result OboeLooper::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    wantRunning_ = true;
    if (open_) return oboe::Result::OK;
    return openLocked();
}

void OboeLooper::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    wantRunning_ = false;
    closeLocked();
    core_.suspend();  // no audio thread now: park the looper
}

oboe::Result OboeLooper::setInputDevice(int32_t deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    inputDeviceId_ = deviceId;
    if (!open_) return oboe::Result::OK;
    closeLocked();
    return openLocked();
}

OboeLooper::Status OboeLooper::status() {
    std::lock_guard<std::mutex> lock(mutex_);
    Status status;
    status.snapshot = core_.snapshot();
    status.running = open_;
    status.xruns = xrunsBeforeReopen_;
    if (output_) {
        auto xruns = output_->getXRunCount();
        if (xruns) status.xruns += xruns.value();
    }
    status.errorCount = errorCount_.load();
    status.lastError = lastError_.load();
    status.inputDeviceId = inputDeviceId_;
    return status;
}

double OboeLooper::maintain() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_ && !streamsHealthyLocked()) {
        // FullDuplexStream stops the output after an input read error, and no error
        // callback follows. Treat it like a disconnect.
        __android_log_print(ANDROID_LOG_WARN, kTag, "streams stopped unexpectedly; reopening");
        errorCount_++;
        lastError_ = static_cast<int32_t>(oboe::Result::ErrorDisconnected);
        closeLocked();
        if (wantRunning_ && openLocked() == oboe::Result::OK) lastError_ = 0;
    }
    return refreshLatencyLocked();
}

bool OboeLooper::streamsHealthyLocked() {
    auto running = [](oboe::AudioStream& stream) {
        const oboe::StreamState state = stream.getState();
        return state == oboe::StreamState::Starting || state == oboe::StreamState::Started;
    };
    return output_ && input_ && running(*output_) && running(*input_);
}

OboeLooper::ExportResult OboeLooper::exportWav(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (core_.snapshot().loopFrames == 0) return ExportResult::NoLoop;
    if (!open_) return core_.writeWav(fd) ? ExportResult::Ok : ExportResult::IoError;

    // The audio thread keeps playing but stops changing the loop while we read it.
    using looper::FreezeState;
    const uint32_t freeze = core_.requestFreeze(true);
    const bool answered = waitFor([&] { return core_.freezeState(freeze) != FreezeState::Waiting; },
                                  std::chrono::milliseconds(1000));
    ExportResult result;
    if (!answered) {
        result = ExportResult::Timeout;
    } else if (core_.freezeState(freeze) == FreezeState::Refused) {
        result = ExportResult::Busy;
    } else {
        result = core_.writeWav(fd) ? ExportResult::Ok : ExportResult::IoError;
    }
    const uint32_t resume = core_.requestFreeze(false);
    waitFor([&] { return core_.freezeState(resume) == FreezeState::Running; }, std::chrono::milliseconds(500));
    return result;
}

oboe::Result OboeLooper::openLocked() {
    auto configure = [&](oboe::AudioStreamBuilder& builder, oboe::Direction direction) {
        builder.setDirection(direction)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Mono)
            // Every reopen uses the rate the loop was recorded at; Oboe resamples
            // when a newly routed device runs at another rate.
            ->setSampleRate(core_.sampleRate())
            ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
            ->setErrorCallback(duplex_);
    };

    oboe::AudioStreamBuilder outputBuilder;
    configure(outputBuilder, oboe::Direction::Output);
    outputBuilder.setDataCallback(duplex_);
    oboe::Result result = outputBuilder.openStream(output_);
    if (result != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "output open failed: %s", oboe::convertToText(result));
        output_.reset();
        lastError_ = static_cast<int32_t>(result);
        return result;
    }

    oboe::AudioStreamBuilder inputBuilder;
    configure(inputBuilder, oboe::Direction::Input);
    inputBuilder.setInputPreset(oboe::InputPreset::Unprocessed)
        ->setDeviceId(inputDeviceId_)
        ->setBufferCapacityInFrames(output_->getBufferCapacityInFrames() * 2);
    result = inputBuilder.openStream(input_);
    if (result != oboe::Result::OK) {
        // Not every device offers an unprocessed source.
        inputBuilder.setInputPreset(oboe::InputPreset::VoiceRecognition);
        result = inputBuilder.openStream(input_);
    }
    if (result != oboe::Result::OK && inputDeviceId_ != oboe::kUnspecified) {
        // The chosen input has gone away; fall back to the default microphone.
        inputDeviceId_ = oboe::kUnspecified;
        inputBuilder.setDeviceId(oboe::kUnspecified);
        result = inputBuilder.openStream(input_);
    }
    if (result != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "input open failed: %s", oboe::convertToText(result));
        output_->close();
        output_.reset();
        input_.reset();
        lastError_ = static_cast<int32_t>(result);
        return result;
    }

    duplex_->setSharedInputStream(input_);
    duplex_->setSharedOutputStream(output_);
    result = duplex_->start();
    if (result != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "start failed: %s", oboe::convertToText(result));
        closeLocked();
        lastError_ = static_cast<int32_t>(result);
        return result;
    }
    open_ = true;
    refreshLatencyLocked();
    __android_log_print(ANDROID_LOG_INFO, kTag, "streams open: %d Hz, burst %d, output buffer %d",
                        output_->getSampleRate(), output_->getFramesPerBurst(),
                        output_->getBufferSizeInFrames());
    return oboe::Result::OK;
}

void OboeLooper::closeLocked() {
    if (output_ || input_) duplex_->stop();
    if (output_) {
        auto xruns = output_->getXRunCount();
        if (xruns) xrunsBeforeReopen_ += xruns.value();
        output_->stop();
        output_->close();
    }
    if (input_) {
        input_->stop();
        input_->close();
    }
    // The streams hold the duplex callback and the duplex holds the streams; drop
    // both sides so closed streams are freed.
    std::shared_ptr<oboe::AudioStream> none;
    duplex_->setSharedInputStream(none);
    duplex_->setSharedOutputStream(none);
    output_.reset();
    input_.reset();
    open_ = false;
}

double OboeLooper::refreshLatencyLocked() {
    if (!open_ || !output_ || !input_) return -1.0;
    double ms;
    auto outputLatency = output_->calculateLatencyMillis();
    auto inputLatency = input_->calculateLatencyMillis();
    if (outputLatency && inputLatency) {
        ms = outputLatency.value() + inputLatency.value();
    } else {
        // No timestamps (older devices): estimate from the buffer sizes.
        ms = (output_->getBufferSizeInFrames() + 2.0 * input_->getFramesPerBurst()) * 1000.0 /
             core_.sampleRate();
    }
    if (ms < 0.0 || ms > 1000.0) return -1.0;
    core_.setRoundTripFrames(static_cast<int32_t>(ms * core_.sampleRate() / 1000.0));
    return ms;
}

void OboeLooper::onStreamError(oboe::AudioStream* stream, oboe::Result error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream != output_.get() && stream != input_.get()) return;  // from an earlier open
    __android_log_print(ANDROID_LOG_WARN, kTag, "stream error: %s; reopening", oboe::convertToText(error));
    errorCount_++;
    lastError_ = static_cast<int32_t>(error);
    closeLocked();
    for (int attempt = 0; wantRunning_ && attempt < kReopenAttempts; ++attempt) {
        if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
        if (openLocked() == oboe::Result::OK) {
            lastError_ = 0;
            return;
        }
    }
}
