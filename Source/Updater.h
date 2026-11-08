#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

#include "Common.h"

/*  Aviso de versión nueva.

    Cada compilación de main se publica como Release en GitHub ("build-N") y el
    programa lleva grabado su número de build. Al abrir (y cada 6 horas) se consulta
    la última Release: si es más nueva, aparece el aviso con lo que cambió y el botón
    ACTUALIZAR baja el instalador, verifica que llegó entero y lo abre.
*/
namespace grab
{
    class Updater : private juce::Thread
    {
    public:
        enum class State { idle, checking, upToDate, available, downloading, launching, failed };

        struct Release
        {
            int build = 0;
            juce::String label;              // "1.3.0 · build 12"
            juce::StringArray changes;       // novedades, una por línea
            juce::String pageUrl, installerUrl, installerDigest, zipUrl;
            juce::int64 installerSize = 0;
        };

        struct Status
        {
            State state = State::idle;
            Release latest;
            float progress = -1.0f;
            juce::String error;
            bool dismissed = false;          // "MÁS TARDE" en esta sesión
        };

        Updater();
        ~Updater() override;

        void checkNow();
        void install();
        void dismiss();

        Status getStatus() const;
        juce::uint32 getRevision() const noexcept { return revision.load(); }

        static int currentBuild() noexcept;
        static juce::String currentLabel();

        static constexpr const char* apiUrl  = "https://api.github.com/repos/joaquinnachman/claraboyagrab/releases/latest";
        static constexpr const char* pageUrl = "https://github.com/joaquinnachman/claraboyagrab/releases/latest";

        // Funciones puras / reutilizables (se prueban aparte)
        static bool parseLatest (const juce::var& json, Release& out);
        static bool download (const juce::String& url, juce::int64 expectedSize, const juce::String& digest,
                              const juce::File& dest, const std::function<void (float)>& onProgress,
                              const std::function<bool()>& shouldStop, juce::String& error);

        /** Solo para capturas/pruebas. */
        void setStatusForTesting (const Status&);

    private:
        void run() override;
        void doCheck (bool manual);
        void doInstall();
        void setStatus (const std::function<void (Status&)>&);

        mutable juce::CriticalSection lock;
        Status status;
        std::atomic<bool> checkRequested { false }, installRequested { false };
        std::atomic<juce::uint32> revision { 1 };
        juce::uint32 lastCheckMs = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Updater)
    };
}
