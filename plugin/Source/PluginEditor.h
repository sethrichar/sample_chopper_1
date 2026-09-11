#pragma once
#include "PluginProcessor.h"
#include "WaveformView.h"

class SampleChopperEditor : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit SampleChopperEditor(SampleChopperProcessor&);
    ~SampleChopperEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshFromProcessor();
    void openFile();
    void chooseFolderAndExport();
    void textChanged();

    using Apvts = juce::AudioProcessorValueTreeState;
    SampleChopperProcessor& proc_;

    // toolbar
    juce::TextButton openButton { "Open..." }, captureButton { "Capture" }, analyzeButton { "Re-detect" }, exportButton { "Export..." };
    juce::Label statusLabel;

    WaveformView waveform;

    // detection
    juce::GroupComponent detectGroup { {}, "Slicing" };
    juce::ComboBox modeBox, gridBox;
    juce::Slider sensitivitySlider, minSliceSlider;
    juce::ToggleButton includeStartToggle { "Slice at file start" };
    juce::Label modeLabel { {}, "Mode" }, gridLabel { {}, "Grid" }, sensLabel { {}, "Sensitivity" }, minLabel { {}, "Min slice (ms)" };

    // naming
    juce::GroupComponent nameGroup { {}, "Naming & mapping" };
    juce::TextEditor nameEditor, templateEditor, bpmEditor;
    juce::Slider rootSlider;
    juce::ToggleButton chromaticToggle { "Chromatic (one key per slice)" };
    juce::ComboBox octaveBox;
    juce::Label nameLabel { {}, "Name" }, templateLabel { {}, "Template" }, rootLabel { {}, "Root key" }, bpmLabel { {}, "BPM" }, octaveLabel { {}, "Octaves" }, previewLabel;

    // export
    juce::GroupComponent exportGroup { {}, "Export" };
    juce::ComboBox bitDepthBox;
    juce::Slider fadeSlider;
    juce::ToggleButton trimToggle { "Trim tail silence" }, normalizeToggle { "Normalize slices" },
                       sfzToggle { ".sfz instrument" }, midiToggle { ".mid trigger file" }, jsonToggle { ".json manifest" },
                       markersToggle { "marker .wav (cue points)" }, midiTrigToggle { "MIDI notes play slices" };
    juce::Label bitLabel { {}, "Bit depth" }, fadeLabel { {}, "Fade out (ms)" }, folderLabel;

    std::unique_ptr<Apvts::ComboBoxAttachment> modeAtt, gridAtt, bitAtt, octaveAtt;
    std::unique_ptr<Apvts::SliderAttachment> sensAtt, minAtt, rootAtt, fadeAtt;
    std::unique_ptr<Apvts::ButtonAttachment> includeStartAtt, chromaticAtt, trimAtt, normAtt, sfzAtt, midiAtt, jsonAtt, markersAtt, midiTrigAtt;

    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleChopperEditor)
};
