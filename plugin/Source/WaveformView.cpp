#include "WaveformView.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMarkerGrabPx = 5.0f;
constexpr int kTopBand = 18;   // label strip above the waveform
}

WaveformView::WaveformView() { setMouseCursor(juce::MouseCursor::NormalCursor); }

void WaveformView::setAudio(const chopper::AudioData* audio)
{
    audio_ = (audio != nullptr && !audio->empty()) ? audio : nullptr;
    rebuildPeaks();
    repaint();
}

void WaveformView::setSlices(std::vector<chopper::Slice> slices, std::vector<juce::String> labels)
{
    slices_ = std::move(slices); labels_ = std::move(labels);
    repaint();
}

void WaveformView::setPlayback(int playingSlice, int playheadFrame)
{
    const bool changed = playingSlice != playingSlice_ || playheadFrame != playheadFrame_;
    playingSlice_ = playingSlice; playheadFrame_ = playheadFrame;
    if (playheadFrame >= 0 && !isTimerRunning()) startTimerHz(30);
    if (playheadFrame < 0 && isTimerRunning()) stopTimer();
    if (changed) repaint();
}

void WaveformView::resized() { rebuildPeaks(); }

void WaveformView::rebuildPeaks()
{
    peaks_.clear();
    const int w = getWidth();
    if (audio_ == nullptr || w <= 0) return;
    const auto mono = audio_->mono();
    const size_t n = mono.size();
    peaks_.resize(size_t(w), { 0.0f, 0.0f });
    for (int x = 0; x < w; ++x) {
        const size_t a = size_t(double(n) * x / w), b = std::max(a + 1, size_t(double(n) * (x + 1) / w));
        float lo = 0, hi = 0;
        for (size_t i = a; i < b && i < n; ++i) { lo = std::min(lo, mono[i]); hi = std::max(hi, mono[i]); }
        peaks_[size_t(x)] = { lo, hi };
    }
}

float WaveformView::frameToX(double frame) const
{
    if (audio_ == nullptr) return 0;
    return float(frame / double(audio_->numFrames()) * getWidth());
}
double WaveformView::xToFrame(float x) const
{
    if (audio_ == nullptr) return 0;
    return juce::jlimit(0.0, double(audio_->numFrames()), double(x) / getWidth() * double(audio_->numFrames()));
}
int WaveformView::markerAt(float x) const
{
    for (size_t i = 1; i < slices_.size(); ++i)   // marker 0 is the file start and can't move
        if (std::fabs(frameToX(double(slices_[i].start)) - x) <= kMarkerGrabPx) return int(i);
    return -1;
}
int WaveformView::sliceAt(float x) const
{
    const double f = xToFrame(x);
    for (size_t i = 0; i < slices_.size(); ++i)
        if (f >= double(slices_[i].start) && f < double(slices_[i].end)) return int(i);
    return -1;
}

void WaveformView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1b1d22)); g.fillRoundedRectangle(bounds, 6.0f);

    if (audio_ == nullptr) {
        g.setColour(juce::Colours::grey); g.setFont(16.0f);
        g.drawFittedText("Drop an audio file here, click Open, or Capture from your DAW.", getLocalBounds(), juce::Justification::centred, 2);
        return;
    }
    const float top = float(kTopBand), h = float(getHeight() - kTopBand), mid = top + h * 0.5f;

    // slice backgrounds
    for (size_t i = 0; i < slices_.size(); ++i) {
        const float x0 = frameToX(double(slices_[i].start)), x1 = frameToX(double(slices_[i].end));
        juce::Colour c = (i % 2 == 0) ? juce::Colour(0xff23262d) : juce::Colour(0xff1f2228);
        if (int(i) == playingSlice_) c = juce::Colour(0xff2d4a3a);
        g.setColour(c); g.fillRect(x0, top, x1 - x0, h);
    }
    // waveform
    g.setColour(juce::Colour(0xff7fb4ff));
    for (int x = 0; x < int(peaks_.size()); ++x) {
        const auto [lo, hi] = peaks_[size_t(x)];
        const float y0 = mid - hi * h * 0.48f, y1 = mid - lo * h * 0.48f;
        g.drawVerticalLine(x, std::min(y0, y1), std::max(y0, y1) + 1.0f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.15f)); g.drawHorizontalLine(int(mid), 0.0f, float(getWidth()));

    // markers + labels
    g.setFont(11.0f);
    for (size_t i = 0; i < slices_.size(); ++i) {
        const float x = frameToX(double(slices_[i].start));
        const bool hot = int(i) == hoverMarker_ || int(i) == dragMarker_;
        g.setColour(slices_[i].manual ? juce::Colour(0xffffc857) : juce::Colour(0xffff6b6b));
        if (hot) g.setColour(juce::Colours::white);
        g.fillRect(x - (hot ? 1.5f : 0.5f), top, hot ? 3.0f : 1.0f, h);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        juce::String lab = juce::String(int(i) + 1);
        if (i < labels_.size() && labels_[i].isNotEmpty()) lab += " " + labels_[i];
        const float x1 = i + 1 < slices_.size() ? frameToX(double(slices_[i + 1].start)) : float(getWidth());
        g.drawText(lab, juce::Rectangle<float>(x + 3, 2, std::max(10.0f, x1 - x - 4), float(kTopBand) - 3), juce::Justification::centredLeft, true);
    }
    if (playheadFrame_ >= 0) {
        g.setColour(juce::Colours::white); const float x = frameToX(double(playheadFrame_)); g.fillRect(x, top, 1.5f, h);
    }
    g.setColour(juce::Colours::white.withAlpha(0.35f)); g.setFont(11.0f);
    g.drawText("double-click: add cut   drag: move   alt/right-click: delete   click: audition", getLocalBounds().removeFromBottom(16).reduced(6, 0), juce::Justification::centredRight);
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    const int m = markerAt(float(e.x));
    if (m != hoverMarker_) { hoverMarker_ = m; repaint(); }
    setMouseCursor(m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void WaveformView::mouseExit(const juce::MouseEvent&) { if (hoverMarker_ != -1) { hoverMarker_ = -1; repaint(); } }

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    dragged_ = false;
    const int m = markerAt(float(e.x));
    if (m >= 0 && (e.mods.isAltDown() || e.mods.isPopupMenu())) { if (onRemovePoint) onRemovePoint(size_t(m)); dragMarker_ = -1; hoverMarker_ = -1; return; }
    dragMarker_ = m;
}
void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMarker_ < 0 || audio_ == nullptr) return;
    dragged_ = true;
    if (onMovePoint) onMovePoint(size_t(dragMarker_), size_t(xToFrame(float(e.x))));
}
void WaveformView::mouseUp(const juce::MouseEvent& e)
{
    if (dragMarker_ < 0 && !dragged_ && audio_ != nullptr && !e.mods.isPopupMenu() && !e.mods.isAltDown()) {
        const int s = sliceAt(float(e.x));
        if (s >= 0 && onAudition) onAudition(s);
    }
    dragMarker_ = -1; dragged_ = false;
    mouseMove(e);
}
void WaveformView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (audio_ == nullptr || markerAt(float(e.x)) >= 0) return;
    if (onAddPoint) onAddPoint(size_t(xToFrame(float(e.x))));
}
