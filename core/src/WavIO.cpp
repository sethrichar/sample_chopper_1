#include "chopper/WavIO.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace chopper {
namespace {

// ---- little-endian helpers -------------------------------------------------------------
void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, uint16_t(v)); put16(b, uint16_t(v >> 16)); }
void putTag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }
void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v); b[at + 1] = uint8_t(v >> 8); b[at + 2] = uint8_t(v >> 16); b[at + 3] = uint8_t(v >> 24);
}
uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

int32_t clampToInt(float x, float scale) {
    float v = std::round(x * scale);
    if (v > scale - 1.0f) v = scale - 1.0f;
    if (v < -scale) v = -scale;
    return static_cast<int32_t>(v);
}

} // namespace

// =========================================================================================
std::vector<uint8_t> encodeWav(const AudioData& audio, const WavWriteOptions& opts)
{
    const int    channels = std::max(1, audio.numChannels());
    const size_t frames   = audio.numFrames();
    const int    bits     = static_cast<int>(opts.bitDepth);
    const bool   isFloat  = opts.bitDepth == BitDepth::Float32;
    const int    bytesPerSample = bits / 8;
    const uint32_t sampleRate = static_cast<uint32_t>(std::lround(audio.sampleRate));
    const uint32_t blockAlign = static_cast<uint32_t>(channels * bytesPerSample);
    const uint32_t dataBytes  = static_cast<uint32_t>(frames * blockAlign);

    std::vector<uint8_t> b;
    b.reserve(dataBytes + 256 + opts.markers.size() * 64);

    putTag(b, "RIFF"); put32(b, 0); putTag(b, "WAVE");

    // fmt (use plain PCM/float tag for max compatibility; extensible only for >2 ch)
    const bool extensible = channels > 2;
    putTag(b, "fmt "); put32(b, extensible ? 40 : 16);
    put16(b, extensible ? 0xFFFE : (isFloat ? 3 : 1));
    put16(b, uint16_t(channels));
    put32(b, sampleRate);
    put32(b, sampleRate * blockAlign);
    put16(b, uint16_t(blockAlign));
    put16(b, uint16_t(bits));
    if (extensible) {
        put16(b, 22); put16(b, uint16_t(bits)); put32(b, 0);
        // KSDATAFORMAT_SUBTYPE_PCM / IEEE_FLOAT GUIDs
        put16(b, isFloat ? 3 : 1); put16(b, 0x0000); put16(b, 0x0010); put16(b, 0x8000);
        const uint8_t tail[8] = { 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71, 0, 0 };
        b.insert(b.end(), tail, tail + 8);
    }

    // data
    putTag(b, "data"); put32(b, dataBytes);
    if (isFloat) {
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c) {
                float v = c < audio.numChannels() ? audio.channels[size_t(c)][i] : 0.0f;
                uint32_t u; std::memcpy(&u, &v, 4); put32(b, u);
            }
    } else if (bits == 16) {
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c) {
                float v = c < audio.numChannels() ? audio.channels[size_t(c)][i] : 0.0f;
                put16(b, uint16_t(int16_t(clampToInt(v, 32768.0f))));
            }
    } else { // 24
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c) {
                float v = c < audio.numChannels() ? audio.channels[size_t(c)][i] : 0.0f;
                int32_t s = clampToInt(v, 8388608.0f);
                b.push_back(uint8_t(s)); b.push_back(uint8_t(s >> 8)); b.push_back(uint8_t(s >> 16));
            }
    }
    if (dataBytes & 1) b.push_back(0);

    // smpl: root note for samplers that auto-map from metadata
    if (opts.rootNote) {
        putTag(b, "smpl"); put32(b, 36);
        put32(b, 0); put32(b, 0);                                       // manufacturer, product
        put32(b, uint32_t(1000000000.0 / std::max(1u, sampleRate)));   // sample period (ns)
        put32(b, uint32_t(std::clamp(*opts.rootNote, 0, 127)));        // MIDI unity note
        put32(b, 0); put32(b, 0); put32(b, 0);                          // pitch fraction, SMPTE fmt/offset
        put32(b, 0); put32(b, 0);                                       // num loops, sampler data
    }

    // cue + labels
    if (!opts.markers.empty()) {
        putTag(b, "cue "); put32(b, uint32_t(4 + 24 * opts.markers.size()));
        put32(b, uint32_t(opts.markers.size()));
        for (size_t i = 0; i < opts.markers.size(); ++i) {
            put32(b, uint32_t(i + 1));                 // id
            put32(b, opts.markers[i].sampleOffset);    // position (play order)
            putTag(b, "data"); put32(b, 0); put32(b, 0);
            put32(b, opts.markers[i].sampleOffset);    // sample offset
        }
        putTag(b, "LIST");
        const size_t listSizeAt = b.size(); put32(b, 0);
        putTag(b, "adtl");
        for (size_t i = 0; i < opts.markers.size(); ++i) {
            std::string text = opts.markers[i].label;
            const uint32_t sz = uint32_t(4 + text.size() + 1);
            putTag(b, "labl"); put32(b, sz); put32(b, uint32_t(i + 1));
            b.insert(b.end(), text.begin(), text.end()); b.push_back(0);
            if (sz & 1) b.push_back(0);
        }
        patch32(b, listSizeAt, uint32_t(b.size() - listSizeAt - 4));
    }

    patch32(b, 4, uint32_t(b.size() - 8));
    return b;
}

bool writeWav(const std::string& path, const AudioData& audio, const WavWriteOptions& opts, std::string& error)
{
    if (audio.numChannels() == 0) { error = "no channels to write"; return false; }
    const auto bytes = encodeWav(audio, opts);
    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "cannot open for writing: " + path; return false; }
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!f) { error = "write failed: " + path; return false; }
    return true;
}

// =========================================================================================
bool decodeWav(const std::vector<uint8_t>& bytes, AudioData& out, std::string& error, WavInfo* info)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file"; return false;
    }
    uint16_t format = 0, channels = 0, bits = 0; uint32_t rate = 0;
    const uint8_t* data = nullptr; size_t dataSize = 0;
    WavInfo wi;
    std::vector<std::pair<uint32_t, uint32_t>> cues;            // id, offset
    std::vector<std::pair<uint32_t, std::string>> labels;      // id, text

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const uint8_t* p = bytes.data() + pos;
        uint32_t sz = get32(p + 4);
        const size_t body = pos + 8;
        if (body + sz > bytes.size()) sz = uint32_t(bytes.size() - body);   // tolerate truncated files
        if (std::memcmp(p, "fmt ", 4) == 0 && sz >= 16) {
            format   = get16(p + 8); channels = get16(p + 10); rate = get32(p + 12); bits = get16(p + 22);
            if (format == 0xFFFE && sz >= 40) format = get16(p + 8 + 24);  // sub-format GUID's first word
        } else if (std::memcmp(p, "data", 4) == 0) {
            data = p + 8; dataSize = sz;
        } else if (std::memcmp(p, "smpl", 4) == 0 && sz >= 16) {
            wi.rootNote = int(get32(p + 8 + 12));
        } else if (std::memcmp(p, "cue ", 4) == 0 && sz >= 4) {
            uint32_t n = get32(p + 8);
            for (uint32_t i = 0; i < n && 4 + 24 * (i + 1) <= sz; ++i) {
                const uint8_t* c = p + 12 + 24 * i;
                cues.emplace_back(get32(c), get32(c + 20));
            }
        } else if (std::memcmp(p, "LIST", 4) == 0 && sz >= 4 && std::memcmp(p + 8, "adtl", 4) == 0) {
            size_t q = 4;
            while (q + 8 <= sz) {
                const uint8_t* s = p + 8 + q; uint32_t ssz = get32(s + 4);
                if (std::memcmp(s, "labl", 4) == 0 && ssz >= 4) {
                    std::string text(reinterpret_cast<const char*>(s + 12), ssz - 4);
                    text = text.c_str();  // strip at NUL
                    labels.emplace_back(get32(s + 8), text);
                }
                q += 8 + ssz + (ssz & 1);
            }
        }
        pos = body + sz + (sz & 1);
    }
    if (!data || channels == 0 || rate == 0 || bits == 0) { error = "missing fmt or data chunk"; return false; }
    const bool isFloat = format == 3;
    if (!(format == 1 || format == 3)) { error = "unsupported WAV format tag " + std::to_string(format); return false; }
    if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32 || (isFloat && bits == 64))) {
        error = "unsupported bit depth " + std::to_string(bits); return false;
    }
    const size_t bps = bits / 8, frames = dataSize / (bps * channels);
    out.sampleRate = rate;
    out.resize(channels, frames);
    const uint8_t* p = data;
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c, p += bps) {
            float v = 0;
            if (isFloat && bits == 32) { uint32_t u = get32(p); std::memcpy(&v, &u, 4); }
            else if (isFloat)          { uint64_t u = uint64_t(get32(p)) | (uint64_t(get32(p + 4)) << 32); double d; std::memcpy(&d, &u, 8); v = float(d); }
            else if (bits == 8)        { v = (int(p[0]) - 128) / 128.0f; }
            else if (bits == 16)       { v = int16_t(get16(p)) / 32768.0f; }
            else if (bits == 24)       { int32_t s = int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8; v = s / 8388608.0f; }
            else                       { v = int32_t(get32(p)) / 2147483648.0f; }
            out.channels[size_t(c)][i] = v;
        }
    if (info) {
        wi.sampleRate = rate; wi.channels = channels; wi.bitsPerSample = bits; wi.isFloat = isFloat; wi.frames = frames;
        for (auto& [id, off] : cues) {
            WavMarker m; m.sampleOffset = off;
            for (auto& [lid, text] : labels) if (lid == id) m.label = text;
            wi.markers.push_back(m);
        }
        *info = wi;
    }
    return true;
}

bool readWav(const std::string& path, AudioData& out, std::string& error, WavInfo* info)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open: " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return decodeWav(bytes, out, error, info);
}

} // namespace chopper
