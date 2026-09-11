#pragma once
#include <string>

namespace chopper {

enum class SharpStyle { Hash, Letter_s, Flat };   // C#3 / Cs3 / Db3

struct NoteNaming {
    /// Octave number for MIDI note 60. Ableton, Kontakt, Logic, Reason use 3 (C3 = 60);
    /// Cubase/Steinberg and general MIDI use 4 (C4 = 60).
    int middleCOctave = 3;
    SharpStyle sharps = SharpStyle::Hash;
};

std::string midiToNoteName(int midi, const NoteNaming& n = {});
/// Parses "C1", "c#3", "Db2", "Cs3", or a plain number "36". Returns -1 on failure.
int noteNameToMidi(const std::string& text, int middleCOctave = 3);

struct NameContext {
    std::string baseName;     ///< {name}
    int    index = 0;         ///< {index}  (1-based, zero-padded to indexWidth)
    int    indexWidth = 3;
    int    count = 0;         ///< {count}  total slices
    int    midiNote = 60;     ///< {midi} and {note}
    double bpm = 0;           ///< {bpm}   (rounded to integer, or "" if 0)
    double startMs = 0;       ///< {ms}
    double lengthMs = 0;      ///< {len}
    NoteNaming noteNaming;
};

/// Expands tokens: {name} {index} {count} {note} {midi} {bpm} {ms} {len}.
/// Unknown tokens are left as-is. The result is filesystem-safe (no extension added).
std::string expandTemplate(const std::string& tmpl, const NameContext& ctx);

/// Replace characters that are illegal in filenames on Windows/macOS/Linux.
std::string sanitizeFilename(std::string s);

} // namespace chopper
