#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "chopper/Slicer.h"
#include "chopper/Audio.h"
#include <functional>
#include <vector>

/// Draws the source waveform with slice markers; supports add / drag / delete / audition.
class WaveformView : public juce::Component, private juce::Timer
{
public:
    WaveformView();

    void setAudio(const chopper::AudioData* audio);          ///< nullptr = nothing loaded
    void setSlices(std::vector<chopper::Slice> slices, std::vector<juce::String> labels);
    void setPlayback(int playingSlice, int playheadFrame);   ///< -1 = idle

    std::function<void(size_t frame)> onAddPoint;
    std::function<void(size_t index, size_t frame)> onMovePoint;
    std::function<void(size_t index)> onRemovePoint;
    std::function<void(int index)> onAudition;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    void rebuildPeaks();
    float frameToX(double frame) const;
    double xToFrame(float x) const;
    int markerAt(float x) const;   ///< index of the slice whose start marker is under x, or -1
    int sliceAt(float x) const;

    const chopper::AudioData* audio_ = nullptr;
    std::vector<chopper::Slice> slices_;
    std::vector<juce::String> labels_;
    std::vector<std::pair<float, float>> peaks_;   // per pixel column: min, max
    int hoverMarker_ = -1, dragMarker_ = -1;
    bool dragged_ = false;
    int playingSlice_ = -1, playheadFrame_ = -1;
};
