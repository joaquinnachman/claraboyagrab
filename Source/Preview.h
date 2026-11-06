#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>

#include "Common.h"

/*  Preview: escuchar ~30 s de una opción antes de bajarla.

    Baja solo ese fragmento con yt-dlp (--download-sections), lo pasa por FFmpeg
    (nivelado en loudness para que todas las opciones suenen parejas) y lo reproduce:
    - en el plugin, sumado a la salida del canal donde está insertado;
    - en la app suelta, por la salida de audio de Windows.
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
        };

        explicit PreviewPlayer (Engine&);
        ~PreviewPlayer() override;

        void play (const juce::String& videoId, const juce::String& url, const juce::String& title, double durationSec);
        void stop();

        Status getStatus() const;
        juce::uint32 getRevision() const noexcept { return revision.load(); }

        /** Frecuencia a la que se reproduce (la de la sesión o la de la placa). */
        void setPlaybackRate (double sr) noexcept { if (sr > 0.0) playbackRate.store (sr); }

        /** La app suelta abre la salida de audio recién cuando hace falta. Devuelve un error o vacío. */
        std::function<juce::String()> ensureOutput;

        /** Hilo de audio: suma el preview a lo que ya hay en el buffer. */
        void render (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept;

        /** Solo para capturas: muestra un estado armado a mano. */
        void setStatusForTesting (const Status&);

        static constexpr double clipSeconds = 30.0;
        static double chooseStart (double durationSec);
        static juce::File cacheFolder();

    private:
        struct Request
        {
            juce::String videoId, url, title;
            double duration = 0.0;
            int generation = 0;
        };

        struct Clip
        {
            juce::AudioBuffer<float> audio;
            double sampleRate = 48000.0;
            double pos = 0.0;      // solo lo toca el hilo de audio
        };

        void run() override;
        bool loadClip (const Request&, std::unique_ptr<Clip>& clip, juce::String& error);
        int runTool (const juce::StringArray& args, int generation, int timeoutMs,
                     const std::function<void (const juce::String&)>& onLine);
        bool isStale (int generation) const { return threadShouldExit() || generation != latestGen.load(); }
        void setStatus (int generation, const std::function<void (Status&)>&);

        Engine& engine;

        juce::CriticalSection procLock;
        juce::ChildProcess* currentProc = nullptr;
        void killCurrentProcess();

        bool hasTestStatus = false;
        Status testStatus;

        mutable juce::CriticalSection lock;
        Request pending;
        bool hasPending = false;
        Status status;
        std::atomic<int> latestGen { 0 };
        std::atomic<juce::uint32> revision { 1 };
        std::atomic<double> playbackRate { 0.0 };

        // Lo que suena: el hilo de audio solo lo lee con try-lock (nunca espera).
        juce::SpinLock clipLock;
        std::unique_ptr<Clip> clip;
        std::atomic<bool> playing { false };
        std::atomic<float> progress { 0.0f };
        std::atomic<juce::uint32> lastRenderMs { 0 };
        std::atomic<juce::uint32> playStartedMs { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPlayer)
    };
}
