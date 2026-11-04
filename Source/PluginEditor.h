#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

#include "PluginProcessor.h"
#include "BrandLook.h"

class GrabEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer,
                         private juce::ListBoxModel,
                         private juce::KeyListener
{
public:
    explicit GrabEditor (GrabProcessor&);
    ~GrabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // Lista de descargas (en curso, con error, y el historial)
    struct Item
    {
        enum class Kind { active, failed, file };

        Kind kind = Kind::file;
        int jobId = 0;
        juce::File file;
        juce::String title, sub;
        float progress = -1.0f;

        bool draggable() const noexcept { return kind == Kind::file; }
    };

    const Item* getItem (int row) const;
    void rowClicked (int row, bool onRightZone);
    void rowDoubleClicked (int row);
    void showRowMenu (int row, juce::Component* target);
    juce::File dragFileForRow (int row) const;

    //==============================================================================
    // Resultados del buscador
    const grab::Searcher::Result& getSearchResult() const { return searchResult; }
    int getHighlighted() const noexcept                   { return highlighted; }
    void setHighlighted (int);
    void pick (int index);
    juce::Image getThumb (const juce::String& url)        { return processorRef.thumbs.get (url); }

    const BrandFonts& getFonts() const { return *fonts; }
    grab::Engine& getEngine()          { return engine; }

    /** Para capturas y pruebas. */
    void setInputTextForTesting (const juce::String& t) { setInputSilently (t); }
    void showResultsForTesting (const grab::Searcher::Result&, int queueIndex, int queueTotal, int highlight);

private:
    class Row;
    class ToolsBanner;
    class ResultsPanel;

    void timerCallback() override;
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    void startFromUI();
    void handleTyping();
    void requestSearch (const juce::String& text, bool resolveLink, const grab::TrackQuery& expected = {});
    void setSearchKind (grab::SearchKind);
    void openNextPending();
    void advanceAfterPick();
    void closeResults();
    void showResults (bool);
    bool resultsVisible() const;
    void setInputSilently (const juce::String&);
    void updateGoButton();

    void showSettings();
    void chooseFolder();
    void rebuildItems();
    void updateBanner();
    void updateFolderLabel();
    void toast (const juce::String&, bool isError = false);
    juce::String fileInfo (const juce::File&);

    GrabProcessor& processorRef;
    grab::Engine& engine;
    grab::Searcher& searcher;
    BrandLook look;
    juce::SharedResourcePointer<BrandFonts> fonts;
    juce::Image logo;

    juce::TextEditor input;
    juce::TextButton goButton, folderButton, settingsButton, clearButton;
    std::unique_ptr<ToolsBanner> banner;
    std::unique_ptr<ResultsPanel> results;
    juce::ListBox list;

    std::vector<Item> items;
    std::map<juce::String, juce::String> infoCache;
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> chooser;

    // Buscador
    grab::Searcher::Result searchResult;
    grab::SearchKind searchKind = grab::SearchKind::songs;
    juce::StringArray pending;
    int pendingTotal = 0, pendingIndex = 0;
    int highlighted = -1;
    juce::uint32 searchDue = 0, lastSearchRevision = 0, lastThumbRevision = 0;
    juce::String lastLinkRequested;
    bool suppressTyping = false;

    juce::uint32 lastRevision = 0, lastRebuild = 0, toastUntil = 0;
    juce::String lastSignature, toastText;
    bool toastError = false, toolsWereBusy = false;
    juce::File lastFolder;
    juce::Rectangle<int> headerArea, hintArea, listHeaderArea, footerInfoArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrabEditor)
};
