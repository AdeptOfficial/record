#include "LooperCore.h"

#include <algorithm>
#include <cmath>

#include "WavWriter.h"

namespace looper {
namespace {

constexpr int64_t kMergeChunk = 65536;   // base-merge work per audio block (~60 us)
constexpr int64_t kUrgentMergeChunk = kMergeChunk * 4;  // while an overdub waits for a slot
constexpr uint32_t kTokenMask = 0x3FFFFFFF;
constexpr double kClockSmoothing = 0.05;
constexpr double kClockResetNs = 50e6;   // larger jumps mean a restart or stall
constexpr int64_t kGestureWindowSeconds = 2;

int64_t nextPowerOfTwo(int64_t v) {
    int64_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

}  // namespace

float softClip(float x) {
    if (!std::isfinite(x)) return 0.0f;
    constexpr float kKnee = 0.8f;
    const float magnitude = std::fabs(x);
    if (magnitude <= kKnee) return x;
    const float shaped = kKnee + (1.0f - kKnee) * std::tanh((magnitude - kKnee) / (1.0f - kKnee));
    return std::copysign(shaped, x);
}

LooperCore::LooperCore(int32_t sampleRate, double maxLoopSeconds)
    : rate_(sampleRate > 0 ? sampleRate : 48000),
      maxLoop_(std::max<int64_t>(rate_, std::llround(maxLoopSeconds * rate_))),
      // Base loop plus at least two overdub layers of the longest loop.
      arenaFrames_(maxLoop_ * 3),
      minLoop_(std::max<int64_t>(1, rate_ / 20)),
      historyFrames_(nextPowerOfTwo(int64_t{rate_} * 2 + 2 * kMaxBlockFrames)),
      historyMask_(historyFrames_ - 1),
      maxLookBack_(historyFrames_ - 2 * kMaxBlockFrames),
      // Left uninitialised: pages are touched only as audio is written, and the
      // layer bookkeeping never reads a position before writing it.
      arena_(new float[static_cast<size_t>(arenaFrames_)]),
      history_(new float[static_cast<size_t>(historyFrames_)]()) {}

bool LooperCore::post(Command command, int64_t eventTimeNs) {
    return queue_.push(Pending{command, eventTimeNs});
}

void LooperCore::setRoundTripFrames(int32_t frames) {
    roundTrip_.store(std::clamp(frames, 0, rate_), std::memory_order_relaxed);
}

uint32_t LooperCore::requestFreeze(bool frozen) {
    freezeToken_ = (freezeToken_ + 1) & kTokenMask;
    if (freezeToken_ == 0) freezeToken_ = 1;
    freezeRequest_.store(freezeToken_ << 1 | (frozen ? 1u : 0u), std::memory_order_release);
    return freezeToken_;
}

FreezeState LooperCore::freezeState(uint32_t token) const {
    const uint32_t ack = freezeAck_.load(std::memory_order_acquire);
    if (ack >> 2 != (token & kTokenMask)) return FreezeState::Waiting;
    return static_cast<FreezeState>(ack & 3u);
}

Snapshot LooperCore::snapshot() {
    Snapshot s;
    // Retry while the audio thread is mid-publish so fields always belong together.
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t before = pubSeq_.load(std::memory_order_acquire);
        s.state = static_cast<State>(pubState_.load(std::memory_order_relaxed));
        s.loopFrames = pubLoop_.load(std::memory_order_relaxed);
        s.positionFrames = pubPos_.load(std::memory_order_relaxed);
        s.recordedFrames = pubRecorded_.load(std::memory_order_relaxed);
        s.layers = pubLayers_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if ((before & 1u) == 0 && pubSeq_.load(std::memory_order_relaxed) == before) break;
    }
    s.sampleRate = rate_;
    s.inputPeak = peak_.exchange(0.0f, std::memory_order_relaxed);
    s.roundTripFrames = roundTrip_.load(std::memory_order_relaxed);
    s.maxLoopFrames = maxLoop_;
    return s;
}

void LooperCore::process(const float* input, int32_t inputFrames, float* output,
                         int32_t outputFrames, int64_t nowNs) {
    if (output == nullptr || outputFrames <= 0) return;
    updateClock(nowNs, outputFrames);
    const bool frozen = updateFreeze();
    if (!frozen) {
        drainCommands();
        if (waitingCount_ > 0) serviceWaitingOverdubs();
    }

    const bool monitor = monitoring_.load(std::memory_order_relaxed);
    const int32_t available = input == nullptr ? 0 : std::clamp(inputFrames, 0, outputFrames);
    float* base = arena_.get();
    float peak = 0.0f;
    for (int32_t i = 0; i < outputFrames; ++i) {
        float x = i < available ? input[i] : 0.0f;
        if (!std::isfinite(x)) x = 0.0f;
        history_[(frame_ + i) & historyMask_] = x;
        peak = std::max(peak, std::fabs(x));

        float y = 0.0f;
        switch (state_) {
            case State::Recording:
                if (recorded_ < maxLoop_) base[recorded_++] = x;
                break;
            case State::Playing:
            case State::Overdubbing:
                // Read before writing: new overdub audio is heard from the next pass,
                // never doubled with the live monitor.
                y = mixAt(playPos_);
                if (state_ == State::Overdubbing && active_ >= 0) writeOverdub(layers_[active_], x);
                if (++playPos_ == loopLen_) playPos_ = 0;
                break;
            case State::Idle:
            case State::Stopped:
                break;
        }
        if (monitor) y += x;
        output[i] = softClip(y);
    }
    frame_ += outputFrames;

    if (state_ == State::Recording && recorded_ >= maxLoop_) closeLoop(recordStart_ + maxLoop_);
    if (!frozen) mergeStep(kMergeChunk);
    publish(peak);
}

void LooperCore::suspend() {
    drainCommands();
    if (state_ == State::Recording) closeLoop(frame_);
    stop(frame_);
    // No audio thread is waiting on us now, so finish pending work in one go.
    for (int round = 0; waitingCount_ > 0 && round <= kMaxWaiting; ++round) {
        if (freeSlot() < 0) {
            maybeStartMerge();
            mergeStep(loopLen_);
        }
        resolveWaitingOverdubs();
    }
    if (merging_ >= 0) mergeStep(loopLen_);
    clockValid_ = false;
    lastTap_.valid = false;
    lastDoubleTap_.valid = false;
    publish(0.0f);
}

bool LooperCore::writeWav(int fd) const {
    if (loopLen_ <= 0) return false;
    return wav::writePcm16Mono(fd, rate_, loopLen_,
                               [this](int64_t pos) { return softClip(mixAt(pos)); });
}

void LooperCore::updateClock(int64_t nowNs, int32_t frames) {
    const auto now = static_cast<double>(nowNs);
    bool anchor = !clockValid_;
    if (!anchor) {
        const double predicted = blockTimeNs_ + lastBlockFrames_ * 1e9 / rate_;
        const double error = now - predicted;
        anchor = std::fabs(error) > kClockResetNs;  // a restart or a stall
        if (!anchor) blockTimeNs_ = predicted + error * kClockSmoothing;
    }
    if (anchor) {
        // Touches from before this point happened while no audio was flowing; they
        // must not reach back into input captured before the gap.
        blockTimeNs_ = now;
        epochStart_ = frame_;
        clockValid_ = true;
    }
    lastBlockFrames_ = frames;
}

bool LooperCore::updateFreeze() {
    const uint32_t request = freezeRequest_.load(std::memory_order_acquire);
    FreezeState answer = FreezeState::Running;
    if (request & 1u) {
        // Recording, overdubbing and a waiting overdub change the loop every block,
        // so they cannot pause for an export.
        const bool safe = waitingCount_ == 0 &&
                          (state_ == State::Idle || state_ == State::Playing || state_ == State::Stopped);
        answer = safe ? FreezeState::Frozen : FreezeState::Refused;
    }
    const uint32_t ack = (request >> 1) << 2 | static_cast<uint32_t>(answer);
    if (freezeAck_.load(std::memory_order_relaxed) != ack) freezeAck_.store(ack, std::memory_order_release);
    return answer == FreezeState::Frozen;
}

void LooperCore::drainCommands() {
    Pending pending{};
    while (queue_.pop(pending)) handle(pending.command, frameAt(pending.timeNs));
}

int64_t LooperCore::frameAt(int64_t timeNs) const {
    if (!clockValid_) return frame_;
    const double offset = (static_cast<double>(timeNs) - blockTimeNs_) * rate_ / 1e9;
    const int64_t frame = frame_ + static_cast<int64_t>(std::llround(offset));
    return std::clamp(frame, std::max({int64_t{0}, frame_ - maxLookBack_, epochStart_}), frame_);
}

State LooperCore::priorFor(TapContext& context, int64_t frame) {
    // Only a recent tap can have started this gesture; otherwise act on the state as is.
    const bool fresh = context.valid && frame - context.frame <= kGestureWindowSeconds * rate_;
    context.valid = false;
    return fresh ? context.prior : state_;
}

void LooperCore::handleTap(int64_t frame) {
    const State prior = state_;
    switch (state_) {
        case State::Idle: startRecording(frame); break;
        case State::Recording: closeLoop(frame); break;
        case State::Playing: startOverdub(frame); break;
        case State::Overdubbing: endOverdub(frame); break;
        case State::Stopped: resume(); break;
    }
    lastTap_ = TapContext{true, prior, frame};
    lastDoubleTap_.valid = false;
}

void LooperCore::handle(Command command, int64_t frame) {
    switch (command) {
        case Command::Tap:
            handleTap(frame);
            return;
        case Command::DoubleTap: {
            const State prior = priorFor(lastTap_, frame);
            switch (prior) {
                case State::Idle:  // the first tap started a recording: drop it
                    if (state_ == State::Recording) cancelRecording();
                    break;
                case State::Playing:  // the first tap started an overdub: drop it
                    cancelOverdub();
                    stop(frame);
                    break;
                case State::Stopped:  // the first tap resumed: stay where we were
                    if (state_ == State::Playing) state_ = State::Stopped;
                    break;
                case State::Recording:
                case State::Overdubbing:
                    stop(frame);
                    break;
            }
            lastDoubleTap_ = TapContext{true, prior, frame};
            return;
        }
        case Command::Hold: {
            const State prior = priorFor(lastTap_, frame);
            switch (prior) {
                case State::Idle:
                    if (state_ == State::Recording) cancelRecording();
                    break;
                case State::Recording:
                    break;
                case State::Playing:
                    cancelOverdub();
                    undo();
                    break;
                case State::Overdubbing:
                    endOverdub(frame);
                    undo();
                    break;
                case State::Stopped:
                    clear();
                    break;
            }
            return;
        }
        case Command::DoubleTapHold:
            if (priorFor(lastDoubleTap_, frame) != State::Idle) clear();
            return;
        case Command::Undo:
            endOverdub(frame);
            undo();
            break;
        case Command::Stop:
            if (state_ == State::Recording) closeLoop(frame);
            stop(frame);
            break;
        case Command::Clear:
            clear();
            break;
    }
    lastTap_.valid = false;
    lastDoubleTap_.valid = false;
}

void LooperCore::startRecording(int64_t frame) {
    clear();
    recordStart_ = frame;
    float* base = arena_.get();
    for (int64_t n = frame; n < frame_ && recorded_ < maxLoop_; ++n) {
        base[recorded_++] = history_[n & historyMask_];
    }
    state_ = State::Recording;
}

void LooperCore::cancelRecording() {
    if (state_ != State::Recording) return;
    state_ = State::Idle;
    recorded_ = 0;
}

void LooperCore::closeLoop(int64_t frame) {
    if (state_ != State::Recording) return;
    const int64_t len = std::clamp(frame, recordStart_, recordStart_ + recorded_) - recordStart_;
    if (len < minLoop_) {
        cancelRecording();
        return;
    }
    loopLen_ = len;
    slotCount_ = static_cast<int>(std::min<int64_t>(kMaxLayers, (arenaFrames_ - len) / len));
    for (int s = 0; s < slotCount_; ++s) {
        layers_[s] = Layer{};
        layers_[s].data = arena_.get() + len * (s + 1);
    }
    orderCount_ = 0;
    active_ = -1;
    merging_ = -1;
    waitingCount_ = 0;
    // The loop restarts at the touch, not when the command arrived, so the first repeat
    // stays on the beat; the few milliseconds since then are skipped once.
    origin_ = recordStart_ + len;
    playStart_ = origin_;
    playPos_ = loopPos(frame_);
    state_ = State::Playing;
}

void LooperCore::startOverdub(int64_t frame) {
    if (state_ != State::Playing || loopLen_ == 0) return;
    frame = std::max(frame, playStart_);
    // Input arriving now was played against audio heard one round trip earlier.
    const int32_t latency = roundTrip_.load(std::memory_order_relaxed);
    state_ = State::Overdubbing;
    resolveWaitingOverdubs();
    if (waitingCount_ == 0) {
        const int slot = freeSlot();
        if (slot >= 0) {
            createLayer(slot, frame, frame_, latency);
            active_ = slot;
            return;
        }
    }
    if (waitingCount_ == kMaxWaiting) {  // very fast tapping on a full stack
        syncMergeFrames_ += mergeStep(loopLen_);
        resolveWaitingOverdubs();
    }
    // Every slot holds an undoable layer: fold the oldest into the base over the next
    // few blocks and take this overdub's audio from the input history meanwhile.
    maybeStartMerge();
    waiting_[waitingCount_++] = WaitingOverdub{frame, -1, latency};
}

void LooperCore::createLayer(int slot, int64_t startFrame, int64_t endFrame, int32_t latency) {
    Layer& layer = layers_[slot];
    layer.used = true;
    layer.startFrame = startFrame;
    layer.startPos = loopPos(startFrame - latency);
    layer.written = 0;
    layer.writePos = layer.startPos;
    layer.merged = 0;
    order_[orderCount_++] = slot;
    for (int64_t n = startFrame; n < endFrame; ++n) writeOverdub(layer, history_[n & historyMask_]);
}

void LooperCore::serviceWaitingOverdubs() {
    if (freeSlot() < 0) {
        if (merging_ < 0) maybeStartMerge();
        mergeStep(kUrgentMergeChunk);
    }
    if (waitingCount_ > 0 && freeSlot() < 0 &&
        frame_ + kMaxBlockFrames - waiting_[0].start >= maxLookBack_) {
        // The oldest waiting audio is about to leave the history: finish the merge now.
        syncMergeFrames_ += mergeStep(loopLen_);
    }
    resolveWaitingOverdubs();
}

void LooperCore::resolveWaitingOverdubs() {
    while (waitingCount_ > 0) {
        const int slot = freeSlot();
        if (slot < 0) return;
        const WaitingOverdub next = waiting_[0];
        for (int i = 1; i < waitingCount_; ++i) waiting_[i - 1] = waiting_[i];
        --waitingCount_;
        const bool stillOverdubbing = next.end < 0;
        createLayer(slot, next.start, stillOverdubbing ? frame_ : next.end, next.latency);
        if (stillOverdubbing) {
            active_ = slot;
        } else if (layers_[slot].written == 0) {
            releaseSlot(slot);
        }
    }
}

bool LooperCore::newestIsWaiting(bool running) const {
    return waitingCount_ > 0 && (waiting_[waitingCount_ - 1].end < 0) == running;
}

void LooperCore::endOverdub(int64_t frame) {
    if (state_ != State::Overdubbing) return;
    state_ = State::Playing;
    if (newestIsWaiting(true)) {
        WaitingOverdub& newest = waiting_[waitingCount_ - 1];
        newest.end = std::clamp(frame, newest.start, frame_);
        return;
    }
    if (active_ < 0) return;
    const int slot = active_;
    Layer& layer = layers_[slot];
    unwriteOverdub(layer, frame_ - std::clamp(frame, layer.startFrame, frame_));
    active_ = -1;
    if (layer.written == 0) releaseSlot(slot);
}

void LooperCore::cancelOverdub() {
    if (state_ != State::Overdubbing) return;
    state_ = State::Playing;
    if (newestIsWaiting(true)) {
        --waitingCount_;
        return;
    }
    if (active_ >= 0) releaseSlot(active_);
}

void LooperCore::stop(int64_t frame) {
    if (state_ == State::Overdubbing) endOverdub(frame);
    if (state_ != State::Playing) return;
    stoppedPos_ = loopPos(std::clamp(frame, playStart_, frame_));
    state_ = State::Stopped;
}

void LooperCore::resume() {
    if (state_ != State::Stopped || loopLen_ == 0) return;
    origin_ = frame_ - stoppedPos_;
    playStart_ = frame_;
    playPos_ = stoppedPos_;
    state_ = State::Playing;
}

void LooperCore::undo() {
    // The newest layer may still be waiting for a slot; undoing it just drops it.
    if (newestIsWaiting(false)) {
        --waitingCount_;
        return;
    }
    // The base loop is never undone; only finished overdubs that are not being merged.
    for (int j = orderCount_ - 1; j >= 0; --j) {
        const int slot = order_[j];
        if (slot == active_ || slot == merging_) continue;
        releaseSlot(slot);
        return;
    }
}

void LooperCore::clear() {
    state_ = State::Idle;
    recorded_ = 0;
    loopLen_ = 0;
    playPos_ = 0;
    stoppedPos_ = 0;
    for (Layer& layer : layers_) layer.used = false;
    slotCount_ = 0;
    orderCount_ = 0;
    active_ = -1;
    merging_ = -1;
    waitingCount_ = 0;
}

int64_t LooperCore::loopPos(int64_t frame) const {
    const int64_t pos = (frame - origin_) % loopLen_;
    return pos < 0 ? pos + loopLen_ : pos;
}

float LooperCore::mixAt(int64_t pos) const {
    float sum = arena_[pos];
    for (int j = 0; j < orderCount_; ++j) {
        const Layer& layer = layers_[order_[j]];
        int64_t offset = pos - layer.startPos;
        if (offset < 0) offset += loopLen_;
        // Positions this layer has not reached yet are silent; merged ones live in the base.
        if (offset >= layer.merged && offset < std::min(layer.written, loopLen_)) {
            sum += layer.data[pos];
        }
    }
    return sum;
}

void LooperCore::writeOverdub(Layer& layer, float sample) {
    float& slot = layer.data[layer.writePos];
    slot = layer.written < loopLen_ ? sample : slot + sample;
    ++layer.written;
    if (++layer.writePos == loopLen_) layer.writePos = 0;
}

void LooperCore::unwriteOverdub(Layer& layer, int64_t count) {
    for (int64_t i = 0; i < count && layer.written > 0; ++i) {
        --layer.written;
        layer.writePos = (layer.writePos == 0 ? loopLen_ : layer.writePos) - 1;
        // First-pass writes simply become uncovered; later passes were added on top.
        if (layer.written >= loopLen_) {
            layer.data[layer.writePos] -= history_[(layer.startFrame + layer.written) & historyMask_];
        }
    }
}

int LooperCore::freeSlot() const {
    for (int s = 0; s < slotCount_; ++s) {
        if (!layers_[s].used) return s;
    }
    return -1;
}

void LooperCore::releaseSlot(int slot) {
    layers_[slot].used = false;
    int kept = 0;
    for (int j = 0; j < orderCount_; ++j) {
        if (order_[j] != slot) order_[kept++] = order_[j];
    }
    orderCount_ = kept;
    if (active_ == slot) active_ = -1;
    if (merging_ == slot) merging_ = -1;
}

void LooperCore::maybeStartMerge() {
    // Make room for an overdub by folding the oldest layer into the base.
    if (merging_ >= 0 || orderCount_ == 0 || freeSlot() >= 0) return;
    if (order_[0] != active_) merging_ = order_[0];
}

int64_t LooperCore::mergeStep(int64_t budget) {
    if (merging_ < 0) return 0;
    Layer& layer = layers_[merging_];
    const int64_t covered = std::min(layer.written, loopLen_);
    const int64_t end = std::min(covered, layer.merged + budget);
    float* base = arena_.get();
    int64_t pos = layer.startPos + layer.merged;
    if (pos >= loopLen_) pos -= loopLen_;
    for (int64_t offset = layer.merged; offset < end; ++offset) {
        base[pos] += layer.data[pos];
        if (++pos == loopLen_) pos = 0;
    }
    const int64_t done = end - layer.merged;
    layer.merged = end;
    if (layer.merged >= covered) releaseSlot(merging_);
    return done;
}

int LooperCore::undoableLayers() const {
    int waitingDone = 0;
    for (int i = 0; i < waitingCount_; ++i) waitingDone += waiting_[i].end >= 0 ? 1 : 0;
    return orderCount_ - (active_ >= 0 ? 1 : 0) - (merging_ >= 0 ? 1 : 0) + waitingDone;
}

void LooperCore::publish(float peak) {
    const uint32_t seq = pubSeq_.load(std::memory_order_relaxed);
    pubSeq_.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    pubState_.store(static_cast<int32_t>(state_), std::memory_order_relaxed);
    pubLoop_.store(loopLen_, std::memory_order_relaxed);
    pubPos_.store(state_ == State::Stopped ? stoppedPos_ : playPos_, std::memory_order_relaxed);
    pubRecorded_.store(recorded_, std::memory_order_relaxed);
    pubLayers_.store(undoableLayers(), std::memory_order_relaxed);
    pubSeq_.store(seq + 2, std::memory_order_release);
    float previous = peak_.load(std::memory_order_relaxed);
    while (peak > previous &&
           !peak_.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {
    }
}

}  // namespace looper
