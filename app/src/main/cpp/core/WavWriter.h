#pragma once

#include <cstdint>
#include <functional>

namespace looper::wav {

// Writes a 16-bit PCM mono WAV to an open file descriptor. `sampleAt(i)` returns
// sample i in [-1, 1]. Returns false on any write error. Not for the audio thread.
bool writePcm16Mono(int fd, int32_t sampleRate, int64_t frames,
                    const std::function<float(int64_t)>& sampleAt);

}  // namespace looper::wav
