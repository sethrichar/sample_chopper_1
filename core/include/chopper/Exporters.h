#pragma once
#include "Audio.h"
#include "Naming.h"
#include "Slicer.h"
#include "WavIO.h"
#include <string>
#include <vector>

namespace chopper {

struct ExportedSlice {
    std::string fileName;   ///< e.g. "Break_001_C1.wav" (relative to output dir)
    int    midiNote = 0;
    size_t start = 0, end = 0;
    double startSeconds = 0, lengthSeconds = 0;
};

/// Standard MIDI file: one note per slice at the slice's position, so the sliced loop can be
/// played back from any sampler that has the slices mapped (this is what ReCycle/REX users
/// actually use the MIDI for).
bool writeSliceMidi(const std::string& path, const std::vector<ExportedSlice>& slices,
                    double bpm, int velocity, std::string& error);

/// SFZ instrument that maps each slice to its key (opens in sforzando, Decent Sampler,
/// Bitwig, TX16Wx, Sitala, Kontakt 7+, etc.).
bool writeSfz(const std::string& path, const std::vector<ExportedSlice>& slices,
              const std::string& instrumentName, std::string& error);

/// JSON manifest with every slice's position, note and file (for scripts or other tools).
bool writeJsonManifest(const std::string& path, const std::string& sourceName, double sampleRate,
                       double bpm, int rootNote, const std::vector<ExportedSlice>& slices,
                       std::string& error);

/// Re-writes the whole source file with a 'cue ' point + label per slice.
/// REAPER, Logic, WaveLab, Sound Forge, Audacity, RX etc. show these as markers.
bool writeMarkerWav(const std::string& path, const AudioData& source, const std::vector<Slice>& slices,
                    const std::vector<std::string>& labels, BitDepth depth, std::string& error);

} // namespace chopper
