// Behaviour tests for LooperCore, including regressions for the bugs found in the
// original engine (crash on repeated undo, playhead reset, short-layer repeats,
// doubled monitoring, 300 ms late taps, no latency compensation).
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "Check.h"
#include "core/LooperCore.h"

using looper::Command;
using looper::FreezeState;
using looper::LooperCore;
using looper::State;

namespace {

constexpr int32_t kRate = 48000;
constexpr int32_t kBlock = 192;

// Drives the core with a perfect clock: frame n starts at n / kRate seconds.
struct Rig {
    explicit Rig(double maxLoopSeconds = 10.0) : core(kRate, maxLoopSeconds) {}

    // The rig's clock; `gapNs` shifts it to simulate time passing with no audio.
    int64_t timeNs(int64_t frame) const { return frame * 1'000'000'000LL / kRate + gapNs; }

    std::vector<float> run(int64_t count, const std::function<float(int64_t)>& input) {
        std::vector<float> out;
        out.reserve(static_cast<size_t>(count));
        float in[kBlock];
        float block[kBlock];
        while (count > 0) {
            const auto n = static_cast<int32_t>(std::min<int64_t>(kBlock, count));
            for (int32_t i = 0; i < n; ++i) in[i] = input(frames + i);
            core.process(in, n, block, n, timeNs(frames));
            out.insert(out.end(), block, block + n);
            frames += n;
            count -= n;
        }
        return out;
    }

    std::vector<float> run(int64_t count, float level = 0.0f) {
        return run(count, [level](int64_t) { return level; });
    }

    void post(Command command, int64_t atFrame) { CHECK(core.post(command, timeNs(atFrame))); }
    void post(Command command) { post(command, frames); }

    // Records a loop of `len` frames from `input` and leaves the core Playing.
    void recordLoop(int64_t len, const std::function<float(int64_t)>& input) {
        post(Command::Tap);
        const int64_t start = frames;
        run(len, [&](int64_t f) { return input(f - start); });
        post(Command::Tap, start + len);
        run(kBlock);  // the close is applied at the start of this block
    }

    looper::Snapshot snap() { return core.snapshot(); }

    LooperCore core;
    int64_t frames = 0;
    int64_t gapNs = 0;
};

// Output for one full loop pass, indexed by loop position.
std::vector<float> loopPass(Rig& rig) {
    const auto s = rig.snap();
    const int64_t len = s.loopFrames;
    const int64_t pos = s.positionFrames;
    const std::vector<float> out = rig.run(len);
    std::vector<float> byPos(static_cast<size_t>(len));
    for (int64_t i = 0; i < len; ++i) byPos[static_cast<size_t>((pos + i) % len)] = out[static_cast<size_t>(i)];
    return byPos;
}

float ramp(int64_t pos, int64_t len) { return 0.5f * static_cast<float>(pos) / static_cast<float>(len); }

}  // namespace

TEST(repeatedUndoWhileOverdubbingKeepsTheBaseLoop) {
    Rig rig;
    rig.recordLoop(4800, [](int64_t) { return 0.25f; });
    rig.post(Command::Tap);  // start overdub
    rig.run(960, 0.1f);
    rig.post(Command::Undo);  // was: removes the layer being written
    rig.post(Command::Undo);  // was: removes the base loop, then crashes on back()
    rig.run(kBlock);
    rig.post(Command::Tap);
    rig.run(480, 0.1f);
    rig.post(Command::Hold);
    rig.post(Command::Tap);
    rig.post(Command::Hold);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Playing);
    CHECK(rig.snap().loopFrames == 4800);
    CHECK(rig.snap().layers == 0);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.25, 1e-6);
}

TEST(startingAnOverdubKeepsThePlayhead) {
    Rig rig;
    const int64_t len = 9600;
    rig.recordLoop(len, [&](int64_t f) { return ramp(f, len); });
    rig.run(4000);
    const int64_t before = rig.snap().positionFrames;
    rig.post(Command::Tap);
    rig.run(kBlock);
    const int64_t after = rig.snap().positionFrames;
    CHECK(rig.snap().state == State::Overdubbing);
    CHECK((after - before + len) % len == kBlock);  // was: jumped back to 0
}

TEST(shortOverdubPlaysOnlyWhereItWasRecorded) {
    Rig rig;
    const int64_t len = 9600;
    rig.recordLoop(len, [](int64_t) { return 0.0f; });
    rig.post(Command::Tap);
    const int64_t start = rig.snap().positionFrames;
    rig.run(1200, 0.5f);
    rig.post(Command::Tap);
    rig.run(kBlock);
    const auto pass = loopPass(rig);
    int64_t loud = 0;
    for (int64_t p = 0; p < len; ++p) {
        const bool inside = (p - start + len) % len < 1200;
        CHECK_NEAR(pass[p], inside ? 0.5 : 0.0, 1e-6);
        loud += pass[p] > 0.25f;
    }
    CHECK(loud == 1200);  // was: the 1200-frame layer repeated eight times per loop
}

TEST(overdubIsNotDoubledWithTheMonitor) {
    Rig rig;
    rig.core.setMonitoring(true);
    rig.recordLoop(9600, [](int64_t) { return 0.0f; });
    rig.post(Command::Tap);
    const auto during = rig.run(4800, 0.3f);
    for (size_t i = kBlock; i < during.size(); ++i) CHECK_NEAR(during[i], 0.3, 1e-6);  // was 0.6
}

TEST(lateTapsStillCutTheLoopAtTheTouchTime) {
    Rig rig;
    const int64_t lag = 14400;  // each command reaches the engine 300 ms after its touch
    rig.run(1000);
    const int64_t down1 = rig.frames;
    rig.run(lag);
    rig.post(Command::Tap, down1);
    const int64_t down2 = down1 + 48000;
    rig.run(down2 + lag - rig.frames, 0.1f);
    rig.post(Command::Tap, down2);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Playing);
    CHECK(rig.snap().loopFrames == 48000);  // was: every tap fired 300 ms late
    // Playback is in phase with the touch, not with the late command.
    CHECK(rig.snap().positionFrames == (rig.frames - down2) % 48000);
}

TEST(overdubIsShiftedByTheRoundTripLatency) {
    Rig rig;
    const int64_t len = 9600;
    const int32_t latency = 960;
    rig.core.setRoundTripFrames(latency);
    rig.recordLoop(len, [](int64_t) { return 0.0f; });
    const int64_t origin = rig.frames - kBlock;  // loop position 0 played here
    rig.post(Command::Tap);
    // The player hears position 2000 and answers; the sound returns one round trip later.
    const int64_t heard = origin + len + 2000;
    rig.run(len * 2, [&](int64_t f) { return f == heard + latency ? 0.7f : 0.0f; });
    rig.post(Command::Tap);
    rig.run(kBlock);
    const auto pass = loopPass(rig);
    CHECK_NEAR(pass[2000], 0.7, 1e-6);
    CHECK_NEAR(pass[(2000 + latency) % len], 0.0, 1e-6);
}

TEST(doubleTapWhilePlayingStopsWithoutKeepingTheOverdub) {
    Rig rig;
    rig.recordLoop(9600, [](int64_t) { return 0.2f; });
    rig.post(Command::Tap);  // first tap starts an overdub at once
    rig.run(4800, 0.4f);
    rig.post(Command::DoubleTap);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Stopped);
    CHECK(rig.snap().layers == 0);
    rig.post(Command::Tap);
    rig.run(kBlock);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.2, 1e-6);
}

TEST(doubleTapWhileRecordingKeepsTheLoopAndStops) {
    Rig rig;
    rig.post(Command::Tap);
    const int64_t start = rig.frames;
    rig.run(24000, 0.1f);
    rig.post(Command::Tap);
    rig.run(4800, 0.1f);
    rig.post(Command::DoubleTap);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Stopped);
    CHECK(rig.snap().loopFrames == rig.frames - kBlock - 4800 - start);
}

TEST(holdUndoesOverdubsButNeverTheBase) {
    Rig rig;
    rig.recordLoop(9600, [](int64_t) { return 0.2f; });
    for (int i = 0; i < 2; ++i) {
        rig.post(Command::Tap);
        rig.run(9600, 0.1f);
        rig.post(Command::Tap);
        rig.run(kBlock);
    }
    CHECK(rig.snap().layers == 2);
    for (int i = 0; i < 4; ++i) {  // each hold press starts with a tap
        rig.post(Command::Tap);
        rig.run(24000);
        rig.post(Command::Hold);
        rig.run(kBlock);
    }
    CHECK(rig.snap().layers == 0);
    CHECK(rig.snap().state == State::Playing);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.2, 1e-6);
}

TEST(holdWhileStoppedAndDoubleTapHoldClear) {
    Rig rig;
    rig.recordLoop(9600, [](int64_t) { return 0.2f; });
    rig.post(Command::DoubleTap);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Stopped);
    rig.post(Command::Tap);  // resumes at once...
    rig.post(Command::Hold);  // ...then the hold deletes everything
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Idle);
    CHECK(rig.snap().loopFrames == 0);

    rig.recordLoop(9600, [](int64_t) { return 0.2f; });
    rig.post(Command::Tap);
    rig.post(Command::DoubleTap);
    rig.post(Command::DoubleTapHold);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Idle);
}

TEST(resumeContinuesFromTheStopPosition) {
    Rig rig;
    const int64_t len = 9600;
    rig.recordLoop(len, [&](int64_t f) { return ramp(f, len); });
    rig.run(3000);
    rig.post(Command::Stop);
    rig.run(kBlock);
    const int64_t stopped = rig.snap().positionFrames;
    rig.run(9000);
    rig.post(Command::Tap);
    const auto out = rig.run(kBlock);
    CHECK_NEAR(out[0], ramp(stopped, len), 1e-6);
}

TEST(mergingKeepsEveryOverdubWhenLayersRunOut) {
    Rig rig(10.0);
    const int64_t len = 4 * kRate;  // leaves room for six layers
    rig.recordLoop(len, [](int64_t) { return 0.1f; });
    for (int k = 1; k <= 10; ++k) {
        rig.post(Command::Tap);
        rig.run(len, 0.01f * static_cast<float>(k));
        rig.post(Command::Tap);
        rig.run(kBlock);
    }
    rig.run(len * 3);  // let background merges finish
    const auto s = rig.snap();
    CHECK(s.layers == 6);  // merges happen only when a new overdub needs the slot
    CHECK(rig.core.debugSyncMergeFrames() == 0);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.1 + 0.55, 1e-4);
    for (int i = 0; i < 10; ++i) rig.post(Command::Undo);
    rig.run(kBlock);
    CHECK(rig.snap().layers == 0);
    double kept = 0.1;  // overdubs merged into the base stay; undone ones go
    for (int k = 1; k <= 10 - s.layers; ++k) kept += 0.01 * k;
    for (float v : loopPass(rig)) CHECK_NEAR(v, kept, 1e-4);
}

TEST(tooShortLoopsAreDiscarded) {
    Rig rig;
    rig.post(Command::Tap);
    rig.run(480);
    rig.post(Command::Tap);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Idle);
    CHECK(rig.snap().loopFrames == 0);
}

TEST(recordingStopsAtTheMaximumLength) {
    Rig rig(1.0);
    rig.post(Command::Tap);
    rig.run(kRate + kRate / 2, 0.1f);
    CHECK(rig.snap().state == State::Playing);
    CHECK(rig.snap().loopFrames == kRate);
}

TEST(suspendKeepsTheLoopAndStops) {
    Rig rig;
    rig.post(Command::Tap);
    rig.run(9600, 0.1f);
    rig.core.suspend();
    CHECK(rig.snap().state == State::Stopped);
    CHECK(rig.snap().loopFrames == 9600);
    rig.post(Command::Tap);
    rig.run(kBlock);
    rig.post(Command::Tap);  // overdub, then background
    rig.run(4800, 0.1f);
    rig.core.suspend();
    CHECK(rig.snap().state == State::Stopped);
    CHECK(rig.snap().layers == 1);
}

TEST(freezeIsRefusedWhileWritingAndHoldsCommands) {
    Rig rig;
    rig.recordLoop(9600, [](int64_t) { return 0.1f; });
    rig.post(Command::Tap);
    rig.run(kBlock);
    uint32_t token = rig.core.requestFreeze(true);
    CHECK(rig.core.freezeState(token) == FreezeState::Waiting);
    rig.run(kBlock);
    CHECK(rig.core.freezeState(token) == FreezeState::Refused);
    rig.core.requestFreeze(false);
    rig.post(Command::Tap);
    rig.run(kBlock);
    token = rig.core.requestFreeze(true);
    rig.run(kBlock);
    CHECK(rig.core.freezeState(token) == FreezeState::Frozen);
    rig.post(Command::Stop);
    rig.run(kBlock * 4);
    CHECK(rig.snap().state == State::Playing);  // queued until the export ends
    token = rig.core.requestFreeze(false);
    rig.run(kBlock);
    CHECK(rig.core.freezeState(token) == FreezeState::Running);
    CHECK(rig.snap().state == State::Stopped);
    // A stale answer is never taken for a new request.
    token = rig.core.requestFreeze(true);
    CHECK(rig.core.freezeState(token) == FreezeState::Waiting);
}

TEST(exportedWavMatchesPlayback) {
    Rig rig;
    const int64_t len = 9600;
    rig.recordLoop(len, [&](int64_t f) { return ramp(f, len); });
    rig.post(Command::Tap);
    rig.run(len, 0.6f);  // pushes the mix into the limiter
    rig.post(Command::Tap);
    rig.run(kBlock);
    const auto pass = loopPass(rig);

    FILE* file = std::tmpfile();
    CHECK(file != nullptr);
    CHECK(rig.core.writeWav(fileno(file)));
    std::fseek(file, 0, SEEK_SET);
    std::vector<unsigned char> bytes(44 + static_cast<size_t>(len) * 2 + 1);
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    CHECK(read == 44 + static_cast<size_t>(len) * 2);
    auto u32 = [&](size_t at) { return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | bytes[at + 3] << 24; };
    CHECK(u32(24) == kRate);
    CHECK(u32(40) == len * 2);
    for (int64_t p = 0; p < len; p += 97) {
        const auto sample = static_cast<int16_t>(bytes[44 + p * 2] | bytes[45 + p * 2] << 8);
        CHECK_NEAR(sample / 32767.0, pass[p], 1.0 / 16000);
        CHECK(std::fabs(pass[p]) < 1.0f);
    }
}

TEST(softClipStaysInRange) {
    CHECK_NEAR(looper::softClip(0.5f), 0.5f, 0.0);
    CHECK_NEAR(looper::softClip(-0.8f), -0.8f, 0.0);
    CHECK_NEAR(looper::softClip(std::nanf("")), 0.0, 0.0);
    for (float x = -100.0f; x <= 100.0f; x += 0.37f) CHECK(std::fabs(looper::softClip(x)) <= 1.0f);
}

TEST(shortInputBlocksAreSilencePadded) {
    Rig rig;
    rig.core.setMonitoring(true);
    float in[kBlock];
    float out[kBlock];
    std::fill(in, in + kBlock, 0.4f);
    rig.core.process(in, kBlock / 2, out, kBlock, 0);
    CHECK_NEAR(out[0], 0.4, 1e-6);
    CHECK_NEAR(out[kBlock - 1], 0.0, 0.0);
    rig.core.process(nullptr, 0, out, kBlock, rig.timeNs(kBlock));
    CHECK_NEAR(out[0], 0.0, 0.0);
}

TEST(overdubOnAFullStackWaitsForTheMergeInsteadOfStalling) {
    Rig rig(10.0);
    const int64_t len = 9 * kRate;  // two layer slots
    rig.recordLoop(len, [](int64_t) { return 0.1f; });
    for (float level : {0.01f, 0.02f}) {
        rig.post(Command::Tap);
        rig.run(len, level);
        rig.post(Command::Tap);
        rig.run(kBlock);
    }
    CHECK(rig.snap().layers == 2);
    rig.post(Command::Tap);  // no free slot: the oldest layer is merged in the background
    rig.run(kBlock, 0.04f);
    CHECK(rig.snap().state == State::Overdubbing);
    rig.run(len - kBlock, 0.04f);
    rig.post(Command::Tap);
    rig.run(len);
    CHECK(rig.core.debugSyncMergeFrames() == 0);  // was: a full 9 s merge in one block
    CHECK(rig.snap().layers == 2);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.1 + 0.01 + 0.02 + 0.04, 1e-4);
}

TEST(holdOnAFullStackDropsTheWaitingOverdub) {
    Rig rig(10.0);
    const int64_t len = 9 * kRate;
    rig.recordLoop(len, [](int64_t) { return 0.1f; });
    for (float level : {0.01f, 0.02f}) {
        rig.post(Command::Tap);
        rig.run(len, level);
        rig.post(Command::Tap);
        rig.run(kBlock);
    }
    rig.post(Command::Tap);
    rig.run(24000, 0.3f);
    rig.post(Command::Hold);  // drops the waiting overdub, then undoes the 0.02 layer
    rig.run(len);
    CHECK(rig.core.debugSyncMergeFrames() == 0);
    CHECK(rig.snap().state == State::Playing);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.1 + 0.01, 1e-4);
}

TEST(waitingOverdubEndedBeforeASlotFreesKeepsItsAudio) {
    Rig rig(10.0);
    const int64_t len = 9 * kRate;
    rig.recordLoop(len, [](int64_t) { return 0.0f; });
    for (int i = 0; i < 2; ++i) {
        rig.post(Command::Tap);
        rig.run(len);
        rig.post(Command::Tap);
        rig.run(kBlock);
    }
    const int64_t start = rig.frames;
    rig.run(2000, 0.3f);
    rig.post(Command::Tap, start);         // both touches reach the engine in one block,
    rig.post(Command::Tap, start + 960);   // before the merge has freed a slot
    rig.run(len);
    CHECK(rig.snap().layers == 2);  // the oldest layer was merged to make room
    int64_t loud = 0;
    for (float v : loopPass(rig)) loud += v > 0.15f;
    CHECK(loud == 960);
    rig.post(Command::Undo);
    rig.run(kBlock);
    loud = 0;
    for (float v : loopPass(rig)) loud += v > 0.15f;
    CHECK(loud == 0);
}

TEST(touchesFromBeforeARestartDoNotReachOldAudio) {
    Rig rig;
    rig.run(9600, 0.9f);  // audio from before the app was backgrounded
    rig.core.suspend();
    rig.gapNs += 2'000'000'000;  // two seconds with no audio
    const int64_t duringGap = rig.timeNs(rig.frames) - 500'000'000;
    CHECK(rig.core.post(Command::Tap, duringGap));
    rig.run(9600, 0.1f);
    rig.post(Command::Tap);
    rig.run(kBlock);
    CHECK(rig.snap().state == State::Playing);
    CHECK(rig.snap().loopFrames == 9600);
    for (float v : loopPass(rig)) CHECK_NEAR(v, 0.1, 1e-6);  // was: began with the 0.9 audio
}

TEST(quickOverdubsOnAFullStackQueueInsteadOfStalling) {
    Rig rig(10.0);
    const int64_t len = 9 * kRate;  // two layer slots
    rig.recordLoop(len, [](int64_t) { return 0.1f; });
    for (float level : {0.01f, 0.02f}) {
        rig.post(Command::Tap);
        rig.run(len, level);
        rig.post(Command::Tap);
        rig.run(kBlock);
    }
    const int64_t t0 = rig.frames;
    rig.run(4000, [&](int64_t f) {
        if (f >= t0 && f < t0 + 960) return 0.3f;
        if (f >= t0 + 1920 && f < t0 + 2880) return 0.5f;
        return 0.0f;
    });
    // Two overdubs, started and finished before any slot is free.
    rig.post(Command::Tap, t0);
    rig.post(Command::Tap, t0 + 960);
    rig.post(Command::Tap, t0 + 1920);
    rig.post(Command::Tap, t0 + 2880);
    rig.run(len * 2);
    CHECK(rig.core.debugSyncMergeFrames() == 0);
    CHECK(rig.snap().layers == 2);
    int64_t first = 0, second = 0;
    for (float v : loopPass(rig)) {
        first += std::fabs(v - 0.43f) < 1e-4f;
        second += std::fabs(v - 0.63f) < 1e-4f;
    }
    CHECK(first == 960);
    CHECK(second == 960);
}

int main(int argc, char** argv) { return check::runAll(argc > 1 ? argv[1] : nullptr); }
