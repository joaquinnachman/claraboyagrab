#include "GrabEngine.h"
#include "Resolver.h"
#include "Tools.h"

namespace grab
{
namespace
{
    juce::String mbString (juce::int64 bytes)
    {
        return juce::String ((int) (bytes / (1024 * 1024))) + " MB";
    }

    juce::File absoluteOrNone (const juce::String& path)
    {
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }
}

//==============================================================================
Engine::Engine() : juce::Thread ("Claraboya Grab")
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Claraboya Grab";
    o.filenameSuffix      = ".settings";
    o.folderName          = "Claraboya Grab";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1;   // se guarda a mano, sin timers
    props.setStorageParameters (o);

    loadHistory();

    tools.installed = Tools::allInstalled();
    tools.text = tools.installed ? TR ("Herramientas listas") : TR ("Faltan las herramientas");

    startThread();

    push ([this]
    {
        // Restos de descargas cortadas (cierre de Cubase en el medio, etc.).
        auto tmpRoot = getOutputFolder().getChildFile (".claraboya-tmp");
        for (auto& d : tmpRoot.findChildFiles (juce::File::findDirectories, false))
            if (d.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::hours (12))
                d.deleteRecursively();

        if (Tools::allInstalled())
            refreshVersion();
    });

    push ([this] { maintainYtdlp(); });
}

Engine::~Engine()
{
    {
        const juce::ScopedLock sl (lock);
        tasks.clear();
        for (auto& j : jobs)
            cancelled.push_back (j.id);
    }

    signalThreadShouldExit();

    {
        const juce::ScopedLock sl (procLock);
        if (currentProc != nullptr)
            currentProc->kill();
    }

    notify();
    stopThread (10000);
}

//==============================================================================
double Engine::getTargetSampleRate() const
{
    auto sr = sampleRate.load();
    return sr > 0.0 ? sr : getAppSampleRate();
}

double Engine::getAppSampleRate() const
{
    const juce::ScopedLock sl (settingsLock);
    const auto sr = props.getUserSettings()->getDoubleValue ("appSampleRate", 48000.0);
    return sr >= 8000.0 && sr <= 384000.0 ? sr : 48000.0;
}

void Engine::setAppSampleRate (double sr)
{
    {
        const juce::ScopedLock sl (settingsLock);
        props.getUserSettings()->setValue ("appSampleRate", sr);
        props.saveIfNeeded();
    }
    bump();
}

//==============================================================================
juce::File Engine::historyFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Claraboya Grab").getChildFile ("historial.json");
}

void Engine::loadHistory()
{
    const auto f = historyFile();
    std::vector<HistoryItem> items;

    if (f.existsAsFile())
    {
        const auto parsed = juce::JSON::parse (f);   // tiene que vivir mientras se recorre

        if (auto* arr = parsed.getArray())
        {
            for (auto& v : *arr)
            {
                HistoryItem h;
                h.file   = absoluteOrNone (v.getProperty ("path", {}).toString());
                h.title  = v.getProperty ("title", {}).toString();
                h.detail = v.getProperty ("detail", {}).toString();
                h.time   = (juce::int64) v.getProperty ("time", 0);

                if (h.file.existsAsFile())
                    items.push_back (h);
            }
        }
    }
    else
    {
        // Primera vez con historial: arrancar con lo último que ya hay en la carpeta.
        auto wavs = getOutputFolder().findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.wav");
        std::sort (wavs.begin(), wavs.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

        for (int i = 0; i < juce::jmin (30, wavs.size()); ++i)
            items.push_back ({ wavs[i], wavs[i].getFileNameWithoutExtension(), {},
                               wavs[i].getLastModificationTime().toMilliseconds() });
    }

    const juce::ScopedLock sl (lock);
    history = std::move (items);
    saveHistory();
}

void Engine::saveHistory()
{
    juce::Array<juce::var> arr;

    for (auto& h : history)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", h.file.getFullPathName());
        o->setProperty ("title", h.title);
        o->setProperty ("detail", h.detail);
        o->setProperty ("time", h.time);
        arr.add (juce::var (o));
    }

    const auto f = historyFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (arr)));
}

std::vector<HistoryItem> Engine::getHistory() const
{
    const juce::ScopedLock sl (lock);
    return history;
}

void Engine::clearList()
{
    {
        const juce::ScopedLock sl (lock);
        history.clear();
        jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [] (const Job& j) { return ! j.isActive(); }), jobs.end());
        saveHistory();
    }
    bump();
}

void Engine::forget (const juce::File& file)
{
    {
        const juce::ScopedLock sl (lock);
        history.erase (std::remove_if (history.begin(), history.end(), [&] (const HistoryItem& h) { return h.file == file; }),
                       history.end());
        saveHistory();
    }
    bump();
}

juce::File Engine::getOutputFolder() const
{
    const juce::ScopedLock sl (settingsLock);
    auto path = props.getUserSettings()->getValue ("outputFolder");
    auto f = absoluteOrNone (path);

    if (f == juce::File())
        f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Claraboya Grab");

    return f;
}

void Engine::setOutputFolder (const juce::File& f)
{
    {
        const juce::ScopedLock sl (settingsLock);
        props.getUserSettings()->setValue ("outputFolder", f.getFullPathName());
        props.saveIfNeeded();
    }
    bump();
}

bool Engine::getUseFirefoxCookies() const
{
    const juce::ScopedLock sl (settingsLock);
    return props.getUserSettings()->getBoolValue ("firefoxCookies", false);
}

void Engine::setUseFirefoxCookies (bool b)
{
    {
        const juce::ScopedLock sl (settingsLock);
        props.getUserSettings()->setValue ("firefoxCookies", b);
        props.saveIfNeeded();
    }
    bump();
}

juce::StringArray Engine::getExtraYtdlpArgs() const
{
    const juce::ScopedLock sl (settingsLock);
    return extraArgs;
}

void Engine::setExtraYtdlpArgs (const juce::StringArray& a)
{
    const juce::ScopedLock sl (settingsLock);
    extraArgs = a;
}

//==============================================================================
void Engine::push (std::function<void()> task)
{
    {
        const juce::ScopedLock sl (lock);
        tasks.push_back (std::move (task));
    }
    notify();
}

void Engine::run()
{
    while (! threadShouldExit())
    {
        std::function<void()> task;

        {
            const juce::ScopedLock sl (lock);
            if (! tasks.empty())
            {
                task = std::move (tasks.front());
                tasks.pop_front();
                taskRunning = true;
            }
        }

        if (task)
        {
            task();
            const juce::ScopedLock sl (lock);
            taskRunning = false;
        }
        else
        {
            wait (300);
        }
    }
}

bool Engine::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (juce::Time::getMillisecondCounter() < end)
    {
        {
            const juce::ScopedLock sl (lock);
            if (tasks.empty() && ! taskRunning)
                return true;
        }
        juce::Thread::sleep (50);
    }

    return false;
}

//==============================================================================
void Engine::enqueueDownload (const juce::String& raw, const juce::String& titleHint, const juce::String& artistHint)
{
    auto input = raw.trim();
    if (input.isEmpty())
        return;

    int id = 0;

    {
        const juce::ScopedLock sl (lock);
        Job j;
        j.id = id = nextJobId++;
        j.input = input;
        j.titleHint = titleHint;
        j.artistHint = artistHint;
        j.title = titleHint.isNotEmpty() ? makeNiceName (titleHint, artistHint) : input;
        classify (input, j.platform);
        j.detail = j.platform + TR (" · en cola…");
        jobs.push_back (j);
    }

    bump();
    push ([this, id] { doDownload (id); });
}

void Engine::retry (int jobId)
{
    auto j = getJob (jobId);
    if (j.id == 0)
        return;

    dismiss (jobId);
    enqueueDownload (j.input, j.titleHint, j.artistHint);
}

void Engine::enqueueInstallTools (bool forceAll)
{
    {
        const juce::ScopedLock sl (lock);
        if (tools.busy) return;
        tools.busy = true;
        tools.lastFailed = false;
        tools.progress = 0.0f;
        tools.text = TR ("Preparando la instalación…");
    }
    bump();
    push ([this, forceAll] { doInstall (forceAll); });
}

void Engine::enqueueUpdateYtdlp()
{
    {
        const juce::ScopedLock sl (lock);
        if (tools.busy) return;
        tools.busy = true;
        tools.lastFailed = false;
        tools.progress = -1.0f;
        tools.text = TR ("Actualizando yt-dlp…");
    }
    bump();
    push ([this] { doUpdate(); });
}

void Engine::cancel (int jobId)
{
    {
        const juce::ScopedLock sl (lock);
        cancelled.push_back (jobId);

        for (auto it = jobs.begin(); it != jobs.end(); ++it)
        {
            if (it->id != jobId) continue;

            if (it->state == Job::State::queued)
                jobs.erase (it);
            else
                it->detail = TR ("Cancelando…");
            break;
        }
    }

    {
        const juce::ScopedLock sl (procLock);
        if (currentProc != nullptr && currentProcJob == jobId)
            currentProc->kill();
    }

    bump();
}

void Engine::dismiss (int jobId)
{
    bool active = false;

    {
        const juce::ScopedLock sl (lock);
        for (auto it = jobs.begin(); it != jobs.end(); ++it)
        {
            if (it->id != jobId) continue;
            active = it->isActive();
            if (! active) jobs.erase (it);
            break;
        }
    }

    if (active)
        cancel (jobId);

    bump();
}

std::vector<Job> Engine::getJobs() const
{
    const juce::ScopedLock sl (lock);
    return jobs;
}

ToolStatus Engine::getToolStatus() const
{
    const juce::ScopedLock sl (lock);
    return tools;
}

//==============================================================================
void Engine::updateJob (int jobId, const std::function<void (Job&)>& fn)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& j : jobs)
            if (j.id == jobId) { fn (j); break; }
    }
    bump();
}

Job Engine::getJob (int jobId) const
{
    const juce::ScopedLock sl (lock);
    for (auto& j : jobs)
        if (j.id == jobId)
            return j;
    return {};
}

bool Engine::isCancelled (int jobId) const
{
    if (jobId == 0)
        return false;

    const juce::ScopedLock sl (lock);
    return std::find (cancelled.begin(), cancelled.end(), jobId) != cancelled.end();
}

void Engine::fail (int jobId, const juce::String& message)
{
    updateJob (jobId, [&] (Job& j)
    {
        j.state = Job::State::failed;
        j.progress = -1.0f;
        j.detail = message;
    });
}

void Engine::setTools (const std::function<void (ToolStatus&)>& fn)
{
    {
        const juce::ScopedLock sl (lock);
        fn (tools);
    }
    bump();
}

//==============================================================================
int Engine::runProcess (const juce::StringArray& args, int jobId, const std::function<void (const juce::String&)>& onLine)
{
    juce::ChildProcess proc;

    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1000;

    {
        const juce::ScopedLock sl (procLock);
        currentProc = &proc;
        currentProcJob = jobId;
    }

    std::string line;
    char buffer[32];

    auto emit = [&]
    {
        if (! line.empty())
            onLine (juce::String::fromUTF8 (line.data(), (int) line.size()));
        line.clear();
    };

    int exitCode = 0;
    bool finished = false;

    for (;;)
    {
        if (threadShouldExit() || isCancelled (jobId))
        {
            proc.kill();
            break;
        }

        const auto n = proc.readProcessOutput (buffer, (int) sizeof (buffer));

        if (n <= 0)
        {
            if (! proc.isRunning())
            {
                exitCode = (int) proc.getExitCode();
                finished = true;
                break;
            }

            juce::Thread::sleep (15);
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            const char c = buffer[i];
            if (c == '\n' || c == '\r') emit();
            else                        line.push_back (c);
        }
    }

    emit();

    {
        const juce::ScopedLock sl (procLock);
        currentProc = nullptr;
        currentProcJob = 0;
    }

    // El código de salida se toma apenas termina: en Linux/Mac, volver a preguntar lo pisa con 0.
    if (! finished)
    {
        proc.waitForProcessToFinish (5000);
        exitCode = (int) proc.getExitCode();
    }

    return exitCode;
}

//==============================================================================
void Engine::doDownload (int jobId)
{
    const auto job = getJob (jobId);

    if (job.id == 0 || isCancelled (jobId))
        return;

    auto removeJob = [this, jobId]
    {
        {
            const juce::ScopedLock sl (lock);
            jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [jobId] (const Job& j) { return j.id == jobId; }), jobs.end());
        }
        bump();
    };

    if (! Tools::allInstalled())
        return fail (jobId, TR ("Faltan las herramientas: tocá INSTALAR."));

    const auto outDir = getOutputFolder();

    if (! outDir.createDirectory())
        return fail (jobId, TR ("No pude crear la carpeta de descarga: ") + outDir.getFullPathName());

    juce::String platform;

    if (classify (job.input, platform) != LinkKind::direct)
        return fail (jobId, TR ("Para Spotify, Apple Music o una búsqueda, elegí el tema en el buscador."));

    const auto label = platform;
    const auto url = normaliseUrl (job.input);

    updateJob (jobId, [&] (Job& j)
    {
        j.state = Job::State::downloading;
        j.platform = platform;
        j.detail = label + TR (" · conectando…");
    });

    const auto tmpRoot = outDir.getChildFile (".claraboya-tmp");
    const auto tmp = tmpRoot.getChildFile (juce::String (jobId) + "-"
                                           + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));

    if (! tmp.createDirectory())
        return fail (jobId, TR ("No pude escribir en la carpeta de descarga."));

    auto cleanup = [tmp, tmpRoot]
    {
        tmp.deleteRecursively();
        if (tmpRoot.isDirectory() && tmpRoot.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
            tmpRoot.deleteFile();
    };

    //==============================================================================
    // 1) yt-dlp baja el mejor audio disponible, sin recodificar.
    juce::StringArray args;
    args.add (Tools::ytdlp().getFullPathName());
    args.addArray (juce::StringArray { "--newline", "--color", "never", "--encoding", "utf-8", "--no-mtime",
                                       "--no-simulate", "-f", "bestaudio/best" });

    args.add ("--ffmpeg-location");
    args.add (Tools::ffmpeg().getParentDirectory().getFullPathName());

    if (auto deno = Tools::deno(); deno.existsAsFile())
    {
        args.add ("--js-runtimes");
        args.add ("deno:" + deno.getFullPathName());
    }

    args.add ("-o");
    args.add (tmp.getFullPathName() + juce::File::getSeparatorString() + "%(id)s.%(ext)s");
    args.add ("--print");
    args.add ("before_dl:INFO|%(title)s|%(artists.0,artist,creator,uploader,channel)s|%(duration)s|%(extractor_key)s");
    args.add ("--print");
    args.add ("after_move:FILE|%(filepath)s");
    args.add ("--progress-template");
    args.add ("download:PROG|%(progress._percent_str)s");

    args.add ("--no-playlist");

    if (getUseFirefoxCookies())
    {
        args.add ("--cookies-from-browser");
        args.add ("firefox");
    }

    {
        const juce::ScopedLock sl (settingsLock);
        args.addArray (extraArgs);
    }

    args.add ("--");   // lo que sigue es el link, nunca una opción
    args.add (url);

    juce::String lastError, printedFile, infoTitle, infoArtist;
    double duration = 0.0;
    int lastPct = -1;

    const int code = runProcess (args, jobId, [&] (const juce::String& line)
    {
        if (line.startsWith ("PROG|"))
        {
            const auto pct = line.substring (5).trim().upToFirstOccurrenceOf ("%", false, false).trim().getFloatValue();
            const auto ip = (int) pct;

            if (ip != lastPct)
            {
                lastPct = ip;
                updateJob (jobId, [&] (Job& j)
                {
                    j.state = Job::State::downloading;
                    j.progress = juce::jlimit (0.0f, 1.0f, pct / 100.0f);
                    j.detail = label + TR (" · bajando ") + juce::String (ip) + "%";
                });
            }
        }
        else if (line.startsWith ("INFO|"))
        {
            // El título puede traer "|": se corta de derecha a izquierda.
            auto rest = line.substring (5);
            rest = rest.upToLastOccurrenceOf ("|", false, false);                  // saca extractor
            duration = rest.fromLastOccurrenceOf ("|", false, false).getDoubleValue();
            rest = rest.upToLastOccurrenceOf ("|", false, false);
            infoArtist = rest.fromLastOccurrenceOf ("|", false, false).trim();
            infoTitle  = rest.upToLastOccurrenceOf ("|", false, false).trim();

            if (infoArtist == "NA") infoArtist = {};

            updateJob (jobId, [&] (Job& j)
            {
                j.state = Job::State::downloading;
                if (j.titleHint.isEmpty())
                    j.title = makeNiceName (infoTitle, infoArtist);
                j.detail = label + (duration > 0 ? TR (" · ") + formatDuration (duration) : juce::String()) + TR (" · arrancando…");
            });
        }
        else if (line.startsWith ("FILE|"))
        {
            printedFile = line.substring (5).trim();
        }
        else if (line.startsWith ("ERROR:"))
        {
            lastError = line.substring (6).trim();
        }
    });

    if (isCancelled (jobId) || threadShouldExit())
    {
        cleanup();
        return removeJob();
    }

    auto downloaded = absoluteOrNone (printedFile);

    if (! downloaded.existsAsFile())
    {
        for (auto& f : tmp.findChildFiles (juce::File::findFiles, false))
            if (! f.hasFileExtension ("part;ytdl;json;tmp"))
                { downloaded = f; break; }
    }

    if (code != 0 || ! downloaded.existsAsFile())
    {
        cleanup();

        if (code == -1000)
            return fail (jobId, TR ("No pude ejecutar yt-dlp. Probá «Reinstalar herramientas» en AJUSTES."));

        return fail (jobId, friendlyError (lastError));
    }

    //==============================================================================
    // 2) FFmpeg lo pasa a WAV 24 bit a la frecuencia de la sesión.
    const auto sr = getTargetSampleRate();

    // Nombre: el del tema elegido en el buscador, si no lo que informa la plataforma.
    const auto niceName = job.titleHint.isNotEmpty()
                        ? makeNiceName (job.titleHint, job.artistHint)
                        : makeNiceName (infoTitle.isNotEmpty() ? infoTitle : downloaded.getFileNameWithoutExtension(), infoArtist);

    auto name = juce::File::createLegalFileName (niceName).trim().trimCharactersAtEnd (". ");
    if (name.length() > 120) name = name.substring (0, 120).trim();
    if (name.isEmpty())      name = "audio";

    auto wav = outDir.getChildFile (name + ".wav");
    if (wav.exists())
        wav = wav.getNonexistentSibling (true);

    updateJob (jobId, [&] (Job& j)
    {
        j.state = Job::State::converting;
        j.progress = -1.0f;
        j.detail = label + TR (" · pasando a WAV ") + formatRate (sr) + " / 24 bit…";
    });

    juce::StringArray ff;
    ff.add (Tools::ffmpeg().getFullPathName());
    ff.addArray (juce::StringArray { "-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-i" });
    ff.add (downloaded.getFullPathName());
    ff.addArray (juce::StringArray { "-vn", "-sn", "-dn", "-map_metadata", "-1", "-ar" });
    ff.add (juce::String ((int) std::lround (sr)));
    ff.addArray (juce::StringArray { "-c:a", "pcm_s24le" });
    ff.add (wav.getFullPathName());

    juce::String ffError;
    const int ffCode = runProcess (ff, jobId, [&] (const juce::String& l) { ffError = l; });

    cleanup();

    if (isCancelled (jobId) || threadShouldExit())
    {
        wav.deleteFile();
        return removeJob();
    }

    if (ffCode != 0 || ! wav.existsAsFile() || wav.getSize() < 1024)
    {
        wav.deleteFile();
        return fail (jobId, TR ("No pude pasarlo a WAV") + (ffError.isNotEmpty() ? ": " + ffError.substring (0, 120)
                                                                                : juce::String (".")));
    }

    // Listo: pasa de la cola al historial.
    {
        const juce::ScopedLock sl (lock);

        HistoryItem h;
        h.file = wav;
        h.title = wav.getFileNameWithoutExtension();
        h.detail = label + (duration > 0 ? TR (" · ") + formatDuration (duration) : juce::String())
                 + TR (" · ") + formatRate (sr) + " / 24 bit";
        h.time = juce::Time::currentTimeMillis();

        history.erase (std::remove_if (history.begin(), history.end(), [&] (const HistoryItem& x) { return x.file == wav; }),
                       history.end());
        history.insert (history.begin(), h);
        if (history.size() > 200)
            history.resize (200);
        saveHistory();

        jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [jobId] (const Job& j) { return j.id == jobId; }), jobs.end());
    }

    bump();
}

juce::String Engine::friendlyError (const juce::String& e)
{
    auto has = [&e] (const char* s) { return e.containsIgnoreCase (juce::String (s)); };

    if (e.isEmpty())
        return TR ("Algo falló al bajar. Probá de nuevo.");

    if (has ("Unsupported URL"))                        return TR ("Ese link no es compatible.");
    if (has ("Incomplete YouTube ID") || has ("truncated_id") || has ("is not a valid URL"))
        return TR ("El link está incompleto: copialo de nuevo.");
    if (has ("firefox"))                                return TR ("No pude leer la sesión de Firefox. ¿Está instalado? Si no, desactivalo en AJUSTES.");
    if (has ("Private video"))                          return TR ("El video es privado.");
    if (has ("confirm your age") || has ("age-restricted") || has ("inappropriate for some users"))
        return TR ("Tiene restricción de edad. Activá «Usar sesión de Firefox» en AJUSTES.");
    if (has ("Sign in") || has ("not a bot") || has ("cookies"))
        return TR ("La plataforma pide iniciar sesión. Activá «Usar sesión de Firefox» en AJUSTES.");
    if (has ("HTTP Error 429") || has ("Too Many Requests"))
        return TR ("La plataforma está frenando descargas. Probá en un rato.");
    if (has ("HTTP Error 403"))
        return TR ("La plataforma rechazó la descarga. Probá «Actualizar yt-dlp» en AJUSTES.");
    if (has ("Requested format is not available"))
        return TR ("No encontré audio para bajar en ese link.");
    if (has ("Unable to extract") || has ("nsig") || has ("signature") || has ("challenge") || has ("JavaScript"))
        return TR ("Cambió algo en la plataforma. Probá «Actualizar yt-dlp» en AJUSTES.");
    if (has ("Video unavailable") || has ("not available") || has ("has been removed") || has ("blocked"))
        return TR ("No está disponible (borrado o bloqueado en tu país).");
    if (has ("timed out") || has ("getaddrinfo") || has ("Connection") || has ("Unable to download webpage"))
        return TR ("Problema de conexión. Revisá internet y probá de nuevo.");

    // Otro error: sin el "[plataforma] id:" del principio.
    auto clean = e.trim();
    if (clean.startsWithChar ('['))
        clean = clean.fromFirstOccurrenceOf ("] ", false, false);
    if (const auto head = clean.upToFirstOccurrenceOf (": ", false, false); head.isNotEmpty() && ! head.containsChar (' ')
                                                                           && clean.contains (": "))
        clean = clean.fromFirstOccurrenceOf (": ", false, false);

    return clean.substring (0, 160);
}

//==============================================================================
bool Engine::downloadUrlToFile (const juce::String& url, const juce::File& dest, const juce::String& label,
                                float p0, float p1, juce::String& error)
{
    setTools ([&] (ToolStatus& t) { t.busy = true; t.progress = p0; t.text = TR ("Bajando ") + label + TR ("…"); });

    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (30000)
                    .withNumRedirectsToFollow (10)
                    .withStatusCode (&status)
                    .withExtraHeaders ("User-Agent: ClaraboyaGrab/1.0\r\n");

    auto in = juce::URL::createWithoutParsing (url).createInputStream (opts);

    if (in == nullptr || status >= 400)
    {
        error = TR ("no pude conectar para bajar ") + label
              + (status >= 400 ? " (HTTP " + juce::String (status) + ")" : juce::String());
        return false;
    }

    const auto total = in->getTotalLength();
    dest.deleteFile();

    juce::FileOutputStream out (dest);
    if (! out.openedOk())
    {
        error = TR ("no pude escribir en ") + dest.getParentDirectory().getFullPathName();
        return false;
    }

    constexpr int chunk = 1 << 16;
    juce::HeapBlock<char> buffer (chunk);
    juce::int64 done = 0;
    auto lastUi = juce::Time::getMillisecondCounter();

    for (;;)
    {
        if (threadShouldExit())
            return false;

        const auto n = in->read (buffer.get(), chunk);
        if (n <= 0)
            break;

        if (! out.write (buffer.get(), (size_t) n))
        {
            error = TR ("no hay lugar en el disco o falta permiso");
            return false;
        }

        done += n;

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastUi > 150)
        {
            lastUi = now;
            const float frac = total > 0 ? (float) done / (float) total : 0.0f;
            setTools ([&] (ToolStatus& t)
            {
                t.progress = p0 + (p1 - p0) * frac;
                t.text = TR ("Bajando ") + label + TR ("… ") + mbString (done)
                       + (total > 0 ? " / " + mbString (total) : juce::String());
            });
        }
    }

    out.flush();

    if (done == 0 || (total > 0 && done < total))
    {
        error = TR ("la descarga de ") + label + TR (" quedó cortada");
        return false;
    }

    return true;
}

bool Engine::extractFromZip (const juce::File& zipFile, const juce::StringArray& names, const juce::File& destDir,
                             juce::String& error)
{
    juce::ZipFile zip (zipFile);
    juce::StringArray found;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        if (threadShouldExit())
            return false;

        const auto* entry = zip.getEntry (i);
        if (entry == nullptr)
            continue;

        const auto name = entry->filename.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);

        if (! names.contains (name, true) || found.contains (name, true))
            continue;

        setTools ([&] (ToolStatus& t) { t.text = TR ("Descomprimiendo ") + name + TR ("…"); });

        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        if (in == nullptr)
        {
            error = TR ("no pude abrir ") + name + TR (" dentro del zip");
            return false;
        }

        const auto part = destDir.getChildFile (name + ".part");
        part.deleteFile();

        {
            juce::FileOutputStream out (part);
            if (! out.openedOk())
            {
                error = TR ("no pude escribir ") + name;
                return false;
            }
            out.writeFromInputStream (*in, -1);
            out.flush();
        }

        if (! part.moveFileTo (destDir.getChildFile (name)))
        {
            part.deleteFile();
            error = TR ("no pude reemplazar ") + name + TR (" (¿se está usando?)");
            return false;
        }

        found.add (name);
    }

    if (found.size() != names.size())
    {
        error = TR ("el zip no trae ") + names.joinIntoString (", ");
        return false;
    }

    return true;
}

void Engine::doInstall (bool forceAll)
{
   #if JUCE_WINDOWS
    const auto bin = Tools::binDir();
    const auto staging = bin.getChildFile ("_descarga");
    juce::String err;

    bool ok = bin.createDirectory() && staging.createDirectory();
    if (! ok)
        err = TR ("no pude crear ") + bin.getFullPathName();

    if (ok && (forceAll || ! Tools::ytdlpIsFastBuild()))
        ok = installYtdlp (0.0f, 0.15f, err);

    if (ok && (forceAll || ! Tools::deno().existsAsFile()))
    {
        const auto zip = staging.getChildFile ("deno.zip");
        ok = downloadUrlToFile (Tools::denoUrl, zip, "Deno", 0.15f, 0.40f, err)
          && extractFromZip (zip, { "deno.exe" }, bin, err);
    }

    if (ok && (forceAll || ! Tools::ffmpeg().existsAsFile() || ! Tools::ffprobe().existsAsFile()))
    {
        const auto zip = staging.getChildFile ("ffmpeg.zip");
        ok = downloadUrlToFile (Tools::ffmpegUrl, zip, "FFmpeg", 0.40f, 0.97f, err)
          && extractFromZip (zip, { "ffmpeg.exe", "ffprobe.exe" }, bin, err);
    }

    staging.deleteRecursively();

    if (ok)
    {
        setTools ([] (ToolStatus& t) { t.progress = 1.0f; t.text = TR ("Probando yt-dlp…"); });
        refreshVersion();
    }

    setTools ([&] (ToolStatus& t)
    {
        t.busy = false;
        t.progress = -1.0f;
        t.installed = Tools::allInstalled();
        t.lastFailed = ! ok || ! t.installed;
        t.text = ok ? TR ("Herramientas listas")
                    : TR ("No se pudo instalar: ") + err + TR (". Revisá la conexión y probá de nuevo.");
    });
   #else
    juce::ignoreUnused (forceAll);
    setTools ([] (ToolStatus& t)
    {
        t.busy = false;
        t.progress = -1.0f;
        t.installed = Tools::allInstalled();
        t.lastFailed = ! t.installed;
        t.text = t.installed ? TR ("Herramientas listas")
                             : TR ("En Mac/Linux instalá yt-dlp, ffmpeg y deno desde la terminal (brew / apt).");
    });
    if (Tools::allInstalled())
        refreshVersion();
   #endif
}

void Engine::doUpdate()
{
   #if JUCE_WINDOWS
    if (! Tools::deno().existsAsFile() || ! Tools::ffmpeg().existsAsFile())
        doInstall (false);
   #endif

    setTools ([] (ToolStatus& t) { t.busy = true; t.progress = -1.0f; t.text = TR ("Actualizando yt-dlp…"); });

    juce::String result;
    bool ok = false;

   #if JUCE_WINDOWS
    // La versión en carpeta no se actualiza sola (-U): se baja de nuevo si hay una más nueva.
    const auto installed = getToolStatus().version;
    const auto latest = latestYtdlpVersion();

    if (Tools::ytdlpIsFastBuild() && latest.isNotEmpty() && latest == installed)
    {
        ok = true;
        result = TR ("yt-dlp ya estaba al día (") + installed + ")";
    }
    else
    {
        juce::String err;
        ok = installYtdlp (0.0f, 1.0f, err);
        refreshVersion();
        result = ok ? TR ("yt-dlp actualizado (") + getToolStatus().version + ")"
                    : TR ("No se pudo actualizar yt-dlp: ") + err;
    }
   #else
    if (Tools::ytdlp().existsAsFile())
    {
        juce::StringArray a;
        a.add (Tools::ytdlp().getFullPathName());
        a.addArray (juce::StringArray { "-U", "--color", "never" });

        juce::String all;
        const int code = runProcess (a, 0, [&] (const juce::String& l) { all << l << "\n"; });
        ok = code == 0;

        if (all.containsIgnoreCase ("up to date"))          result = TR ("yt-dlp ya estaba al día");
        else if (all.containsIgnoreCase ("Updated yt-dlp")) result = TR ("yt-dlp actualizado");
        else if (ok)                                        result = TR ("yt-dlp revisado");
        else result = TR ("No se pudo actualizar: ") + all.trim().fromLastOccurrenceOf ("\n", false, false).substring (0, 120);
    }
    else
    {
        result = TR ("Falta yt-dlp: tocá INSTALAR");
    }
   #endif

    refreshVersion();

    setTools ([&] (ToolStatus& t)
    {
        t.busy = false;
        t.progress = -1.0f;
        t.installed = Tools::allInstalled();
        t.lastFailed = ! ok;
        t.text = result;
    });
}

bool Engine::installYtdlp (float p0, float p1, juce::String& err)
{
   #if JUCE_WINDOWS
    const auto bin = Tools::binDir();
    const auto zip = bin.getChildFile ("yt-dlp_win.zip");
    const auto fresh = bin.getChildFile ("yt-dlp.nuevo");
    const auto target = Tools::ytdlpDir();

    if (! bin.createDirectory())
    {
        err = TR ("no pude crear ") + bin.getFullPathName();
        return false;
    }

    if (! downloadUrlToFile (Tools::ytdlpZipUrl, zip, "yt-dlp", p0, p0 + (p1 - p0) * 0.85f, err))
        return false;

    setTools ([] (ToolStatus& t) { t.text = TR ("Descomprimiendo yt-dlp…"); });
    fresh.deleteRecursively();

    {
        juce::ZipFile z (zip);
        const auto r = z.uncompressTo (fresh, true);

        if (r.failed() || ! fresh.getChildFile ("yt-dlp.exe").existsAsFile())
        {
            zip.deleteFile();
            fresh.deleteRecursively();
            err = TR ("el zip de yt-dlp vino incompleto");
            return false;
        }
    }

    zip.deleteFile();

    // Cambiar la carpeta vieja por la nueva (si justo se está usando, reintenta un rato)
    const auto old = bin.getChildFile ("yt-dlp.viejo-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    bool swapped = false;

    for (int i = 0; i < 20 && ! swapped && ! threadShouldExit(); ++i)
    {
        if (target.exists() && ! target.moveFileTo (old))
        {
            juce::Thread::sleep (500);
            continue;
        }

        swapped = fresh.moveFileTo (target);
        if (! swapped)
            juce::Thread::sleep (500);
    }

    if (! swapped)
    {
        err = TR ("yt-dlp se está usando; probá de nuevo en un rato");
        return false;
    }

    // Limpieza: carpetas viejas y el .exe suelto de versiones anteriores
    for (auto& d : bin.findChildFiles (juce::File::findDirectories, false, "yt-dlp.viejo-*"))
        d.deleteRecursively();
    bin.getChildFile ("yt-dlp.exe").deleteFile();

    setTools ([p1] (ToolStatus& t) { t.progress = p1; });
    return true;
   #else
    juce::ignoreUnused (p0, p1);
    err = TR ("en Mac/Linux se usa el yt-dlp del sistema");
    return false;
   #endif
}

juce::String Engine::latestYtdlpVersion()
{
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (10000)
                    .withStatusCode (&status)
                    .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: ClaraboyaGrab\r\n");

    if (auto in = juce::URL::createWithoutParsing (Tools::ytdlpLatestApi).createInputStream (opts); in != nullptr && status == 200)
        return juce::JSON::parse (in->readEntireStreamAsString()).getProperty ("tag_name", {}).toString().trim();

    return {};
}

void Engine::maintainYtdlp()
{
   #if JUCE_WINDOWS
    if (! Tools::allInstalled())
        return;

    const auto ownsBanner = [this] { return ! getToolStatus().busy; };

    // 1) Instalaciones viejas (yt-dlp.exe suelto): pasar a la versión en carpeta, que arranca mucho más rápido.
    if (! Tools::ytdlpIsFastBuild())
    {
        if (! ownsBanner())
            return;

        setTools ([] (ToolStatus& t) { t.busy = true; t.progress = 0.0f; t.text = TR ("Pasando yt-dlp a la versión rápida (una sola vez)…"); });
        juce::String err;
        const bool ok = installYtdlp (0.0f, 1.0f, err);
        refreshVersion();
        setTools ([&] (ToolStatus& t)
        {
            t.busy = false;
            t.progress = -1.0f;
            t.installed = Tools::allInstalled();
            t.text = ok ? TR ("yt-dlp listo (versión rápida)") : TR ("No se pudo pasar yt-dlp a la versión rápida: ") + err;
        });
        return;
    }

    // 2) Una vez por día: si salió un yt-dlp nuevo, bajarlo (YouTube cambia seguido y los viejos dejan de andar).
    const auto now = juce::Time::currentTimeMillis();
    juce::int64 last = 0;
    {
        const juce::ScopedLock sl (settingsLock);
        last = props.getUserSettings()->getValue ("ytdlpCheckedAt", "0").getLargeIntValue();
    }

    if (now - last < 24LL * 60 * 60 * 1000)
        return;

    const auto latest = latestYtdlpVersion();
    if (latest.isEmpty())
        return;

    {
        const juce::ScopedLock sl (settingsLock);
        props.getUserSettings()->setValue ("ytdlpCheckedAt", juce::String (now));
        props.saveIfNeeded();
    }

    if (latest != getToolStatus().version && ownsBanner())
    {
        setTools ([&] (ToolStatus& t) { t.busy = true; t.progress = 0.0f; t.text = TR ("Actualizando yt-dlp a ") + latest + TR ("…"); });
        juce::String err;
        const bool ok = installYtdlp (0.0f, 1.0f, err);
        refreshVersion();
        setTools ([&] (ToolStatus& t)
        {
            t.busy = false;
            t.progress = -1.0f;
            t.installed = Tools::allInstalled();
            t.text = ok ? TR ("yt-dlp actualizado (") + t.version + ")" : TR ("No se pudo actualizar yt-dlp: ") + err;
        });
    }
   #endif
}

void Engine::refreshVersion()
{
    juce::String v;

    if (Tools::ytdlp().existsAsFile())
    {
        juce::StringArray a;
        a.add (Tools::ytdlp().getFullPathName());
        a.add ("--version");
        runProcess (a, 0, [&v] (const juce::String& l) { if (v.isEmpty()) v = l.trim(); });
    }

    setTools ([&] (ToolStatus& t)
    {
        t.version = v;
        t.installed = Tools::allInstalled();
    });
}
}
