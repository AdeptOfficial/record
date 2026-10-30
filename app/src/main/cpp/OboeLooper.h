#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "core/LooperCore.h"

// Connects LooperCore to a synchronised Oboe input/output pair and keeps that pair
// alive: a disconnected device (headphones plugged in, USB mic removed) reopens the
// streams on the default route instead of leaving the looper silent.
class OboeLooper {
public:
    enum class ExportResult : int32_t { Ok = 0, NoLoop = 1, Busy = 2, IoError = 3, Timeout = 4 };

    struct Status {
        looper::Snapshot snapshot;
        bool running = false;
        int32_t xruns = 0;
        int32_t errorCount = 0;
        int32_t lastError = 0;  // oboe::Result of the latest failure, 0 when none
        int32_t inputDeviceId = 0;
    };

    OboeLooper(int32_t sampleRate, double maxLoopSeconds);
    ~OboeLooper();

    oboe::Result start();
    void stop();
    oboe::Result setInputDevice(int32_t deviceId);
    void setMonitoring(bool enabled) { core_.setMonitoring(enabled); }
    bool post(looper::Command command, int64_t eventTimeNs) { return core_.post(command, eventTimeNs); }
    Status status();
    // Call about once a second: re-measures the round-trip latency (ms, or -1 when not
    // running) and reopens the streams if Oboe stopped them without reporting an error.
    double maintain();
    ExportResult exportWav(int fd);

private:
    class Duplex;
    struct Gate;

    oboe::Result openLocked();
    void closeLocked();
    double refreshLatencyLocked();
    bool streamsHealthyLocked();
    void onStreamError(oboe::AudioStream* stream, oboe::Result error);

    looper::LooperCore core_;
    std::shared_ptr<Gate> gate_;
    std::shared_ptr<Duplex> duplex_;
    std::mutex mutex_;
    std::shared_ptr<oboe::AudioStream> output_;
    std::shared_ptr<oboe::AudioStream> input_;
    bool wantRunning_ = false;
    bool open_ = false;
    int32_t inputDeviceId_ = oboe::kUnspecified;
    int32_t xrunsBeforeReopen_ = 0;
    std::atomic<int32_t> errorCount_{0};
    std::atomic<int32_t> lastError_{0};
};
