#include "Resolver.h"

#include <cctype>

namespace grab
{
namespace
{
    //==============================================================================
    juce::String extractUrlToken (const juce::String& input)
    {
        auto tokens = juce::StringArray::fromTokens (input, " \t\r\n", "");
        for (auto& t : tokens)
            if (t.startsWithIgnoreCase ("http://") || t.startsWithIgnoreCase ("https://") || t.startsWithIgnoreCase ("file://"))
                return t;
        return input.trim();
    }

    juce::String hostOf (const juce::String& url)
    {
        auto rest = url.fromFirstOccurrenceOf ("://", false, false);
        auto host = rest.upToFirstOccurrenceOf ("/", false, false)
                        .upToFirstOccurrenceOf ("?", false, false)
                        .upToFirstOccurrenceOf ("#", false, false);
        host = host.fromLastOccurrenceOf ("@", false, false).upToFirstOccurrenceOf (":", false, false);
        return host.toLowerCase();
    }

    juce::String pathOf (const juce::String& url)
    {
        auto rest = url.fromFirstOccurrenceOf ("://", false, false);
        if (! rest.containsChar ('/'))
            return "/";
        return rest.fromFirstOccurrenceOf ("/", true, false)
                   .upToFirstOccurrenceOf ("?", false, false)
                   .upToFirstOccurrenceOf ("#", false, false);
    }

    juce::String queryParam (const juce::String& url, const juce::String& name)
    {
        auto query = url.fromFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
        for (auto& kv : juce::StringArray::fromTokens (query, "&", ""))
            if (kv.upToFirstOccurrenceOf ("=", false, false) == name)
                return juce::URL::removeEscapeChars (kv.fromFirstOccurrenceOf ("=", false, false));
        return {};
    }

    bool hostIs (const juce::String& host, const char* domain)
    {
        juce::String d (domain);
        return host == d || host.endsWith ("." + d);
    }

    bool isDigits (const juce::String& s)
    {
        return s.isNotEmpty() && s.containsOnly ("0123456789");
    }

    std::string toLowerAscii (std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = (char) (c + 32);
        return s;
    }

    std::string attr (const std::string& tag, const std::string& tagLower, const std::string& name)
    {
        size_t pos = 0;
        const auto needle = name + "=";

        while ((pos = tagLower.find (needle, pos)) != std::string::npos)
        {
            const bool boundary = pos > 0 && std::isspace ((unsigned char) tagLower[pos - 1]);
            const size_t q = pos + needle.size();

            if (! boundary) { pos = q; continue; }
            if (q >= tag.size()) break;

            const char quote = tag[q];

            if (quote == '"' || quote == '\'')
            {
                const auto end = tag.find (quote, q + 1);
                return end == std::string::npos ? std::string() : tag.substr (q + 1, end - q - 1);
            }

            auto end = q;
            while (end < tag.size() && ! std::isspace ((unsigned char) tag[end]) && tag[end] != '>' && tag[end] != '/')
                ++end;

            return tag.substr (q, end - q);
        }

        return {};
    }

    juce::String fetchText (const juce::String& url, int timeoutMs = 15000)
    {
        int status = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (timeoutMs)
                        .withNumRedirectsToFollow (8)
                        .withStatusCode (&status)
                        .withExtraHeaders ("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                           "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36\r\n"
                                           "Accept-Language: es-AR,es;q=0.9,en;q=0.8\r\n");

        if (auto in = juce::URL::createWithoutParsing (url).createInputStream (opts))
            if (status < 400)
                return in->readEntireStreamAsString();

        return {};
    }

    juce::String firstPathSegment (const juce::String& url)
    {
        return pathOf (url).trimCharactersAtStart ("/").upToFirstOccurrenceOf ("/", false, false);
    }

    juce::String splitArtistFromTitle (juce::String& title)
    {
        // Patrones típicos de og:title: "Tema - Song by Artista", "Tema by Artista", "Tema - Canción de Artista"
        for (auto sep : { " - Song by ", " - Single by ", " - Canción de ", " by " })
        {
            auto s = juce::String::fromUTF8 (sep);
            if (title.containsIgnoreCase (s))
            {
                auto artist = title.fromLastOccurrenceOf (s, false, true).trim();
                title = title.upToLastOccurrenceOf (s, false, true).trim();
                return artist;
            }
        }
        return {};
    }
}

//==============================================================================
juce::String TrackQuery::searchText() const
{
    return (artist + " " + title).trim();
}

juce::String TrackQuery::displayName() const
{
    return artist.isNotEmpty() ? artist + " - " + title : title;
}

//==============================================================================
juce::String normaliseUrl (const juce::String& input)
{
    auto url = extractUrlToken (input).trim().trimCharactersAtEnd (".,;");

    if (! url.containsIgnoreCase ("://"))
        url = "https://" + url;

    // Va como argumento a yt-dlp: nada de comillas sueltas.
    return url.replace ("\"", "%22");
}

LinkKind classify (const juce::String& raw, juce::String& platform)
{
    auto input = raw.trim();
    auto token = extractUrlToken (input);

    const bool hasScheme = token.startsWithIgnoreCase ("http://") || token.startsWithIgnoreCase ("https://")
                        || token.startsWithIgnoreCase ("file://");
    const bool looksLikeDomain = ! input.containsAnyOf (" \t") && input.containsChar ('.') && input.containsChar ('/');

    if (! hasScheme && ! looksLikeDomain)
    {
        platform = TR ("Búsqueda");
        return LinkKind::textSearch;
    }

    auto url = normaliseUrl (input);

    if (url.startsWithIgnoreCase ("file://"))
    {
        platform = "Archivo";
        return LinkKind::direct;
    }

    auto host = hostOf (url);
    auto path = pathOf (url).toLowerCase();

    if (hostIs (host, "spotify.com") || hostIs (host, "spotify.link") || hostIs (host, "spoti.fi"))
        { platform = "Spotify"; return LinkKind::streaming; }
    if (hostIs (host, "music.apple.com") || hostIs (host, "itunes.apple.com"))
        { platform = "Apple Music"; return LinkKind::streaming; }
    if (hostIs (host, "deezer.com") || hostIs (host, "deezer.page.link") || hostIs (host, "dzr.page.link"))
        { platform = "Deezer"; return LinkKind::streaming; }
    if (hostIs (host, "tidal.com"))
        { platform = "TIDAL"; return LinkKind::streaming; }
    if (host.startsWith ("music.amazon.") || (host.contains ("amazon.") && path.contains ("/music")))
        { platform = "Amazon Music"; return LinkKind::streaming; }

    struct Named { const char* domain; const char* name; };
    static const Named named[] = {
        { "music.youtube.com", "YouTube Music" },
        { "youtube.com", "YouTube" }, { "youtu.be", "YouTube" }, { "youtube-nocookie.com", "YouTube" },
        { "soundcloud.com", "SoundCloud" }, { "bandcamp.com", "Bandcamp" }, { "vimeo.com", "Vimeo" },
        { "instagram.com", "Instagram" }, { "tiktok.com", "TikTok" }, { "facebook.com", "Facebook" },
        { "fb.watch", "Facebook" }, { "x.com", "X" }, { "twitter.com", "X" }, { "mixcloud.com", "Mixcloud" },
        { "audiomack.com", "Audiomack" }, { "drive.google.com", "Google Drive" }, { "dailymotion.com", "Dailymotion" },
        { "twitch.tv", "Twitch" }
    };

    platform = host.startsWith ("www.") ? host.substring (4) : host;

    for (auto& n : named)
        if (hostIs (host, n.domain))
            { platform = n.name; break; }

    return LinkKind::direct;
}

juce::String buildYTMusicSearchUrl (const juce::String& text)
{
    return "https://music.youtube.com/search?q=" + juce::URL::addEscapeChars (text.trim(), true) + "#songs";
}

//==============================================================================
juce::String decodeEntities (const juce::String& s)
{
    if (! s.containsChar ('&'))
        return s;

    juce::String out;
    out.preallocateBytes (s.getNumBytesAsUTF8());

    auto p = s.getCharPointer();

    while (! p.isEmpty())
    {
        auto c = p.getAndAdvance();

        if (c != '&')
        {
            out << juce::String::charToString (c);
            continue;
        }

        juce::String ent;
        auto q = p;
        bool closed = false;

        for (int i = 0; i < 10 && ! q.isEmpty(); ++i)
        {
            auto d = q.getAndAdvance();
            if (d == ';') { closed = true; break; }
            ent << juce::String::charToString (d);
        }

        juce::juce_wchar rep = 0;

        if (closed)
        {
            if      (ent == "amp")  rep = '&';
            else if (ent == "quot") rep = '"';
            else if (ent == "apos") rep = '\'';
            else if (ent == "lt")   rep = '<';
            else if (ent == "gt")   rep = '>';
            else if (ent == "nbsp") rep = ' ';
            else if (ent.startsWithChar ('#'))
                rep = (ent[1] == 'x' || ent[1] == 'X') ? (juce::juce_wchar) ent.substring (2).getHexValue32()
                                                       : (juce::juce_wchar) ent.substring (1).getIntValue();
        }

        if (rep > 0)
        {
            out << juce::String::charToString (rep);
            p = q;
        }
        else
        {
            out << "&";
        }
    }

    return out;
}

juce::String getMetaContent (const std::string& html, const std::string& key)
{
    const auto lower = toLowerAscii (html);
    const auto keyL  = toLowerAscii (key);
    size_t pos = 0;

    while ((pos = lower.find ("<meta", pos)) != std::string::npos)
    {
        const auto end = lower.find ('>', pos);
        if (end == std::string::npos)
            break;

        const auto tag  = html.substr (pos, end - pos + 1);
        const auto tagL = lower.substr (pos, end - pos + 1);

        auto which = attr (tag, tagL, "property");
        if (which.empty()) which = attr (tag, tagL, "name");
        if (which.empty()) which = attr (tag, tagL, "itemprop");

        if (toLowerAscii (which) == keyL)
            return decodeEntities (juce::String::fromUTF8 (attr (tag, tagL, "content").c_str())).trim();

        pos = end + 1;
    }

    return {};
}

juce::String getTitleTag (const std::string& html)
{
    const auto lower = toLowerAscii (html);
    auto start = lower.find ("<title");
    if (start == std::string::npos) return {};
    start = lower.find ('>', start);
    if (start == std::string::npos) return {};
    const auto end = lower.find ("</title>", start);
    if (end == std::string::npos) return {};

    return decodeEntities (juce::String::fromUTF8 (html.substr (start + 1, end - start - 1).c_str())).trim();
}

juce::String cleanTitle (juce::String s)
{
    // Marcas invisibles de dirección de texto (Apple las mete al principio).
    for (auto ch : { 0x200e, 0x200f, 0x202a, 0x202b, 0x202c, 0x202d, 0x202e, 0xfeff })
        s = s.removeCharacters (juce::String::charToString ((juce::juce_wchar) ch));

    s = s.replaceCharacter ((juce::juce_wchar) 0xa0, ' ').trim();

    static const char* suffixes[] = {
        " | Spotify", " - Spotify", " on Spotify", " en Spotify",
        " on Apple Music", " en Apple Music", " - Apple Music", " | Apple Music",
        " on TIDAL", " en TIDAL", " | TIDAL", " - TIDAL",
        " on Deezer", " en Deezer", " | Deezer", " - Deezer",
        " on Amazon Music", " en Amazon Music", " | Amazon Music", " - Amazon Music", " - Amazon.com"
    };

    for (bool changed = true; changed;)
    {
        changed = false;
        for (auto suf : suffixes)
        {
            juce::String sx (suf);
            if (s.endsWithIgnoreCase (sx))
            {
                s = s.dropLastCharacters (sx.length()).trim();
                changed = true;
            }
        }
    }

    if (s.startsWithIgnoreCase ("Listen to "))
        s = s.substring (10);

    while (s.contains ("  "))
        s = s.replace ("  ", " ");

    return s.trim();
}

TrackQuery parseStreamingPage (const juce::String& platform, const std::string& html)
{
    TrackQuery q;

    const auto ogTitle  = getMetaContent (html, "og:title");
    const auto twTitle  = getMetaContent (html, "twitter:title");
    const auto ogDesc   = getMetaContent (html, "og:description");
    const auto musician = getMetaContent (html, "music:musician_description");
    const auto titleTag = getTitleTag (html);

    q.durationSec = getMetaContent (html, "music:duration").getDoubleValue();

    if (platform == "Spotify")
    {
        q.title  = cleanTitle (ogTitle.isNotEmpty() ? ogTitle : twTitle);
        q.artist = musician;

        // <title>Tema - song and lyrics by Artista | Spotify</title>
        if (q.artist.isEmpty() && titleTag.containsIgnoreCase (" by "))
            q.artist = titleTag.fromLastOccurrenceOf (" by ", false, true).upToFirstOccurrenceOf (" | ", false, false).trim();

        // og:description: "Artista · Disco · Song · 2020"
        if (q.artist.isEmpty())
        {
            auto parts = juce::StringArray::fromTokens (ogDesc, TR ("·"), "");
            parts.trim();
            parts.removeEmptyStrings();
            if (parts.size() >= 3)
                q.artist = parts[0];
        }

        if (q.title.isEmpty() && titleTag.isNotEmpty())
            q.title = cleanTitle (titleTag.upToFirstOccurrenceOf (" - song", false, true)
                                          .upToFirstOccurrenceOf (" | ", false, false));
    }
    else
    {
        q.title  = cleanTitle (ogTitle.isNotEmpty() ? ogTitle : (twTitle.isNotEmpty() ? twTitle : titleTag));
        q.artist = musician;

        auto other = splitArtistFromTitle (q.title);
        if (q.artist.isEmpty())
            q.artist = other;
    }

    // Spotify a veces pone varios artistas separados por coma: con el primero alcanza para buscar.
    q.artist = q.artist.upToFirstOccurrenceOf (", ", false, false).trim();
    return q;
}

juce::String appleTrackId (const juce::String& url)
{
    auto i = queryParam (url, "i");
    if (isDigits (i))
        return i;

    auto path = pathOf (url);
    if (path.containsIgnoreCase ("/song/"))
    {
        auto last = path.trimCharactersAtEnd ("/").fromLastOccurrenceOf ("/", false, false);
        if (isDigits (last))
            return last;
    }

    return {};
}

juce::String deezerTrackId (const juce::String& text)
{
    auto idx = text.indexOfIgnoreCase ("/track/");
    if (idx < 0)
        return {};

    auto rest = text.substring (idx + 7);
    juce::String id;
    for (auto p = rest.getCharPointer(); ! p.isEmpty(); ++p)
    {
        if (*p < '0' || *p > '9') break;
        id << juce::String::charToString (*p);
    }
    return id;
}

//==============================================================================
TrackQuery resolveStreaming (const juce::String& url, const juce::String& platform)
{
    TrackQuery q;
    const auto path = pathOf (url).toLowerCase();

    const bool looksLikeCollection = path.contains ("/album/") || path.contains ("/playlist/")
                                  || path.contains ("/artist/") || path.contains ("/albums/")
                                  || path.contains ("/playlists/");

    if (platform == "Spotify")
    {
        if (path.contains ("/episode/") || path.contains ("/show/"))
            { q.error = TR ("Los podcasts de Spotify no se pueden bajar."); return q; }
        if (looksLikeCollection)
            { q.error = TR ("Pegá el link de un tema, no de un disco, playlist o artista."); return q; }
    }

    if (platform == "Apple Music")
    {
        auto id = appleTrackId (url);

        if (id.isNotEmpty())
        {
            auto country = firstPathSegment (url);
            if (country.length() != 2) country = "us";

            auto json = juce::JSON::parse (fetchText ("https://itunes.apple.com/lookup?id=" + id + "&country=" + country));
            auto resultsVar = json.getProperty ("results", {});

            if (auto* results = resultsVar.getArray(); results != nullptr && ! results->isEmpty())
            {
                auto& r = results->getReference (0);
                q.title = r.getProperty ("trackName", {}).toString();
                q.artist = r.getProperty ("artistName", {}).toString();
                q.durationSec = (double) r.getProperty ("trackTimeMillis", 0) / 1000.0;

                if (q.title.isNotEmpty())
                    return q;
            }
        }
        else if (looksLikeCollection)
        {
            q.error = TR ("Pegá el link de un tema, no del disco (en Apple Music: ··· en el tema › Compartir › Copiar link).");
            return q;
        }
    }

    if (platform == "Deezer")
    {
        auto id = deezerTrackId (url);

        if (id.isEmpty() && ! looksLikeCollection)
        {
            // Link corto (link.deezer.com / deezer.page.link): la página final trae el link largo.
            auto html = fetchText (url).toStdString();
            id = deezerTrackId (getMetaContent (html, "og:url"));
            if (id.isEmpty())
                id = deezerTrackId (juce::String::fromUTF8 (html.c_str()));
        }

        if (id.isNotEmpty())
        {
            auto json = juce::JSON::parse (fetchText ("https://api.deezer.com/track/" + id));
            q.title = json.getProperty ("title", {}).toString();
            q.artist = json.getProperty ("artist", {}).getProperty ("name", {}).toString();
            q.durationSec = (double) json.getProperty ("duration", 0);

            if (q.title.isNotEmpty())
                return q;
        }
        else if (looksLikeCollection)
        {
            q.error = TR ("Pegá el link de un tema, no de un disco o playlist.");
            return q;
        }
    }

    if ((platform == "TIDAL" || platform == "Amazon Music") && looksLikeCollection && ! url.containsIgnoreCase ("trackAsin"))
    {
        q.error = TR ("Pegá el link de un tema, no de un disco o playlist.");
        return q;
    }

    // Genérico: leer la página y sacar los datos de las etiquetas meta.
    const auto html = fetchText (url).toStdString();

    if (html.empty())
    {
        q.error = TR ("No pude abrir el link de ") + platform + TR (". Revisá la conexión.");
        return q;
    }

    q = parseStreamingPage (platform, html);

    if (q.title.isEmpty() && platform == "Spotify")
    {
        auto json = juce::JSON::parse (fetchText ("https://open.spotify.com/oembed?url=" + juce::URL::addEscapeChars (url, true)));
        q.title = json.getProperty ("title", {}).toString();
    }

    if (q.title.isEmpty())
        q.error = TR ("No pude leer qué tema es. Probá escribiendo artista y tema.");

    return q;
}

//==============================================================================
juce::String makeNiceName (const juce::String& rawTitle, const juce::String& rawArtist)
{
    auto t = rawTitle.trim();

    static const char* junk[] = {
        "(official music video)", "(official video)", "(official audio)", "(official lyric video)",
        "(official visualizer)", "(official hd video)", "(lyric video)", "(lyrics video)", "(lyrics)",
        "(visualizer)", "(audio oficial)", "(video oficial)", "(videoclip oficial)", "(letra)",
        "[official music video]", "[official video]", "[official audio]", "[audio oficial]",
        "[video oficial]", "(audio)", "[audio]", "(hd)", "[hd]", "(hq)", "[hq]", "(4k)", "[4k]"
    };

    for (auto j : junk)
        t = t.replace (juce::String::fromUTF8 (j), "", true);

    while (t.contains ("  "))
        t = t.replace ("  ", " ");

    t = t.trim().trimCharactersAtEnd (" -|").trim();

    auto artist = rawArtist.trim();
    if (artist.endsWithIgnoreCase (" - Topic"))
        artist = artist.dropLastCharacters (8).trim();
    if (artist == "NA")
        artist = {};

    if (t.isEmpty() || t == "NA")
        t = "audio";

    if (artist.isNotEmpty() && ! t.containsIgnoreCase (artist))
        t = artist + " - " + t;

    return t;
}
}
