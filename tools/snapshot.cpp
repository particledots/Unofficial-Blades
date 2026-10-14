// Draws the plug-in's panel into a PNG (used by the build to show what the panel looks like). Usage: blades_snapshot out.png
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    UnofficialBladesProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(argc > 1 ? argv[1] : "panel.png");
    out.deleteFile();
    juce::FileOutputStream fos(out);
    juce::PNGImageFormat png;
    if (!fos.openedOk() || !png.writeImageToStream(img, fos)) return 1;
    return 0;
}
