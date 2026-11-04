#pragma once

#include "Common.h"

/*  Herramientas externas que usa el plugin. En Windows se bajan solas la primera vez
    a %APPDATA%\Claraboya Grab\bin (yt-dlp, Deno para resolver YouTube, FFmpeg para
    pasar a WAV). En Mac/Linux se buscan en el sistema.
*/
namespace grab
{
    struct Tools
    {
        static juce::File binDir();

        static juce::File ytdlp()   { return find ("yt-dlp"); }
        static juce::File ffmpeg()  { return find ("ffmpeg"); }
        static juce::File ffprobe() { return find ("ffprobe"); }
        static juce::File deno()    { return find ("deno"); }

        static bool allInstalled();

        /** En Windows el plugin baja las herramientas solo. */
        static bool canAutoInstall();

        // Descargas oficiales (Windows x64)
        static constexpr const char* ytdlpUrl  = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
        static constexpr const char* denoUrl   = "https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip";
        static constexpr const char* ffmpegUrl = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";

        static juce::String exeName (const juce::String& base);

    private:
        static juce::File find (const juce::String& base);
    };
}
