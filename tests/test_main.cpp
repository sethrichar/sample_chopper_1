// Minimal self-contained tests for the chopper core (no framework needed).
#include "chopper/ChopSession.h"
#include "chopper/Tempo.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace chopper;
static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++checks; double _a=(a), _b=(b); if (std::fabs(_a-_b) > (tol)) { ++failures; std::printf("FAIL %s:%d: %s=%g vs %s=%g (tol %g)\n", __FILE__, __LINE__, #a, _a, #b, _b, double(tol)); } } while (0)

/// A "drum loop": decaying noise bursts at the given positions over a quiet pad-ish hum.
static AudioData synthLoop(double sr, double seconds, const std::vector<double>& hitTimes, const std::vector<float>& gains, unsigned seed = 7)
{
    AudioData a; a.sampleRate = sr; a.resize(2, size_t(sr * seconds));
    std::mt19937 rng(seed); std::normal_distribution<float> noise(0.0f, 1.0f);
    for (size_t i = 0; i < a.numFrames(); ++i) {
        const float hum = 0.02f * std::sin(2.0f * float(M_PI) * 110.0f * float(i) / float(sr));
        a.channels[0][i] = hum; a.channels[1][i] = hum;
    }
    for (size_t h = 0; h < hitTimes.size(); ++h) {
        const size_t start = size_t(hitTimes[h] * sr);
        const float g = h < gains.size() ? gains[h] : 0.8f;
        for (size_t i = start; i < a.numFrames() && i < start + size_t(0.15 * sr); ++i) {
            const float env = std::exp(-float(i - start) / float(0.03 * sr));
            const float v = g * env * noise(rng) * 0.5f;
            a.channels[0][i] += v; a.channels[1][i] += v * 0.9f;
        }
    }
    return a;
}

static void testWavRoundTrip()
{
    AudioData a; a.sampleRate = 48000; a.resize(2, 1000);
    for (size_t i = 0; i < 1000; ++i) { a.channels[0][i] = std::sin(float(i) * 0.05f) * 0.9f; a.channels[1][i] = -a.channels[0][i] * 0.5f; }
    for (BitDepth d : { BitDepth::Pcm16, BitDepth::Pcm24, BitDepth::Float32 }) {
        WavWriteOptions o; o.bitDepth = d; o.rootNote = 48;
        o.markers = { { 10, "one" }, { 500, "two" } };
        auto bytes = encodeWav(a, o);
        AudioData b; std::string err; WavInfo info;
        CHECK(decodeWav(bytes, b, err, &info));
        CHECK(b.numChannels() == 2 && b.numFrames() == 1000);
        CHECK_NEAR(b.sampleRate, 48000, 0);
        CHECK(info.bitsPerSample == int(d));
        CHECK(info.rootNote && *info.rootNote == 48);
        CHECK(info.markers.size() == 2 && info.markers[1].sampleOffset == 500 && info.markers[1].label == "two");
        const double tol = d == BitDepth::Pcm16 ? 1.0 / 32000 : d == BitDepth::Pcm24 ? 1.0 / 8000000 : 1e-7;
        double maxErr = 0; for (size_t i = 0; i < 1000; ++i) maxErr = std::max(maxErr, std::fabs(double(a.channels[1][i] - b.channels[1][i])));
        CHECK(maxErr <= tol);
    }
    // garbage in
    AudioData g; std::string err; CHECK(!decodeWav({ 1, 2, 3 }, g, err));
}

static void testNaming()
{
    CHECK(midiToNoteName(60) == "C3");
    CHECK(midiToNoteName(60, { 4, SharpStyle::Hash }) == "C4");
    CHECK(midiToNoteName(36) == "C1");
    CHECK(midiToNoteName(61) == "C#3");
    CHECK(midiToNoteName(61, { 3, SharpStyle::Letter_s }) == "Cs3");
    CHECK(midiToNoteName(61, { 3, SharpStyle::Flat }) == "Db3");
    CHECK(midiToNoteName(0) == "C-2");
    CHECK(noteNameToMidi("C1") == 36);
    CHECK(noteNameToMidi("c#3") == 61);
    CHECK(noteNameToMidi("Db3") == 61);
    CHECK(noteNameToMidi("Cs3") == 61);
    CHECK(noteNameToMidi("B2") == 59);
    CHECK(noteNameToMidi("Bb2") == 58);
    CHECK(noteNameToMidi("C-2") == 0);
    CHECK(noteNameToMidi("60") == 60);
    CHECK(noteNameToMidi("C4", 4) == 60);
    CHECK(noteNameToMidi("nope") == -1);
    NameContext c; c.baseName = "Break"; c.index = 7; c.count = 12; c.midiNote = 42; c.bpm = 93.6; c.startMs = 1234.4; c.lengthMs = 250;
    CHECK(expandTemplate("{name}_{index}_{note}", c) == "Break_007_F#1");
    CHECK(expandTemplate("{name}-{index:2}-{midi}-{bpm}bpm-{ms}-{len}-{count}", c) == "Break-07-42-94bpm-1234-250-12");
    CHECK(expandTemplate("{unknown}/{name}", c) == "{unknown}_Break");
    CHECK(sanitizeFilename("a:b*c?d\"e<f>g|h. ") == "a_b_c_d_e_f_g_h");
}

static void testTempo()
{
    auto c = estimateBpmFromLength(2.0);           // 1 bar at 120
    CHECK(!c.empty()); CHECK_NEAR(c[0].bpm, 120.0, 1e-9); CHECK(c[0].bars == 1);
    c = estimateBpmFromLength(60.0 / 93.0 * 8);    // 2 bars at 93
    CHECK(!c.empty()); CHECK_NEAR(c[0].bpm, 93.0, 1e-6); CHECK(c[0].bars == 2);
    CHECK(estimateBpmFromLength(0).empty());
}

static void testDetection()
{
    const double sr = 44100;
    // 16th-note pattern at 120 bpm with rests and varied gains
    const std::vector<double> hits = { 0.0, 0.25, 0.5, 0.625, 1.0, 1.25, 1.375, 1.5, 1.75 };
    const std::vector<float> gains = { 1.0f, 0.6f, 0.9f, 0.4f, 1.0f, 0.5f, 0.3f, 0.9f, 0.7f };
    AudioData a = synthLoop(sr, 2.0, hits, gains);

    TransientDetector det;
    auto onsets = det.detect(a);
    CHECK(onsets.size() == hits.size());
    if (onsets.size() == hits.size())
        for (size_t i = 0; i < hits.size(); ++i) {
            const double expectMs = hits[i] * 1000.0, gotMs = 1000.0 * double(onsets[i].sample) / sr;
            CHECK_NEAR(gotMs, expectMs, 4.0);                 // within 4 ms
            CHECK(gotMs <= expectMs + 0.5);                   // never *after* the attack
        }
    // low sensitivity keeps only the loud hits, high keeps everything
    DetectorSettings low; low.sensitivity = 0.0f;
    auto few = TransientDetector(low).detect(a);
    CHECK(few.size() < onsets.size() && few.size() >= 2);
    DetectorSettings high; high.sensitivity = 1.0f;
    CHECK(TransientDetector(high).detect(a).size() >= onsets.size());
    // min slice length merges close hits
    DetectorSettings wide; wide.minSliceMs = 200.0f;
    auto merged = TransientDetector(wide).detect(a);
    for (size_t i = 1; i < merged.size(); ++i) CHECK(merged[i].sample - merged[i - 1].sample >= size_t(0.2 * sr));
    // silence -> nothing
    AudioData silent; silent.sampleRate = sr; silent.resize(1, 44100);
    CHECK(det.detect(silent).empty());
}

static void testSlicerEditing()
{
    std::vector<Onset> on = { { 1000, 0.5f }, { 5000, 0.9f }, { 5010, 0.1f }, { 9000, 0.7f } };
    auto sl = slicesFromOnsets(on, 12000, true, 100);
    CHECK(sl.size() == 4);   // 0, 1000, 5000, 9000 (5010 too close)
    CHECK(sl[0].start == 0 && sl[0].end == 1000 && sl.back().end == 12000);
    addSlicePoint(sl, 3000, 100);
    CHECK(sl.size() == 5 && sl[1].end == 3000 && sl[2].start == 3000 && sl[2].manual);
    addSlicePoint(sl, 3050, 100);     // too close, ignored
    CHECK(sl.size() == 5);
    moveSlicePoint(sl, 2, 2500, 100);
    CHECK(sl[1].end == 2500 && sl[2].start == 2500);
    moveSlicePoint(sl, 2, 0, 100);    // clamped
    CHECK(sl[2].start == 1100);
    removeSlicePoint(sl, 2);
    CHECK(sl.size() == 4 && sl[1].end == 5000);
    removeSlicePoint(sl, 0);          // first slice can't be removed
    CHECK(sl.size() == 4);
    auto grid = gridSlices(1000, 16);
    CHECK(grid.size() == 16 && grid.front().start == 0 && grid.back().end == 1000);
    CHECK(slicesFromOnsets({}, 500, false, 10).empty());

    // rendering: fades + trim + normalise
    AudioData a; a.sampleRate = 1000; a.resize(1, 1000);
    for (size_t i = 0; i < 300; ++i) a.channels[0][i] = 0.5f;
    SliceProcessing p; p.fadeInMs = 10; p.fadeOutMs = 10; p.trimTrailingSilence = true; p.minTailMs = 20; p.normalize = true; p.normalizeTargetDb = 0;
    AudioData r = renderSlice(a, Slice{ 0, 1000 }, p);
    CHECK(r.numFrames() == 320);
    CHECK_NEAR(r.channels[0][0], 0.0, 1e-6);
    CHECK_NEAR(r.channels[0][150], 1.0, 1e-5);
    CHECK_NEAR(r.channels[0][319], 0.0, 1e-6);
}

static std::vector<uint8_t> slurp(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary); return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

static void testSessionExport()
{
    const double sr = 44100;
    const std::vector<double> hits = { 0.0, 0.5, 1.0, 1.5 };
    ChopSession session;
    session.setAudio(synthLoop(sr, 2.0, hits, { 1, 1, 1, 1 }), "MyBreak.wav");
    CHECK(session.settings().baseName == "MyBreak");
    session.settings().writeMarkerWav = true;
    session.analyze();
    CHECK(session.slices().size() == 4);
    CHECK(session.fileNameForSlice(0) == "MyBreak_001_C1.wav");
    CHECK(session.fileNameForSlice(3) == "MyBreak_004_D#1.wav");
    CHECK_NEAR(session.effectiveBpm(), 120.0, 1e-9);

    const auto dir = std::filesystem::temp_directory_path() / "chopper_test_out";
    std::filesystem::remove_all(dir);
    float lastProgress = 0; auto r = session.exportAll(dir.string(), [&](float p) { lastProgress = p; });
    CHECK(r.ok); if (!r.ok) std::printf("  export error: %s\n", r.error.c_str());
    CHECK_NEAR(lastProgress, 1.0, 1e-6);
    CHECK(r.files.size() == 4 + 4);   // 4 wavs + sfz + mid + json + markers
    CHECK(std::filesystem::exists(dir / "MyBreak_001_C1.wav"));
    CHECK(std::filesystem::exists(dir / "MyBreak.sfz"));
    CHECK(std::filesystem::exists(dir / "MyBreak.mid"));
    CHECK(std::filesystem::exists(dir / "MyBreak.json"));
    CHECK(std::filesystem::exists(dir / "MyBreak_markers.wav"));

    // slice wav carries the root note
    AudioData sl; std::string err; WavInfo info;
    CHECK(readWav((dir / "MyBreak_002_C#1.wav").string(), sl, err, &info));
    CHECK(info.rootNote && *info.rootNote == 37);
    CHECK(info.bitsPerSample == 24 && info.channels == 2);
    CHECK_NEAR(sl.durationSeconds(), 0.5, 0.01);

    // marker wav has 4 cues
    CHECK(readWav((dir / "MyBreak_markers.wav").string(), sl, err, &info));
    CHECK(info.markers.size() == 4 && info.markers[0].label == "MyBreak_001_C1");

    // MIDI: header, format 0, 4 note-ons
    auto mid = slurp(dir / "MyBreak.mid");
    CHECK(mid.size() > 30 && std::string(mid.begin(), mid.begin() + 4) == "MThd");
    int noteOns = 0; for (size_t i = 0; i + 2 < mid.size(); ++i) if (mid[i] == 0x90 && mid[i + 2] == 100) ++noteOns;
    CHECK(noteOns == 4);
    // tempo meta = 500000 us (120 bpm)
    bool tempoOk = false; for (size_t i = 0; i + 5 < mid.size(); ++i) if (mid[i] == 0xFF && mid[i + 1] == 0x51 && mid[i + 3] == 0x07 && mid[i + 4] == 0xA1 && mid[i + 5] == 0x20) tempoOk = true;
    CHECK(tempoOk);

    // SFZ has 4 regions with keys 36..39
    std::ifstream sfz(dir / "MyBreak.sfz"); std::string text((std::istreambuf_iterator<char>(sfz)), {});
    CHECK(text.find("<region> sample=MyBreak_001_C1.wav key=36") != std::string::npos);
    CHECK(text.find("key=39") != std::string::npos);
    CHECK(text.find("loop_mode=one_shot") != std::string::npos);

    // JSON mentions the source
    std::ifstream js(dir / "MyBreak.json"); std::string jtext((std::istreambuf_iterator<char>(js)), {});
    CHECK(jtext.find("\"source\": \"MyBreak.wav\"") != std::string::npos);
    CHECK(jtext.find("\"midiNote\": 39") != std::string::npos);

    // duplicate names from a template without unique tokens get disambiguated
    session.settings().nameTemplate = "{name}";
    session.settings().writeMarkerWav = false;
    auto r2 = session.exportAll((dir / "dup").string());
    CHECK(r2.ok && r2.slices.size() == 4 && r2.slices[0].fileName == "MyBreak.wav" && r2.slices[1].fileName == "MyBreak_2.wav");

    // grid mode + same-key
    session.settings().mode = SliceMode::Grid; session.settings().gridDivisions = 8; session.settings().chromatic = false;
    session.analyze();
    CHECK(session.slices().size() == 8 && session.noteForSlice(7) == 36);

    // manual editing through the session
    session.clearSlices(); CHECK(session.slices().size() == 1);
    session.addSlicePoint(size_t(sr)); CHECK(session.slices().size() == 2);
    session.removeSlicePoint(1); CHECK(session.slices().size() == 1);

    std::filesystem::remove_all(dir);
}

int main()
{
    testWavRoundTrip();
    testNaming();
    testTempo();
    testDetection();
    testSlicerEditing();
    testSessionExport();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
