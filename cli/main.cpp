// chopper - command-line front end for the Sample Chopper core.
#include "chopper/ChopSession.h"
#include "chopper/Tempo.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

using namespace chopper;

static void usage()
{
    std::printf(
        "chopper - split audio at transients and export sampler-ready slices\n\n"
        "usage: chopper --in <file.wav> [--out <dir>] [options]\n\n"
        "  --in <file>          source WAV\n"
        "  --out <dir>          output folder (default: <file>_slices next to the source)\n"
        "  --name <base>        base name for slices (default: source file stem)\n"
        "  --template <tmpl>    file name template, default \"{name}_{index}_{note}\"\n"
        "                       tokens: {name} {index} {index:2} {count} {note} {midi} {bpm} {ms} {len}\n"
        "  --sensitivity <0-1>  transient sensitivity (default 0.5)\n"
        "  --min-ms <ms>        minimum slice length (default 40)\n"
        "  --preroll-ms <ms>    move cuts this far before the attack (default 1)\n"
        "  --grid <n>           ignore transients, cut into n equal parts\n"
        "  --no-start           don't force a slice at the very start of the file\n"
        "  --root <note>        first slice's key, e.g. C1 or 36 (default C1)\n"
        "  --same-key           map every slice to the root key instead of chromatically\n"
        "  --octave <3|4>       octave number of MIDI 60: 3 = Ableton/Kontakt/Logic (default), 4 = Cubase\n"
        "  --sharps <hash|s|flat>  C#1 (default) / Cs1 / Db1\n"
        "  --bpm <bpm>          tempo for the MIDI file and {bpm} token (default: estimate from length)\n"
        "  --bits <16|24|32>    output bit depth (default 24; 32 = float)\n"
        "  --fade-out <ms>      fade-out at the end of each slice (default 5)\n"
        "  --trim               trim trailing silence from each slice\n"
        "  --normalize          peak-normalise each slice\n"
        "  --no-sfz --no-midi --no-json   skip those companion files\n"
        "  --markers            also write <name>_markers.wav (source with cue points)\n"
        "  --dry-run            list the slices, write nothing\n");
}

int main(int argc, char** argv)
{
    std::string in, out; bool dry = false;
    ChopSession session;
    ChopSettings& s = session.settings();
    std::string nameOverride;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); } return argv[++i]; };
        if      (a == "--in")          in = next("--in");
        else if (a == "--out")         out = next("--out");
        else if (a == "--name")        nameOverride = next("--name");
        else if (a == "--template")    s.nameTemplate = next("--template");
        else if (a == "--sensitivity") s.detector.sensitivity = std::stof(next(a.c_str()));
        else if (a == "--min-ms")      s.detector.minSliceMs = std::stof(next(a.c_str()));
        else if (a == "--preroll-ms")  s.detector.preRollMs = std::stof(next(a.c_str()));
        else if (a == "--grid")        { s.mode = SliceMode::Grid; s.gridDivisions = std::atoi(next(a.c_str()).c_str()); }
        else if (a == "--no-start")    s.includeStart = false;
        else if (a == "--root")        { int n = noteNameToMidi(next(a.c_str()), s.noteNaming.middleCOctave); if (n < 0) { std::fprintf(stderr, "bad --root\n"); return 2; } s.rootNote = n; }
        else if (a == "--same-key")    s.chromatic = false;
        else if (a == "--octave")      s.noteNaming.middleCOctave = std::atoi(next(a.c_str()).c_str());
        else if (a == "--sharps")      { std::string v = next(a.c_str()); s.noteNaming.sharps = v == "s" ? SharpStyle::Letter_s : v == "flat" ? SharpStyle::Flat : SharpStyle::Hash; }
        else if (a == "--bpm")         s.bpm = std::stod(next(a.c_str()));
        else if (a == "--bits")        { int b = std::atoi(next(a.c_str()).c_str()); s.bitDepth = b == 16 ? BitDepth::Pcm16 : b == 32 ? BitDepth::Float32 : BitDepth::Pcm24; }
        else if (a == "--fade-out")    s.processing.fadeOutMs = std::stof(next(a.c_str()));
        else if (a == "--trim")        s.processing.trimTrailingSilence = true;
        else if (a == "--normalize")   s.processing.normalize = true;
        else if (a == "--no-sfz")      s.writeSfz = false;
        else if (a == "--no-midi")     s.writeMidi = false;
        else if (a == "--no-json")     s.writeJson = false;
        else if (a == "--markers")     s.writeMarkerWav = true;
        else if (a == "--dry-run")     dry = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n\n", a.c_str()); usage(); return 2; }
    }
    if (in.empty()) { usage(); return 2; }

    AudioData audio; std::string err; WavInfo info;
    if (!readWav(in, audio, err, &info)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    session.setAudio(std::move(audio), std::filesystem::path(in).filename().string());
    if (!nameOverride.empty()) s.baseName = nameOverride;
    if (out.empty()) out = (std::filesystem::path(in).parent_path() / (std::filesystem::path(in).stem().string() + "_slices")).string();

    session.analyze();
    const auto& sl = session.slices();
    const double sr = session.audio().sampleRate;
    std::printf("%s: %.2f s, %d ch, %.0f Hz, %d-bit%s -> %zu slices, tempo %.2f bpm%s\n",
                session.sourceName().c_str(), session.audio().durationSeconds(), session.audio().numChannels(), sr,
                info.bitsPerSample, info.isFloat ? " float" : "", sl.size(), session.effectiveBpm(), s.bpm > 0 ? "" : " (estimated)");
    for (size_t i = 0; i < sl.size(); ++i)
        std::printf("  %3zu  %9.1f ms  %8.1f ms  str %.2f  %s\n", i + 1, 1000.0 * double(sl[i].start) / sr,
                    1000.0 * double(sl[i].length()) / sr, double(sl[i].strength), session.fileNameForSlice(i).c_str());
    if (dry) return 0;

    ExportReport r = session.exportAll(out);
    if (!r.ok) { std::fprintf(stderr, "export failed: %s\n", r.error.c_str()); return 1; }
    std::printf("wrote %zu files to %s\n", r.files.size(), r.outputDir.c_str());
    return 0;
}
