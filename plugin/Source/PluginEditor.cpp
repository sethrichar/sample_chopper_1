#include "PluginEditor.h"
#include "chopper/Tempo.h"

using namespace chopper;

SampleChopperEditor::SampleChopperEditor(SampleChopperProcessor& p)
    : AudioProcessorEditor(&p), proc_(p)
{
    setResizable(true, true);
    setResizeLimits(820, 560, 2400, 1600);
    setSize(960, 620);

    // ---- toolbar ---------------------------------------------------------------------------
    for (auto* b : { &openButton, &captureButton, &analyzeButton, &exportButton }) addAndMakeVisible(b);
    openButton.onClick = [this] { openFile(); };
    captureButton.onClick = [this] { proc_.toggleCapture(); refreshFromProcessor(); };
    analyzeButton.onClick = [this] { proc_.reanalyze(); };
    exportButton.onClick = [this] { chooseFolderAndExport(); };
    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));

    // ---- waveform --------------------------------------------------------------------------
    addAndMakeVisible(waveform);
    waveform.onAddPoint = [this](size_t f) { proc_.addSlicePoint(f); };
    waveform.onMovePoint = [this](size_t i, size_t f) { proc_.moveSlicePoint(i, f); };
    waveform.onRemovePoint = [this](size_t i) { proc_.removeSlicePoint(i); };
    waveform.onAudition = [this](int i) { proc_.previewSlice(i); };

    // ---- slicing group ---------------------------------------------------------------------
    addAndMakeVisible(detectGroup);
    for (auto* c : std::initializer_list<juce::Component*> { &modeBox, &gridBox, &sensitivitySlider, &minSliceSlider, &includeStartToggle, &modeLabel, &gridLabel, &sensLabel, &minLabel })
        addAndMakeVisible(c);
    modeBox.addItemList({ "Transients", "Grid" }, 1);
    gridBox.addItemList({ "4", "8", "16", "32", "64" }, 1);
    for (auto* s : { &sensitivitySlider, &minSliceSlider })
        { s->setSliderStyle(juce::Slider::LinearHorizontal); s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20); }
    modeAtt = std::make_unique<Apvts::ComboBoxAttachment>(proc_.apvts, ids::mode, modeBox);
    gridAtt = std::make_unique<Apvts::ComboBoxAttachment>(proc_.apvts, ids::gridDivisions, gridBox);
    sensAtt = std::make_unique<Apvts::SliderAttachment>(proc_.apvts, ids::sensitivity, sensitivitySlider);
    minAtt = std::make_unique<Apvts::SliderAttachment>(proc_.apvts, ids::minSliceMs, minSliceSlider);
    includeStartAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::includeStart, includeStartToggle);

    // ---- naming group ----------------------------------------------------------------------
    addAndMakeVisible(nameGroup);
    for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &templateEditor, &bpmEditor, &rootSlider, &chromaticToggle, &octaveBox, &nameLabel, &templateLabel, &rootLabel, &bpmLabel, &octaveLabel, &previewLabel })
        addAndMakeVisible(c);
    for (auto* e : { &nameEditor, &templateEditor, &bpmEditor }) {
        e->setSelectAllWhenFocused(true);
        e->onReturnKey = [this] { textChanged(); };
        e->onFocusLost = [this] { textChanged(); };
    }
    templateEditor.setTooltip("{name} {index} {index:2} {count} {note} {midi} {bpm} {ms} {len}");
    bpmEditor.setTextToShowWhenEmpty("auto", juce::Colours::grey);
    bpmEditor.setInputRestrictions(7, "0123456789.");
    rootSlider.setSliderStyle(juce::Slider::IncDecButtons);
    rootSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 22);
    rootSlider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_AutoDirection);
    rootAtt = std::make_unique<Apvts::SliderAttachment>(proc_.apvts, ids::rootNote, rootSlider);
    rootSlider.textFromValueFunction = [this](double v) { return juce::String(midiToNoteName(int(v), proc_.session().settings().noteNaming)); };
    rootSlider.valueFromTextFunction = [this](const juce::String& t) { const int n = noteNameToMidi(t.toStdString(), proc_.session().settings().noteNaming.middleCOctave); return n < 0 ? rootSlider.getValue() : double(n); };
    rootSlider.updateText();
    octaveBox.addItemList({ "C3 = 60 (Ableton/Kontakt/Logic)", "C4 = 60 (Cubase)" }, 1);
    octaveAtt = std::make_unique<Apvts::ComboBoxAttachment>(proc_.apvts, ids::octave, octaveBox);
    octaveBox.onChange = [this] { rootSlider.updateText(); };
    chromaticAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::chromatic, chromaticToggle);
    previewLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    previewLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    // ---- export group ----------------------------------------------------------------------
    addAndMakeVisible(exportGroup);
    for (auto* c : std::initializer_list<juce::Component*> { &bitDepthBox, &fadeSlider, &trimToggle, &normalizeToggle, &sfzToggle, &midiToggle, &jsonToggle, &markersToggle, &midiTrigToggle, &bitLabel, &fadeLabel, &folderLabel })
        addAndMakeVisible(c);
    bitDepthBox.addItemList({ "16-bit", "24-bit", "32-bit float" }, 1);
    bitAtt = std::make_unique<Apvts::ComboBoxAttachment>(proc_.apvts, ids::bitDepth, bitDepthBox);
    fadeSlider.setSliderStyle(juce::Slider::LinearHorizontal); fadeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
    fadeAtt = std::make_unique<Apvts::SliderAttachment>(proc_.apvts, ids::fadeOutMs, fadeSlider);
    trimAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::trim, trimToggle);
    normAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::normalize, normalizeToggle);
    sfzAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::writeSfz, sfzToggle);
    midiAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::writeMidi, midiToggle);
    jsonAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::writeJson, jsonToggle);
    markersAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::writeMarkers, markersToggle);
    midiTrigAtt = std::make_unique<Apvts::ButtonAttachment>(proc_.apvts, ids::midiTriggers, midiTrigToggle);
    folderLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    folderLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    for (auto* l : { &modeLabel, &gridLabel, &sensLabel, &minLabel, &nameLabel, &templateLabel, &rootLabel, &bpmLabel, &octaveLabel, &bitLabel, &fadeLabel })
        l->setJustificationType(juce::Justification::centredRight);

    proc_.addChangeListener(this);
    refreshFromProcessor();
    startTimerHz(20);
}

SampleChopperEditor::~SampleChopperEditor() { proc_.removeChangeListener(this); }

// ------------------------------------------------------------------------------------------
void SampleChopperEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2d34));
}

void SampleChopperEditor::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto bar = r.removeFromTop(30);
    for (auto* b : { &openButton, &captureButton, &analyzeButton, &exportButton }) { b->setBounds(bar.removeFromLeft(b == &captureButton ? 150 : 90)); bar.removeFromLeft(6); }
    statusLabel.setBounds(bar);
    r.removeFromTop(8);

    const int panelH = 236;
    auto panels = r.removeFromBottom(panelH);
    r.removeFromBottom(8);
    waveform.setBounds(r);

    const int gap = 8, colW = (panels.getWidth() - 2 * gap) / 3;
    auto col1 = panels.removeFromLeft(colW); panels.removeFromLeft(gap);
    auto col2 = panels.removeFromLeft(colW); panels.removeFromLeft(gap);
    auto col3 = panels;
    detectGroup.setBounds(col1); nameGroup.setBounds(col2); exportGroup.setBounds(col3);

    auto row = [](juce::Rectangle<int>& area, int h = 26) { auto x = area.removeFromTop(h); area.removeFromTop(4); return x; };
    auto labelled = [](juce::Rectangle<int> rr, juce::Label& l, juce::Component& c, int lw = 90) { l.setBounds(rr.removeFromLeft(lw)); rr.removeFromLeft(6); c.setBounds(rr); };

    auto a = col1.reduced(10, 0); a.removeFromTop(22);
    labelled(row(a), modeLabel, modeBox);
    labelled(row(a), gridLabel, gridBox);
    labelled(row(a), sensLabel, sensitivitySlider);
    labelled(row(a), minLabel, minSliceSlider);
    includeStartToggle.setBounds(row(a).withTrimmedLeft(96));

    auto b = col2.reduced(10, 0); b.removeFromTop(22);
    labelled(row(b), nameLabel, nameEditor, 70);
    labelled(row(b), templateLabel, templateEditor, 70);
    { auto rr = row(b); auto left = rr.removeFromLeft(rr.getWidth() / 2); labelled(left, rootLabel, rootSlider, 70); rr.removeFromLeft(6); labelled(rr, bpmLabel, bpmEditor, 40); }
    labelled(row(b), octaveLabel, octaveBox, 70);
    chromaticToggle.setBounds(row(b).withTrimmedLeft(76));
    previewLabel.setBounds(row(b, 22).withTrimmedLeft(76));

    auto c = col3.reduced(10, 0); c.removeFromTop(22);
    labelled(row(c), bitLabel, bitDepthBox, 90);
    labelled(row(c), fadeLabel, fadeSlider, 90);
    { auto rr = row(c, 22); trimToggle.setBounds(rr.removeFromLeft(rr.getWidth() / 2)); normalizeToggle.setBounds(rr); }
    { auto rr = row(c, 22); sfzToggle.setBounds(rr.removeFromLeft(rr.getWidth() / 2)); midiToggle.setBounds(rr); }
    { auto rr = row(c, 22); jsonToggle.setBounds(rr.removeFromLeft(rr.getWidth() / 2)); markersToggle.setBounds(rr); }
    midiTrigToggle.setBounds(row(c, 22));
    folderLabel.setBounds(row(c, 20));
}

// ------------------------------------------------------------------------------------------
void SampleChopperEditor::changeListenerCallback(juce::ChangeBroadcaster*) { refreshFromProcessor(); }

void SampleChopperEditor::timerCallback()
{
    waveform.setPlayback(proc_.playingSlice(), proc_.playheadFrame());
    const auto st = proc_.captureState();
    if (st == SampleChopperProcessor::CaptureState::Recording)
        captureButton.setButtonText("Stop (" + juce::String(proc_.captureSeconds(), 1) + " s)");
    if (proc_.isExporting())
        statusLabel.setText("Exporting... " + juce::String(int(proc_.exportProgress() * 100)) + "%", juce::dontSendNotification);
}

void SampleChopperEditor::refreshFromProcessor()
{
    const auto& s = proc_.session();
    waveform.setAudio(s.hasAudio() ? &s.audio() : nullptr);
    std::vector<juce::String> labels;
    for (size_t i = 0; i < s.slices().size(); ++i) labels.emplace_back(midiToNoteName(s.noteForSlice(i), s.settings().noteNaming));
    waveform.setSlices(s.slices(), labels);

    if (!nameEditor.hasKeyboardFocus(true)) nameEditor.setText(proc_.extras.getProperty(ids::baseName).toString(), false);
    if (!templateEditor.hasKeyboardFocus(true)) templateEditor.setText(proc_.extras.getProperty(ids::nameTemplate).toString(), false);
    if (!bpmEditor.hasKeyboardFocus(true)) {
        const double bpm = double(proc_.extras.getProperty(ids::bpm));
        bpmEditor.setText(bpm > 0 ? juce::String(bpm, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".") : "", false);
        bpmEditor.setTextToShowWhenEmpty(s.hasAudio() ? "auto (" + juce::String(s.effectiveBpm(), 1) + ")" : "auto", juce::Colours::grey);
    }
    rootSlider.updateText();

    juce::String preview;
    if (s.hasAudio() && !s.slices().empty())
        preview = juce::String(s.slices().size()) + " slices  ->  " + juce::String(s.fileNameForSlice(0)) + " ...";
    previewLabel.setText(preview, juce::dontSendNotification);

    const auto st = proc_.captureState();
    captureButton.setButtonText(st == SampleChopperProcessor::CaptureState::Idle ? "Capture from input" : st == SampleChopperProcessor::CaptureState::Armed ? "Armed - click to start" : "Stop");
    captureButton.setColour(juce::TextButton::buttonColourId, st == SampleChopperProcessor::CaptureState::Idle ? getLookAndFeel().findColour(juce::TextButton::buttonColourId) : juce::Colour(0xffb03a3a));
    exportButton.setEnabled(s.hasAudio() && !proc_.isExporting());
    analyzeButton.setEnabled(s.hasAudio());

    const auto dir = proc_.extras.getProperty(ids::outputDir).toString();
    folderLabel.setText(dir.isNotEmpty() ? "Last export: " + dir : "", juce::dontSendNotification);
    statusLabel.setText(proc_.status, juce::dontSendNotification);
    repaint();
}

void SampleChopperEditor::textChanged()
{
    bool changed = false;
    auto set = [&](const char* id, const juce::var& v) { if (proc_.extras.getProperty(id) != v) { proc_.extras.setProperty(id, v, nullptr); changed = true; } };
    set(ids::baseName, nameEditor.getText().trim());
    set(ids::nameTemplate, templateEditor.getText().trim().isEmpty() ? juce::String("{name}_{index}_{note}") : templateEditor.getText().trim());
    set(ids::bpm, bpmEditor.getText().getDoubleValue());
    if (changed) { proc_.applySettingsFromState(); refreshFromProcessor(); }
}

// ------------------------------------------------------------------------------------------
void SampleChopperEditor::openFile()
{
    chooser_ = std::make_unique<juce::FileChooser>("Open audio file", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        juce::String err;
        if (!proc_.loadFile(f, err)) { proc_.status = err; refreshFromProcessor(); }
    });
}

void SampleChopperEditor::chooseFolderAndExport()
{
    textChanged();
    const auto last = proc_.extras.getProperty(ids::outputDir).toString();
    const juce::File start = last.isNotEmpty() ? juce::File(last) : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Choose the folder to export slices into", start);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (dir == juce::File()) return;
        proc_.status = "Exporting...";
        refreshFromProcessor();
        proc_.exportTo(dir, nullptr);
    });
}

bool SampleChopperEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File(f).hasFileExtension("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void SampleChopperEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (juce::File(f).hasFileExtension("wav;aif;aiff;flac;ogg;mp3")) {
            juce::String err;
            if (!proc_.loadFile(juce::File(f), err)) proc_.status = err;
            refreshFromProcessor();
            return;
        }
}
