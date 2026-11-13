#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "Common.h"

/*  Preview: escuchar ~30 s de una opción antes de bajarla, rápido.

    1. yt-dlp solo resuelve el link del audio (con un cliente de YouTube que no necesita
       JavaScript; si falla, el de siempre). Ese link se guarda unas horas y se pide por
       adelantado para las primeras opciones y la que está bajo el mouse.
    2. Si el tema tiene publicidad al principio, YouTube no deja usar el link hasta que
       "pasaría" el anuncio (available_at): se espera ese tiempo, y como el link se pidió
       por adelantado, normalmente la espera ya pasó cuando tocás play.
    3. FFmpeg arranca directo en el 30 % del tema y manda el audio decodificado al reproductor
       a medida que llega: suena apenas hay medio segundo, sin esperar los 30 s.
    4. Plan B: si por ese camino no llega audio, yt-dlp baja solo el fragmento (como la 1.3).
    5. El volumen se empareja con una ganancia liviana (por RMS) mientras suena.

    Lo que pasa queda anotado en %APPDATA%\Claraboya Grab\preview-log.txt.

    Suena sumado a la salida del canal del plugin, o por la salida de Windows en la app suelta.
*/
namespace grab
{
    class Engine;

    class PreviewPlayer : private juce::Thread
    {
    public:
        enum class State { idle, loading, playing, failed };

        struct Status
        {
            juce::String videoId, title;
            State state = State::idle;
            float progress = 0.0f;          // 0..1 dentro del fragmento
            double startSec = 0.0, lengthSec = 0.0;
            juce::String error;
            bool stalled = false;           // "suena" pero nadie lo está reproduciendo (canal dormido)
            juce::int64 waitUntilMs = 0;    // YouTube pide esperar hasta esta hora (ms desde 1970)
            bool slowPath = false;          // usando el plan B
        };

        /** Link del audio ya resuelto por yt-dlp. */
        struct Stream
        {
            juce::String url, userAgent;
            juce::StringPairArray headers;  // los que usa yt-dlp para ese link
            juce::int64 resolvedAt = 0;     // ms desde 1970
            juce::int64 availableAt = 0;    // ms desde 1970: antes de esto YouTube rechaza el link (0 = ya)
            bool fastClient = false;        // resuelto con el cliente sin JavaScript

            /** Hasta cuándo hay que esperar (0 si no hace falta). */
            juce::int64 mustWaitUntil() const noexcept
            {
                return availableAt > resolvedAt ? availableAt + 1000 : 0;   // +1 s: available_at viene en segundos enteros
            }

            /** Los headers como los pasa yt-dlp a FFmpeg ("Clave: valor\r\n" cada uno). */
            juce::String headerBlock() const;
        };

        explicit PreviewPlayer (Engine&);
        ~PreviewPlayer() override;

        void play (const juce::String& videoId, const juce::String& url, const juce::String& title, double durationSec);
        void stop();

        /** Resolver el link por adelantado (no suena nada). Este va primero en la fila. */
        void prefetch (const juce::String& videoId, const juce::String& url);
        /** Varias opciones por adelantado, en orden (reemplaza las que todavía no se resolvieron). */
        void prefetchMany (const std::vector<std::pair<juce::String, juce::String>>& idsAndUrls);
        bool isResolved (const juce::String& videoId) const;

        Status getStatus() const;
        juce::uint32 getRevision() const noexcept { return revision.load(); }

        /** Frecuencia a la que se reproduce (la de la sesión o la de la placa). */
        void setPlaybackRate (double sr) noexcept { if (sr > 0.0) playbackRate.store (sr); }

        /** La app suelta abre la salida de audio recién cuando hace falta. Devuelve un error o vacío. */
        std::function<juce::String()> ensureOutput;

        /** Hilo de audio: suma el preview a lo que ya hay en el buffer. Nunca espera. */
        void render (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept;

        /** Cuánto tardó en sonar el último preview (ms), para medir. */
        int getLastTimeToSoundMs() const noexcept { return lastTimeToSound.load(); }

        /** Solo para capturas: muestra un estado armado a mano. */
        void setStatusForTesting (const Status&);

        static constexpr double clipSeconds  = 30.0;
        static constexpr double startBuffer  = 0.35;   // segundos en memoria antes de empezar a sonar
        static double chooseStart (double durationSec);
        static float gainForRms (float rms) noexcept;
        static bool parseResolveOutput (const juce::StringArray& lines, Stream& out);
        static juce::String formatSeconds (double seconds);   // "88.000", siempre con punto
        static juce::File logFile();

        /** Solo para pruebas: usar directo el plan B. */
        void setForceSlowPathForTesting (bool b) noexcept { forceSlowPath = b; }
        int getPathLevel() const noexcept { return pathLevel.load(); }

    private:
        struct Request
        {
            juce::String videoId, url, title;
            double duration = 0.0;
            int generation = 0;
        };

        struct Clip
        {
            juce::AudioBuffer<float> audio;          // capacidad para todo el fragmento
            float* data[2] {};                       // punteros de escritura (se toman antes de compartirlo)
            double sampleRate = 48000.0;
            int planned = 0;                         // cuadros del fragmento completo
            juce::String key;
            std::atomic<int> available { 0 };        // cuadros listos (escribe el cargador, lee el audio)
            std::atomic<bool> complete { false };
            std::atomic<float> targetGain { 1.0f };
            // Solo los toca el hilo de audio (o alguien con clipLock tomado):
            double pos = 0.0;
            float gain = 1.0f, env = 0.0f;
        };

        void run() override;
        void handlePlay (const Request&);
        void handlePrefetch (const juce::String& videoId, const juce::String& url);
        bool resolve (const juce::String& videoId, const juce::String& url, bool fastClient, Stream& out,
                      juce::String& error, const std::function<bool()>& isStale);
        bool waitUntilAvailable (const Request&, juce::int64 untilMs);
        bool streamInto (const Request&, const juce::String& input, const juce::String& headerBlock,
                         double startSec, const std::shared_ptr<Clip>&, juce::String& error);
        juce::String probeError (const juce::String& input, const juce::String& headerBlock, double startSec,
                                 const std::function<bool()>& isStale);
        bool loadSection (const Request&, const std::shared_ptr<Clip>&, juce::String& error);
        std::shared_ptr<Clip> makeClip (const juce::String& key, double rate, double len) const;
        static void log (const juce::String&);
        void startPlaying (const std::shared_ptr<Clip>&, int generation);
        int runTool (const juce::StringArray& args, const std::function<bool()>& isStale, int timeoutMs,
                     const std::function<void (const juce::String&)>& onLine);
        void killCurrentProcess (const juce::String& keepResolveOf = {}, bool keepPrefetch = false);
        void forgetStream (const juce::String& videoId);
        bool isStale (int generation) const { return threadShouldExit() || generation != latestGen.load(); }
        void setStatus (int generation, const std::function<void (Status&)>&);
        std::shared_ptr<Clip> findCachedClip (const juce::String& key);
        void rememberClip (const std::shared_ptr<Clip>&);
        bool cachedStream (const juce::String& videoId, Stream& out) const;
        static juce::String clipKey (const juce::String& videoId, double start, double rate);

        Engine& engine;

        mutable juce::CriticalSection lock;
        Request pending;
        bool hasPending = false;
        std::vector<std::pair<juce::String, juce::String>> prefetchQueue;   // id, link
        Status status;
        std::atomic<int> latestGen { 0 };
        std::atomic<juce::uint32> revision { 1 };
        std::atomic<double> playbackRate { 0.0 };
        std::atomic<juce::uint32> playRequestedMs { 0 };
        std::atomic<int> lastTimeToSound { -1 };

        // Procesos en marcha (para cortarlos al toque)
        juce::CriticalSection procLock;
        juce::ChildProcess* currentProc = nullptr;
        juce::String inFlightResolveId;
        bool inFlightIsPrefetch = false;
        std::atomic<juce::uint32> killSerial { 0 };

        // Links resueltos y fragmentos recientes
        mutable juce::CriticalSection cacheLock;
        std::map<juce::String, Stream> streams;
        std::vector<std::shared_ptr<Clip>> recentClips;

        // Lo que suena: el hilo de audio solo lo lee con try-lock (nunca espera).
        juce::SpinLock clipLock;
        std::shared_ptr<Clip> current;
        std::atomic<bool> playing { false }, stopRequested { false };
        std::atomic<float> progress { 0.0f };
        std::atomic<juce::uint32> lastRenderMs { 0 };
        std::atomic<juce::uint32> playStartedMs { 0 };

        // Camino que se usa en esta sesión: 0 = link con el cliente rápido, 1 = link con el cliente
        // normal, 2 = plan B directo. Baja un escalón cuando uno falla y el plan B sí anda.
        std::atomic<int> pathLevel { 0 };
        std::atomic<bool> forceSlowPath { false };

        bool hasTestStatus = false;
        Status testStatus;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPlayer)
    };
}
