#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "chopper/ChopSession.h"
#include <atomic>
#include <functional>
#include <memory>

namespace ids {
inline constexpr const char* sensitivity   = "sensitivity";
inline constexpr const char* minSliceMs    = "minSliceMs";
inline constexpr const char* fadeOutMs     = "fadeOutMs";
inline constexpr const char* mode          = "mode";           // 0 transients, 1 grid
inline constexpr const char* gridDivisions = "gridDivisions";  // choice index
inline constexpr const char* rootNote      = "rootNote";
inline constexpr const char* chromatic     = "chromatic";
inline constexpr const char* bitDepth      = "bitDepth";       // 0=16 1=24 2=32f
inline constexpr const char* writeSfz      = "writeSfz";
inline constexpr const char* writeMidi     = "writeMidi";
inline constexpr const char* writeJson     = "writeJson";
inline constexpr const char* writeMarkers  = "writeMarkers";
inline constexpr const char* trim          = "trim";
inline constexpr const char* normalize     = "normalize";
inline constexpr const char* midiTriggers  = "midiTriggers";
inline constexpr const char* includeStart  = "includeStart";
inline constexpr const char* octave        = "octave";         // 0 => C3=60, 1 => C4=60
// non-automatable text state lives in `extras`
inline constexpr const char* baseName      = "baseName";
inline constexpr const char* nameTemplate  = "nameTemplate";
inline constexpr const char* bpm           = "bpm";            // 0 = auto
inline constexpr const char* outputDir     = "outputDir";
}

class SampleChopperProcessor : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::AsyncUpdater,
                               private juce::Timer,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    SampleChopperProcessor();
    ~SampleChopperProcessor() override;

    // ---- AudioProcessor ----------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- message-thread API used by the editor ------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    juce::ValueTree extras { "EXTRAS" };

    const chopper::ChopSession& session() const { return session_; }
    chopper::ChopSettings& settings() { return session_.settings(); }

    bool loadFile(const juce::File& file, juce::String& error);
    void setSourceAudio(chopper::AudioData audio, const juce::String& sourceName);
    void reanalyze();                         ///< re-run detection with current settings
    void applySettingsFromState();            ///< push apvts/extras into the session settings

    void addSlicePoint(size_t frame);
    void removeSlicePoint(size_t index);
    void moveSlicePoint(size_t index, size_t frame);
    void clearSlices();

    enum class CaptureState { Idle = 0, Armed = 1, Recording = 2 };
    CaptureState captureState() const { return static_cast<CaptureState>(captureState_.load()); }
    void toggleCapture();                     ///< Idle→Armed→Recording→Idle
    double captureSeconds() const { return captureWritePos_.load() / currentSampleRate_; }
    bool hostHasTransport() const { return hostTransportSeen_.load(); }
    static constexpr double kMaxCaptureSeconds = 180.0;

    void previewSlice(int index);             ///< -1 stops all voices
    int  playingSlice() const { return playingSlice_.load(); }
    int  playheadFrame() const { return playheadFrame_.load(); }

    void exportTo(const juce::File& dir, std::function<void(const chopper::ExportReport&)> onDone);
    bool isExporting() const { return exporting_.load(); }
    float exportProgress() const { return exportProgress_.load(); }

    juce::String status;                      ///< last message for the status bar

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;        // capture finished (posted from the audio thread)
    void timerCallback() override;            // debounced re-analysis
    void refreshPlaybackSource();

    chopper::ChopSession session_;            // message thread only
    double currentSampleRate_ = 44100.0;

    // capture
    juce::AudioBuffer<float> captureBuffer_;
    std::atomic<int> captureWritePos_ { 0 };
    std::atomic<int> captureState_ { 0 };
    std::atomic<bool> hostTransportSeen_ { false };
    bool wasPlaying_ = false;

    // slice playback (preview + MIDI); the audio thread reads `source_` under a try-lock
    struct PlaybackSource {
        juce::AudioBuffer<float> audio;
        std::vector<std::pair<int, int>> slices;
        int rootNote = 36;
        bool chromatic = true;
    };
    juce::SpinLock sourceLock_;
    std::shared_ptr<PlaybackSource> source_;
    struct Voice { int slice = -1; int pos = 0; int end = 0; };
    static constexpr int kMaxVoices = 8;
    Voice voices_[kMaxVoices];
    std::atomic<int> pendingPreview_ { -1 };  // -1 nothing, -2 stop all, >=0 slice
    std::atomic<int> playingSlice_ { -1 };
    std::atomic<int> playheadFrame_ { -1 };

    // export
    std::atomic<bool> exporting_ { false };
    std::atomic<float> exportProgress_ { 0.0f };
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleChopperProcessor)
};
