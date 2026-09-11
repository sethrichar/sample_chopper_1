#pragma once
#include "Audio.h"
#include <cstddef>
#include <vector>

namespace chopper {

struct Onset {
    size_t sample   = 0;     ///< refined onset position, in frames of the source
    float  strength = 0.0f;  ///< 0..1, relative to the strongest onset in the file
};

struct DetectorSettings {
    /// 0 = only the most obvious hits, 1 = everything that twitches. 0.5 is a good start.
    float sensitivity = 0.5f;
    /// Two onsets closer than this are merged (the stronger one wins).
    float minSliceMs = 40.0f;
    /// Move each onset this far *before* the detected attack so the transient isn't clipped.
    float preRollMs = 1.0f;
    /// Snap the (pre-rolled) onset to the nearest preceding zero crossing.
    bool snapToZeroCrossing = true;
    /// Analysis frame / hop, in samples. 1024/256 is a good default at 44.1–48 kHz.
    int frameSize = 1024;
    int hopSize   = 256;
};

/// Spectral-flux onset detector with adaptive peak picking and sample-accurate refinement.
class TransientDetector {
public:
    explicit TransientDetector(DetectorSettings s = {}) : settings_(s) {}
    void setSettings(const DetectorSettings& s) { settings_ = s; }
    const DetectorSettings& settings() const { return settings_; }

    std::vector<Onset> detect(const AudioData& audio) const;

    /// Exposed for the UI: the normalised onset-detection function (one value per hop).
    std::vector<float> detectionFunction(const std::vector<float>& mono, double sampleRate) const;

private:
    DetectorSettings settings_;
};

} // namespace chopper
