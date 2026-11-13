// Pruebas del motor (sin interfaz). Solo para desarrollo: cmake -DCLARABOYA_BUILD_TESTS=ON
//   ClaraboyaGrabTests          pruebas sin internet
//   ClaraboyaGrabTests --live   además prueba contra Spotify y YouTube Music de verdad
//   ClaraboyaGrabTests --instalar   primero instala yt-dlp/FFmpeg/Deno como el botón INSTALAR (Windows)
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

#include "Resolver.h"
#include "Search.h"
#include "Preview.h"
#include "Updater.h"
#include <juce_cryptography/juce_cryptography.h>
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

// file:///... que entienden yt-dlp y FFmpeg en Linux, Mac y Windows (file:///C:/Users/...)
static juce::String fileUrl (const juce::File& f)
{
    juce::StringArray parts;
    parts.addTokens (f.getFullPathName().replaceCharacter ('\\', '/'), "/", {});

    juce::String out;
    for (int i = 0; i < parts.size(); ++i)
    {
        if (i > 0) out << "/";
        out << (i == 0 && parts[i].endsWithChar (':') ? parts[i] : juce::URL::addEscapeChars (parts[i], false, true));
    }

    return out.startsWithChar ('/') ? "file://" + out : "file:///" + out;
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
   "flexColumns":[
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[{"text":"Like A Stone (Originally Performed By Audioslave) (Karaoke Version)"}]}}},
     {"musicResponsiveListItemFlexColumnRenderer":{"text":{"runs":[
        {"text":"Hit Tunes Karaoke","navigationEndpoint":{"browseEndpoint":{"browseEndpointContextSupportedConfigs":{"browseEndpointContextMusicConfig":{"pageType":"MUSIC_PAGE_TYPE_ARTIST"}}}}},
        {"text":" • "},{"text":"Karaoke Vol. 227"},{"text":" • "},{"text":"4:54"}]}}}],
   "playlistItemData":{"videoId":"KARAOKEvid1"}}},
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
        check (items.size() == 4, "4 resultados (saltea el artista y el repetido)", juce::String ((int) items.size()));

        if (items.size() == 4)
        {
            eq (items[0].title, "Like a Stone (Live)", "1: título");
            eq (items[0].artist, "Audioslave", "1: artista");
            eq (items[0].extra, "Live in Cuba", "1: disco");
            check (items[0].duration == 331.0, "1: duración 5:31", juce::String (items[0].duration));
            eq (items[0].thumbUrl, "https://lh3.googleusercontent.com/a=w120-h120-l90-rj", "1: tapa de 120 px (nítida en pantallas 2x)");
            eq (items[0].url(), "https://www.youtube.com/watch?v=LIVEvid0001", "1: link para bajar");
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
            check (! items[1].durationMatch && ! items[2].durationMatch && ! items[3].durationMatch,
                   "no marca el vivo, el cover ni el karaoke (aunque duren igual o nombren a la banda)");
            eq (items[1].videoId, "COVERvid001", "orden: después el cover que dura igual");
            eq (items[2].videoId, "LIVEvid0001", "orden: después el vivo");
            eq (items[3].videoId, "KARAOKEvid1", "orden: el karaoke queda último");
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
    e.enqueueDownload (fileUrl (tone), TR ("Tema de Prueba"), TR ("Los Ñandúes"));
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
static float rms (const juce::AudioBuffer<float>& b)
{
    return b.getRMSLevel (0, 0, b.getNumSamples());
}

static bool waitForPreview (PreviewPlayer& pp, int timeoutMs, juce::int64* sawWaitUntil = nullptr, bool* sawSlowPath = nullptr)
{
    for (int i = 0; i < timeoutMs / 50; ++i)
    {
        const auto st = pp.getStatus();
        if (sawWaitUntil != nullptr && st.waitUntilMs > *sawWaitUntil) *sawWaitUntil = st.waitUntilMs;
        if (sawSlowPath != nullptr && st.slowPath) *sawSlowPath = true;
        if (st.state == PreviewPlayer::State::playing || st.state == PreviewPlayer::State::failed)
            return true;
        juce::Thread::sleep (50);
    }
    return false;
}

static juce::String pythonExe()
{
   #if JUCE_WINDOWS
    return "python";
   #else
    return "python3";
   #endif
}

// Servidor HTTP de prueba: sirve una carpeta y, si se le pide (/negar?n=N), rechaza con 403 los
// próximos N pedidos de FFmpeg, como hace YouTube cuando el link se usa antes de tiempo.
static const char* testServerPy = R"PY(
import functools, http.server, io, sys, urllib.parse
port, root = int(sys.argv[1]), sys.argv[2]
state = {'deny': 0}
class H(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == '/negar':
            state['deny'] = int(urllib.parse.parse_qs(u.query).get('n', ['0'])[0])
            self.send_response(200)
            self.send_header('Content-Length', '2')
            self.end_headers()
            return io.BytesIO(b'ok')
        if self.headers.get('Icy-MetaData') and state['deny'] > 0:
            state['deny'] -= 1
            self.send_error(403, 'Forbidden')
            return None
        return super().send_head()
    def log_message(self, *a):
        pass
http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(H, directory=root)).serve_forever()
)PY";

static juce::String logSince (juce::int64 sizeBefore)
{
    auto in = PreviewPlayer::logFile().createInputStream();
    if (in == nullptr) return {};
    if (sizeBefore > 0 && sizeBefore <= in->getTotalLength()) in->setPosition (sizeBefore);
    return in->readEntireStreamAsString();
}

static void previewTest()
{
    std::cout << "\n== Preview (sin internet)\n";
    check (PreviewPlayer::chooseStart (294.0) == 88.0, "tema de 4:54: arranca en 1:28 (30 %)");
    check (PreviewPlayer::chooseStart (40.0) == 0.0, "tema corto: desde el principio");
    check (PreviewPlayer::chooseStart (0.0) == 0.0, "sin duración: desde el principio");
    check (std::abs (PreviewPlayer::gainForRms (0.3f) - 0.4333f) < 0.001f, "un master fuerte se baja (~ -7 dB)");
    check (PreviewPlayer::gainForRms (0.01f) == 2.0f && PreviewPlayer::gainForRms (0.0f) == 1.0f, "nunca sube más de +6 dB ni amplifica silencio");
    eq (PreviewPlayer::formatSeconds (88.0), "88.000", "segundos para FFmpeg siempre con punto");
    eq (PreviewPlayer::formatSeconds (29.9876), "29.988", "redondeo a milisegundos");

    {
        PreviewPlayer::Stream st;
        check (PreviewPlayer::parseResolveOutput ({ "[youtube] abc: Downloading", "URL|https://rr1.googlevideo.com/x?y=1",
                                                    "UA|Mozilla/5.0 Prueba" }, st)
               && st.url == "https://rr1.googlevideo.com/x?y=1" && st.userAgent == "Mozilla/5.0 Prueba", "lee el link y el user-agent");
        check (PreviewPlayer::parseResolveOutput ({ "URL|https://a/b", "UA|NA" }, st) && st.userAgent.isEmpty(), "sin user-agent");
        check (! PreviewPlayer::parseResolveOutput ({ "ERROR: algo" }, st), "sin link = falla");

        // Headers de yt-dlp y la espera que pide YouTube por la publicidad
        const auto nowSec = juce::Time::currentTimeMillis() / 1000;
        check (PreviewPlayer::parseResolveOutput ({ "URL|https://a/b", "UA|Mozilla/5.0 X",
                                                    R"(HDR|{"User-Agent": "Mozilla/5.0 X", "Accept-Language": "en-us,en;q=0.5", "Sec-Fetch-Mode": "navigate"})",
                                                    "AT|" + juce::String (nowSec + 6) }, st), "lee headers y available_at");
        eq (st.headerBlock(), "User-Agent: Mozilla/5.0 X\r\nAccept-Language: en-us,en;q=0.5\r\nSec-Fetch-Mode: navigate\r\n",
            "headers como los pasa yt-dlp a FFmpeg");
        const auto wait = st.mustWaitUntil() - juce::Time::currentTimeMillis();
        check (wait > 5000 && wait <= 8000, "con publicidad: espera ~6 s + 1 de margen", juce::String (wait));

        check (PreviewPlayer::parseResolveOutput ({ "URL|https://a/b", "AT|" + juce::String (nowSec - 1) }, st)
               && st.mustWaitUntil() == 0, "sin publicidad: no espera nada");
        check (PreviewPlayer::parseResolveOutput ({ "URL|https://a/b", "AT|NA" }, st) && st.mustWaitUntil() == 0, "sin dato: no espera");
        check (PreviewPlayer::parseResolveOutput ({ "URL|https://a/b", "UA|Mozilla/5.0 Solo" }, st)
               && st.headerBlock() == "User-Agent: Mozilla/5.0 Solo\r\n", "sin headers: al menos el user-agent");
    }

    if (! Tools::allInstalled())
    {
        std::cout << "(salteada: faltan yt-dlp/ffmpeg)\n";
        return;
    }

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("grabpreview_" + juce::String (juce::Random::getSystemRandom().nextInt (999999)));
    tmp.createDirectory();
    auto tone = tmp.getChildFile ("tono largo.mp3");
    run ({ Tools::ffmpeg().getFullPathName(), "-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=330:duration=60",
           "-ar", "44100", "-ac", "2", "-c:a", "libmp3lame", "-b:a", "128k", tone.getFullPathName() });
    auto big = tmp.getChildFile ("largo.webm");
    run ({ Tools::ffmpeg().getFullPathName(), "-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=220:duration=300",
           "-ar", "48000", "-ac", "2", "-c:a", "libopus", "-b:a", "128k", big.getFullPathName() });
    check (tone.getSize() > 10000 && big.getSize() > 10000, "archivos de prueba");

    // Todo por HTTP, como YouTube (y anda igual en Windows y en Linux)
    auto script = tmp.getChildFile ("servidor.py");
    script.replaceWithText (testServerPy);
    juce::ChildProcess server;
    const int port = 18000 + juce::Random::getSystemRandom().nextInt (2000);
    const auto base = "http://127.0.0.1:" + juce::String (port) + "/";
    check (server.start (juce::StringArray { pythonExe(), script.getFullPathName(), juce::String (port), tmp.getFullPathName() }),
           "servidor de prueba (python)");
    for (int i = 0; i < 100 && httpGet (base + "negar?n=0") != "ok"; ++i)
        juce::Thread::sleep (100);
    check (httpGet (base + "negar?n=0") == "ok", "servidor de prueba responde");

    const auto toneUrl = base + "tono%20largo.mp3";
    const auto bigUrl = base + "largo.webm";

    Engine e;
    PreviewPlayer pp (e);
    pp.setPlaybackRate (48000.0);

    pp.play ("tono-prueba", toneUrl, "Tono de prueba", 60.0);
    check (pp.getStatus().state == PreviewPlayer::State::loading, "arranca cargando");
    check (waitForPreview (pp, 60000), "carga a tiempo");

    auto st = pp.getStatus();
    std::cout << "      tono: suena a los " << pp.getLastTimeToSoundMs() << " ms"
              << (st.error.isNotEmpty() ? "  error: " + st.error : juce::String()) << "\n";
    check (st.state == PreviewPlayer::State::playing, "suena", st.error);
    check (! st.slowPath, "por el camino rápido");
    check (st.startSec == 18.0 && st.lengthSec == 30.0, "fragmento de 30 s desde el 30 %");

    juce::Thread::sleep (1500);   // que termine de llegar el audio (en la vida real se va reproduciendo)
    juce::AudioBuffer<float> buf (2, 48000);
    buf.clear();
    pp.render (buf, 0, 48000);
    std::cout << "      nivel RMS: " << rms (buf) << ", progreso tras 1 s: " << pp.getStatus().progress << "\n";
    check (rms (buf) > 0.06f && rms (buf) < 0.2f, "suena y a un nivel parejo (ganancia automática)");
    check (std::abs (pp.getStatus().progress - 1.0f / 30.0f) < 0.005f, "dura 30 s (avanza 1/30 por segundo)");
    check (! pp.getStatus().stalled, "no se marca como trabado mientras suena");

    pp.setPlaybackRate (96000.0);
    const auto before = pp.getStatus().progress;
    juce::AudioBuffer<float> b96 (2, 96000);
    b96.clear();
    pp.render (b96, 0, 96000);
    check (std::abs ((pp.getStatus().progress - before) - 1.0f / 30.0f) < 0.005f, "a 96 kHz también avanza 1 s por segundo");
    pp.setPlaybackRate (48000.0);

    juce::Thread::sleep (1700);
    check (pp.getStatus().stalled, "si nadie lo reproduce, avisa que no llega sonido");

    pp.stop();
    juce::AudioBuffer<float> fade (2, 4800);
    fade.clear();
    pp.render (fade, 0, 4800);
    check (fade.getMagnitude (0, 0, 480) > 0.0f && fade.getMagnitude (0, 2400, 2400) == 0.0f, "al parar baja el volumen en 40 ms (sin click)");
    fade.clear();
    pp.render (fade, 0, 4800);
    check (rms (fade) == 0.0f && pp.getStatus().state == PreviewPlayer::State::idle, "parar = silencio");

    pp.play ("tono-prueba", toneUrl, "Tono de prueba", 60.0);
    check (pp.getStatus().state == PreviewPlayer::State::playing && pp.getLastTimeToSoundMs() == 0,
           "la segunda vez suena al instante (desde la memoria)");

    juce::AudioBuffer<float> chunk (2, 4800);
    for (int i = 0; i < 400 && pp.getStatus().state == PreviewPlayer::State::playing; ++i)
    {
        chunk.clear();
        pp.render (chunk, 0, chunk.getNumSamples());
    }
    check (pp.getStatus().state == PreviewPlayer::State::idle, "al terminar el fragmento se apaga solo");

    // Antes vs ahora
    {
        // Método 1.3: fragmento con yt-dlp --download-sections + FFmpeg con loudnorm, y recién ahí suena
        auto t0 = juce::Time::getMillisecondCounter();
        auto old = tmp.getChildFile ("viejo");
        old.createDirectory();
        run ({ Tools::ytdlp().getFullPathName(), "--no-warnings", "-f", "bestaudio/best", "--download-sections", "*90-120",
               "-o", old.getFullPathName() + "/x.%(ext)s", "--", bigUrl });
        auto section = old.findChildFiles (juce::File::findFiles, false);
        run ({ Tools::ffmpeg().getFullPathName(), "-v", "error", "-y", "-i", section.isEmpty() ? juce::String() : section[0].getFullPathName(),
               "-ac", "2", "-ar", "48000", "-af", "loudnorm=I=-16:TP=-1.5:LRA=11", "-c:a", "pcm_f32le", old.getFullPathName() + "/x.wav" });
        const auto oldMs = (int) (juce::Time::getMillisecondCounter() - t0);

        pp.play ("http-largo", bigUrl, "Largo", 300.0);
        waitForPreview (pp, 60000);
        st = pp.getStatus();
        const auto newMs = pp.getLastTimeToSoundMs();
        std::cout << "      HTTP: antes sonaba a los " << oldMs << " ms, ahora a los " << newMs << " ms\n";
        check (st.state == PreviewPlayer::State::playing, "por HTTP suena mientras baja", st.error);
        check (st.startSec == 90.0, "tema de 5:00: arranca en 1:30");
        // Acá todo es local (sin la demora de internet): solo se pide que no sea más lento.
        check (newMs < oldMs + 1000, "no tarda más que el método anterior (con internet la diferencia es mucho mayor)");
        juce::Thread::sleep (1500);
        buf.clear();
        pp.render (buf, 0, 48000);
        check (rms (buf) > 0.06f, "fragmento con audio");
        pp.stop();

        // Con los links resueltos por adelantado (las primeras opciones de la búsqueda)
        pp.prefetchMany ({ { "adelantado-1", bigUrl + "?a=1" }, { "adelantado-2", bigUrl + "?a=2" } });
        for (int i = 0; i < 400 && ! (pp.isResolved ("adelantado-1") && pp.isResolved ("adelantado-2")); ++i)
            juce::Thread::sleep (50);
        check (pp.isResolved ("adelantado-1") && pp.isResolved ("adelantado-2"), "resuelve por adelantado las primeras opciones");
        pp.play ("adelantado-2", bigUrl + "?a=2", "Largo", 300.0);
        waitForPreview (pp, 60000);
        std::cout << "      con el link adelantado suena a los " << pp.getLastTimeToSoundMs() << " ms\n";
        check (pp.getStatus().state == PreviewPlayer::State::playing && pp.getLastTimeToSoundMs() < newMs,
               "con el link adelantado suena todavía antes");
        pp.stop();
    }

    // Las primeras opciones se adelantan de a varias a la vez, y si tocás una que se está
    // adelantando, se espera a esa (no se pide otro link)
    {
        const auto logBefore = PreviewPlayer::logFile().getSize();
        std::vector<std::pair<juce::String, juce::String>> six;
        for (int i = 1; i <= 6; ++i)
            six.push_back ({ "varios-" + juce::String (i), bigUrl + "?v=" + juce::String (i) });

        const auto t0 = juce::Time::getMillisecondCounter();
        pp.prefetchMany (six);
        juce::Thread::sleep (300);
        pp.play ("varios-2", bigUrl + "?v=2", "Varios 2", 300.0);   // justo se está adelantando
        waitForPreview (pp, 60000);
        check (pp.getStatus().state == PreviewPlayer::State::playing, "tocar una que se está adelantando: suena", pp.getStatus().error);

        bool all = false;
        for (int i = 0; i < 600 && ! all; ++i)
        {
            all = true;
            for (auto& p : six)
                all = all && pp.isResolved (p.first);
            if (! all) juce::Thread::sleep (50);
        }
        const auto ms = (int) (juce::Time::getMillisecondCounter() - t0);
        std::cout << "      6 opciones adelantadas en " << ms << " ms (de a 3)\n";
        check (all, "adelanta las 6 primeras opciones");
        const auto log = logSince (logBefore);
        const auto afterPlay = log.fromFirstOccurrenceOf ("[varios-2]", false, false);
        std::cout << "      al tocarla: " << afterPlay.fromFirstOccurrenceOf ("link (", true, false).upToFirstOccurrenceOf ("\n", false, false) << "\n";
        pp.stop();
    }

    // Con «Usar sesión de Firefox» activado, el camino rápido va sin sesión (no la acepta y leerla tarda)
    {
        Engine ec;
        const bool before = ec.getUseFirefoxCookies();
        ec.setUseFirefoxCookies (true);   // en esta máquina no hay Firefox: si se usara, fallaría
        PreviewPlayer pc (ec);
        pc.setPlaybackRate (48000.0);
        bool slow = false;
        pc.play ("con-firefox", bigUrl + "?f=1", "Con Firefox", 300.0);
        waitForPreview (pc, 60000, nullptr, &slow);
        check (pc.getStatus().state == PreviewPlayer::State::playing && ! slow,
               "con sesión de Firefox activada: suena por el camino rápido", pc.getStatus().error);
        pc.stop();
        ec.setUseFirefoxCookies (before);
    }

    // YouTube: si el tema tiene publicidad, el link no se puede usar hasta que "pasaría" el anuncio
    {
        Engine ew;
        const auto at = juce::Time::currentTimeMillis() / 1000 + 4;
        ew.setExtraYtdlpArgs ({ "--parse-metadata", "t=" + juce::String (at) + ":t=(?P<available_at>\\d+)" });
        PreviewPlayer pw (ew);
        pw.setPlaybackRate (48000.0);
        juce::int64 sawWait = 0;
        pw.play ("con-publicidad", bigUrl + "?p=1", "Con publicidad", 300.0);
        waitForPreview (pw, 60000, &sawWait);
        const auto stw = pw.getStatus();
        std::cout << "      con publicidad: suena a los " << pw.getLastTimeToSoundMs() << " ms\n";
        check (stw.state == PreviewPlayer::State::playing, "con publicidad: suena", stw.error);
        check (sawWait >= (at + 1) * 1000 - 50, "muestra la cuenta regresiva hasta que YouTube deja", juce::String (sawWait - at * 1000));
        check (juce::Time::currentTimeMillis() >= (at + 1) * 1000 - 50, "no abre el link antes de tiempo");
        pw.stop();
    }

    // Si FFmpeg recibe 403 (YouTube hizo esperar sin avisar): reintenta a los 5 s con el mismo link
    {
        const auto logBefore = PreviewPlayer::logFile().getSize();
        httpGet (base + "negar?n=2");   // el primer intento y la prueba de diagnóstico
        pp.play ("con-403", bigUrl + "?r=1", "Con 403", 300.0);
        bool slow = false;
        waitForPreview (pp, 60000, nullptr, &slow);
        st = pp.getStatus();
        const auto log = logSince (logBefore);
        std::cout << "      con 403: suena a los " << pp.getLastTimeToSoundMs() << " ms\n";
        check (st.state == PreviewPlayer::State::playing && ! slow, "con 403 al principio: suena al reintentar", st.error);
        check (log.contains ("403") && log.contains ("reintento"), "queda anotado en preview-log.txt", log.getLastCharacters (300));
        check (pp.getPathLevel() == 0, "un 403 suelto no cambia el camino");
        pp.stop();
    }

    // Plan B: si por el camino rápido no llega audio, yt-dlp baja el fragmento (como la 1.3)
    {
        Engine eb;
        PreviewPlayer pb (eb);
        pb.setPlaybackRate (48000.0);
        httpGet (base + "negar?n=4");   // intento, diagnóstico, reintento, diagnóstico: todo 403
        bool slow = false;
        pb.play ("plan-b", bigUrl + "?b=1", "Plan B", 300.0);
        waitForPreview (pb, 90000, nullptr, &slow);
        auto sb = pb.getStatus();
        std::cout << "      plan B: suena a los " << pb.getLastTimeToSoundMs() << " ms\n";
        check (sb.state == PreviewPlayer::State::playing && slow, "si el link directo no anda, suena por el plan B", sb.error);
        juce::Thread::sleep (1500);
        buf.clear();
        pb.render (buf, 0, 48000);
        check (rms (buf) > 0.06f, "plan B: fragmento con audio");
        for (int i = 0; i < 300 && pb.getPathLevel() == 0; ++i)   // se decide cuando termina de leer el fragmento
            juce::Thread::sleep (50);
        check (pb.getPathLevel() == 1, "la próxima vez prueba con el cliente normal", juce::String (pb.getPathLevel()));
        pb.stop();

        pb.setForceSlowPathForTesting (true);
        slow = false;
        pb.play ("plan-b-directo", toneUrl + "?b=2", "Plan B directo", 60.0);
        waitForPreview (pb, 90000, nullptr, &slow);
        sb = pb.getStatus();
        check (sb.state == PreviewPlayer::State::playing && slow && sb.startSec == 18.0, "plan B directo (mp3 desde el 30 %)", sb.error);
        pb.stop();
    }

    // Uno que no existe
    pp.play ("roto", base + "no-existe.mp3", "Roto", 100.0);
    waitForPreview (pp, 90000);
    st = pp.getStatus();
    check (st.state == PreviewPlayer::State::failed && st.error.isNotEmpty(), "un link roto avisa el error", st.error);
    std::cout << "      mensaje: " << st.error << "\n";

    // Cambiar de opción mientras carga: gana la última
    pp.play ("roto2", base + "no-existe2.mp3", "Roto 2", 100.0);
    pp.play ("tono-otro", toneUrl + "?o=1", "Tono", 60.0);
    waitForPreview (pp, 30000);
    check (pp.getStatus().videoId == "tono-otro" && pp.getStatus().state == PreviewPlayer::State::playing,
           "si cambiás de opción rápido, suena la última");
    pp.stop();

    server.kill();
    tmp.deleteRecursively();
}

//==============================================================================
static void updaterTest()
{
    std::cout << "\n== Aviso de versión nueva (sin internet)\n";

    const char* fixture = R"J({
      "tag_name": "build-12",
      "name": "Claraboya Grab 1.3.0 · build 12",
      "html_url": "https://github.com/joaquinnachman/claraboyagrab/releases/tag/build-12",
      "body": "Plugin VST3 para Cubase y programa suelto, para Windows.

Novedades:
- Preview: escuchar 30 s de cada opción
- Aviso de versión nueva
",
      "assets": [
        { "name": "ClaraboyaGrab-Windows.zip", "size": 6960592,
          "browser_download_url": "https://github.com/joaquinnachman/claraboyagrab/releases/download/build-12/ClaraboyaGrab-Windows.zip" },
        { "name": "ClaraboyaGrab-Instalador.exe", "size": 7340032, "digest": "sha256:abc123",
          "browser_download_url": "https://github.com/joaquinnachman/claraboyagrab/releases/download/build-12/ClaraboyaGrab-Instalador.exe" },
        { "name": "Otro-Instalador.exe", "size": 1,
          "browser_download_url": "https://ejemplo.com/Otro-Instalador.exe" }
      ]})J";

    Updater::Release r;
    check (Updater::parseLatest (juce::JSON::parse (juce::String::fromUTF8 (fixture)), r), "lee la última Release");
    check (r.build == 12, "build 12", juce::String (r.build));
    eq (r.label, TR ("1.3.0 · build 12"), "etiqueta de versión");
    check (r.changes.size() == 2, "2 novedades", juce::String (r.changes.size()));
    eq (r.changes[0], TR ("Preview: escuchar 30 s de cada opción"), "primera novedad");
    eq (r.installerUrl, "https://github.com/joaquinnachman/claraboyagrab/releases/download/build-12/ClaraboyaGrab-Instalador.exe",
        "instalador de este repo (ignora links de otros sitios)");
    check (r.installerSize == 7340032 && r.installerDigest == "sha256:abc123", "tamaño y SHA-256 del instalador");
    check (r.zipUrl.endsWith ("ClaraboyaGrab-Windows.zip"), "también el .zip");

    Updater::Release none;
    check (! Updater::parseLatest (juce::JSON::parse ("{\"tag_name\": \"v1.0\"}"), none), "ignora tags que no son build-N");
    check (Updater::currentBuild() == 0, "las pruebas son compilación de prueba (build 0)");

    // Descarga local verificada (sin internet): con un servidor HTTP propio
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("grabupd_" + juce::String (juce::Random::getSystemRandom().nextInt (999999)));
    tmp.createDirectory();
    auto payload = tmp.getChildFile ("ClaraboyaGrab-Instalador.exe");
    {
        juce::MemoryBlock mb;
        juce::Random rnd (42);
        for (int i = 0; i < 300000; ++i) { const auto b = (char) rnd.nextInt (256); mb.append (&b, 1); }
        payload.replaceWithData (mb.getData(), mb.getSize());
    }
    const auto digest = "sha256:" + juce::SHA256 (payload).toHexString();

    juce::ChildProcess server;
    const int port = 20000 + juce::Random::getSystemRandom().nextInt (2000);
    server.start (juce::StringArray { "python3", "-m", "http.server", juce::String (port), "--bind", "127.0.0.1",
                                      "--directory", tmp.getFullPathName() });
    juce::Thread::sleep (1200);
    const auto url = "http://127.0.0.1:" + juce::String (port) + "/ClaraboyaGrab-Instalador.exe";

    juce::String err;
    float lastProgress = 0.0f;
    auto dest = tmp.getChildFile ("bajado.exe");
    check (Updater::download (url, payload.getSize(), digest, dest, [&] (float p) { lastProgress = p; }, nullptr, err),
           "baja y verifica el SHA-256", err);
    check (dest.getSize() == payload.getSize() && lastProgress > 0.99f, "completo, con progreso hasta el 100 %");

    check (! Updater::download (url, payload.getSize(), "sha256:0000", tmp.getChildFile ("malo.exe"), nullptr, nullptr, err)
           && ! tmp.getChildFile ("malo.exe").exists() && ! tmp.getChildFile ("malo.exe.part").exists(),
           "si el SHA-256 no coincide, lo descarta", err);
    std::cout << "      mensaje: " << err << "\n";

    check (! Updater::download (url, payload.getSize() + 10, {}, tmp.getChildFile ("corto.exe"), nullptr, nullptr, err),
           "si llega cortado, lo descarta", err);

    server.kill();
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

    std::cout << "\n== Aviso de versión nueva: la Release real en GitHub\n";
    {
        int status = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (5).withStatusCode (&status)
                        .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: ClaraboyaGrab-pruebas\r\n");
        Updater::Release r;
        bool ok = false;
        if (auto in = juce::URL::createWithoutParsing (Updater::apiUrl).createInputStream (opts); in != nullptr && status == 200)
            ok = Updater::parseLatest (juce::JSON::parse (in->readEntireStreamAsString()), r);

        std::cout << "      HTTP " << status << " -> " << r.label << ", instalador: " << (r.installerUrl.isNotEmpty() ? "sí" : "no")
                  << ", novedades: " << r.changes.size() << "\n";
        for (auto& c : r.changes) std::cout << "        - " << c << "\n";
        check (ok && r.build >= 9 && r.zipUrl.isNotEmpty(), "lee la última Release publicada");

        // Descarga real con verificación (el instalador si ya existe; si no, el .zip)
        const auto url = r.installerUrl.isNotEmpty() ? r.installerUrl : r.zipUrl;
        const auto size = r.installerUrl.isNotEmpty() ? r.installerSize : (juce::int64) 0;
        const auto dg = r.installerUrl.isNotEmpty() ? r.installerDigest : juce::String();
        juce::String err;
        auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("grab-update-test.bin");
        const auto t0 = juce::Time::getMillisecondCounter();
        check (Updater::download (url, size, dg, dest, nullptr, nullptr, err) && dest.getSize() > 1000000,
               "baja desde GitHub (sigue las redirecciones)", err);
        std::cout << "      " << dest.getSize() / 1024 << " KB en " << (int) (juce::Time::getMillisecondCounter() - t0) << " ms"
                  << (dg.isNotEmpty() ? " (SHA-256 verificado)" : "") << "\n";
        dest.deleteFile();
    }

    std::cout << "\n== Preview real (informativo: YouTube suele bloquear a los servidores de GitHub)\n";
    if (! songs.empty() && Tools::allInstalled())
    {
        Engine e;
        PreviewPlayer pp (e);
        pp.setPlaybackRate (48000.0);
        const auto t0 = juce::Time::getMillisecondCounter();
        pp.play (songs[0].videoId, songs[0].url(), songs[0].niceName(), songs[0].duration);
        waitForPreview (pp, 120000);
        const auto st = pp.getStatus();
        std::cout << "      " << (st.state == PreviewPlayer::State::playing ? "sonó" : "no sonó: " + st.error)
                  << " (" << (int) (juce::Time::getMillisecondCounter() - t0) << " ms)\n";
        pp.stop();
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

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    const bool live = args.contains ("--live");

    // En Windows (CI): instalar yt-dlp, FFmpeg y Deno con el mismo código que el botón INSTALAR.
    if (args.contains ("--instalar") && ! Tools::allInstalled())
    {
        std::cout << "\n== Instalar herramientas (como el botón INSTALAR)\n";
        Engine e;
        e.enqueueInstallTools (false);
        e.waitUntilIdle (1200000);
        const auto ts = e.getToolStatus();
        std::cout << "      " << ts.text << "  (yt-dlp " << ts.version << ")\n";
        check (Tools::allInstalled(), "yt-dlp, FFmpeg y Deno instalados", ts.text);
        check (Tools::ytdlpIsFastBuild(), "yt-dlp en carpeta (arranca rápido)");
    }

    if (! args.contains ("--solo-vivo"))
    {
        offlineTests();
        pipelineTest();
        previewTest();
        updaterTest();
    }

    const int offlineFailures = failures;

    if (live)
        liveTests();

    std::cout << "\n" << (failures == 0 ? juce::String ("TODO OK")
                                         : juce::String (failures) + " FALLAS (" + juce::String (offlineFailures) + " sin internet)") << "\n";
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return failures == 0 ? 0 : 1;
}
