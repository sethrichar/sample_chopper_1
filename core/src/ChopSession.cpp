#include "chopper/ChopSession.h"
#include "chopper/Tempo.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

namespace chopper {

void ChopSession::setAudio(AudioData audio, std::string sourceName)
{
    audio_ = std::move(audio);
    sourceName_ = std::move(sourceName);
    onsets_.clear(); slices_.clear();
    if (settings_.baseName.empty() || settings_.baseName == "Slice") {
        std::string base = std::filesystem::path(sourceName_).stem().string();
        if (!base.empty()) settings_.baseName = base;
    }
}

size_t ChopSession::minLengthFrames() const
{
    return size_t(std::max(1.0, settings_.detector.minSliceMs * audio_.sampleRate / 1000.0));
}

void ChopSession::analyze()
{
    onsets_.clear(); slices_.clear();
    if (audio_.empty()) return;
    if (settings_.mode == SliceMode::Grid) {
        slices_ = gridSlices(audio_.numFrames(), settings_.gridDivisions);
        return;
    }
    TransientDetector det(settings_.detector);
    onsets_ = det.detect(audio_);
    slices_ = slicesFromOnsets(onsets_, audio_.numFrames(), settings_.includeStart, minLengthFrames());
    if (slices_.empty()) slices_ = { Slice{ 0, audio_.numFrames(), 1.0f, false } };
}

void ChopSession::addSlicePoint(size_t frame)   { chopper::addSlicePoint(slices_, frame, minLengthFrames()); }
void ChopSession::removeSlicePoint(size_t i)    { chopper::removeSlicePoint(slices_, i); }
void ChopSession::moveSlicePoint(size_t i, size_t frame) { chopper::moveSlicePoint(slices_, i, frame, minLengthFrames()); }
void ChopSession::clearSlices()
{
    slices_.clear();
    if (!audio_.empty()) slices_ = { Slice{ 0, audio_.numFrames(), 1.0f, true } };
}

double ChopSession::effectiveBpm() const
{
    if (settings_.bpm > 0) return settings_.bpm;
    const auto c = estimateBpmFromLength(audio_.durationSeconds());
    return c.empty() ? 120.0 : c.front().bpm;
}

int ChopSession::noteForSlice(size_t i) const
{
    return settings_.chromatic ? std::min(127, settings_.rootNote + int(i)) : settings_.rootNote;
}

std::string ChopSession::fileNameForSlice(size_t i) const
{
    if (i >= slices_.size()) return {};
    const auto& s = slices_[i];
    NameContext c;
    c.baseName = sanitizeFilename(settings_.baseName);
    c.index = int(i) + 1; c.indexWidth = settings_.indexWidth; c.count = int(slices_.size());
    c.midiNote = noteForSlice(i); c.bpm = effectiveBpm();
    c.startMs = 1000.0 * double(s.start) / audio_.sampleRate;
    c.lengthMs = 1000.0 * double(s.length()) / audio_.sampleRate;
    c.noteNaming = settings_.noteNaming;
    return expandTemplate(settings_.nameTemplate, c) + ".wav";
}

ExportReport ChopSession::exportAll(const std::string& outputDir, const std::function<void(float)>& progress) const
{
    ExportReport r; r.outputDir = outputDir;
    if (audio_.empty()) { r.error = "no audio loaded"; return r; }
    if (slices_.empty()) { r.error = "no slices - run analyze() first"; return r; }

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) { r.error = "cannot create output directory: " + ec.message(); return r; }
    const std::filesystem::path dir(outputDir);
    const std::string base = sanitizeFilename(settings_.baseName);
    r.bpmUsed = effectiveBpm();

    std::set<std::string> used;
    std::vector<std::string> labels;
    for (size_t i = 0; i < slices_.size(); ++i) {
        const Slice& s = slices_[i];
        std::string name = fileNameForSlice(i);
        if (used.count(name)) {   // template without a unique token: disambiguate
            const std::string stem = name.substr(0, name.size() - 4);
            int k = 2; do { name = stem + "_" + std::to_string(k++) + ".wav"; } while (used.count(name));
        }
        used.insert(name);
        AudioData rendered = renderSlice(audio_, s, settings_.processing);
        WavWriteOptions o; o.bitDepth = settings_.bitDepth; o.rootNote = noteForSlice(i);
        std::string err;
        if (!writeWav((dir / name).string(), rendered, o, err)) { r.error = err; return r; }
        ExportedSlice e;
        e.fileName = name; e.midiNote = noteForSlice(i); e.start = s.start; e.end = s.end;
        e.startSeconds = double(s.start) / audio_.sampleRate;
        e.lengthSeconds = double(rendered.numFrames()) / audio_.sampleRate;
        r.slices.push_back(e); r.files.push_back(name);
        labels.push_back(name.substr(0, name.size() - 4));
        if (progress) progress(float(i + 1) / float(slices_.size() + 1));
    }

    std::string err;
    if (settings_.writeSfz) {
        const std::string f = base + ".sfz";
        if (!writeSfz((dir / f).string(), r.slices, base, err)) { r.error = err; return r; }
        r.files.push_back(f);
    }
    if (settings_.writeMidi) {
        const std::string f = base + ".mid";
        if (!writeSliceMidi((dir / f).string(), r.slices, r.bpmUsed, settings_.midiVelocity, err)) { r.error = err; return r; }
        r.files.push_back(f);
    }
    if (settings_.writeJson) {
        const std::string f = base + ".json";
        if (!writeJsonManifest((dir / f).string(), sourceName_, audio_.sampleRate, r.bpmUsed, settings_.rootNote, r.slices, err)) { r.error = err; return r; }
        r.files.push_back(f);
    }
    if (settings_.writeMarkerWav) {
        const std::string f = base + "_markers.wav";
        if (!writeMarkerWav((dir / f).string(), audio_, slices_, labels, settings_.bitDepth, err)) { r.error = err; return r; }
        r.files.push_back(f);
    }
    if (progress) progress(1.0f);
    r.ok = true;
    return r;
}

} // namespace chopper
