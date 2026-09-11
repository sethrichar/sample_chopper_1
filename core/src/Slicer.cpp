#include "chopper/Slicer.h"
#include <algorithm>
#include <cmath>

namespace chopper {

std::vector<Slice> slicesFromOnsets(const std::vector<Onset>& onsets, size_t totalFrames,
                                    bool includeStart, size_t minLengthFrames)
{
    std::vector<size_t> starts;
    std::vector<float> strengths;
    if (includeStart) { starts.push_back(0); strengths.push_back(1.0f); }
    for (const auto& o : onsets) {
        if (o.sample >= totalFrames) continue;
        if (!starts.empty() && o.sample < starts.back() + minLengthFrames) continue;
        if (!starts.empty() && o.sample == starts.back()) continue;
        starts.push_back(o.sample); strengths.push_back(o.strength);
    }
    std::vector<Slice> out;
    for (size_t i = 0; i < starts.size(); ++i) {
        Slice s; s.start = starts[i]; s.end = i + 1 < starts.size() ? starts[i + 1] : totalFrames; s.strength = strengths[i];
        if (s.length() > 0) out.push_back(s);
    }
    return out;
}

std::vector<Slice> gridSlices(size_t totalFrames, int divisions)
{
    std::vector<Slice> out;
    divisions = std::max(1, divisions);
    for (int i = 0; i < divisions; ++i) {
        Slice s;
        s.start = size_t(std::llround(double(totalFrames) * i / divisions));
        s.end   = size_t(std::llround(double(totalFrames) * (i + 1) / divisions));
        s.manual = true;
        if (s.length() > 0) out.push_back(s);
    }
    return out;
}

void addSlicePoint(std::vector<Slice>& slices, size_t frame, size_t minLengthFrames)
{
    for (size_t i = 0; i < slices.size(); ++i) {
        Slice& s = slices[i];
        if (frame > s.start && frame < s.end) {
            if (frame - s.start < minLengthFrames || s.end - frame < minLengthFrames) return;
            Slice n; n.start = frame; n.end = s.end; n.manual = true; n.strength = 1.0f;
            s.end = frame;
            slices.insert(slices.begin() + long(i) + 1, n);
            return;
        }
    }
}

void removeSlicePoint(std::vector<Slice>& slices, size_t i)
{
    if (i == 0 || i >= slices.size()) return;      // slice 0 always starts at 0
    slices[i - 1].end = slices[i].end;
    slices.erase(slices.begin() + long(i));
}

void moveSlicePoint(std::vector<Slice>& slices, size_t i, size_t frame, size_t minLengthFrames)
{
    if (i == 0 || i >= slices.size()) return;
    const size_t lo = slices[i - 1].start + minLengthFrames;
    const size_t hi = slices[i].end > minLengthFrames ? slices[i].end - minLengthFrames : 0;
    if (hi <= lo) return;
    frame = std::clamp(frame, lo, hi);
    slices[i - 1].end = frame; slices[i].start = frame; slices[i].manual = true;
}

AudioData renderSlice(const AudioData& source, const Slice& s, const SliceProcessing& p)
{
    AudioData out = source.slice(s.start, s.end);
    const double sr = source.sampleRate;
    const size_t n = out.numFrames();
    if (n == 0) return out;

    if (p.trimTrailingSilence) {
        const float thr = std::pow(10.0f, p.silenceThresholdDb / 20.0f);
        size_t last = 0;
        for (const auto& ch : out.channels)
            for (size_t i = n; i-- > 0;) if (std::fabs(ch[i]) > thr) { last = std::max(last, i); break; }
        size_t keep = std::min(n, last + 1 + size_t(p.minTailMs * sr / 1000.0));
        if (keep < n) for (auto& ch : out.channels) ch.resize(keep);
    }
    const size_t m = out.numFrames();
    if (p.normalize) {
        float peak = 0; for (const auto& ch : out.channels) for (float v : ch) peak = std::max(peak, std::fabs(v));
        if (peak > 0) { const float g = std::pow(10.0f, p.normalizeTargetDb / 20.0f) / peak; for (auto& ch : out.channels) for (auto& v : ch) v *= g; }
    }
    const size_t fi = std::min(m, size_t(p.fadeInMs * sr / 1000.0));
    const size_t fo = std::min(m, size_t(p.fadeOutMs * sr / 1000.0));
    for (auto& ch : out.channels) {
        for (size_t i = 0; i < fi; ++i) ch[i] *= float(i) / float(fi);
        for (size_t i = 0; i < fo; ++i) ch[m - 1 - i] *= float(i) / float(fo);
    }
    return out;
}

} // namespace chopper
