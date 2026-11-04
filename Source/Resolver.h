#pragma once

#include "Common.h"

/*  Qué hacer con lo que pega el usuario:

    - Link "directo" (YouTube, SoundCloud, Bandcamp, Vimeo, Instagram, TikTok…):
      lo baja yt-dlp tal cual.
    - Link de streaming con DRM (Spotify, Apple Music, Deezer, TIDAL, Amazon Music):
      el audio viene cifrado y no se baja de ahí. Leemos qué tema es (título, artista,
      duración) y lo buscamos en la sección "Canciones" de YouTube Music.
    - Texto suelto ("charly garcía rezo por vos"): búsqueda directa en YouTube Music.
*/
namespace grab
{
    enum class LinkKind { direct, streaming, textSearch };

    struct TrackQuery
    {
        juce::String title, artist;
        double durationSec = 0.0;
        juce::String error;

        juce::String searchText() const;
        juce::String displayName() const;
    };

    /** Clasifica la entrada y devuelve el nombre lindo de la plataforma. */
    LinkKind classify (const juce::String& input, juce::String& platformName);

    /** Agrega https:// si falta y escapa comillas. */
    juce::String normaliseUrl (const juce::String& input);

    juce::String buildYTMusicSearchUrl (const juce::String& text);

    /** Lee título/artista/duración de un link de streaming (bloquea: usar en un hilo). */
    TrackQuery resolveStreaming (const juce::String& url, const juce::String& platform);

    //==============================================================================
    // Funciones puras (se prueban sin red)
    juce::String decodeEntities (const juce::String& s);
    juce::String getMetaContent (const std::string& html, const std::string& key);
    juce::String getTitleTag (const std::string& html);
    juce::String cleanTitle (juce::String s);
    TrackQuery parseStreamingPage (const juce::String& platform, const std::string& html);
    juce::String appleTrackId (const juce::String& url);
    juce::String spotifyTrackId (const juce::String& text);
    TrackQuery parseSpotifyEmbed (const std::string& html);
    juce::String deezerTrackId (const juce::String& text);

    /** "Artista - Tema" prolijo para el nombre del WAV. */
    juce::String makeNiceName (const juce::String& title, const juce::String& artist);
}
