// Pruebas del motor (sin interfaz). Solo para desarrollo: cmake -DCLARABOYA_BUILD_TESTS=ON
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

#include "Resolver.h"
#include "GrabEngine.h"
#include "Tools.h"

using namespace grab;

static int failures = 0;

static void check (bool ok, const juce::String& what, const juce::String& got = {})
{
    std::cout << (ok ? "ok    " : "FALLA ") << what;
    if (! ok && got.isNotEmpty()) std::cout << "   -> [" << got << "]";
    std::cout << "\n";
    if (! ok) ++failures;
}

static void eq (const juce::String& got, const juce::String& expected, const juce::String& what)
{
    check (got == expected, what + " == \"" + expected + "\"", got);
}

static int run (const juce::StringArray& args)
{
    juce::ChildProcess p;
    if (! p.start (args)) return -1;
    p.readAllProcessOutput();
    return (int) p.getExitCode();
}

int main()
{
    juce::MessageManager::getInstance();

    //==========================================================================
    std::cout << "\n== Lectura de páginas de streaming\n";
    {
        std::string spot = u8R"(<html><head><title>Canción Rara - song and lyrics by Los Ñandúes | Spotify</title>
            <meta property="og:title" content="Canción Rara"/>
            <meta property="og:description" content="Los Ñandúes · Disco Prueba · Song · 2024"/>
            <meta name="music:musician_description" content="Los Ñandúes, Otro Artista"/>
            <meta name="music:duration" content="236"/></head><body></body></html>)";
        auto q = parseStreamingPage ("Spotify", spot);
        eq (q.title, TR ("Canción Rara"), "Spotify título");
        eq (q.artist, TR ("Los Ñandúes"), "Spotify artista (primero de la lista)");
        check (q.durationSec == 236.0, "Spotify duración 236", juce::String (q.durationSec));
        eq (q.searchText(), TR ("Los Ñandúes Canción Rara"), "texto de búsqueda");
    }
    {
        std::string spot = u8R"(<title>Canción Rara - song and lyrics by Los Ñandúes | Spotify</title>
            <meta content="Canción Rara" property="og:title">)";
        auto q = parseStreamingPage ("Spotify", spot);
        eq (q.title, TR ("Canción Rara"), "Spotify título (atributos al revés)");
        eq (q.artist, TR ("Los Ñandúes"), "Spotify artista desde <title>");
    }
    {
        std::string apple = u8R"(<meta property="og:title" content="&#x200E;Canción Rara by Los Ñandúes on Apple Music">)";
        auto q = parseStreamingPage ("Apple Music", apple);
        eq (q.title, TR ("Canción Rara"), "Apple título");
        eq (q.artist, TR ("Los Ñandúes"), "Apple artista");
    }
    {
        std::string tidal = u8R"(<meta property='og:title' content='Canción Rara by Los Ñandúes on TIDAL' />)";
        auto q = parseStreamingPage ("TIDAL", tidal);
        eq (q.title, TR ("Canción Rara"), "TIDAL título");
        eq (q.artist, TR ("Los Ñandúes"), "TIDAL artista");
    }
    {
        std::string deezer = u8R"(<meta property="og:title" content="Canción Rara - Los Ñandúes | Deezer">)";
        auto q = parseStreamingPage ("Deezer", deezer);
        eq (q.title, TR ("Canción Rara - Los Ñandúes"), "Deezer título (busca con todo)");
    }
    eq (decodeEntities ("A &amp; B &#39;x&#39; &quot;y&quot; &#x00F1; &nbsp;z &foo"), TR ("A & B 'x' \"y\" ñ  z &foo"), "entidades HTML");

    //==========================================================================
    std::cout << "\n== Clasificación de links\n";
    {
        juce::String p;
        check (classify ("https://open.spotify.com/intl-es/track/4abc?si=12", p) == LinkKind::streaming && p == "Spotify", "Spotify = streaming", p);
        check (classify ("https://spotify.link/xyz", p) == LinkKind::streaming, "link corto de Spotify", p);
        check (classify ("https://music.apple.com/ar/album/x/123?i=456", p) == LinkKind::streaming && p == "Apple Music", "Apple Music", p);
        check (classify ("https://www.deezer.com/es/track/9988", p) == LinkKind::streaming && p == "Deezer", "Deezer", p);
        check (classify ("https://tidal.com/browse/track/1234", p) == LinkKind::streaming && p == "TIDAL", "TIDAL", p);
        check (classify ("https://music.amazon.com/albums/B0X?trackAsin=B0Y", p) == LinkKind::streaming, "Amazon Music", p);
        check (classify ("youtu.be/abcDEF", p) == LinkKind::direct && p == "YouTube", "youtu.be sin https", p);
        check (classify ("https://music.youtube.com/watch?v=abc", p) == LinkKind::direct && p == "YouTube Music", "YouTube Music directo", p);
        check (classify (TR ("mirá este https://soundcloud.com/a/b"), p) == LinkKind::direct && p == "SoundCloud", "link dentro de un texto", p);
        check (classify ("https://artista.bandcamp.com/track/tema", p) == LinkKind::direct && p == "Bandcamp", "Bandcamp", p);
        check (classify (TR ("los ñandúes canción rara"), p) == LinkKind::textSearch, "texto = búsqueda", p);
        check (classify ("rezo.mp3", p) == LinkKind::textSearch, "palabra suelta con punto = búsqueda", p);
        eq (normaliseUrl (TR ("mirá https://youtu.be/x.")), "https://youtu.be/x", "saca el link y el punto final");
        eq (appleTrackId ("https://music.apple.com/ar/album/x/123?i=456&l=es"), "456", "id de tema de Apple (?i=)");
        eq (appleTrackId ("https://music.apple.com/us/song/name/789"), "789", "id de tema de Apple (/song/)");
        eq (appleTrackId ("https://music.apple.com/us/album/name/123"), "", "disco de Apple no da id");
        eq (deezerTrackId ("https://www.deezer.com/es/track/9988?utm=1"), "9988", "id de Deezer");
        auto s = buildYTMusicSearchUrl (TR ("los ñandúes"));
        check (s.startsWith ("https://music.youtube.com/search?q=los%20") && s.endsWith ("#songs"), "URL de búsqueda", s);
    }

    //==========================================================================
    std::cout << "\n== Nombres de archivo\n";
    eq (makeNiceName (TR ("Canción Rara (Official Video)"), TR ("Los Ñandúes - Topic")), TR ("Los Ñandúes - Canción Rara"), "saca (Official Video) y - Topic");
    eq (makeNiceName (TR ("Los Ñandúes - Canción Rara [Audio Oficial]"), TR ("Los Ñandúes")), TR ("Los Ñandúes - Canción Rara"), "no repite el artista");
    eq (makeNiceName ("NA", "NA"), "audio", "sin datos");

    //==========================================================================
    std::cout << "\n== Descarga completa (archivo local, sin internet)\n";
    if (! Tools::allInstalled())
    {
        std::cout << "(salteada: faltan yt-dlp/ffmpeg)\n";
    }
    else
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("grabtest_" + juce::String (juce::Random::getSystemRandom().nextInt (999999)));
        tmp.createDirectory();
        auto tone = tmp.getChildFile (TR ("tono de prueba ñ.mp3"));

        run ({ Tools::ffmpeg().getFullPathName(), "-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=3",
               "-ar", "44100", "-ac", "2", "-c:a", "libmp3lame", "-b:a", "128k", tone.getFullPathName() });
        check (tone.existsAsFile(), "se generó el mp3 de prueba");

        Engine e;
        auto out = tmp.getChildFile ("salida");
        e.setOutputFolder (out);
        e.setSampleRate (48000.0);
        e.setExtraYtdlpArgs ({ "--enable-file-urls" });
        e.enqueueDownload ("file://" + juce::URL::addEscapeChars (tone.getFullPathName(), false, true).replace ("%2F", "/"));
        check (e.waitUntilIdle (90000), "terminó a tiempo");

        auto jobs = e.getJobs();
        check (jobs.size() == 1, "un trabajo en la lista");

        if (! jobs.empty())
        {
            const auto& j = jobs.front();
            std::cout << "      título: " << j.title << "\n      detalle: " << j.detail << "\n";
            check (j.state == Job::State::done, "quedó listo", j.detail);
            check (j.result.existsAsFile(), "existe el WAV", j.result.getFullPathName());

            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (j.result)); r != nullptr)
            {
                check (r->sampleRate == 48000.0, "WAV a 48 kHz", juce::String (r->sampleRate));
                check (r->bitsPerSample == 24, "WAV 24 bit", juce::String (r->bitsPerSample));
                const auto secs = (double) r->lengthInSamples / r->sampleRate;
                check (std::abs (secs - 3.0) < 0.15, "dura ~3 s", juce::String (secs));
            }
            else check (false, "se puede leer el WAV");
        }

        check (! out.getChildFile (".claraboya-tmp").exists(), "limpió los temporales");

        // Link que no existe: tiene que fallar con un mensaje, no colgarse.
        e.clearFinished();
        e.enqueueDownload ("file:///no/existe/nada.mp3");
        e.waitUntilIdle (60000);
        jobs = e.getJobs();
        check (jobs.size() == 1 && jobs.front().state == Job::State::failed, "un link roto falla prolijo",
               jobs.empty() ? juce::String() : jobs.front().detail);
        if (! jobs.empty()) std::cout << "      mensaje: " << jobs.front().detail << "\n";

        tmp.deleteRecursively();
    }

    std::cout << "\n" << (failures == 0 ? "TODO OK" : juce::String (failures) + " FALLAS") << "\n";
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return failures == 0 ? 0 : 1;
}
