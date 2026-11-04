#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <vector>

#include "Common.h"
#include "Resolver.h"

/*  Buscador con opciones: en vez de bajar el primer resultado, trae varios
    (título, artista, disco, duración, tapa) para que elijas cuál bajar.

    Fuente principal: YouTube Music (canciones o videos).
    Respaldo: la búsqueda de YouTube de yt-dlp, por si YouTube Music no responde.
*/
namespace grab
{
    enum class SearchKind { songs, videos };

    struct Candidate
    {
        juce::String videoId, title, artist, extra, thumbUrl;   // extra: disco (canciones) o vistas (videos)
        double duration = 0.0;
        bool isVideo = false;
        bool durationMatch = false;   // misma duración que el tema del link (Spotify, etc.)
        float score = 0.0f;

        juce::String url() const;
        juce::String niceName() const { return makeNiceName (title, artist); }
    };

    //==============================================================================
    // Funciones puras (se prueban sin red)
    std::vector<Candidate> parseYTMusicResponse (const juce::var& json, SearchKind);
    std::vector<Candidate> parseYtdlpSearchLines (const juce::StringArray& jsonLines);
    void rankCandidates (std::vector<Candidate>&, const TrackQuery& expected);
    juce::String normaliseForMatch (const juce::String&);
    double parseDurationText (const juce::String&);

    // Con red (bloquean: usar en un hilo)
    std::vector<Candidate> searchYTMusic (const juce::String& query, SearchKind, juce::String& error,
                                          juce::String* rawResponse = nullptr);
    std::vector<Candidate> searchWithYtdlp (const juce::String& query, SearchKind, juce::String& error,
                                            const std::function<bool()>& shouldStop);

    //==============================================================================
    /** Hilo de búsqueda: siempre gana el último pedido (para buscar mientras escribís). */
    class Searcher : private juce::Thread
    {
    public:
        struct Request
        {
            juce::String text;            // texto a buscar, o el link de Spotify/Apple/etc.
            SearchKind kind = SearchKind::songs;
            bool resolveLink = false;     // primero leer qué tema es el link
            TrackQuery expected;          // datos del tema original (para ordenar y marcar coincidencias)
            juce::String platform;
        };

        struct Result
        {
            enum class State { idle, resolving, searching, done, failed };

            int generation = 0;
            State state = State::idle;
            Request request;
            juce::String query;           // lo que efectivamente se buscó
            TrackQuery source;            // tema leído del link (vacío en búsquedas de texto)
            std::vector<Candidate> items;
            juce::String error;
            bool usedFallback = false;

            bool hasSource() const { return source.title.isNotEmpty(); }
        };

        Searcher();
        ~Searcher() override;

        int request (const Request&);
        void clear();
        Result getResult() const;
        juce::uint32 getRevision() const noexcept { return revision.load(); }

        /** Solo para capturas/pruebas: muestra un resultado armado a mano. */
        void injectForTesting (Result);

    private:
        void run() override;
        void publish (int gen, const std::function<void (Result&)>&);

        mutable juce::CriticalSection lock;
        Request pending;
        bool hasPending = false;
        Result result;
        std::atomic<int> latestGen { 0 };
        std::atomic<juce::uint32> revision { 1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Searcher)
    };
}
