#include "WavWriter.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace looper::wav {
namespace {

bool writeAll(int fd, const uint8_t* data, size_t size) {
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

}  // namespace

bool writePcm16Mono(int fd, int32_t sampleRate, int64_t frames,
                    const std::function<float(int64_t)>& sampleAt) {
    constexpr uint16_t kChannels = 1;
    constexpr uint16_t kBits = 16;
    constexpr uint16_t kBlockAlign = kChannels * kBits / 8;
    const uint64_t dataSize = static_cast<uint64_t>(frames) * kBlockAlign;
    if (fd < 0 || frames <= 0 || sampleRate <= 0 || dataSize > 0xFFFFFFFFull - 36) return false;

    uint8_t header[44];
    std::memcpy(header, "RIFF", 4);
    put32(header + 4, static_cast<uint32_t>(36 + dataSize));
    std::memcpy(header + 8, "WAVEfmt ", 8);
    put32(header + 16, 16);  // fmt chunk size
    put16(header + 20, 1);   // PCM
    put16(header + 22, kChannels);
    put32(header + 24, static_cast<uint32_t>(sampleRate));
    put32(header + 28, static_cast<uint32_t>(sampleRate) * kBlockAlign);
    put16(header + 32, kBlockAlign);
    put16(header + 34, kBits);
    std::memcpy(header + 36, "data", 4);
    put32(header + 40, static_cast<uint32_t>(dataSize));
    if (!writeAll(fd, header, sizeof header)) return false;

    constexpr int64_t kChunk = 4096;
    uint8_t buffer[kChunk * kBlockAlign];
    for (int64_t start = 0; start < frames; start += kChunk) {
        const int64_t count = std::min(kChunk, frames - start);
        for (int64_t i = 0; i < count; ++i) {
            float s = sampleAt(start + i);
            if (!std::isfinite(s)) s = 0.0f;
            s = std::clamp(s, -1.0f, 1.0f);
            const auto pcm = static_cast<int16_t>(std::lround(s * 32767.0f));
            put16(buffer + i * kBlockAlign, static_cast<uint16_t>(pcm));
        }
        if (!writeAll(fd, buffer, static_cast<size_t>(count * kBlockAlign))) return false;
    }
    return true;
}

}  // namespace looper::wav
