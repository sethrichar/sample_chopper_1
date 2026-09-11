#pragma once
#include <vector>

namespace chopper {

struct BpmCandidate { double bpm; int bars; };

/// Estimates tempo from loop length by assuming the file is a whole number of 4/4 bars.
/// Returns candidates sorted by plausibility (best first). Empty if durationSeconds <= 0.
std::vector<BpmCandidate> estimateBpmFromLength(double durationSeconds,
                                                double minBpm = 60.0, double maxBpm = 190.0);

} // namespace chopper
