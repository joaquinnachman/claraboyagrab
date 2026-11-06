#include "Search.h"
#include "Tools.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace grab
{
namespace
{
    // Cliente web de YouTube Music (mismo que usa yt-dlp).
    constexpr const char* ytmClientVersion = "1.20260707.12.00";
    constexpr const char* songsParams      = "EgWKAQIIAWoKEAoQAxAEEAkQBQ==";
    constexpr const char* videosParams     = "EgWKAQIQAWoKEAoQAxAEEAkQBQ==";

    //==============================================================================
    // Acceso seguro a JSON (nunca rompe si falta algo)
    juce::var prop (const juce::var& v, const char* name)
    {
        return v.getProperty (juce::Identifier (name), {});
    }

    juce::var at (const juce::var& v, int i)
    {
        if (auto* a = v.getArray())
            if (juce::isPositiveAndBelow (i, a->size()))
                return a->getReference (i);
        return {};
    }

    int sizeOf (const juce::var& v)
    {
        if (auto* a = v.getArray())
            return a->size();
        return 0;
    }

    juce::var path (juce::var v, std::initializer_list<const char*> keys)
    {
        for (auto k : keys)
            v = prop (v, k);
        return v;
    }

    void collectRenderers (const juce::var& v, juce::Array<juce::var>& out, int depth)
    {
        if (depth > 40)
            return;

        if (auto* obj = v.getDynamicObject())
        {
            for (auto& p : obj->getProperties())
            {
                if (p.name.toString() == "musicResponsiveListItemRenderer")
                    out.add (p.value);
                else
                    collectRenderers (p.value, out, depth + 1);
            }
        }
        else if (auto* arr = v.getArray())
        {
            for (auto& e : *arr)
                collectRenderers (e, out, depth + 1);
        }
    }

    juce::String runsText (const juce::var& runs)
    {
        juce::String s;
        for (int i = 0; i < sizeOf (runs); ++i)
            s << prop (at (runs, i), "text").toString();
        return s;
    }

    juce::String bestThumb (const juce::var& thumbs, int minWidth)
    {
        juce::String chosen;
        int chosenW = 1 << 30;

        for (int i = 0; i < sizeOf (thumbs); ++i)
        {
            const auto t = at (thumbs, i);
            const int w = (int) prop (t, "width");
            const auto u = prop (t, "url").toString();

            if (u.isEmpty())
                continue;

            if (chosen.isEmpty() || (w >= minWidth && w < chosenW))
            {
                chosen = u;
                chosenW = w >= minWidth ? w : chosenW;
            }
        }

        if (chosen.startsWith ("//"))
            chosen = "https:" + chosen;

        return chosen;
    }

    juce::String formatViews (double n)
    {
        if (n <= 0.0)
            return {};
        if (n >= 1.0e9) return juce::String (n / 1.0e9, 1).replace (".", ",") + " MM de vistas";
        if (n >= 1.0e6) return juce::String (n / 1.0e6, 1).replace (".", ",") + " M de vistas";
        if (n >= 1.0e3) return juce::String ((int) (n / 1.0e3)) + " k vistas";
        return juce::String ((int) n) + " vistas";
    }

    bool isTypeLabel (const juce::String& s)
    {
        static const juce::StringArray labels = juce::StringArray::fromTokens (
            TR ("Canción|Song|Video|Vídeo|Episodio|Episode|Sencillo|Single|EP|Álbum|Album"), "|", "");
        return labels.contains (s.trim(), true);
    }

    Candidate parseItem (const juce::var& r, SearchKind kind)
    {
        Candidate c;
        c.isVideo = kind == SearchKind::videos;

        const auto flex = prop (r, "flexColumns");
        auto runs = [&flex] (int i)
        {
            return path (at (flex, i), { "musicResponsiveListItemFlexColumnRenderer", "text", "runs" });
        };

        const auto titleRuns = runs (0);
        c.title = runsText (titleRuns).trim();

        c.videoId = path (r, { "playlistItemData", "videoId" }).toString();
        if (c.videoId.isEmpty())
            c.videoId = path (at (titleRuns, 0), { "navigationEndpoint", "watchEndpoint", "videoId" }).toString();
        if (c.videoId.isEmpty())
            c.videoId = path (r, { "overlay", "musicItemThumbnailOverlayRenderer", "content", "musicPlayButtonRenderer",
                                   "playNavigationEndpoint", "watchEndpoint", "videoId" }).toString();

        // Columnas siguientes: "Artista • Disco • 4:54" (canciones) o "Artista • 1,2 M de vistas • 4:54" (videos)
        juce::StringArray artists, parts;
        juce::String album;

        auto addParts = [&parts] (const juce::String& text)
        {
            for (auto& p : juce::StringArray::fromTokens (text, TR ("•"), ""))
                if (p.trim().isNotEmpty())
                    parts.add (p.trim());
        };

        for (int col = 1; col < sizeOf (flex); ++col)
        {
            const auto colRuns = runs (col);
            juce::String colText;

            for (int i = 0; i < sizeOf (colRuns); ++i)
            {
                const auto run = at (colRuns, i);
                const auto text = prop (run, "text").toString();
                const auto pageType = path (run, { "navigationEndpoint", "browseEndpoint", "browseEndpointContextSupportedConfigs",
                                                   "browseEndpointContextMusicConfig", "pageType" }).toString();

                if (pageType == "MUSIC_PAGE_TYPE_ARTIST" || pageType == "MUSIC_PAGE_TYPE_USER_CHANNEL")
                    artists.addIfNotAlreadyThere (text.trim());
                else if (pageType == "MUSIC_PAGE_TYPE_ALBUM")
                    album = text.trim();

                colText << text;
            }

            addParts (colText);
        }

        const auto fixed = prop (r, "fixedColumns");
        for (int col = 0; col < sizeOf (fixed); ++col)
            addParts (runsText (path (at (fixed, col), { "musicResponsiveListItemFixedColumnRenderer", "text", "runs" })));

        for (int i = parts.size(); --i >= 0;)
        {
            if (const auto d = parseDurationText (parts[i]); d > 0.0)
            {
                c.duration = d;
                parts.remove (i);
                break;
            }
        }

        if (! parts.isEmpty() && isTypeLabel (parts[0]))
            parts.remove (0);

        c.artist = artists.isEmpty() ? parts[0] : artists.joinIntoString (", ");
        c.extra  = album.isNotEmpty() ? album : parts[1];

        if (album.isEmpty() && c.extra == c.artist)
            c.extra = {};

        c.thumbUrl = bestThumb (path (r, { "thumbnail", "musicThumbnailRenderer", "thumbnail", "thumbnails" }), 120);
        return c;
    }

    //==============================================================================
    juce::String removeAccents (const juce::String& s)
    {
        static const juce::String from = TR ("áàäâãåéèëêíìïîóòöôõúùüûñçÁÀÄÂÃÅÉÈËÊÍÌÏÎÓÒÖÔÕÚÙÜÛÑÇ");
        static const juce::String to   = "aaaaaaeeeeiiiiooooouuuuncAAAAAAEEEEIIIIOOOOOUUUUNC";

        juce::String out;
        out.preallocateBytes (s.getNumBytesAsUTF8());

        for (auto p = s.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            const auto idx = from.indexOfChar (c);
            out << juce::String::charToString (idx >= 0 ? to[idx] : c);
        }

        return out;
    }

    juce::StringArray tokens (const juce::String& s)
    {
        auto t = juce::StringArray::fromTokens (normaliseForMatch (s), " ", "");
        t.removeEmptyStrings();
        return t;
    }

    /** Qué parte de las palabras de a aparecen en b (0..1). */
    float overlap (const juce::StringArray& a, const juce::StringArray& b)
    {
        if (a.isEmpty())
            return 0.0f;

        int found = 0;
        for (auto& w : a)
            if (b.contains (w))
                ++found;

        return (float) found / (float) a.size();
    }

    juce::String tail (const juce::String& s, int maxLen)
    {
        return s.length() > maxLen ? s.substring (s.length() - maxLen) : s;
    }
}

//==============================================================================
juce::String Candidate::url() const
{
    // Mismo id en YouTube y YouTube Music: se baja por el link común de YouTube (el camino más probado de yt-dlp).
    return "https://www.youtube.com/watch?v=" + videoId;
}

double parseDurationText (const juce::String& raw)
{
    const auto s = raw.trim();
    auto bits = juce::StringArray::fromTokens (s, ":", "");

    if (bits.size() < 2 || bits.size() > 3)
        return 0.0;

    double total = 0.0;

    for (int i = 0; i < bits.size(); ++i)
    {
        const auto& b = bits[i];
        if (b.isEmpty() || ! b.containsOnly ("0123456789") || b.length() > (i == 0 ? 3 : 2))
            return 0.0;
        if (i > 0 && b.length() != 2)
            return 0.0;
        total = total * 60.0 + b.getDoubleValue();
    }

    return total;
}

juce::String normaliseForMatch (const juce::String& raw)
{
    auto s = removeAccents (raw).toLowerCase();

    juce::String cleaned;
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        cleaned << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String (" "));
    }

    static const juce::StringArray stop { "remaster", "remastered", "remasterizado", "version", "official", "oficial",
                                          "audio", "video", "lyric", "lyrics", "letra", "hd", "hq", "4k", "feat", "ft",
                                          "topic", "the", "el", "la", "los", "las", "a" };

    auto words = juce::StringArray::fromTokens (cleaned, " ", "");
    words.removeEmptyStrings();

    for (int i = words.size(); --i >= 0;)
        if (stop.contains (words[i]))
            words.remove (i);

    return words.joinIntoString (" ");
}

std::vector<Candidate> parseYTMusicResponse (const juce::var& json, SearchKind kind)
{
    juce::Array<juce::var> renderers;
    collectRenderers (json, renderers, 0);

    std::vector<Candidate> out;
    std::set<juce::String> seen;

    for (auto& r : renderers)
    {
        auto c = parseItem (r, kind);

        if (c.videoId.isEmpty() || c.title.isEmpty() || ! seen.insert (c.videoId).second)
            continue;

        out.push_back (std::move (c));
    }

    return out;
}

std::vector<Candidate> parseYtdlpSearchLines (const juce::StringArray& lines)
{
    std::vector<Candidate> out;
    std::set<juce::String> seen;

    for (auto& line : lines)
    {
        if (! line.trimStart().startsWithChar ('{'))
            continue;

        const auto j = juce::JSON::parse (line);
        Candidate c;
        c.isVideo  = true;
        c.videoId  = prop (j, "id").toString();
        c.title    = prop (j, "title").toString().trim();
        c.duration = (double) prop (j, "duration");

        for (auto key : { "channel", "uploader", "artist" })
            if (c.artist.isEmpty())
                c.artist = prop (j, key).toString().trim();

        c.extra = formatViews ((double) prop (j, "view_count"));

        if (c.videoId.isNotEmpty())
            c.thumbUrl = "https://i.ytimg.com/vi/" + c.videoId + "/mqdefault.jpg";

        if (c.videoId.isEmpty() || c.title.isEmpty() || ! seen.insert (c.videoId).second)
            continue;

        out.push_back (std::move (c));
    }

    return out;
}

void rankCandidates (std::vector<Candidate>& items, const TrackQuery& expected)
{
    if (expected.title.isEmpty() || items.empty())
        return;

    const auto eT = tokens (expected.title);
    const auto eA = tokens (expected.artist);

    for (size_t i = 0; i < items.size(); ++i)
    {
        auto& c = items[i];
        const auto cT = tokens (c.title);
        const auto cA = tokens (c.artist);

        const float titleSim  = 0.7f * overlap (eT, cT) + 0.3f * overlap (cT, eT);
        // En videos el artista suele estar en el título ("Banda - Tema"); en canciones solo vale el artista.
        const float artistSim = eA.isEmpty() ? 0.5f
                              : c.isVideo   ? juce::jmax (overlap (eA, cA), overlap (eA, cT))
                                            : overlap (eA, cA);

        float durSim = 0.0f;
        double delta = -1.0;

        if (expected.durationSec > 0.0 && c.duration > 0.0)
        {
            delta = std::abs (expected.durationSec - c.duration);
            durSim = delta <= 2.5 ? 1.0f : delta <= 6.0 ? 0.6f : delta <= 15.0 ? 0.2f : delta > 45.0 ? -0.6f : 0.0f;
        }

        // Versiones distintas (vivo, remix, karaoke…) van más abajo si el original no lo es.
        static const juce::StringArray versionWords { "live", "vivo", "acoustic", "acustico", "acustica", "unplugged",
                                                      "remix", "cover", "karaoke", "instrumental", "demo", "slowed",
                                                      "sped", "nightcore" };
        auto cWords = cT;
        cWords.addArray (tokens (c.extra));
        float versionPenalty = 0.0f;

        for (auto& w : versionWords)
            if (cWords.contains (w) && ! eT.contains (w))
                versionPenalty = 1.0f;

        c.score = 3.0f * titleSim + 2.0f * artistSim + 1.5f * durSim - versionPenalty - 0.04f * (float) i;
        // La marca va solo si además es el mismo tema del mismo artista (no un cover que dura igual).
        c.durationMatch = delta >= 0.0 && delta <= 2.5 && titleSim >= 0.5f && versionPenalty <= 0.0f
                       && (eA.isEmpty() || artistSim >= 0.5f);
    }

    std::stable_sort (items.begin(), items.end(), [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });
}

//==============================================================================
std::vector<Candidate> searchYTMusic (const juce::String& query, SearchKind kind, juce::String& error,
                                      juce::String* rawResponse)
{
    auto* client = new juce::DynamicObject();
    client->setProperty ("clientName", "WEB_REMIX");
    client->setProperty ("clientVersion", ytmClientVersion);
    client->setProperty ("hl", "es");
    client->setProperty ("gl", "AR");

    auto* context = new juce::DynamicObject();
    context->setProperty ("client", juce::var (client));

    auto* body = new juce::DynamicObject();
    body->setProperty ("context", juce::var (context));
    body->setProperty ("query", query);
    body->setProperty ("params", kind == SearchKind::videos ? videosParams : songsParams);

    const auto payload = juce::JSON::toString (juce::var (body), true);

    int status = 0;
    const auto url = juce::URL::createWithoutParsing ("https://music.youtube.com/youtubei/v1/search?prettyPrint=false")
                         .withPOSTData (payload);

    const auto headers = juce::String ("Content-Type: application/json\r\n"
                                       "Origin: https://music.youtube.com\r\n"
                                       "Referer: https://music.youtube.com/\r\n"
                                       "X-YouTube-Client-Name: 67\r\n"
                                       "X-YouTube-Client-Version: ") + ytmClientVersion + "\r\n"
                         "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/141.0.0.0 Safari/537.36\r\n"
                         "Accept-Language: es-AR,es;q=0.9,en;q=0.8\r\n";

    auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                         .withConnectionTimeoutMs (8000)
                                         .withStatusCode (&status)
                                         .withExtraHeaders (headers));

    if (in == nullptr)
    {
        error = TR ("No pude conectar con YouTube Music. Revisá internet.");
        return {};
    }

    const auto text = in->readEntireStreamAsString();

    if (rawResponse != nullptr)
        *rawResponse = text;

    if (status >= 400)
    {
        error = "YouTube Music: HTTP " + juce::String (status);
        return {};
    }

    const auto json = juce::JSON::parse (text);

    if (! json.isObject())
    {
        error = TR ("YouTube Music devolvió algo raro.");
        return {};
    }

    return parseYTMusicResponse (json, kind);
}

std::vector<Candidate> searchWithYtdlp (const juce::String& query, SearchKind kind, juce::String& error,
                                        const std::function<bool()>& shouldStop)
{
    const auto exe = Tools::ytdlp();

    if (! exe.existsAsFile())
    {
        error = TR ("Faltan las herramientas: tocá INSTALAR.");
        return {};
    }

    juce::StringArray args { exe.getFullPathName(), "--flat-playlist", "--no-warnings", "--ignore-config",
                             "--encoding", "utf-8", "-j", "--",
                             "ytsearch10:" + query.replaceCharacter ('\n', ' ') + (kind == SearchKind::songs ? " audio" : "") };

    juce::ChildProcess proc;
    if (! proc.start (args, juce::ChildProcess::wantStdOut))
    {
        error = TR ("No pude ejecutar yt-dlp.");
        return {};
    }

    juce::MemoryOutputStream out;
    char buffer[4096];
    const auto deadline = juce::Time::getMillisecondCounter() + 25000;

    for (;;)
    {
        if ((shouldStop && shouldStop()) || juce::Time::getMillisecondCounter() > deadline)
        {
            proc.kill();
            error = TR ("La búsqueda tardó demasiado.");
            return {};
        }

        const auto n = proc.readProcessOutput (buffer, (int) sizeof (buffer));

        if (n > 0)
            out.write (buffer, (size_t) n);
        else if (! proc.isRunning())
            break;
        else
            juce::Thread::sleep (15);
    }

    auto items = parseYtdlpSearchLines (juce::StringArray::fromLines (out.toUTF8()));

    if (items.empty())
        error = TR ("No encontré nada.") + (proc.getExitCode() != 0 ? " (yt-dlp: " + tail (out.toUTF8(), 80) + ")" : juce::String());

    return items;
}

//==============================================================================
Searcher::Searcher() : juce::Thread ("Claraboya Grab - busqueda")
{
    startThread();
}

Searcher::~Searcher()
{
    ++latestGen;
    signalThreadShouldExit();
    notify();
    stopThread (12000);
}

int Searcher::request (const Request& r)
{
    const int gen = ++latestGen;

    {
        const juce::ScopedLock sl (lock);
        pending = r;
        hasPending = true;

        result = {};
        result.generation = gen;
        result.request = r;
        result.state = r.resolveLink ? Result::State::resolving : Result::State::searching;
        result.query = r.resolveLink ? juce::String() : r.text;
        result.source = r.resolveLink ? TrackQuery() : r.expected;
    }

    ++revision;
    notify();
    return gen;
}

void Searcher::clear()
{
    const int gen = ++latestGen;

    {
        const juce::ScopedLock sl (lock);
        hasPending = false;
        result = {};
        result.generation = gen;
    }

    ++revision;
}

void Searcher::injectForTesting (Result r)
{
    {
        const juce::ScopedLock sl (lock);
        hasPending = false;
        r.generation = ++latestGen;
        result = std::move (r);
    }
    ++revision;
}

Searcher::Result Searcher::getResult() const
{
    const juce::ScopedLock sl (lock);
    return result;
}

void Searcher::publish (int gen, const std::function<void (Result&)>& fn)
{
    {
        const juce::ScopedLock sl (lock);
        if (result.generation != gen)
            return;
        fn (result);
    }
    ++revision;
}

void Searcher::run()
{
    while (! threadShouldExit())
    {
        Request req;
        int gen = 0;

        {
            const juce::ScopedLock sl (lock);
            if (hasPending)
            {
                req = pending;
                hasPending = false;
                gen = latestGen.load();
            }
        }

        if (gen == 0)
        {
            wait (500);
            continue;
        }

        const auto stale = [this, gen] { return threadShouldExit() || gen != latestGen.load(); };

        TrackQuery source = req.expected;
        juce::String query = req.text;

        if (req.resolveLink)
        {
            juce::String platform;
            classify (req.text, platform);
            source = resolveStreaming (normaliseUrl (req.text), platform);

            if (stale())
                continue;

            if (source.error.isNotEmpty() || source.title.isEmpty())
            {
                const auto msg = source.error.isNotEmpty() ? source.error : TR ("No pude leer qué tema es.");
                publish (gen, [&] (Result& r) { r.state = Result::State::failed; r.error = msg; });
                continue;
            }

            query = source.searchText();
            publish (gen, [&] (Result& r)
            {
                r.state = Result::State::searching;
                r.source = source;
                r.query = query;
                r.request.platform = platform;
            });
        }

        juce::String error;
        auto items = searchYTMusic (query, req.kind, error);
        bool fallback = false;

        if (stale())
            continue;

        if (items.empty())
        {
            juce::String error2;
            auto alt = searchWithYtdlp (query, req.kind, error2, stale);

            if (stale())
                continue;

            if (! alt.empty())
            {
                items = std::move (alt);
                fallback = true;
                error = {};
            }
            else if (error.isEmpty())
            {
                error = error2;
            }
        }

        rankCandidates (items, source);

        publish (gen, [&] (Result& r)
        {
            r.items = std::move (items);
            r.usedFallback = fallback;
            r.query = query;
            r.source = source;
            r.error = r.items.empty() ? error : juce::String();
            r.state = r.items.empty() && error.isNotEmpty() ? Result::State::failed : Result::State::done;
        });
    }
}
}
