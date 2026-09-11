#pragma once
#include "Audio.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chopper {

enum class BitDepth { Pcm16 = 16, Pcm24 = 24, Float32 = 32 };

struct WavMarker {
    uint32_t    sampleOffset = 0;
    std::string label;
};

struct WavWriteOptions {
    BitDepth bitDepth = BitDepth::Pcm24;
    /// If set, a 'smpl' chunk is written with this MIDI unity (root) note.
    /// Samplers such as Kontakt, Logic, Reason, Bitwig, Renoise and MPC read it.
    std::optional<int> rootNote;
    /// Cue points + labels ('cue ' and 'LIST/adtl/labl' chunks).
    std::vector<WavMarker> markers;
};

struct WavInfo {
    double   sampleRate = 0;
    int      channels   = 0;
    int      bitsPerSample = 0;
    bool     isFloat = false;
    size_t   frames = 0;
    std::optional<int> rootNote;      // from smpl chunk, if present
    std::vector<WavMarker> markers;   // from cue/labl chunks, if present
};

/// Reads PCM 8/16/24/32 and IEEE float 32/64, plain or WAVE_FORMAT_EXTENSIBLE.
/// Returns false and fills `error` on failure.
bool readWav(const std::string& path, AudioData& out, std::string& error, WavInfo* info = nullptr);

/// Writes a canonical RIFF/WAVE file.
bool writeWav(const std::string& path, const AudioData& audio, const WavWriteOptions& opts, std::string& error);

/// Same as writeWav but into memory (used by tests and the plugin's clipboard export).
std::vector<uint8_t> encodeWav(const AudioData& audio, const WavWriteOptions& opts);
bool decodeWav(const std::vector<uint8_t>& bytes, AudioData& out, std::string& error, WavInfo* info = nullptr);

} // namespace chopper
