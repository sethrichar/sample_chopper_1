#include "chopper/Tempo.h"
#include <algorithm>
#include <cmath>

namespace chopper {

std::vector<BpmCandidate> estimateBpmFromLength(double durationSeconds, double minBpm, double maxBpm)
{
    std::vector<BpmCandidate> out;
    if (durationSeconds <= 0) return out;
    const int barsList[] = { 1, 2, 4, 8, 16, 32, 3, 6, 12 };
    struct Scored { BpmCandidate c; double score; };
    std::vector<Scored> scored;
    for (int bars : barsList) {
        const double bpm = bars * 4.0 * 60.0 / durationSeconds;
        if (bpm < minBpm || bpm > maxBpm) continue;
        double score = std::fabs(std::log2(bpm / 112.0));          // prefer "normal" tempos
        if (bars % 3 == 0) score += 0.6;                            // odd bar counts are rare
        scored.push_back({ { bpm, bars }, score });
    }
    if (scored.empty()) {   // nothing in range: fall back to the nearest one-bar guess anyway
        const double bpm = 4.0 * 60.0 / durationSeconds;
        double b = bpm; int bars = 1;
        while (b < minBpm) { b *= 2; bars *= 2; }
        while (b > maxBpm && bars > 1) { b /= 2; bars /= 2; }
        out.push_back({ b, bars });
        return out;
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });
    for (const auto& s : scored) out.push_back(s.c);
    return out;
}

} // namespace chopper
