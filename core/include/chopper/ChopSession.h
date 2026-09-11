#pragma once
#include "Audio.h"
#include "Exporters.h"
#include "Naming.h"
#include "Slicer.h"
#include "TransientDetector.h"
#include "WavIO.h"
#include <functional>
#include <string>
#include <vector>

namespace chopper {

enum class SliceMode { Transients, Grid };

struct ChopSettings {
    SliceMode mode = SliceMode::Transients;
    DetectorSettings detector;
    bool includeStart = true;      ///< always make the first slice start at frame 0
    int  gridDivisions = 16;       ///< for SliceMode::Grid

    SliceProcessing processing;

    // Naming / mapping
    std::string baseName = "Slice";
    std::string nameTemplate = "{name}_{index}_{note}";
    int  indexWidth = 3;
    int  rootNote = 36;            ///< first slice's key (36 = C1, the usual first drum-rack pad)
    bool chromatic = true;         ///< each next slice one semitone up; false = all on rootNote
    NoteNaming noteNaming;

    // Output
    BitDepth bitDepth = BitDepth::Pcm24;
    double bpm = 0.0;              ///< 0 = estimate from loop length
    bool writeSfz = true;
    bool writeMidi = true;
    bool writeJson = true;
    bool writeMarkerWav = false;
    int  midiVelocity = 100;
};

struct ExportReport {
    bool ok = false;
    std::string error;
    std::string outputDir;
    std::vector<std::string> files;   ///< everything written, relative to outputDir
    std::vector<ExportedSlice> slices;
    double bpmUsed = 0;
};

/// Everything the CLI and the plugin need: hold audio, detect, let the user edit, export.
class ChopSession {
public:
    void setAudio(AudioData audio, std::string sourceName);
    const AudioData& audio() const { return audio_; }
    const std::string& sourceName() const { return sourceName_; }
    bool hasAudio() const { return !audio_.empty(); }

    ChopSettings& settings() { return settings_; }
    const ChopSettings& settings() const { return settings_; }

    /// Runs detection (or grid) with the current settings. Manual edits are discarded.
    void analyze();
    const std::vector<Slice>& slices() const { return slices_; }
    const std::vector<Onset>& onsets() const { return onsets_; }

    // Manual editing
    void addSlicePoint(size_t frame);
    void removeSlicePoint(size_t index);
    void moveSlicePoint(size_t index, size_t frame);
    void clearSlices();

    /// Tempo actually used for export/naming (settings().bpm, or the estimate when 0).
    double effectiveBpm() const;
    /// MIDI note assigned to slice i.
    int noteForSlice(size_t i) const;
    /// File name (without directory) slice i would get.
    std::string fileNameForSlice(size_t i) const;

    /// Writes everything into outputDir (created if needed).
    ExportReport exportAll(const std::string& outputDir,
                           const std::function<void(float)>& progress = nullptr) const;

private:
    size_t minLengthFrames() const;

    AudioData audio_;
    std::string sourceName_;
    ChopSettings settings_;
    std::vector<Onset> onsets_;
    std::vector<Slice> slices_;
};

} // namespace chopper
