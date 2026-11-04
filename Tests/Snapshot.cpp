// Saca capturas de la ventana del plugin sin abrir un DAW (solo desarrollo).
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Tools.h"

static void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

static void save (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const juce::File wavDir (argc > 2 ? juce::String (argv[2]) : outDir.getChildFile ("wavs").getFullPathName());

    {
        // Vacío
        auto empty = outDir.getChildFile ("vacia");
        empty.createDirectory();
        GrabProcessor p;
        p.engine.setOutputFolder (empty);
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        pump (400);
        save (*ed, outDir.getChildFile ("captura-vacia.png"));
    }

    {
        // Con archivos, un error y una búsqueda
        GrabProcessor p;
        p.engine.setOutputFolder (wavDir);
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        p.engine.enqueueDownload ("https://open.spotify.com/album/123");
        p.engine.waitUntilIdle (20000);
        pump (2200);
        save (*ed, outDir.getChildFile ("captura-lista.png"));
    }

    return 0;
}
