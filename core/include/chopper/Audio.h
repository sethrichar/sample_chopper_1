#pragma once
#include <cstddef>
#include <vector>

namespace chopper {

/// Planar float audio. channels[c][frame].
struct AudioData {
    double sampleRate = 44100.0;
    std::vector<std::vector<float>> channels;

    int    numChannels() const { return static_cast<int>(channels.size()); }
    size_t numFrames()   const { return channels.empty() ? 0 : channels[0].size(); }
    bool   empty()       const { return numFrames() == 0; }
    double durationSeconds() const { return sampleRate > 0 ? numFrames() / sampleRate : 0.0; }

    void resize(int numCh, size_t frames) {
        channels.assign(static_cast<size_t>(numCh), std::vector<float>(frames, 0.0f));
    }

    /// Equal-weight downmix to mono.
    std::vector<float> mono() const {
        std::vector<float> out(numFrames(), 0.0f);
        if (channels.empty()) return out;
        const float g = 1.0f / static_cast<float>(channels.size());
        for (const auto& ch : channels)
            for (size_t i = 0; i < out.size(); ++i) out[i] += ch[i] * g;
        return out;
    }

    /// Copy of frames [start, end).
    AudioData slice(size_t start, size_t end) const {
        AudioData out;
        out.sampleRate = sampleRate;
        if (end > numFrames()) end = numFrames();
        if (start > end) start = end;
        out.channels.reserve(channels.size());
        for (const auto& ch : channels)
            out.channels.emplace_back(ch.begin() + static_cast<std::ptrdiff_t>(start),
                                      ch.begin() + static_cast<std::ptrdiff_t>(end));
        return out;
    }
};

} // namespace chopper
