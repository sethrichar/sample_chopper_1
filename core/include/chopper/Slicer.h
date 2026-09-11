#pragma once
#include "Audio.h"
#include "TransientDetector.h"
#include <cstddef>
#include <vector>

namespace chopper {

struct Slice {
    size_t start = 0;        ///< first frame (inclusive)
    size_t end   = 0;        ///< last frame (exclusive)
    float  strength = 1.0f;  ///< onset strength (1 for manual/grid slices)
    bool   manual = false;   ///< placed/moved by the user rather than the detector
    size_t length() const { return end > start ? end - start : 0; }
};

struct SliceProcessing {
    float fadeInMs  = 0.5f;   ///< tiny fade to kill clicks at the cut
    float fadeOutMs = 5.0f;
    bool  trimTrailingSilence = false;
    float silenceThresholdDb  = -60.0f;
    float minTailMs = 5.0f;   ///< keep at least this much after the last non-silent sample
    bool  normalize = false;  ///< peak-normalise each slice to normalizeTargetDb
    float normalizeTargetDb = -0.3f;
};

/// Turn onset positions into contiguous [start,end) slices covering the file.
std::vector<Slice> slicesFromOnsets(const std::vector<Onset>& onsets, size_t totalFrames,
                                    bool includeStart, size_t minLengthFrames);

/// Equal divisions of the file (e.g. 16 for a one-bar loop of 16ths).
std::vector<Slice> gridSlices(size_t totalFrames, int divisions);

/// Insert a manual slice point; splits the slice that contains `frame`.
void addSlicePoint(std::vector<Slice>& slices, size_t frame, size_t minLengthFrames);
/// Remove the slice starting at index `i` (merges it into the previous slice).
void removeSlicePoint(std::vector<Slice>& slices, size_t i);
/// Move the start of slice `i` to `frame` (bounded by neighbours).
void moveSlicePoint(std::vector<Slice>& slices, size_t i, size_t frame, size_t minLengthFrames);

/// Extract and post-process a single slice.
AudioData renderSlice(const AudioData& source, const Slice& s, const SliceProcessing& p);

} // namespace chopper
