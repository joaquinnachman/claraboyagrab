#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "GrabEngine.h"
#include "Search.h"
#include "ThumbCache.h"
#include "Preview.h"
#include <limits>

/** No procesa audio: lo deja pasar tal cual. Es solo la "casa" del bajador dentro de Cubase. */
class GrabProcessor final : public juce::AudioProcessor
{
public:
    GrabProcessor();
    ~GrabProcessor() override = default;

    void prepareToPlay (double sampleRate, int) override
    {
        engine.setSampleRate (sampleRate);
        preview.setPlaybackRate (sampleRate);
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "Claraboya Grab"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    // "Cola infinita": así Cubase no duerme el plugin en un canal en silencio y el preview suena.
    double getTailLengthSeconds() const override           { return std::numeric_limits<double>::infinity(); }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    grab::Engine engine;
    grab::Searcher searcher;
    ThumbCache thumbs;
    grab::PreviewPlayer preview { engine };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrabProcessor)
};
