// Multi-threaded soak for LooperCore, shaped like the app: an audio thread calls
// process() with varied block sizes and clock jitter, a control thread sends random
// gesture sequences with late timestamps, an export thread freezes the loop and
// writes WAVs, and the "app" suspends and resumes the engine. Runs under TSan,
// ASan/UBSan, or optimised with allocation counting and timing.
//
//   soak_test <seconds> [seed]
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <random>
#include <thread>
#include <vector>

#include "core/LooperCore.h"

using looper::Command;
using looper::FreezeState;
using looper::LooperCore;
using looper::State;

#ifdef SOAK_COUNT_ALLOCATIONS
// Counts heap allocations made on the audio thread; the core must make none.
static thread_local bool t_onAudioThread = false;
static std::atomic<uint64_t> g_audioAllocations{0};
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // replacement new is malloc-backed
void* operator new(size_t size) {
    if (t_onAudioThread) g_audioAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#endif

namespace {

constexpr int32_t kRate = 48000;
constexpr int64_t kNsPerFrame = 1'000'000'000LL / kRate;

struct Shared {
    explicit Shared(double maxLoopSeconds) : core(kRate, maxLoopSeconds) {}
    LooperCore core;
    std::atomic<int64_t> audioTimeNs{0};  // virtual clock of the audio thread
    std::atomic<bool> pauseRequested{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> done{false};
    std::mutex engineMutex;  // serialises export and suspend, as OboeLooper does
    std::atomic<uint64_t> blocks{0}, frames{0}, badSamples{0}, commands{0}, rejected{0};
    std::atomic<uint64_t> exports{0}, exportFailures{0}, refusedFreezes{0}, suspends{0};
    std::atomic<uint64_t> invariantFailures{0}, snapshots{0}, loopsClosed{0};
    std::atomic<int64_t> worstBlockNs{0}, worstBlockFrames{0};
    std::atomic<uint64_t> slowBlocks{0};  // took more than half of their real-time budget
    std::atomic<int64_t> maxLayersSeen{0};
};

void fail(Shared& shared, const char* what) {
    if (shared.invariantFailures.fetch_add(1) < 20) std::printf("INVARIANT: %s\n", what);
}

void audioThread(Shared& shared, uint32_t seed) {
    std::mt19937 rng(seed);
    const int32_t sizes[] = {64, 96, 128, 192, 240, 256, 441, 480, 512, 960, 1024, 2048};
    std::vector<float> in(8192), out(8192);
    std::normal_distribution<double> jitterMs(0.0, 0.7);
#ifdef SOAK_COUNT_ALLOCATIONS
    t_onAudioThread = true;  // after the harness's own buffers exist
#endif
    int64_t frame = 0;
    double phase = 0.0;
    while (!shared.done.load(std::memory_order_relaxed)) {
        if (shared.pauseRequested.load(std::memory_order_acquire)) {
            shared.paused.store(true, std::memory_order_release);
            while (shared.pauseRequested.load(std::memory_order_acquire)) std::this_thread::yield();
            shared.paused.store(false, std::memory_order_release);
            frame += kRate / 2;  // time passes while the streams are closed
        }
        const int32_t n = sizes[rng() % (sizeof sizes / sizeof sizes[0])];
        int32_t inputFrames = n;
        if (rng() % 10 == 0) inputFrames = static_cast<int32_t>(rng() % (n + 1));  // input short
        for (int32_t i = 0; i < inputFrames; ++i) {
            phase += 2.0 * M_PI * 220.0 / kRate;
            in[i] = static_cast<float>(0.3 * std::sin(phase));
        }
        if (rng() % 20000 == 0 && inputFrames > 0) in[0] = std::nanf("");  // broken driver
        const int64_t nowNs = frame * kNsPerFrame + static_cast<int64_t>(jitterMs(rng) * 1e6);
        const auto start = std::chrono::steady_clock::now();
        shared.core.process(in.data(), inputFrames, out.data(), n, nowNs);
        const int64_t took = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - start).count();
        if (took * 2 > n * kNsPerFrame) shared.slowBlocks.fetch_add(1, std::memory_order_relaxed);
        if (took > shared.worstBlockNs.load(std::memory_order_relaxed)) {
            shared.worstBlockNs.store(took, std::memory_order_relaxed);
            shared.worstBlockFrames.store(n, std::memory_order_relaxed);
        }
        for (int32_t i = 0; i < n; ++i) {
            if (!std::isfinite(out[i]) || std::fabs(out[i]) > 1.0f) {
                shared.badSamples.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        frame += n;
        shared.audioTimeNs.store(frame * kNsPerFrame, std::memory_order_release);
        shared.blocks.fetch_add(1, std::memory_order_relaxed);
        shared.frames.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        if (rng() % 64 == 0) std::this_thread::yield();
    }
}

// Waits until the audio clock has moved on by `ms` of audio time.
void waitAudio(Shared& shared, int64_t ms) {
    const int64_t target = shared.audioTimeNs.load(std::memory_order_acquire) + ms * 1'000'000;
    while (!shared.done.load() && shared.audioTimeNs.load(std::memory_order_acquire) < target) {
        if (shared.paused.load(std::memory_order_acquire)) return;
        std::this_thread::yield();
    }
}

void checkSnapshot(Shared& shared, const looper::Snapshot& s) {
    shared.snapshots.fetch_add(1, std::memory_order_relaxed);
    const auto state = static_cast<int32_t>(s.state);
    if (state < 0 || state > 4) fail(shared, "state out of range");
    if (s.loopFrames < 0 || s.loopFrames > s.maxLoopFrames) fail(shared, "loop length out of range");
    if (s.loopFrames > 0 && (s.positionFrames < 0 || s.positionFrames >= s.loopFrames)) {
        fail(shared, "position outside the loop");
    }
    if ((s.state == State::Playing || s.state == State::Overdubbing || s.state == State::Stopped) &&
        s.loopFrames == 0) {
        fail(shared, "playing state without a loop");
    }
    if (s.state == State::Idle && s.loopFrames != 0) fail(shared, "idle with a loop");
    if (s.recordedFrames < 0 || s.recordedFrames > s.maxLoopFrames) fail(shared, "recorded out of range");
    if (s.layers < 0 || s.layers > LooperCore::kMaxLayers + LooperCore::kMaxWaiting) {
        fail(shared, "layer count out of range");
    }
    if (s.inputPeak < 0.0f || !(s.inputPeak <= 1.0f)) fail(shared, "input peak out of range");
    if (s.layers > shared.maxLayersSeen.load()) shared.maxLayersSeen.store(s.layers);
}

void post(Shared& shared, Command command, int64_t timeNs) {
    if (shared.core.post(command, timeNs)) {
        shared.commands.fetch_add(1, std::memory_order_relaxed);
    } else {
        shared.rejected.fetch_add(1, std::memory_order_relaxed);
    }
}

void controlThread(Shared& shared, uint32_t seed) {
    std::mt19937 rng(seed);
    auto chance = [&](int percent) { return static_cast<int>(rng() % 100) < percent; };
    // UI delivery delay: commands carry the touch time but arrive later.
    auto touchTime = [&] {
        return shared.audioTimeNs.load(std::memory_order_acquire) - static_cast<int64_t>(rng() % 80) * 1'000'000;
    };
    while (!shared.done.load()) {
        checkSnapshot(shared, shared.core.snapshot());
        const int roll = static_cast<int>(rng() % 100);
        if (roll < 45) {
            post(shared, Command::Tap, touchTime());
        } else if (roll < 60) {  // double tap, sometimes held
            post(shared, Command::Tap, touchTime());
            waitAudio(shared, 80 + rng() % 200);
            post(shared, Command::DoubleTap, touchTime());
            if (chance(30)) {
                waitAudio(shared, 500);
                post(shared, Command::DoubleTapHold, touchTime());
            }
        } else if (roll < 75) {  // hold
            post(shared, Command::Tap, touchTime());
            waitAudio(shared, 500);
            post(shared, Command::Hold, touchTime());
        } else if (roll < 80) {
            post(shared, Command::Undo, touchTime());
        } else if (roll < 84) {
            post(shared, Command::Stop, touchTime());
        } else if (roll < 86) {
            post(shared, Command::Clear, touchTime());
        } else if (roll < 90) {
            shared.core.setMonitoring(chance(50));
        } else if (roll < 94) {
            shared.core.setRoundTripFrames(static_cast<int32_t>(rng() % 9600));
        } else if (roll < 96) {  // background and foreground
            std::lock_guard<std::mutex> lock(shared.engineMutex);
            shared.pauseRequested.store(true, std::memory_order_release);
            while (!shared.paused.load(std::memory_order_acquire)) std::this_thread::yield();
            shared.core.suspend();
            const auto s = shared.core.snapshot();
            checkSnapshot(shared, s);
            if (s.state == State::Recording || s.state == State::Overdubbing || s.state == State::Playing) {
                fail(shared, "still running after suspend");
            }
            if (chance(30)) post(shared, Command::Tap, touchTime());  // queued while closed
            shared.suspends.fetch_add(1);
            shared.pauseRequested.store(false, std::memory_order_release);
            while (shared.paused.load(std::memory_order_acquire)) std::this_thread::yield();
        }
        // Loop lengths from very short to past the maximum, so recording auto-closes.
        waitAudio(shared, chance(10) ? 2000 + rng() % 7000 : 30 + rng() % 1500);
    }
}

void exportThread(Shared& shared, uint32_t seed) {
    std::mt19937 rng(seed);
    char path[] = "/tmp/record-soak-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) {
        fail(shared, "cannot create export file");
        return;
    }
    unlink(path);
    while (!shared.done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20 + rng() % 200));
        if (shared.done.load()) break;  // the audio thread may already have exited
        std::lock_guard<std::mutex> lock(shared.engineMutex);
        const uint32_t token = shared.core.requestFreeze(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (shared.core.freezeState(token) == FreezeState::Waiting &&
               std::chrono::steady_clock::now() < deadline && !shared.done.load()) {
            std::this_thread::yield();
        }
        const FreezeState state = shared.core.freezeState(token);
        if (state == FreezeState::Waiting && shared.done.load()) {
            shared.core.requestFreeze(false);
            break;
        }
        if (state == FreezeState::Frozen) {
            const int64_t loop = shared.core.snapshot().loopFrames;
            ftruncate(fd, 0);
            lseek(fd, 0, SEEK_SET);
            const bool ok = shared.core.writeWav(fd);
            struct stat st{};
            fstat(fd, &st);
            if (loop > 0 && (!ok || st.st_size != 44 + loop * 2)) {
                shared.exportFailures.fetch_add(1);
                fail(shared, "export size mismatch");
            }
            if (loop == 0 && ok) fail(shared, "exported without a loop");
            shared.exports.fetch_add(1);
        } else if (state == FreezeState::Refused) {
            shared.refusedFreezes.fetch_add(1);
        } else {
            fail(shared, "freeze not answered");
        }
        const uint32_t resume = shared.core.requestFreeze(false);
        while (shared.core.freezeState(resume) != FreezeState::Running && !shared.done.load() &&
               std::chrono::steady_clock::now() < deadline + std::chrono::seconds(2)) {
            std::this_thread::yield();
        }
    }
    close(fd);
}

}  // namespace

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 10;
    const uint32_t seed = argc > 2 ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10))
                                   : static_cast<uint32_t>(std::random_device{}());
    std::printf("soak: %d s, seed %u\n", seconds, seed);
    Shared shared(6.0);
    std::thread audio(audioThread, std::ref(shared), seed);
    std::thread control(controlThread, std::ref(shared), seed + 1);
    std::thread exporter(exportThread, std::ref(shared), seed + 2);

    const auto start = std::chrono::steady_clock::now();
    int64_t lastReport = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - start).count();
        if (elapsed - lastReport >= 60) {
            lastReport = elapsed;
            std::printf("  %4" PRId64 " s: %.1f h audio, %" PRIu64 " commands, %" PRIu64 " exports, "
                        "%" PRIu64 " suspends, %" PRIu64 " invariant failures\n",
                        static_cast<int64_t>(elapsed), shared.frames.load() / double(kRate) / 3600.0,
                        shared.commands.load(), shared.exports.load(), shared.suspends.load(),
                        shared.invariantFailures.load());
            std::fflush(stdout);
        }
        if (elapsed >= seconds) break;
    }
    shared.done = true;
    shared.pauseRequested = false;
    audio.join();
    control.join();
    exporter.join();

    const double audioHours = shared.frames.load() / double(kRate) / 3600.0;
    std::printf("audio processed: %.2f h in %" PRIu64 " blocks\n", audioHours, shared.blocks.load());
    std::printf("commands: %" PRIu64 " (rejected %" PRIu64 "), snapshots %" PRIu64 "\n",
                shared.commands.load(), shared.rejected.load(), shared.snapshots.load());
    std::printf("exports: %" PRIu64 " ok-or-empty, %" PRIu64 " failed, %" PRIu64 " refused while writing\n",
                shared.exports.load(), shared.exportFailures.load(), shared.refusedFreezes.load());
    std::printf("suspend/resume cycles: %" PRIu64 ", max undoable layers seen: %" PRId64 "\n",
                shared.suspends.load(), shared.maxLayersSeen.load());
    std::printf("bad output blocks (NaN or |y|>1): %" PRIu64 "\n", shared.badSamples.load());
    std::printf("worst process() time: %.1f us for %" PRId64 " frames (budget %.0f us)\n",
                shared.worstBlockNs.load() / 1000.0, shared.worstBlockFrames.load(),
                shared.worstBlockFrames.load() * 1e6 / kRate);
    std::printf("blocks over half their budget: %" PRIu64 " of %" PRIu64 " (host scheduling included)\n",
                shared.slowBlocks.load(), shared.blocks.load());
    std::printf("frames merged synchronously on the audio thread: %" PRId64 "\n",
                shared.core.debugSyncMergeFrames());
#ifdef SOAK_COUNT_ALLOCATIONS
    std::printf("heap allocations on the audio thread: %" PRIu64 "\n", g_audioAllocations.load());
    const bool allocationsOk = g_audioAllocations.load() == 0;
#else
    const bool allocationsOk = true;
#endif
    std::printf("invariant failures: %" PRIu64 "\n", shared.invariantFailures.load());
    const bool ok = shared.invariantFailures.load() == 0 && shared.badSamples.load() == 0 &&
                    shared.exportFailures.load() == 0 && allocationsOk && shared.commands.load() > 0;
    std::printf("SOAK %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
