// App suelta (Claraboya Grab.exe): una ventana con la misma interfaz del plugin,
// sin la barra de audio que trae la app estándar de JUCE (no hace falta: no procesa audio).

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "BrandLook.h"

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

namespace
{
    juce::File windowStateFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Claraboya Grab").getChildFile ("ventana.txt");
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (GrabProcessor& p)
            : DocumentWindow (JucePlugin_Name, brand::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            auto* editor = p.createEditor();
            editor->setResizable (false, false);   // la ventana se encarga del tamaño
            setContentOwned (editor, true);

            setResizable (true, false);
            setResizeLimits (480, 460, 1600, 1600);
            setIcon (juce::ImageCache::getFromMemory (BinaryData::iconochico256_png, BinaryData::iconochico256_pngSize));

            const auto state = windowStateFile().loadFileAsString();
            if (state.isEmpty() || ! restoreWindowStateFromString (state))
                centreWithSize (600, 620);

            setVisible (true);
        }

        ~MainWindow() override
        {
            const auto f = windowStateFile();
            f.getParentDirectory().createDirectory();
            f.replaceWithText (getWindowStateAsString());
            clearContentComponent();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}

class GrabApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        processor = std::make_unique<GrabProcessor>();
        window = std::make_unique<MainWindow> (*processor);
    }

    void shutdown() override
    {
        window.reset();      // primero la ventana (tiene la interfaz), después el procesador
        processor.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
        {
            window->setMinimised (false);
            window->toFront (true);
        }
    }

private:
    std::unique_ptr<GrabProcessor> processor;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (GrabApp)

#endif
