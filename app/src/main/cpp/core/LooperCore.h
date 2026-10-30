#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "SpscQueue.h"

namespace looper {

// Codes cross the JNI boundary. Keep them in sync with LoopState.kt and LooperCommand.kt.
enum class State : int32_t { Idle = 0, Recording = 1, Playing = 2, Overdubbing = 3, Stopped = 4 };

enum class Command : int32_t {
    // Touch gestures. A Tap acts at once; DoubleTap and Hold first roll back the
    // tap that began them, so no gesture has to wait for the double-tap window.
    Tap = 0,
    DoubleTap = 1,
    Hold = 2,
    DoubleTapHold = 3,
    // Direct actions (accessibility services). They act on the current state.
    Undo = 4,
    Stop = 5,
    Clear = 6,
};

// Answer to a freeze request. Waiting until the audio thread has seen that request.
enum class FreezeState : int32_t { Running = 0, Frozen = 1, Refused = 2, Waiting = 3 };

struct Snapshot {
    State state = State::Idle;
    int64_t loopFrames = 0;      // 0 until a loop is closed
    int64_t positionFrames = 0;  // playback position inside the loop
    int64_t recordedFrames = 0;  // frames captured so far while Recording
    int32_t layers = 0;          // overdub layers that can still be undone
    int32_t sampleRate = 0;
    float inputPeak = 0.0f;      // peak input level since the previous snapshot
    int32_t roundTripFrames = 0;
    int64_t maxLoopFrames = 0;
};

// Platform-free looper. It owns every buffer it needs and never allocates, locks or
// blocks inside process(), so it runs on a real-time audio thread. Commands carry the
// CLOCK_MONOTONIC time of the touch that caused them and take effect at the matching
// audio frame, not when the UI got round to sending them.
//
// Threads: one control thread calls post/set*/snapshot/requestFreeze. One audio thread calls
// process(). suspend(), writeWav() and the test hooks may run on the control thread
// only while no audio thread is calling process() (or while frozen, for writeWav()).
class LooperCore {
public:
    static constexpr int kMaxLayers = 16;
    static constexpr int kMaxWaiting = 4;
    static constexpr int32_t kMaxBlockFrames = 8192;

    LooperCore(int32_t sampleRate, double maxLoopSeconds);

    // Control thread.
    bool post(Command command, int64_t eventTimeNs);
    void setMonitoring(bool enabled) { monitoring_.store(enabled, std::memory_order_relaxed); }
    void setRoundTripFrames(int32_t frames);
    Snapshot snapshot();
    // Asks the audio thread to stop changing the loop (for an export) or to carry on.
    // Returns a token; freezeState(token) reports the answer to that request only, so a
    // stale answer from before a stream restart is never mistaken for a new one.
    uint32_t requestFreeze(bool frozen);
    FreezeState freezeState(uint32_t token) const;

    // Audio thread. `input` may hold fewer frames than `output` (or be null).
    void process(const float* input, int32_t inputFrames, float* output, int32_t outputFrames,
                 int64_t nowNs);

    // No audio thread running: apply queued commands, then park the looper so nothing
    // keeps recording while the app is in the background.
    void suspend();

    // Frozen or no audio thread running. Writes a 16-bit mono WAV of the mixed loop.
    bool writeWav(int fd) const;

    int32_t sampleRate() const { return rate_; }
    int64_t maxLoopFrames() const { return maxLoop_; }

    // Frames merged into the base inside a single audio block because an overdub could
    // not wait. Tests use it to prove long merges stay spread across blocks.
    int64_t debugSyncMergeFrames() const { return syncMergeFrames_; }

private:
    struct Layer {
        float* data = nullptr;
        int64_t startFrame = 0;  // first input frame written to this layer
        int64_t startPos = 0;    // loop position of that first write
        int64_t written = 0;     // input frames written; positions wrap every loop
        int64_t writePos = 0;    // loop position of the next write
        int64_t merged = 0;      // offsets from startPos already folded into the base
        bool used = false;
    };
    struct Pending {
        Command command;
        int64_t timeNs;
    };
    struct TapContext {
        bool valid = false;
        State prior = State::Idle;
        int64_t frame = 0;
    };
    // An overdub that started while every layer slot was full. Its audio is taken from
    // the input history once the merge of the oldest layer frees a slot.
    struct WaitingOverdub {
        int64_t start = 0;
        int64_t end = -1;  // -1 while still overdubbing (only the newest can be)
        int32_t latency = 0;
    };

    void updateClock(int64_t nowNs, int32_t frames);
    bool updateFreeze();
    void drainCommands();
    int64_t frameAt(int64_t timeNs) const;
    void handle(Command command, int64_t frame);
    void handleTap(int64_t frame);
    State priorFor(TapContext& context, int64_t frame);

    void startRecording(int64_t frame);
    void cancelRecording();
    void closeLoop(int64_t frame);
    void startOverdub(int64_t frame);
    void createLayer(int slot, int64_t startFrame, int64_t endFrame, int32_t latency);
    void serviceWaitingOverdubs();
    void resolveWaitingOverdubs();
    bool newestIsWaiting(bool running) const;
    void endOverdub(int64_t frame);
    void cancelOverdub();
    void stop(int64_t frame);
    void resume();
    void undo();
    void clear();

    int64_t loopPos(int64_t frame) const;
    float mixAt(int64_t pos) const;
    void writeOverdub(Layer& layer, float sample);
    void unwriteOverdub(Layer& layer, int64_t count);
    int freeSlot() const;
    void releaseSlot(int slot);
    int64_t mergeStep(int64_t budget);
    void maybeStartMerge();
    int undoableLayers() const;
    void publish(float peak);

    const int32_t rate_;
    const int64_t maxLoop_;
    const int64_t arenaFrames_;
    const int64_t minLoop_;
    const int64_t historyFrames_;  // power of two
    const int64_t historyMask_;
    const int64_t maxLookBack_;
    std::unique_ptr<float[]> arena_;    // base loop, then overdub layers of loop length
    std::unique_ptr<float[]> history_;  // every input frame of the last few seconds

    // Audio-thread state.
    int64_t frame_ = 0;  // index of the next frame process() will produce
    int64_t epochStart_ = 0;  // first frame since the clock was last anchored
    bool clockValid_ = false;
    double blockTimeNs_ = 0.0;
    int32_t lastBlockFrames_ = 0;
    State state_ = State::Idle;
    int64_t recordStart_ = 0;
    int64_t recorded_ = 0;
    int64_t loopLen_ = 0;
    int64_t origin_ = 0;     // frame at which loop position 0 played
    int64_t playStart_ = 0;  // frame playback last (re)started
    int64_t playPos_ = 0;
    int64_t stoppedPos_ = 0;
    Layer layers_[kMaxLayers];
    int slotCount_ = 0;
    int order_[kMaxLayers] = {};  // used slots, oldest first
    int orderCount_ = 0;
    int active_ = -1;   // slot being overdubbed
    int merging_ = -1;  // slot being folded into the base, always order_[0]
    WaitingOverdub waiting_[kMaxWaiting];  // oldest first
    int waitingCount_ = 0;
    int64_t syncMergeFrames_ = 0;
    TapContext lastTap_;
    TapContext lastDoubleTap_;

    // Shared with the control thread.
    SpscQueue<Pending, 64> queue_;
    std::atomic<bool> monitoring_{false};
    std::atomic<int32_t> roundTrip_{0};
    uint32_t freezeToken_ = 0;                 // control thread only
    std::atomic<uint32_t> freezeRequest_{0};  // (token << 1) | frozen
    std::atomic<uint32_t> freezeAck_{0};      // (token << 2) | FreezeState
    std::atomic<uint32_t> pubSeq_{0};  // seqlock: odd while the fields below change
    std::atomic<int32_t> pubState_{0};
    std::atomic<int64_t> pubLoop_{0};
    std::atomic<int64_t> pubPos_{0};
    std::atomic<int64_t> pubRecorded_{0};
    std::atomic<int32_t> pubLayers_{0};
    std::atomic<float> peak_{0.0f};
};

// Smooth limiter shared by playback and export, so a saved loop sounds as it played.
float softClip(float x);

}  // namespace looper
