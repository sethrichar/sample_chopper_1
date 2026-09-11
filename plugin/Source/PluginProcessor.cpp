#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "chopper/Tempo.h"

using namespace chopper;

namespace {
const juce::StringArray kGridChoices { "4", "8", "16", "32", "64" };
const int kGridValues[] = { 4, 8, 16, 32, 64 };
}

juce::AudioProcessorValueTreeState::ParameterLayout SampleChopperProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto rangeWithSkew = [](float lo, float hi, float step, float centre) { NormalisableRange<float> r(lo, hi, step); r.setSkewForCentre(centre); return r; };

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::sensitivity, 1 }, "Sensitivity", NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::minSliceMs, 1 }, "Min slice (ms)", rangeWithSkew(10.0f, 1000.0f, 1.0f, 80.0f), 40.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::fadeOutMs, 1 }, "Fade out (ms)", rangeWithSkew(0.0f, 300.0f, 0.5f, 20.0f), 5.0f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::mode, 1 }, "Mode", StringArray { "Transients", "Grid" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::gridDivisions, 1 }, "Grid divisions", kGridChoices, 2));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { ids::rootNote, 1 }, "Root note", 0, 127, 36,
        AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return String(midiToNoteName(v)); })));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::chromatic, 1 }, "Chromatic mapping", true));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::bitDepth, 1 }, "Bit depth", StringArray { "16-bit", "24-bit", "32-bit float" }, 1));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::writeSfz, 1 }, "Write SFZ", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::writeMidi, 1 }, "Write MIDI", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::writeJson, 1 }, "Write JSON", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::writeMarkers, 1 }, "Write marker WAV", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::trim, 1 }, "Trim silence", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::normalize, 1 }, "Normalize", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::midiTriggers, 1 }, "MIDI triggers slices", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::includeStart, 1 }, "Slice at start", true));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::octave, 1 }, "Octave convention", StringArray { "C3 = 60 (Ableton, Kontakt, Logic)", "C4 = 60 (Cubase)" }, 0));
    return layout;
}

SampleChopperProcessor::SampleChopperProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    extras.setProperty(ids::baseName, "", nullptr);
    extras.setProperty(ids::nameTemplate, "{name}_{index}_{note}", nullptr);
    extras.setProperty(ids::bpm, 0.0, nullptr);
    extras.setProperty(ids::outputDir, "", nullptr);
    for (auto* id : { ids::sensitivity, ids::minSliceMs, ids::mode, ids::gridDivisions, ids::includeStart, ids::rootNote, ids::chromatic })
        apvts.addParameterListener(id, this);
    applySettingsFromState();
}

SampleChopperProcessor::~SampleChopperProcessor()
{
    alive_->store(false);
    stopTimer();
    cancelPendingUpdate();
}

// ------------------------------------------------------------------------------------------
void SampleChopperProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate_ = sampleRate;
    const int frames = int(kMaxCaptureSeconds * sampleRate);
    if (captureBuffer_.getNumSamples() != frames || captureBuffer_.getNumChannels() != 2)
        captureBuffer_.setSize(2, frames, false, true, false);
    captureWritePos_ = 0;
    for (auto& v : voices_) v.slice = -1;
}

bool SampleChopperProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || in.isDisabled();
}

void SampleChopperProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear(ch, 0, numSamples);

    // ---- capture: the audio passing through becomes the source -------------------------------
    int st = captureState_.load();
    if (st != 0) {
        bool playing = false, haveTransport = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) { haveTransport = true; playing = pos->getIsPlaying(); }
        if (haveTransport) hostTransportSeen_ = true;
        if (st == 1 && haveTransport && playing) { captureWritePos_ = 0; captureState_ = 2; st = 2; }
        if (st == 2) {
            if (haveTransport && wasPlaying_ && !playing) {
                captureState_ = 0; triggerAsyncUpdate();
            } else {
                const int pos = captureWritePos_.load();
                const int n = juce::jmin(captureBuffer_.getNumSamples() - pos, numSamples);
                if (n > 0)
                    for (int ch = 0; ch < 2; ++ch)
                        captureBuffer_.copyFrom(ch, pos, buffer, juce::jmin(ch, juce::jmax(0, numIn - 1)), 0, n);
                captureWritePos_ = pos + n;
                if (n < numSamples) { captureState_ = 0; triggerAsyncUpdate(); }   // buffer full
            }
        }
        wasPlaying_ = playing;
    }

    // ---- slice playback: preview clicks + MIDI notes -----------------------------------------
    juce::SpinLock::ScopedTryLockType lock(sourceLock_);
    if (!lock.isLocked() || source_ == nullptr) { playheadFrame_ = -1; return; }
    const PlaybackSource& src = *source_;
    const int numSlices = int(src.slices.size());

    auto trigger = [&](int slice, bool exclusive) {
        if (slice < 0 || slice >= numSlices) return;
        if (exclusive) for (auto& v : voices_) v.slice = -1;
        Voice* target = nullptr;
        for (auto& v : voices_) if (v.slice < 0) { target = &v; break; }
        if (!target) { target = &voices_[0]; for (auto& v : voices_) if (v.pos > target->pos) target = &v; }
        target->slice = slice; target->pos = src.slices[size_t(slice)].first; target->end = src.slices[size_t(slice)].second;
        playingSlice_ = slice;
    };

    const int pending = pendingPreview_.exchange(-1);
    if (pending == -2) { for (auto& v : voices_) v.slice = -1; playingSlice_ = -1; }
    else if (pending >= 0) trigger(pending, true);

    const bool midiOn = apvts.getRawParameterValue(ids::midiTriggers)->load() > 0.5f;
    if (midiOn)
        for (const auto meta : midi) {
            const auto m = meta.getMessage();
            if (!m.isNoteOn()) continue;
            trigger(src.chromatic ? m.getNoteNumber() - src.rootNote : 0, false);
        }

    bool anyActive = false;
    const int srcCh = src.audio.getNumChannels();
    for (auto& v : voices_) {
        if (v.slice < 0) continue;
        const int n = juce::jmin(numSamples, v.end - v.pos);
        if (n <= 0) { v.slice = -1; continue; }
        for (int ch = 0; ch < numOut; ++ch)
            buffer.addFrom(ch, 0, src.audio, juce::jmin(ch, srcCh - 1), v.pos, n, 0.9f);
        // 2 ms fade at the slice end to avoid clicks
        const int fade = int(0.002 * currentSampleRate_);
        if (v.end - (v.pos + n) < fade) {
            for (int i = 0; i < n; ++i) {
                const int remaining = v.end - (v.pos + i);
                if (remaining < fade) {
                    const float g = float(remaining) / float(fade);
                    for (int ch = 0; ch < numOut; ++ch) {
                        float* d = buffer.getWritePointer(ch);
                        d[i] -= 0.9f * (1.0f - g) * src.audio.getSample(juce::jmin(ch, srcCh - 1), v.pos + i);
                    }
                }
            }
        }
        v.pos += n; anyActive = true;
        if (v.pos >= v.end) v.slice = -1; else playheadFrame_ = v.pos;
    }
    if (!anyActive) { playheadFrame_ = -1; playingSlice_ = -1; }
}

// ------------------------------------------------------------------------------------------
void SampleChopperProcessor::handleAsyncUpdate()
{
    // Capture just finished on the audio thread: copy the recording into the session.
    const int frames = captureWritePos_.load();
    if (frames <= 0) { status = "Capture: nothing recorded."; sendChangeMessage(); return; }
    AudioData a; a.sampleRate = currentSampleRate_; a.resize(2, size_t(frames));
    for (int ch = 0; ch < 2; ++ch) std::copy(captureBuffer_.getReadPointer(ch), captureBuffer_.getReadPointer(ch) + frames, a.channels[size_t(ch)].begin());
    setSourceAudio(std::move(a), "Capture");
    status = "Captured " + juce::String(a.durationSeconds(), 2) + " s.";
    sendChangeMessage();
}

void SampleChopperProcessor::toggleCapture()
{
    const int st = captureState_.load();
    if (st == 0) { captureState_ = 1; status = hostTransportSeen_ ? "Armed: press Play in your DAW (or click again to start now)." : "Armed: click again to start recording."; }
    else if (st == 1) { captureWritePos_ = 0; captureState_ = 2; status = "Recording..."; }
    else { captureState_ = 0; triggerAsyncUpdate(); }
    sendChangeMessage();
}

// ------------------------------------------------------------------------------------------
bool SampleChopperProcessor::loadFile(const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr) { error = "Unsupported or unreadable file: " + file.getFileName(); return false; }
    const int ch = int(reader->numChannels); const juce::int64 len = reader->lengthInSamples;
    if (ch <= 0 || len <= 0) { error = "Empty audio file."; return false; }
    if (len > juce::int64(reader->sampleRate * 600)) { error = "File is longer than 10 minutes; chop something shorter."; return false; }
    juce::AudioBuffer<float> buf(ch, int(len));
    reader->read(&buf, 0, int(len), 0, true, true);
    AudioData a; a.sampleRate = reader->sampleRate; a.resize(ch, size_t(len));
    for (int c = 0; c < ch; ++c) std::copy(buf.getReadPointer(c), buf.getReadPointer(c) + len, a.channels[size_t(c)].begin());
    setSourceAudio(std::move(a), file.getFileName());
    status = "Loaded " + file.getFileName();
    return true;
}

void SampleChopperProcessor::setSourceAudio(AudioData audio, const juce::String& sourceName)
{
    previewSlice(-1);
    session_.settings().baseName.clear();      // let the session derive it from the file name
    session_.setAudio(std::move(audio), sourceName.toStdString());
    extras.setProperty(ids::baseName, juce::String(session_.settings().baseName), nullptr);
    applySettingsFromState();
    session_.analyze();
    refreshPlaybackSource();
    sendChangeMessage();
}

void SampleChopperProcessor::applySettingsFromState()
{
    auto p = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    ChopSettings& s = session_.settings();
    s.mode = p(ids::mode) > 0.5f ? SliceMode::Grid : SliceMode::Transients;
    s.detector.sensitivity = p(ids::sensitivity);
    s.detector.minSliceMs = p(ids::minSliceMs);
    s.gridDivisions = kGridValues[juce::jlimit(0, 4, int(p(ids::gridDivisions)))];
    s.includeStart = p(ids::includeStart) > 0.5f;
    s.processing.fadeOutMs = p(ids::fadeOutMs);
    s.processing.trimTrailingSilence = p(ids::trim) > 0.5f;
    s.processing.normalize = p(ids::normalize) > 0.5f;
    s.rootNote = int(p(ids::rootNote));
    s.chromatic = p(ids::chromatic) > 0.5f;
    s.noteNaming.middleCOctave = p(ids::octave) > 0.5f ? 4 : 3;
    s.bitDepth = int(p(ids::bitDepth)) == 0 ? BitDepth::Pcm16 : int(p(ids::bitDepth)) == 2 ? BitDepth::Float32 : BitDepth::Pcm24;
    s.writeSfz = p(ids::writeSfz) > 0.5f; s.writeMidi = p(ids::writeMidi) > 0.5f;
    s.writeJson = p(ids::writeJson) > 0.5f; s.writeMarkerWav = p(ids::writeMarkers) > 0.5f;
    const juce::String base = extras.getProperty(ids::baseName).toString().trim();
    if (base.isNotEmpty()) s.baseName = base.toStdString();
    const juce::String tmpl = extras.getProperty(ids::nameTemplate).toString().trim();
    s.nameTemplate = tmpl.isNotEmpty() ? tmpl.toStdString() : "{name}_{index}_{note}";
    s.bpm = double(extras.getProperty(ids::bpm));
}

void SampleChopperProcessor::reanalyze()
{
    applySettingsFromState();
    session_.analyze();
    refreshPlaybackSource();
    sendChangeMessage();
}

void SampleChopperProcessor::parameterChanged(const juce::String&, float)
{
    // May be called from the audio thread (automation); debounce onto the message thread.
    juce::MessageManager::callAsync([alive = alive_, this] { if (alive->load()) startTimer(150); });
}

void SampleChopperProcessor::timerCallback()
{
    stopTimer();
    if (session_.hasAudio()) reanalyze();
}

void SampleChopperProcessor::addSlicePoint(size_t frame)          { session_.addSlicePoint(frame); refreshPlaybackSource(); sendChangeMessage(); }
void SampleChopperProcessor::removeSlicePoint(size_t i)            { session_.removeSlicePoint(i); refreshPlaybackSource(); sendChangeMessage(); }
void SampleChopperProcessor::moveSlicePoint(size_t i, size_t frame) { session_.moveSlicePoint(i, frame); refreshPlaybackSource(); sendChangeMessage(); }
void SampleChopperProcessor::clearSlices()                          { session_.clearSlices(); refreshPlaybackSource(); sendChangeMessage(); }

void SampleChopperProcessor::refreshPlaybackSource()
{
    auto src = std::make_shared<PlaybackSource>();
    const AudioData& a = session_.audio();
    if (!a.empty()) {
        src->audio.setSize(a.numChannels(), int(a.numFrames()));
        for (int c = 0; c < a.numChannels(); ++c) src->audio.copyFrom(c, 0, a.channels[size_t(c)].data(), int(a.numFrames()));
        for (const auto& s : session_.slices()) src->slices.emplace_back(int(s.start), int(s.end));
    }
    src->rootNote = session_.settings().rootNote;
    src->chromatic = session_.settings().chromatic;
    std::shared_ptr<PlaybackSource> old;
    {
        juce::SpinLock::ScopedLockType lock(sourceLock_);
        old = std::move(source_); source_ = std::move(src);
        for (auto& v : voices_) v.slice = -1;
    }
    // `old` is freed here, on the message thread
}

void SampleChopperProcessor::previewSlice(int index) { pendingPreview_ = index < 0 ? -2 : index; }

// ------------------------------------------------------------------------------------------
void SampleChopperProcessor::exportTo(const juce::File& dir, std::function<void(const ExportReport&)> onDone)
{
    if (exporting_.exchange(true)) return;
    applySettingsFromState();
    extras.setProperty(ids::outputDir, dir.getFullPathName(), nullptr);
    exportProgress_ = 0.0f;
    auto snapshot = std::make_shared<ChopSession>(session_);   // export works on a copy
    juce::Thread::launch([this, alive = alive_, snapshot, path = dir.getFullPathName().toStdString(), onDone] {
        ExportReport r = snapshot->exportAll(path, [this, alive](float p) { if (alive->load()) exportProgress_ = p; });
        juce::MessageManager::callAsync([this, alive, r, onDone] {
            if (!alive->load()) return;
            exporting_ = false;
            status = r.ok ? "Exported " + juce::String(r.files.size()) + " files to " + juce::String(r.outputDir) : "Export failed: " + juce::String(r.error);
            if (onDone) onDone(r);
            sendChangeMessage();
        });
    });
}

// ------------------------------------------------------------------------------------------
void SampleChopperProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root("SAMPLECHOPPER");
    root.addChild(apvts.copyState(), -1, nullptr);
    root.addChild(extras.createCopy(), -1, nullptr);
    juce::MemoryOutputStream mos(destData, true);
    root.writeToStream(mos);
}

void SampleChopperProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData(data, size_t(sizeInBytes));
    if (!root.isValid()) return;
    if (auto params = root.getChildWithName(apvts.state.getType()); params.isValid()) apvts.replaceState(params);
    if (auto ex = root.getChildWithName("EXTRAS"); ex.isValid()) extras.copyPropertiesFrom(ex, nullptr);
    applySettingsFromState();
}

juce::AudioProcessorEditor* SampleChopperProcessor::createEditor() { return new SampleChopperEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SampleChopperProcessor(); }
