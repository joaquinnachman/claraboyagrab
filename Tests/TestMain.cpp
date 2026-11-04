// Pruebas del motor (sin interfaz). Solo para desarrollo: cmake -DCLARABOYA_BUILD_TESTS=ON
//   ClaraboyaGrabTests          pruebas sin internet
//   ClaraboyaGrabTests --live   además prueba contra Spotify y YouTube Music de verdad
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

#include "Resolver.h"
#include "Search.h"
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

static juce::String httpGet (const juce::String& url)
{
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (8).withStatusCode (&status)
                    .withExtraHeaders ("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                       "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36\r\nAccept-Language: es-AR,es;q=0.9\r\n");
    if (auto in = juce::URL::createWithoutParsing (url).createInputStream (opts))
        return in->readEntireStreamAsString();
    return {};
}

static void printCandidates (const std::vector<Candidate>& items, int max = 6)
{
    for (int i = 0; i < juce::jmin (max, (int) items.size()); ++i)
    {
        const auto& c = items[(size_t) i];
        std::cout << "      " << (i + 1) << ". " << c.title << "  |  " << c.artist << "  |  " << c.extra
                  << "  |  " << formatDuration (c.duration) << (c.durationMatch ? "  [MISMA DURACION]" : "")
                  << "  |  " << c.videoId << "\n         tapa: " << c.thumbUrl.substring (0, 90) << "\n";
    }
}

//==============================================================================
static const char* ytmFixture = R"JSON({"contents":{"tabbedSearchResultsRenderer":{"tabs":[{"tabRenderer":{"content":{"sectionListRenderer":{"contents":[{"musicShelfRenderer":{"contents":[
 {"musicResponsiveListItemRenderer":{
   "thumbnail":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"https://lh3.googleusercontent.com/a=w60-h60-l90-rj","width":60,"height":60},{"url":"https://lh3.googleusercontent.com/a=w120-h120-l90-rj","width":120,"height":120}]}}},
   "flexColumns":[
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Like a Stone (Live)","navigationEndpoint":{"watchEndpoint":{"videoId":"LIVEvid0001"}}}]}}},
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[
        {"text":"Audioslave","navigationEndpoint":{"browseEndpoint":{"browseId":"UC1","browseEndpointContextSupportedConfigs":{"browseEndpointContextMusicConfig":{"pageType":"MUSIC_PAGE_TYPE_ARTIST"}}}}},
        {"text":" • "},
        {"text":"Live in Cuba","navigationEndpoint":{"browseEndpoint":{"browseId":"MPRE1","browseEndpointContextSupportedConfigs":{"browseEndpointContextMusicConfig":{"pageType":"MUSIC_PAGE_TYPE_ALBUM"}}}}},
        {"text":" • "},{"text":"5:31"}]}}}],
   "playlistItemData":{"videoId":"LIVEvid0001"}}},
 {"musicResponsiveListItemRenderer":{
   "thumbnail":{"musicThumbnailRenderer":{"thumbnail":{"thumbnails":[{"url":"//lh3.googleusercontent.com/b=w60-h60","width":60,"height":60}]}}},
   "overlay":{"musicItemThumbnailOverlayRenderer":{"content":{"musicPlayButtonRenderer":{"playNavigationEndpoint":{"watchEndpoint":{"videoId":"STUDIOvid01"}}}}}},
   "flexColumns":[
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Like a Stone"}]}}},
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[
        {"text":"Audioslave","navigationEndpoint":{"browseEndpoint":{"browseEndpointContextSupportedConfigs":{"browseEndpointContextMusicConfig":{"pageType":"MUSIC_PAGE_TYPE_ARTIST"}}}}},
        {"text":" • "},
        {"text":"Audioslave","navigationEndpoint":{"browseEndpoint":{"browseEndpointContextSupportedConfigs":{"browseEndpointContextMusicConfig":{"pageType":"MUSIC_PAGE_TYPE_ALBUM"}}}}},
        {"text":" • "},{"text":"4:54"}]}}}]}},
 {"musicResponsiveListItemRenderer":{
   "flexColumns":[
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Like a Stone"}]}}},
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Canción"},{"text":" • "},{"text":"Banda Tributo"},{"text":" • "},{"text":"Covers 2020"},{"text":" • "},{"text":"4:54"}]}}}],
   "playlistItemData":{"videoId":"COVERvid001"}}},
 {"musicResponsiveListItemRenderer":{
   "flexColumns":[{"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Audioslave"}]}}}]}},
 {"musicResponsiveListItemRenderer":{
   "flexColumns":[
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Repetido"}]}}}],
   "playlistItemData":{"videoId":"STUDIOvid01"}}}
]}}]}}}}]}}})JSON";

//==============================================================================
static void offlineTests()
{
    std::cout << "\n== Lectura de páginas de streaming\n";
    {
        std::string spot = u8R"(<html><head><title>Canción Rara - song and lyrics by Los Ñandúes | Spotify</title>
            <meta property="og:title" content="Canción Rara"/>
            <meta name="music:musician_description" content="Los Ñandúes, Otro Artista"/>
            <meta name="music:duration" content="236"/></head><body></body></html>)";
        auto q = parseStreamingPage ("Spotify", spot);
        eq (q.title, TR ("Canción Rara"), "Spotify (página) título");
        eq (q.artist, TR ("Los Ñandúes"), "Spotify (página) artista");
    }
    {
        std::string embed = u8R"(<html><body><script id="__NEXT_DATA__" type="application/json">{"props":{"pageProps":{"state":{"data":{"entity":{"type":"track","name":"Like a Stone","title":"Like a Stone","artists":[{"name":"Audioslave","uri":"spotify:artist:x"}],"duration":293960}}}}}}</script></body></html>)";
        auto q = parseSpotifyEmbed (embed);
        eq (q.title, "Like a Stone", "Spotify embed título");
        eq (q.artist, "Audioslave", "Spotify embed artista");
        check (std::abs (q.durationSec - 293.96) < 0.01, "Spotify embed duración 293.96 s", juce::String (q.durationSec));
    }
    {
        std::string embed = u8R"(<script id="__NEXT_DATA__" type="application/json">{"props":{"otro":{"track":{"title":"Rezo por vos","artists":[{"name":"Charly García"}],"duration":{"totalMilliseconds":234000}}}}}</script>)";
        auto q = parseSpotifyEmbed (embed);
        eq (q.title, "Rezo por vos", "Spotify embed con otra estructura: título");
        eq (q.artist, TR ("Charly García"), "Spotify embed con otra estructura: artista");
        check (q.durationSec == 234.0, "Spotify embed con otra estructura: duración", juce::String (q.durationSec));
    }
    eq (spotifyTrackId ("https://open.spotify.com/intl-es/track/3YuaBvuZqcwN3CEAyyoaei?si=abc"), "3YuaBvuZqcwN3CEAyyoaei", "id de tema de Spotify");
    eq (spotifyTrackId ("https://open.spotify.com/album/123"), "", "disco de Spotify no da id de tema");
    eq (decodeEntities ("A &amp; B &#39;x&#39; &#x00F1;"), TR ("A & B 'x' ñ"), "entidades HTML");

    std::cout << "\n== Clasificación de links\n";
    {
        juce::String p;
        check (classify ("https://open.spotify.com/intl-es/track/4abc?si=12", p) == LinkKind::streaming && p == "Spotify", "Spotify = opciones", p);
        check (classify ("https://music.apple.com/ar/album/x/123?i=456", p) == LinkKind::streaming, "Apple Music = opciones", p);
        check (classify ("youtu.be/abcDEF", p) == LinkKind::direct && p == "YouTube", "youtu.be = directo", p);
        check (classify ("https://music.youtube.com/watch?v=abc", p) == LinkKind::direct, "YouTube Music = directo", p);
        check (classify (TR ("los ñandúes canción rara"), p) == LinkKind::textSearch, "texto = búsqueda", p);
        eq (appleTrackId ("https://music.apple.com/ar/album/x/123?i=456&l=es"), "456", "id de tema de Apple");
        eq (deezerTrackId ("https://www.deezer.com/es/track/9988?utm=1"), "9988", "id de Deezer");
    }

    std::cout << "\n== Buscador: lectura de resultados de YouTube Music\n";
    {
        auto items = parseYTMusicResponse (juce::JSON::parse (juce::String::fromUTF8 (ytmFixture)), SearchKind::songs);
        check (items.size() == 3, "3 resultados (saltea el artista y el repetido)", juce::String ((int) items.size()));

        if (items.size() == 3)
        {
            eq (items[0].title, "Like a Stone (Live)", "1: título");
            eq (items[0].artist, "Audioslave", "1: artista");
            eq (items[0].extra, "Live in Cuba", "1: disco");
            check (items[0].duration == 331.0, "1: duración 5:31", juce::String (items[0].duration));
            eq (items[0].thumbUrl, "https://lh3.googleusercontent.com/a=w120-h120-l90-rj", "1: tapa de 120 px (nítida en pantallas 2x)");
            eq (items[0].url(), "https://music.youtube.com/watch?v=LIVEvid0001", "1: link para bajar");
            eq (items[1].videoId, "STUDIOvid01", "2: id desde el botón de play");
            eq (items[1].thumbUrl, "https://lh3.googleusercontent.com/b=w60-h60", "2: tapa con // arreglada");
            eq (items[2].artist, "Banda Tributo", "3: artista sin enlaces (saca «Canción»)");
            eq (items[2].extra, "Covers 2020", "3: disco sin enlaces");

            TrackQuery q;
            q.title = "Like a Stone";
            q.artist = "Audioslave";
            q.durationSec = 293.96;
            rankCandidates (items, q);
            eq (items[0].videoId, "STUDIOvid01", "orden: primero la versión de estudio (misma duración)");
            check (items[0].durationMatch, "marca MISMA DURACIÓN en la de estudio");
            check (! items[1].durationMatch && ! items[2].durationMatch, "no marca el vivo ni el cover (aunque el cover dure igual)");
            eq (items[1].videoId, "COVERvid001", "orden: después el cover que dura igual");
            eq (items[2].videoId, "LIVEvid0001", "orden: el vivo queda último");
        }

        TrackQuery none;
        auto plain = parseYTMusicResponse (juce::JSON::parse (juce::String::fromUTF8 (ytmFixture)), SearchKind::videos);
        rankCandidates (plain, none);
        eq (plain.empty() ? juce::String() : plain[0].videoId, "LIVEvid0001", "búsqueda de texto: respeta el orden de YouTube");
        eq (plain.empty() ? juce::String() : plain[0].url(), "https://www.youtube.com/watch?v=LIVEvid0001", "videos: link de YouTube");
    }

    std::cout << "\n== Buscador: respaldo con yt-dlp\n";
    {
        juce::StringArray lines {
            R"J({"id": "abcdefghijk", "title": "Audioslave - Like a Stone (Official Video)", "duration": 299.0, "channel": "AudioslaveVEVO", "view_count": 1234567890})J",
            "WARNING: algo",
            R"J({"id": "zzzzzzzzzzz", "title": "Like a Stone (cover)", "duration": null, "uploader": "Alguien", "view_count": 5400})J"
        };
        auto items = parseYtdlpSearchLines (lines);
        check (items.size() == 2, "2 resultados de yt-dlp", juce::String ((int) items.size()));
        if (items.size() == 2)
        {
            eq (items[0].artist, "AudioslaveVEVO", "canal como artista");
            eq (items[0].extra, "1,2 MM de vistas", "vistas");
            eq (items[0].thumbUrl, "https://i.ytimg.com/vi/abcdefghijk/mqdefault.jpg", "tapa del video");
            eq (items[1].artist, "Alguien", "uploader si no hay canal");
            eq (items[1].extra, "5 k vistas", "pocas vistas");
        }
    }

    std::cout << "\n== Utilidades\n";
    check (parseDurationText ("4:54") == 294.0, "4:54 = 294 s");
    check (parseDurationText ("1:02:03") == 3723.0, "1:02:03 = 3723 s");
    check (parseDurationText ("4 M de vistas") == 0.0 && parseDurationText ("2024") == 0.0 && parseDurationText ("4:5") == 0.0, "no confunde otras cosas con duraciones");
    eq (normaliseForMatch (TR ("Canción Rara - Remastered 2011 (feat. Ñandú)")), "cancion rara 2011 nandu", "normalizar para comparar");
    eq (makeNiceName (TR ("Canción Rara (Official Video)"), TR ("Los Ñandúes - Topic")), TR ("Los Ñandúes - Canción Rara"), "nombre de archivo prolijo");
}

//==============================================================================
static void pipelineTest()
{
    std::cout << "\n== Descarga completa (archivo local, sin internet)\n";

    if (! Tools::allInstalled())
    {
        std::cout << "(salteada: faltan yt-dlp/ffmpeg)\n";
        return;
    }

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
    e.clearList();
    e.enqueueDownload ("file://" + juce::URL::addEscapeChars (tone.getFullPathName(), false, true).replace ("%2F", "/"),
                       TR ("Tema de Prueba"), TR ("Los Ñandúes"));
    check (e.waitUntilIdle (90000), "terminó a tiempo");

    check (e.getJobs().empty(), "la descarga terminada sale de la cola");
    auto hist = e.getHistory();
    check (! hist.empty(), "y aparece en la lista");

    if (! hist.empty())
    {
        const auto& h = hist.front();
        std::cout << "      título: " << h.title << "\n      detalle: " << h.detail << "\n";
        eq (h.file.getFileName(), TR ("Los Ñandúes - Tema de Prueba.wav"), "nombre del tema elegido");

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (h.file)); r != nullptr)
        {
            check (r->sampleRate == 48000.0, "WAV a 48 kHz", juce::String (r->sampleRate));
            check (r->bitsPerSample == 24, "WAV 24 bit", juce::String (r->bitsPerSample));
            check (std::abs ((double) r->lengthInSamples / r->sampleRate - 3.0) < 0.15, "dura ~3 s");
        }
        else check (false, "se puede leer el WAV");

        e.clearList();
        check (e.getHistory().empty(), "LIMPIAR LISTA vacía la lista");
        check (h.file.existsAsFile(), "pero el WAV sigue en la carpeta");
    }

    check (! out.getChildFile (".claraboya-tmp").exists(), "limpió los temporales");

    e.enqueueDownload ("file:///no/existe/nada.mp3");
    e.waitUntilIdle (60000);
    auto jobs = e.getJobs();
    check (jobs.size() == 1 && jobs.front().state == Job::State::failed, "un link roto falla prolijo",
           jobs.empty() ? juce::String() : jobs.front().detail);

    e.enqueueDownload ("https://open.spotify.com/track/3YuaBvuZqcwN3CEAyyoaei");
    e.waitUntilIdle (10000);
    jobs = e.getJobs();
    check (jobs.size() == 2 && jobs.back().state == Job::State::failed, "Spotify nunca se baja a ciegas",
           jobs.empty() ? juce::String() : jobs.back().detail);

    e.clearList();
    tmp.deleteRecursively();
}

//==============================================================================
static void liveTests()
{
    std::cout << "\n========== EN VIVO (internet real) ==========\n";

    std::cout << "\n== Spotify: qué tema es cada link\n";
    TrackQuery likeAStone;

    for (auto id : { "3YuaBvuZqcwN3CEAyyoaei", "4cOdK2wGLETKBW3PvgPWqT" })
    {
        const juce::String url = "https://open.spotify.com/intl-es/track/" + juce::String (id) + "?si=prueba";
        const auto q = resolveStreaming (url, "Spotify");
        std::cout << "      " << id << " -> «" << q.title << "» de «" << q.artist << "», "
                  << formatDuration (q.durationSec) << (q.error.isNotEmpty() ? "  ERROR: " + q.error : juce::String()) << "\n";
        check (q.title.isNotEmpty() && q.artist.isNotEmpty() && q.durationSec > 0, juce::String ("Spotify ") + id + ": título, artista y duración");

        if (q.title.containsIgnoreCase ("stone"))
            likeAStone = q;

        // Diagnóstico: qué leía el método anterior (etiquetas de la página normal)
        const auto old = parseStreamingPage ("Spotify", httpGet (url).toStdString());
        std::cout << "      (método anterior, página normal: «" << old.title << "» / «" << old.artist << "»)\n";
    }

    if (likeAStone.title.isEmpty())
    {
        likeAStone.title = "Like a Stone";
        likeAStone.artist = "Audioslave";
        likeAStone.durationSec = 294.0;
        std::cout << "      (ninguno de los ids era Like a Stone: uso datos fijos para seguir)\n";
    }

    std::cout << "\n== YouTube Music: canciones\n";
    juce::String raw, err;
    auto songs = searchYTMusic (likeAStone.searchText(), SearchKind::songs, err, &raw);
    if (! err.isEmpty()) std::cout << "      error: " << err << "\n";
    check (songs.size() >= 3, "trae varias opciones", juce::String ((int) songs.size()));
    if (songs.empty())
    {
        const auto idx = raw.indexOf ("musicResponsiveListItemRenderer");
        std::cout << "      respuesta cruda (" << raw.length() << " caracteres): "
                  << raw.substring (juce::jmax (0, idx - 200), juce::jmax (0, idx) + 2500) << "\n";
    }
    else
    {
        const auto idx = raw.indexOf ("musicResponsiveListItemRenderer");
        std::cout << "      (muestra de la respuesta: " << raw.substring (idx, idx + 1800) << ")\n";
        check (songs[0].artist.isNotEmpty() && songs[0].duration > 0 && songs[0].thumbUrl.isNotEmpty(),
               "el primero trae artista, duración y tapa");
    }
    rankCandidates (songs, likeAStone);
    printCandidates (songs);
    check (! songs.empty() && songs[0].durationMatch && songs[0].artist.containsIgnoreCase ("audioslave"),
           "el primero es Audioslave con la misma duración");

    std::cout << "\n== YouTube Music: videos\n";
    auto videos = searchYTMusic (likeAStone.searchText(), SearchKind::videos, err);
    check (videos.size() >= 3, "trae varios videos", juce::String ((int) videos.size()));
    printCandidates (videos, 4);

    std::cout << "\n== YouTube Music: búsqueda en castellano\n";
    auto es = searchYTMusic (TR ("charly garcía rezo por vos"), SearchKind::songs, err);
    check (! es.empty(), "encuentra algo", err);
    printCandidates (es, 4);

    std::cout << "\n== Buscador completo con link de Spotify (lo que hace la app)\n";
    {
        Searcher s;
        Searcher::Request r;
        r.text = "https://open.spotify.com/intl-es/track/3YuaBvuZqcwN3CEAyyoaei?si=x";
        r.resolveLink = true;
        r.platform = "Spotify";
        s.request (r);

        Searcher::Result res;
        for (int i = 0; i < 300; ++i)
        {
            res = s.getResult();
            if (res.state == Searcher::Result::State::done || res.state == Searcher::Result::State::failed)
                break;
            juce::Thread::sleep (100);
        }

        std::cout << "      origen: " << res.source.displayName() << " (" << formatDuration (res.source.durationSec)
                  << ")  búsqueda: «" << res.query << "»" << (res.usedFallback ? "  [respaldo yt-dlp]" : "") << "\n";
        check (res.state == Searcher::Result::State::done && ! res.items.empty(), "terminó con opciones", res.error);
        printCandidates (res.items, 4);
    }

    std::cout << "\n== Tapas\n";
    if (! songs.empty())
    {
        auto data = juce::MemoryBlock();
        int status = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (8000).withStatusCode (&status);
        if (auto in = juce::URL::createWithoutParsing (songs[0].thumbUrl).createInputStream (opts))
            in->readIntoMemoryBlock (data);
        auto img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
        std::cout << "      " << (int) data.getSize() << " bytes, imagen " << img.getWidth() << "x" << img.getHeight() << "\n";
        check (img.isValid(), "la tapa se puede dibujar (JPEG/PNG)");
    }

    if (! videos.empty())
    {
        auto data = juce::MemoryBlock();
        if (auto in = juce::URL::createWithoutParsing (videos[0].thumbUrl).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (8000)))
            in->readIntoMemoryBlock (data);
        auto img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
        std::cout << "      video: " << videos[0].thumbUrl << " -> " << img.getWidth() << "x" << img.getHeight() << "\n";
        check (img.isValid(), "la miniatura del video se puede dibujar");
    }

    std::cout << "\n== Respaldo: búsqueda con yt-dlp\n";
    if (Tools::ytdlp().existsAsFile())
    {
        juce::String e2;
        auto alt = searchWithYtdlp (likeAStone.searchText(), SearchKind::songs, e2, nullptr);
        check (! alt.empty(), "yt-dlp también encuentra opciones", e2);
        printCandidates (alt, 3);
    }
    else
    {
        std::cout << "      (sin yt-dlp)\n";
    }

    std::cout << "\n== Descarga real del elegido (informativo: YouTube suele bloquear a los servidores de GitHub)\n";
    if (! songs.empty() && Tools::allInstalled())
    {
        Engine e;
        auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("grablive");
        e.setOutputFolder (out);
        e.setSampleRate (44100.0);
        e.enqueueDownload (songs[0].url(), songs[0].title, songs[0].artist);
        e.waitUntilIdle (240000);
        auto h = e.getHistory();
        auto jobs = e.getJobs();
        if (! h.empty() && h.front().file.existsAsFile())
            std::cout << "      bajó: " << h.front().file.getFileName() << "  " << h.front().detail << "\n";
        else
            std::cout << "      no bajó: " << (jobs.empty() ? juce::String ("?") : jobs.front().detail) << "\n";
        e.clearList();
        out.deleteRecursively();
    }
}

int main (int argc, char** argv)
{
    juce::MessageManager::getInstance();

    const bool live = argc > 1 && juce::String (argv[1]) == "--live";

    offlineTests();
    pipelineTest();

    const int offlineFailures = failures;

    if (live)
        liveTests();

    std::cout << "\n" << (failures == 0 ? juce::String ("TODO OK")
                                         : juce::String (failures) + " FALLAS (" + juce::String (offlineFailures) + " sin internet)") << "\n";
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return failures == 0 ? 0 : 1;
}
