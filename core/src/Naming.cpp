#include "chopper/Naming.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace chopper {

static const char* kSharpNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* kFlatNames[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

std::string midiToNoteName(int midi, const NoteNaming& n)
{
    midi = std::clamp(midi, 0, 127);
    const int pc = midi % 12;
    const int octave = midi / 12 - (5 - n.middleCOctave);   // 60/12 = 5 -> middleCOctave
    std::string name = n.sharps == SharpStyle::Flat ? kFlatNames[pc] : kSharpNames[pc];
    if (n.sharps == SharpStyle::Letter_s) std::replace(name.begin(), name.end(), '#', 's');
    return name + std::to_string(octave);
}

int noteNameToMidi(const std::string& text, int middleCOctave)
{
    std::string s; for (char c : text) if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    if (s.empty()) return -1;
    if (std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        int v = std::atoi(s.c_str()); return v >= 0 && v <= 127 ? v : -1;
    }
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char L = char(std::toupper(static_cast<unsigned char>(s[0])));
    if (L < 'A' || L > 'G') return -1;
    int pc = base[L - 'A']; size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == 's' || s[i] == 'S')) { pc += 1; ++i; }
    else if (i < s.size() && (s[i] == 'b' || s[i] == 'B') && i + 1 < s.size()) { pc -= 1; ++i; }   // "Bb3" vs "B3": needs a digit after
    if (i >= s.size()) return -1;
    char* end = nullptr; long oct = std::strtol(s.c_str() + i, &end, 10);
    if (end == s.c_str() + i || *end != '\0') return -1;
    const int midi = int(oct + (5 - middleCOctave)) * 12 + pc;
    return midi >= 0 && midi <= 127 ? midi : -1;
}

std::string sanitizeFilename(std::string s)
{
    for (auto& c : s) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) c = '_';
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s.empty() ? "untitled" : s;
}

std::string expandTemplate(const std::string& tmpl, const NameContext& ctx)
{
    auto pad = [](int v, int width) { std::string s = std::to_string(v); while (int(s.size()) < width) s = "0" + s; return s; };
    std::string out;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] != '{') { out += tmpl[i++]; continue; }
        const size_t close = tmpl.find('}', i);
        if (close == std::string::npos) { out += tmpl.substr(i); break; }
        std::string tok = tmpl.substr(i + 1, close - i - 1), arg;
        if (auto colon = tok.find(':'); colon != std::string::npos) { arg = tok.substr(colon + 1); tok = tok.substr(0, colon); }
        std::transform(tok.begin(), tok.end(), tok.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if      (tok == "name")  out += ctx.baseName;
        else if (tok == "index") out += pad(ctx.index, arg.empty() ? ctx.indexWidth : std::atoi(arg.c_str()));
        else if (tok == "count") out += std::to_string(ctx.count);
        else if (tok == "note")  out += midiToNoteName(ctx.midiNote, ctx.noteNaming);
        else if (tok == "midi")  out += pad(ctx.midiNote, arg.empty() ? 1 : std::atoi(arg.c_str()));
        else if (tok == "bpm")   out += ctx.bpm > 0 ? std::to_string(int(std::lround(ctx.bpm))) : std::string();
        else if (tok == "ms")    out += std::to_string(int(std::lround(ctx.startMs)));
        else if (tok == "len")   out += std::to_string(int(std::lround(ctx.lengthMs)));
        else out += tmpl.substr(i, close - i + 1);
        i = close + 1;
    }
    return sanitizeFilename(out);
}

} // namespace chopper
