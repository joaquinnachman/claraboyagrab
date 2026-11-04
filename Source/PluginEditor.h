#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

#include "PluginProcessor.h"
#include "BrandLook.h"

class GrabEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer,
                         private juce::ListBoxModel
{
public:
    explicit GrabEditor (GrabProcessor&);
    ~GrabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    struct Item
    {
        enum class Kind { active, failed, doneJob, file };

        Kind kind = Kind::file;
        int jobId = 0;
        juce::File file;
        juce::String title, sub, warning;
        float progress = -1.0f;

        bool draggable() const noexcept { return kind == Kind::file || kind == Kind::doneJob; }
    };

    const Item* getItem (int row) const;
    void rowClicked (int row, bool onRightZone);
    void rowDoubleClicked (int row);
    void showRowMenu (int row, juce::Component* target);
    juce::File dragFileForRow (int row) const;

    const BrandFonts& getFonts() const { return *fonts; }
    grab::Engine& getEngine()          { return engine; }

private:
    class Row;
    class ToolsBanner;

    void timerCallback() override;
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;

    void startFromUI();
    void showSettings();
    void chooseFolder();
    void rescanFolder();
    void rebuildItems();
    void updateBanner();
    void toast (const juce::String&, bool isError = false);
    juce::String fileInfo (const juce::File&);

    GrabProcessor& processorRef;
    grab::Engine& engine;
    BrandLook look;
    juce::SharedResourcePointer<BrandFonts> fonts;

    juce::TextEditor input;
    juce::TextButton goButton, folderButton, settingsButton;
    std::unique_ptr<ToolsBanner> banner;
    juce::ListBox list;

    std::vector<Item> items;
    juce::Array<juce::File> folderFiles;
    std::map<juce::String, juce::String> infoCache;
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::uint32 lastRevision = 0, lastScan = 0, toastUntil = 0;
    juce::String lastSignature, toastText;
    bool toastError = false, toolsWereBusy = false;
    juce::File lastFolder;
    juce::Rectangle<int> headerArea, hintArea, footerInfoArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrabEditor)
};
