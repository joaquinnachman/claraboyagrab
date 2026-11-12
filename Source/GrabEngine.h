#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <deque>
#include <functional>
#include <vector>

#include "Common.h"

namespace grab
{
    struct Job
    {
        enum class State { queued, resolving, downloading, converting, done, failed };

        int id = 0;
        juce::String input;        // el link a bajar
        juce::String titleHint, artistHint;   // si vino del buscador: nombre prolijo del tema
        juce::String platform;     // "YouTube", "Spotify", "Búsqueda"…
        juce::String title;        // lo que se muestra en la lista
        juce::String detail;       // segunda línea (estado, error, datos del WAV)
        juce::String warning;      // ej.: la duración no coincide con el original
        State state = State::queued;
        float progress = -1.0f;    // < 0: sin porcentaje
        juce::File result;

        bool isActive() const noexcept { return state == State::queued || state == State::resolving
                                             || state == State::downloading || state == State::converting; }
    };

    /** Lo que ya se bajó. Vive aparte de los archivos: limpiar la lista no borra nada del disco. */
    struct HistoryItem
    {
        juce::File file;
        juce::String title, detail;
        juce::int64 time = 0;       // ms desde 1970
    };

    struct ToolStatus
    {
        bool installed = false;
        bool busy = false;
        bool lastFailed = false;
        float progress = -1.0f;
        juce::String text;       // qué está haciendo o cómo salió lo último
        juce::String version;    // versión de yt-dlp
    };

    /** Cola de descargas en un hilo propio. Vive en el procesador, así que cerrar la
        ventana del plugin no corta lo que se está bajando. */
    class Engine : private juce::Thread
    {
    public:
        Engine();
        ~Engine() override;

        void setSampleRate (double sr) noexcept       { if (sr > 0.0) sampleRate.store (sr); }
        double getTargetSampleRate() const;

        /** App suelta: no hay sesión, la frecuencia del WAV se elige en AJUSTES. */
        bool followsHostRate() const noexcept          { return sampleRate.load() > 0.0; }
        double getAppSampleRate() const;
        void setAppSampleRate (double);

        juce::File getOutputFolder() const;
        void setOutputFolder (const juce::File&);

        bool getUseFirefoxCookies() const;
        void setUseFirefoxCookies (bool);

        void enqueueDownload (const juce::String& url, const juce::String& titleHint = {},
                              const juce::String& artistHint = {});
        void retry (int jobId);
        void enqueueInstallTools (bool forceAll);
        void enqueueUpdateYtdlp();
        void cancel (int jobId);
        void dismiss (int jobId);

        /** Vacía la lista (historial + errores). Los WAV quedan en la carpeta. */
        void clearList();
        void forget (const juce::File&);

        std::vector<Job> getJobs() const;
        std::vector<HistoryItem> getHistory() const;
        ToolStatus getToolStatus() const;
        juce::uint32 getRevision() const noexcept { return revision.load(); }

        /** Error de yt-dlp en castellano y sin tecnicismos. */
        static juce::String friendlyError (const juce::String& raw);

        juce::StringArray getExtraYtdlpArgs() const;

        // Para el programa de pruebas.
        void setExtraYtdlpArgs (const juce::StringArray&);
        bool waitUntilIdle (int timeoutMs);

    private:
        void run() override;
        void push (std::function<void()> task);
        void bump() noexcept { ++revision; }

        void doDownload (int jobId);
        void doInstall (bool forceAll);
        void doUpdate();
        void refreshVersion();
        bool installYtdlp (float p0, float p1, juce::String& error);
        juce::String latestYtdlpVersion();
        void maintainYtdlp();

        bool downloadUrlToFile (const juce::String& url, const juce::File& dest, const juce::String& label,
                                float p0, float p1, juce::String& error);
        bool extractFromZip (const juce::File& zip, const juce::StringArray& names, const juce::File& destDir,
                             juce::String& error);
        int runProcess (const juce::StringArray& args, int jobId, const std::function<void (const juce::String&)>& onLine);

        void updateJob (int jobId, const std::function<void (Job&)>& fn);
        Job getJob (int jobId) const;
        bool isCancelled (int jobId) const;
        void fail (int jobId, const juce::String& message);
        void setTools (const std::function<void (ToolStatus&)>& fn);


        static juce::File historyFile();
        void loadHistory();
        void saveHistory();   // llamar con lock tomado

        mutable juce::CriticalSection lock;
        std::deque<std::function<void()>> tasks;
        std::vector<Job> jobs;
        std::vector<HistoryItem> history;
        std::vector<int> cancelled;
        ToolStatus tools;
        int nextJobId = 1;
        bool taskRunning = false;

        std::atomic<double> sampleRate { 0.0 };
        std::atomic<juce::uint32> revision { 1 };

        juce::CriticalSection procLock;
        juce::ChildProcess* currentProc = nullptr;
        int currentProcJob = 0;

        mutable juce::CriticalSection settingsLock;
        mutable juce::ApplicationProperties props;
        juce::StringArray extraArgs;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
    };
}
