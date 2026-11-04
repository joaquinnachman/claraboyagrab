#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace grab
{
    /** Textos con tildes/ñ: siempre pasan por acá (JUCE trata los literales como ASCII). */
    inline juce::String TR (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    inline juce::String formatDuration (double secs)
    {
        if (secs <= 0.0)
            return {};

        auto total = (int) std::lround (secs);
        auto h = total / 3600, m = (total / 60) % 60, s = total % 60;
        auto two = [] (int v) { return juce::String (v).paddedLeft ('0', 2); };

        return h > 0 ? juce::String (h) + ":" + two (m) + ":" + two (s)
                     : juce::String (m) + ":" + two (s);
    }

    inline juce::String formatRate (double sampleRate)
    {
        auto k = sampleRate / 1000.0;
        return (std::abs (k - std::round (k)) < 0.01 ? juce::String ((int) std::round (k))
                                                     : juce::String (k, 1)) + " kHz";
    }
}
