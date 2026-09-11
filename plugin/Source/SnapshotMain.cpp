// Headless smoke test: load a file into the real processor + editor and render the UI to a PNG.
//   SampleChopperSnapshot <audio file> <out.png>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <audio file> <out.png>\n", argv[0]); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    SampleChopperProcessor proc;
    proc.prepareToPlay(44100.0, 512);
    juce::String err;
    if (!proc.loadFile(juce::File(argv[1]), err)) { std::fprintf(stderr, "load failed: %s\n", err.toRawUTF8()); return 1; }
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    editor->setSize(960, 620);
    const juce::Image img = editor->createComponentSnapshot(editor->getLocalBounds());
    juce::File out(argv[2]); out.deleteFile();
    juce::FileOutputStream os(out);
    if (!os.openedOk() || !juce::PNGImageFormat().writeImageToStream(img, os)) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    std::printf("%zu slices, wrote %s (%dx%d)\n", proc.session().slices().size(), argv[2], img.getWidth(), img.getHeight());
    return 0;
}
