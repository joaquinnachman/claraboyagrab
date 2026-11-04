#include "Tools.h"

namespace grab
{
juce::File Tools::binDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Claraboya Grab")
               .getChildFile ("bin");
}

juce::String Tools::exeName (const juce::String& base)
{
   #if JUCE_WINDOWS
    return base + ".exe";
   #else
    return base;
   #endif
}

juce::File Tools::find (const juce::String& base)
{
    auto local = binDir().getChildFile (exeName (base));

    if (local.existsAsFile())
        return local;

   #if ! JUCE_WINDOWS
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    for (auto dir : { juce::File ("/opt/homebrew/bin"), juce::File ("/usr/local/bin"), juce::File ("/usr/bin"),
                      home.getChildFile (".deno/bin"), home.getChildFile (".local/bin") })
    {
        auto f = dir.getChildFile (base);
        if (f.existsAsFile())
            return f;
    }
   #endif

    return local;
}

static bool simulateMissingTools()
{
    // Solo para capturas/pruebas: CLARABOYA_GRAB_TEST_NO_TOOLS=1
    return juce::SystemStats::getEnvironmentVariable ("CLARABOYA_GRAB_TEST_NO_TOOLS", {}).isNotEmpty();
}

bool Tools::canAutoInstall()
{
   #if JUCE_WINDOWS
    return true;
   #else
    return simulateMissingTools();
   #endif
}

bool Tools::allInstalled()
{
    if (simulateMissingTools())
        return false;

   #if JUCE_WINDOWS
    return ytdlp().existsAsFile() && ffmpeg().existsAsFile() && deno().existsAsFile();
   #else
    return ytdlp().existsAsFile() && ffmpeg().existsAsFile();
   #endif
}
}
