#include "Preview.h"
#include "GrabEngine.h"
#include "Tools.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

namespace grab
{
namespace
{
    constexpr juce::int64 streamLifetimeMs = 4LL * 60 * 60 * 1000;   // los links de YouTube duran ~6 h
    constexpr size_t maxRecentClips = 4;
    constexpr size_t maxStreams = 60;

    juce::int64 nowMs()                { return juce::Time::currentTimeMillis(); }
    juce::uint32 tick()                { return juce::Time::getMillisecondCounter(); }

    juce::String secondsFromNow (juce::int64 untilMs)
    {
        return juce::String ((untilMs - nowMs() + 999) / 1000) + " s";
    }

    juce::String safeId (const juce::String& id)
    {
        return juce::File::createLegalFileName (id).replaceCharacter (' ', '_').substring (0, 40);
    }

    // Formato del cliente para el preview: lo mismo que se pide al resolver y en el plan B.
    constexpr const char* previewFormat = "bestaudio/best[acodec!=none][height<=480]/best";
}

juce::String PreviewPlayer::formatSeconds (double seconds)
{
    // Siempre con punto: juce::String (double) usa el idioma del sistema/host y FFmpeg no entiende "88,00".
    const auto ms = (juce::int64) std::llround (juce::jmax (0.0, seconds) * 1000.0);
    return juce::String (ms / 1000) + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
}

juce::File PreviewPlayer::logFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Claraboya Grab")
               .getChildFile ("preview-log.txt");
}

void PreviewPlayer::log (const juce::String& message)
{
    static juce::CriticalSection logLock;
    const juce::ScopedLock sl (logLock);

    if (juce::SystemStats::getEnvironmentVariable ("CLARABOYA_GRAB_DEBUG", {}).isNotEmpty())
        std::cerr << "[preview] " << message << std::endl;

    auto f = logFile();
    f.getParentDirectory().createDirectory();

    // Que no crezca para siempre: se queda con lo último.
    if (f.getSize() > 256 * 1024)
        f.replaceWithText (f.loadFileAsString().getLastCharacters (96 * 1024).fromFirstOccurrenceOf ("\n", false, false));

    f.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + message + "\n", false, false, "\n");
}

juce::String PreviewPlayer::Stream::headerBlock() const
{
    juce::String block;
    const auto& keys = headers.getAllKeys();
    const auto& values = headers.getAllValues();

    for (int i = 0; i < keys.size(); ++i)
        if (keys[i].isNotEmpty() && ! keys[i].containsAnyOf ("\r\n:") && ! values[i].containsAnyOf ("\r\n"))
            block << keys[i] << ": " << values[i] << "\r\n";

    if (! headers.containsKey ("User-Agent") && userAgent.isNotEmpty())
        block << "User-Agent: " << userAgent << "\r\n";

    return block;
}

//==============================================================================
double PreviewPlayer::chooseStart (double duration)
{
    // Temas cortos: desde el principio. Si no, desde el 30 % (suele caer cerca del estribillo).
    if (duration <= clipSeconds + 15.0)
        return 0.0;

    return juce::jlimit (0.0, duration - clipSeconds, std::round (duration * 0.3));
}

float PreviewPlayer::gainForRms (float rms) noexcept
{
    // Todas las opciones a un nivel parejo (~ -18 dBFS RMS), sin pasarse: de -15 a +6 dB.
    constexpr float target = 0.13f;
    if (rms < 1.0e-4f)
        return 1.0f;
    return juce::jlimit (0.18f, 2.0f, target / rms);
}

bool PreviewPlayer::parseResolveOutput (const juce::StringArray& lines, Stream& out)
{
    Stream s;

    for (auto& l : lines)
    {
        if (l.startsWith ("URL|"))      s.url = l.substring (4).trim();
        else if (l.startsWith ("UA|"))  s.userAgent = l.substring (3).trim();
        else if (l.startsWith ("HDR|"))
        {
            // Los headers que yt-dlp usaría para bajar ese link (los mismos se le pasan a FFmpeg)
            const auto v = juce::JSON::parse (l.substring (4).trim());
            if (auto* o = v.getDynamicObject())
                for (auto& p : o->getProperties())
                    if (! p.value.isObject() && ! p.value.isArray() && ! p.value.isVoid())
                        s.headers.set (p.name.toString(), p.value.toString());
        }
        else if (l.startsWith ("AT|"))
        {
            // available_at: YouTube no deja usar el link antes de esta hora (segundos desde 1970)
            const auto t = l.substring (3).trim();
            if (t.isNotEmpty() && t.containsOnly ("0123456789."))
                s.availableAt = (juce::int64) std::llround (t.getDoubleValue() * 1000.0);
        }
    }

    if (s.userAgent == "NA")
        s.userAgent = {};

    if (s.userAgent.isEmpty())
        s.userAgent = s.headers.getValue ("User-Agent", {});

    if (! (s.url.startsWithIgnoreCase ("http") || s.url.startsWithIgnoreCase ("file:")))
        return false;

    s.resolvedAt = nowMs();
    out = s;
    return true;
}

juce::String PreviewPlayer::clipKey (const juce::String& videoId, double start, double rate)
{
    return videoId + "|" + juce::String ((int) start) + "|" + juce::String ((int) std::lround (rate));
}

//==============================================================================
PreviewPlayer::PreviewPlayer (Engine& e) : juce::Thread ("Claraboya Grab - preview"), engine (e)
{
    startThread();
}

std::shared_ptr<PreviewPlayer::Clip> PreviewPlayer::makeClip (const juce::String& key, double rate, double len) const
{
    auto clip = std::make_shared<Clip>();
    clip->sampleRate = rate;
    clip->key = key;
    clip->planned = juce::jmax (1, (int) std::ceil (len * rate));
    clip->audio.setSize (2, clip->planned + (int) rate);   // un segundo de margen
    clip->audio.clear();
    clip->data[0] = clip->audio.getWritePointer (0);
    clip->data[1] = clip->audio.getWritePointer (1);
    return clip;
}

PreviewPlayer::~PreviewPlayer()
{
    ++latestGen;
    signalThreadShouldExit();
    killCurrentProcess();
    notify();
    stopThread (8000);
}

void PreviewPlayer::killCurrentProcess (const juce::String& keepResolveOf, bool keepPrefetch)
{
    const juce::ScopedLock pl (procLock);

    if (currentProc == nullptr)
        return;

    if (keepResolveOf.isNotEmpty() && inFlightResolveId == keepResolveOf)
        return;   // justo se está resolviendo este link: que termine

    if (keepPrefetch && inFlightIsPrefetch)
        return;

    ++killSerial;
    currentProc->kill();
}

//==============================================================================
void PreviewPlayer::play (const juce::String& videoId, const juce::String& url, const juce::String& title, double duration)
{
    const auto outputError = ensureOutput ? ensureOutput() : juce::String();
    const double rate  = playbackRate.load() > 0.0 ? playbackRate.load() : 48000.0;
    const double start = chooseStart (duration);
    auto cached = outputError.isEmpty() ? findCachedClip (clipKey (videoId, start, rate)) : nullptr;
    std::shared_ptr<Clip> old;

    {
        const juce::ScopedLock sl (lock);
        const int gen = ++latestGen;
        playRequestedMs = tick();

        status = {};
        status.videoId = videoId;
        status.title = title;
        status.startSec = start;
        status.lengthSec = duration > 0.0 ? juce::jmin (clipSeconds, duration - start) : clipSeconds;

        if (outputError.isNotEmpty())
        {
            status.state = State::failed;
            status.error = TR ("no pude abrir la salida de audio (") + outputError + ")";
            hasPending = false;

            const juce::SpinLock::ScopedLockType csl (clipLock);
            playing = false;
            old = std::move (current);
        }
        else if (cached != nullptr)
        {
            // Ya lo escuchaste hace un rato: suena al toque desde la memoria
            {
                const juce::SpinLock::ScopedLockType csl (clipLock);
                old = std::move (current);
                current = cached;
                cached->pos = 0.0;
                cached->env = 0.0f;
                cached->gain = cached->targetGain.load();
                stopRequested = false;
                progress = 0.0f;
                lastRenderMs = 0;
                playStartedMs = tick();
                playing = true;
            }

            status.state = State::playing;
            hasPending = false;
            lastTimeToSound = 0;
        }
        else
        {
            status.state = State::loading;
            pending = { videoId, url, title, duration, gen };
            hasPending = true;

            const juce::SpinLock::ScopedLockType csl (clipLock);
            playing = false;
            old = std::move (current);
        }
    }

    // Lo que se estaba cargando para otra opción se corta; si justo se resolvía esta, que termine.
    killCurrentProcess (videoId);
    ++revision;
    notify();
}

void PreviewPlayer::stop()
{
    {
        const juce::ScopedLock sl (lock);
        ++latestGen;
        hasPending = false;
        status = {};
    }

    stopRequested = true;                 // el hilo de audio baja el volumen en 40 ms (sin click)
    killCurrentProcess ({}, true);        // corta la carga, pero deja terminar un link que se resuelve por adelantado
    ++revision;
}

void PreviewPlayer::prefetch (const juce::String& videoId, const juce::String& url)
{
    if (videoId.isEmpty() || url.isEmpty() || isResolved (videoId))
        return;

    {
        const juce::ScopedLock sl (lock);
        prefetchQueue.erase (std::remove_if (prefetchQueue.begin(), prefetchQueue.end(),
                                             [&] (const auto& p) { return p.first == videoId; }),
                             prefetchQueue.end());
        prefetchQueue.insert (prefetchQueue.begin(), { videoId, url });   // la que está bajo el mouse, primero

        if (prefetchQueue.size() > 8)
            prefetchQueue.resize (8);
    }

    notify();
}

void PreviewPlayer::prefetchMany (const std::vector<std::pair<juce::String, juce::String>>& idsAndUrls)
{
    std::vector<std::pair<juce::String, juce::String>> todo;

    for (auto& p : idsAndUrls)
        if (p.first.isNotEmpty() && p.second.isNotEmpty() && ! isResolved (p.first) && todo.size() < 8)
            todo.push_back (p);

    {
        const juce::ScopedLock sl (lock);
        prefetchQueue = std::move (todo);   // las de una búsqueda vieja ya no importan
    }

    notify();
}

bool PreviewPlayer::isResolved (const juce::String& videoId) const
{
    Stream s;
    return cachedStream (videoId, s);
}

PreviewPlayer::Status PreviewPlayer::getStatus() const
{
    const juce::ScopedLock sl (lock);

    if (hasTestStatus)
        return testStatus;

    auto s = status;

    if (s.state == State::playing)
    {
        if (! playing.load())
        {
            s.state = State::idle;   // terminó el fragmento
        }
        else
        {
            s.progress = progress.load();
            const auto last = juce::jmax (lastRenderMs.load(), playStartedMs.load());
            s.stalled = tick() - last > 1500;
        }
    }

    return s;
}

void PreviewPlayer::setStatusForTesting (const Status& s)
{
    {
        const juce::ScopedLock sl (lock);
        testStatus = s;
        hasTestStatus = true;
    }
    ++revision;
}

void PreviewPlayer::setStatus (int generation, const std::function<void (Status&)>& fn)
{
    {
        const juce::ScopedLock sl (lock);
        if (generation != latestGen.load())
            return;
        fn (status);
    }
    ++revision;
}

//==============================================================================
void PreviewPlayer::render (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept
{
    if (! playing.load (std::memory_order_relaxed))
        return;

    const juce::SpinLock::ScopedTryLockType tl (clipLock);
    if (! tl.isLocked() || current == nullptr || numSamples <= 0)
        return;

    auto& c = *current;
    const int avail = c.available.load (std::memory_order_acquire);
    const bool complete = c.complete.load (std::memory_order_acquire);
    const int outChannels = juce::jmin (out.getNumChannels(), 8);

    if (outChannels == 0)
        return;

    const double rate = playbackRate.load();
    const double step = rate > 0.0 ? c.sampleRate / rate : 1.0;
    const float fadeIn   = (float) (1.0 / (0.015 * c.sampleRate));
    const float fadeStop = (float) (1.0 / (0.040 * c.sampleRate));
    const double tailFrames = 0.3 * c.sampleRate;
    const float target = c.targetGain.load (std::memory_order_relaxed);
    const bool stopping = stopRequested.load();

    const float* srcL = c.audio.getReadPointer (0);
    const float* srcR = c.audio.getReadPointer (1);
    float* dst[8] {};
    for (int ch = 0; ch < outChannels; ++ch)
        dst[ch] = out.getWritePointer (ch, startSample);

    double pos = c.pos;
    float env = c.env, gain = c.gain;
    bool finished = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = (int) pos;

        if (idx + 1 >= avail)
        {
            if (complete)
                finished = true;
            break;   // si todavía está llegando audio: silencio un instante y sigue
        }

        if (stopping)
        {
            env -= fadeStop;
            if (env <= 0.0f)
            {
                finished = true;
                break;
            }
        }
        else if (env < 1.0f)
        {
            env = juce::jmin (1.0f, env + fadeIn);
        }

        float tail = 1.0f;
        if (complete)
        {
            const double remaining = (double) (avail - 1) - pos;
            if (remaining < tailFrames)
                tail = (float) juce::jmax (0.0, remaining / tailFrames);
        }

        gain += (target - gain) * 0.00005f;   // la ganancia se acomoda suave (~0,5 s)
        const float g = gain * env * tail;
        const float frac = (float) (pos - (double) idx);
        const float l = (srcL[idx] + (srcL[idx + 1] - srcL[idx]) * frac) * g;
        const float r = (srcR[idx] + (srcR[idx + 1] - srcR[idx]) * frac) * g;

        if (outChannels == 1)
            dst[0][i] += 0.5f * (l + r);
        else
            for (int ch = 0; ch < outChannels; ++ch)
                dst[ch][i] += (ch % 2 == 0) ? l : r;

        pos += step;
    }

    c.pos = pos;
    c.env = env;
    c.gain = gain;

    if (finished)
    {
        playing = false;
        stopRequested = false;
    }

    progress.store ((float) juce::jlimit (0.0, 1.0, pos / (double) juce::jmax (1, c.planned)));
    lastRenderMs.store (tick());
}

//==============================================================================
void PreviewPlayer::run()
{
    // Restos de fragmentos en disco (versiones anteriores o el plan B cortado a la mitad)
    juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Claraboya Grab - previews").deleteRecursively();

    while (! threadShouldExit())
    {
        Request req;
        bool havePlay = false;
        juce::String pid, purl;

        {
            const juce::ScopedLock sl (lock);
            if (hasPending)
            {
                req = pending;
                hasPending = false;
                havePlay = true;
            }
            else if (! prefetchQueue.empty())
            {
                pid = prefetchQueue.front().first;
                purl = prefetchQueue.front().second;
                prefetchQueue.erase (prefetchQueue.begin());
            }
        }

        if (havePlay)
        {
            if (! isStale (req.generation))
                handlePlay (req);
        }
        else if (pid.isNotEmpty())
        {
            handlePrefetch (pid, purl);
        }
        else
        {
            wait (500);
        }
    }
}

void PreviewPlayer::handlePrefetch (const juce::String& videoId, const juce::String& url)
{
    const int level = forceSlowPath.load() ? 2 : pathLevel.load();

    if (level >= 2 || isResolved (videoId))
        return;   // con el plan B el link adelantado no se usa

    const auto serial = killSerial.load();
    const auto cancelled = [this, serial] { return threadShouldExit() || killSerial.load() != serial; };

    {
        const juce::ScopedLock pl (procLock);
        inFlightResolveId = videoId;
        inFlightIsPrefetch = true;
    }

    Stream s;
    juce::String error;
    const auto t0 = tick();
    const bool ok = resolve (videoId, url, level == 0, s, error, cancelled);

    {
        const juce::ScopedLock pl (procLock);
        inFlightResolveId = {};
        inFlightIsPrefetch = false;
    }

    if (ok)
    {
        log ("  (adelantado " + videoId + " en " + juce::String ((int) (tick() - t0)) + " ms"
             + (s.mustWaitUntil() > nowMs() ? ", YouTube pide esperar " + secondsFromNow (s.mustWaitUntil()) : juce::String()) + ")");
    }
    else if (cancelled() && ! threadShouldExit())
    {
        // Lo cortó un play de otra opción: vuelve a la fila, al final
        const juce::ScopedLock sl (lock);
        if (prefetchQueue.size() < 8)
            prefetchQueue.push_back ({ videoId, url });
    }
}

void PreviewPlayer::handlePlay (const Request& req)
{
    const auto stale = [this, g = req.generation] { return isStale (g); };
    const double rate = playbackRate.load() > 0.0 ? playbackRate.load() : 48000.0;
    const double start = chooseStart (req.duration);
    const double len = req.duration > 0.0 ? juce::jmin (clipSeconds, req.duration - start) : clipSeconds;
    const auto key = clipKey (req.videoId, start, rate);

    if (auto cached = findCachedClip (key))
    {
        startPlaying (cached, req.generation);
        return;
    }

    const int level = forceSlowPath.load() ? 2 : pathLevel.load();
    log ("PLAY " + req.title + "  [" + req.videoId + "]" + (level > 0 ? "  (camino " + juce::String (level) + ")" : juce::String()));

    juce::String error;
    bool streamFailed = false;

    if (level < 2)
    {
        // 1) El link del audio: el adelantado si está; si no, con el cliente rápido y, si ese no puede, el normal.
        if (Stream c; level == 1 && cachedStream (req.videoId, c) && c.fastClient)
            forgetStream (req.videoId);

        Stream s;
        bool resolved = false;

        for (int attempt = level; attempt < 2 && ! resolved; ++attempt)
        {
            const bool fast = attempt == 0;

            {
                const juce::ScopedLock pl (procLock);
                inFlightResolveId = req.videoId;
                inFlightIsPrefetch = false;
            }

            const auto t0 = tick();
            resolved = resolve (req.videoId, req.url, fast, s, error, stale);

            {
                const juce::ScopedLock pl (procLock);
                inFlightResolveId = {};
            }

            if (stale())
                return;

            if (resolved)
                log ("  link " + juce::String (s.fastClient ? "(cliente rapido)" : "(cliente normal)") + " en "
                     + juce::String ((int) (tick() - t0)) + " ms"
                     + (s.mustWaitUntil() > nowMs() ? ", YouTube pide esperar " + secondsFromNow (s.mustWaitUntil()) : juce::String()));
            else
                log ("  sin link (" + juce::String (fast ? "cliente rapido" : "cliente normal") + "): " + error);
        }

        // 2) Esperar lo que pida YouTube y leer el fragmento directo.
        if (resolved)
        {
            const auto headerBlock = s.headerBlock();

            for (int tryNo = 0; tryNo < 2; ++tryNo)
            {
                const auto until = tryNo == 0 ? s.mustWaitUntil() : nowMs() + 5000;
                if (! waitUntilAvailable (req, until))
                    return;

                auto clip = makeClip (key, rate, len);

                if (streamInto (req, s.url, headerBlock, start, clip, error) || clip->available.load() > 0)
                {
                    log ("  sono a los " + juce::String (lastTimeToSound.load()) + " ms de tocar play");
                    return;   // ya está sonando (o sonó entero)
                }

                if (stale())
                    return;

                const auto why = probeError (s.url, headerBlock, start, stale);
                if (stale())
                    return;

                log ("  FFmpeg no recibio audio: " + why);

                // A veces YouTube hace esperar sin avisarlo: un reintento a los 5 s con el mismo link.
                const bool retry = tryNo == 0 && why.contains ("403") && s.mustWaitUntil() == 0
                                && nowMs() - s.resolvedAt < 120000;
                if (! retry)
                    break;

                log ("  reintento en 5 s");
            }

            forgetStream (req.videoId);   // ese link no sirvió
            streamFailed = true;
        }
    }

    // 3) Plan B: que yt-dlp baje solo el fragmento (respeta solo cualquier espera) y de ahí al reproductor.
    setStatus (req.generation, [] (Status& st) { st.slowPath = true; st.waitUntilMs = 0; });
    auto clip = makeClip (key, rate, len);
    juce::String slowError;
    const auto t0 = tick();

    if (loadSection (req, clip, slowError) || clip->available.load() > 0)
    {
        log ("  sono por el plan B (" + juce::String ((int) (tick() - t0)) + " ms)");

        if (streamFailed && level < 2 && pathLevel.load() == level)
        {
            pathLevel = level + 1;
            log ("  desde ahora, en esta sesion: camino " + juce::String (level + 1));
        }
        return;
    }

    if (stale())
        return;

    log ("  plan B tampoco: " + slowError);

    // El error más útil: el del plan B, salvo que sea el genérico y el primero diga algo concreto.
    const auto generic = Engine::friendlyError ({});
    const auto shown = slowError.isNotEmpty() && (slowError != generic || error.isEmpty()) ? slowError : error;

    setStatus (req.generation, [&] (Status& st)
    {
        st.state = State::failed;
        st.waitUntilMs = 0;
        st.error = shown.isNotEmpty() ? shown : TR ("no llegó audio de ese link.");
    });
}

bool PreviewPlayer::waitUntilAvailable (const Request& req, juce::int64 untilMs)
{
    if (untilMs <= nowMs())
        return ! isStale (req.generation);

    untilMs = juce::jmin (untilMs, nowMs() + 60000);
    setStatus (req.generation, [&] (Status& st) { st.waitUntilMs = untilMs; });

    while (nowMs() < untilMs)
    {
        if (isStale (req.generation))
            return false;
        wait (50);
    }

    setStatus (req.generation, [] (Status& st) { st.waitUntilMs = 0; });
    return ! isStale (req.generation);
}

bool PreviewPlayer::resolve (const juce::String& videoId, const juce::String& url, bool fastClient, Stream& out,
                             juce::String& error, const std::function<bool()>& isStaleFn)
{
    if (cachedStream (videoId, out))
        return true;

    if (! Tools::ytdlp().existsAsFile() || ! Tools::ffmpeg().existsAsFile())
    {
        error = TR ("faltan las herramientas: tocá INSTALAR.");
        return false;
    }

    juce::StringArray a { Tools::ytdlp().getFullPathName(), "--ignore-config", "--no-warnings", "--no-playlist",
                          "--encoding", "utf-8", "-f", previewFormat };

    if (auto deno = Tools::deno(); deno.existsAsFile())
        a.addArray (juce::StringArray { "--js-runtimes", "deno:" + deno.getFullPathName() });

    if (engine.getUseFirefoxCookies())
        a.addArray (juce::StringArray { "--cookies-from-browser", "firefox" });

    a.addArray (engine.getExtraYtdlpArgs());

    // Cliente de YouTube que no necesita resolver el JavaScript del reproductor: una sola consulta, sin Deno.
    if (fastClient)
        a.addArray (juce::StringArray { "--extractor-args", "youtube:player_client=visionos;player_skip=webpage,configs" });

    a.addArray (juce::StringArray { "--print", "URL|%(url)s", "--print", "UA|%(http_headers.User-Agent)s",
                                    "--print", "HDR|%(http_headers)j", "--print", "AT|%(available_at)s", "--", url });

    juce::StringArray lines;
    juce::String lastError;
    const int code = runTool (a, isStaleFn, 60000, [&] (const juce::String& l)
    {
        if (l.startsWith ("ERROR:"))
            lastError = l.substring (6).trim();
        lines.add (l);
    });

    Stream s;
    if (code != 0 || ! parseResolveOutput (lines, s))
    {
        error = isStaleFn() ? juce::String() : (code == -2000 ? TR ("tardó demasiado.") : Engine::friendlyError (lastError));
        return false;
    }

    s.fastClient = fastClient;

    {
        const juce::ScopedLock cl (cacheLock);
        if (streams.size() > maxStreams)
            streams.clear();
        streams[videoId] = s;
    }

    out = s;
    return true;
}

bool PreviewPlayer::streamInto (const Request& req, const juce::String& source, const juce::String& headerBlock,
                                double startSec, const std::shared_ptr<Clip>& clip, juce::String& error)
{
    const auto stale = [this, g = req.generation] { return isStale (g); };
    const double rate = clip->sampleRate;
    const double len  = (double) clip->planned / rate;

    juce::StringArray ff { Tools::ffmpeg().getFullPathName(), "-hide_banner", "-nostdin", "-loglevel", "error" };

    auto input = source;

    if (input.startsWithIgnoreCase ("http"))
    {
        // Igual que cuando yt-dlp le pasa un link a FFmpeg: todos sus headers.
        if (headerBlock.isNotEmpty())
            ff.addArray (juce::StringArray { "-headers", headerBlock });
    }
    else if (input.startsWithIgnoreCase ("file:"))
    {
        input = juce::URL (input).getLocalFile().getFullPathName();   // FFmpeg quiere la ruta, no el link con %20
    }

    // Arranca directo en el punto del preview y manda audio decodificado a medida que llega.
    if (startSec > 0.0)
        ff.addArray (juce::StringArray { "-ss", formatSeconds (startSec) });

    ff.addArray (juce::StringArray { "-t", formatSeconds (len), "-i", input, "-vn", "-sn", "-dn",
                                     "-ac", "2", "-ar", juce::String ((int) std::lround (rate)), "-f", "f32le", "-" });

    juce::ChildProcess proc;
    if (! proc.start (ff, juce::ChildProcess::wantStdOut))
    {
        error = TR ("no pude ejecutar FFmpeg.");
        return false;
    }

    {
        const juce::ScopedLock pl (procLock);
        currentProc = &proc;
    }

    const int startFrames = (int) (rate * startBuffer);
    const int capacity = clip->audio.getNumSamples();
    constexpr int chunk = 16384;
    juce::HeapBlock<char> buffer (chunk + 16);
    std::vector<float> frame (chunk / sizeof (float) + 4);
    size_t carry = 0;
    double sumSquares = 0.0;
    juce::int64 counted = 0;
    bool started = false, finishedOk = false;
    const auto deadline = tick() + 60000;

    for (;;)
    {
        if (stale() || tick() > deadline)
        {
            proc.kill();
            break;
        }

        const int n = proc.readProcessOutput (buffer.get() + carry, chunk);

        if (n <= 0)
        {
            if (! proc.isRunning())
            {
                finishedOk = proc.getExitCode() == 0;
                break;
            }
            juce::Thread::sleep (5);
            continue;
        }

        const size_t total = carry + (size_t) n;
        const size_t frames = total / (2 * sizeof (float));
        const int avail = clip->available.load (std::memory_order_relaxed);
        const int toWrite = (int) juce::jmin ((size_t) (capacity - avail), frames);

        std::memcpy (frame.data(), buffer.get(), frames * 2 * sizeof (float));

        for (int i = 0; i < toWrite; ++i)
        {
            const float l = frame[(size_t) (2 * i)], r = frame[(size_t) (2 * i + 1)];
            clip->data[0][avail + i] = l;
            clip->data[1][avail + i] = r;
            sumSquares += (double) l * l + (double) r * r;
        }

        counted += 2 * toWrite;
        clip->available.store (avail + toWrite, std::memory_order_release);

        if (counted > 0)
            clip->targetGain.store (gainForRms ((float) std::sqrt (sumSquares / (double) counted)));

        carry = total - frames * 2 * sizeof (float);
        if (carry > 0)
            std::memmove (buffer.get(), buffer.get() + frames * 2 * sizeof (float), carry);

        if (! started && avail + toWrite >= startFrames)
        {
            started = true;
            startPlaying (clip, req.generation);
        }

        if (avail + toWrite >= capacity)
        {
            proc.kill();
            finishedOk = true;
            break;
        }
    }

    {
        const juce::ScopedLock pl (procLock);
        currentProc = nullptr;
    }

    clip->complete.store (true, std::memory_order_release);

    if (stale())
        return false;

    const int got = clip->available.load();

    if (! started && got > 0)
        startPlaying (clip, req.generation);

    if (got == 0)
    {
        error = TR ("no llegó audio de ese link.");
        return false;
    }

    if (finishedOk || got >= (int) (rate * 5.0))
        rememberClip (clip);

    return true;
}

juce::String PreviewPlayer::probeError (const juce::String& source, const juce::String& headerBlock, double startSec,
                                        const std::function<bool()>& isStaleFn)
{
    // El FFmpeg del preview no muestra sus errores (su salida es el audio): se repite un instante para leerlos.
    juce::StringArray ff { Tools::ffmpeg().getFullPathName(), "-hide_banner", "-nostdin", "-loglevel", "error" };
    auto input = source;

    if (input.startsWithIgnoreCase ("http"))
    {
        if (headerBlock.isNotEmpty())
            ff.addArray (juce::StringArray { "-headers", headerBlock });
    }
    else if (input.startsWithIgnoreCase ("file:"))
    {
        input = juce::URL (input).getLocalFile().getFullPathName();
    }

    if (startSec > 0.0)
        ff.addArray (juce::StringArray { "-ss", formatSeconds (startSec) });

    ff.addArray (juce::StringArray { "-t", "0.5", "-i", input, "-vn", "-f", "null", "-" });

    juce::StringArray lines;
    const int code = runTool (ff, isStaleFn, 20000, [&] (const juce::String& l) { lines.add (l.trim()); });
    lines.removeEmptyStrings();

    if (code == -2000)
        return "FFmpeg tardo demasiado en responder";

    while (lines.size() > 3)
        lines.remove (0);

    return "FFmpeg codigo " + juce::String (code) + (lines.isEmpty() ? juce::String() : ": " + lines.joinIntoString (" | "));
}

bool PreviewPlayer::loadSection (const Request& req, const std::shared_ptr<Clip>& clip, juce::String& error)
{
    const auto stale = [this, g = req.generation] { return isStale (g); };

    if (! Tools::ytdlp().existsAsFile() || ! Tools::ffmpeg().existsAsFile())
    {
        error = TR ("faltan las herramientas: tocá INSTALAR.");
        return false;
    }

    const double start = chooseStart (req.duration);
    const double len   = (double) clip->planned / clip->sampleRate;

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("Claraboya Grab - previews")
                         .getChildFile (safeId (req.videoId) + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    tmp.createDirectory();

    // Como la 1.3: yt-dlp baja solo ese pedazo (si la plataforma no deja, el audio entero y se recorta).
    auto ytdlpArgs = [&] (bool onlySection)
    {
        juce::StringArray a { Tools::ytdlp().getFullPathName(), "--ignore-config", "--newline", "--color", "never",
                              "--encoding", "utf-8", "--no-playlist", "--no-mtime", "-f", previewFormat,
                              "--ffmpeg-location", Tools::ffmpeg().getParentDirectory().getFullPathName() };

        if (auto deno = Tools::deno(); deno.existsAsFile())
            a.addArray (juce::StringArray { "--js-runtimes", "deno:" + deno.getFullPathName() });

        if (engine.getUseFirefoxCookies())
            a.addArray (juce::StringArray { "--cookies-from-browser", "firefox" });

        a.addArray (engine.getExtraYtdlpArgs());

        if (onlySection)
            a.addArray (juce::StringArray { "--download-sections", "*" + formatSeconds (start) + "-" + formatSeconds (start + len) });

        a.addArray (juce::StringArray { "-o", tmp.getFullPathName() + juce::File::getSeparatorString() + "%(id)s.%(ext)s",
                                        "--print", "after_move:FILE|%(filepath)s", "--", req.url });
        return a;
    };

    juce::String printed, lastError;
    auto onLine = [&] (const juce::String& l)
    {
        if (l.startsWith ("FILE|"))
        {
            printed = l.substring (5).trim();
        }
        else if (l.startsWith ("ERROR:"))
        {
            lastError = l.substring (6).trim();
        }
        else if (l.contains ("Sleeping ") && l.contains ("seconds"))
        {
            // "[download] Sleeping 5.00 seconds as required by the site..." -> cuenta regresiva en la pantalla
            const auto secsToWait = l.fromFirstOccurrenceOf ("Sleeping ", false, false).getDoubleValue();
            if (secsToWait > 0.5)
            {
                const auto until = nowMs() + (juce::int64) (secsToWait * 1000.0);
                setStatus (req.generation, [until] (Status& st) { st.waitUntilMs = until; });
                log ("  plan B: YouTube pide esperar " + juce::String ((int) std::ceil (secsToWait)) + " s");
            }
        }
    };

    bool wholeFile = false;
    int code = runTool (ytdlpArgs (true), stale, 120000, onLine);

    if (code != 0 && ! stale() && lastError.containsIgnoreCase ("partially downloaded"))
    {
        wholeFile = true;
        printed = lastError = {};
        code = runTool (ytdlpArgs (false), stale, 180000, onLine);
    }

    setStatus (req.generation, [] (Status& st) { st.waitUntilMs = 0; });

    if (stale())
    {
        tmp.deleteRecursively();
        return false;
    }

    auto section = juce::File::isAbsolutePath (printed) ? juce::File (printed) : juce::File();
    if (! section.existsAsFile())
        for (auto& f : tmp.findChildFiles (juce::File::findFiles, false))
            if (! f.hasFileExtension ("part;ytdl;json;tmp"))
                { section = f; break; }

    if (code != 0 || ! section.existsAsFile())
    {
        tmp.deleteRecursively();
        error = code == -2000 ? TR ("tardó demasiado.") : Engine::friendlyError (lastError);
        return false;
    }

    // El pedazo ya empieza en el punto del preview (si vino el audio entero, se salta hasta ahí).
    const bool ok = streamInto (req, section.getFullPathName(), {}, wholeFile ? start : 0.0, clip, error);
    tmp.deleteRecursively();
    return ok;
}

void PreviewPlayer::startPlaying (const std::shared_ptr<Clip>& clip, int generation)
{
    std::shared_ptr<Clip> old;

    {
        const juce::ScopedLock sl (lock);
        if (generation != latestGen.load())
            return;

        {
            const juce::SpinLock::ScopedLockType csl (clipLock);
            old = std::move (current);
            current = clip;
            clip->pos = 0.0;
            clip->env = 0.0f;
            clip->gain = clip->targetGain.load();
            stopRequested = false;
            progress = 0.0f;
            lastRenderMs = 0;
            playStartedMs = tick();
            playing = true;
        }

        status.state = State::playing;
        status.progress = 0.0f;
        lastTimeToSound = (int) (tick() - playRequestedMs.load());
    }

    ++revision;
    // "old" se libera acá, nunca en el hilo de audio
}

//==============================================================================
std::shared_ptr<PreviewPlayer::Clip> PreviewPlayer::findCachedClip (const juce::String& key)
{
    const juce::ScopedLock cl (cacheLock);
    for (auto& c : recentClips)
        if (c->key == key && c->complete.load())
            return c;
    return nullptr;
}

void PreviewPlayer::rememberClip (const std::shared_ptr<Clip>& clip)
{
    std::shared_ptr<Clip> dropped;   // se libera fuera del lock
    const juce::ScopedLock cl (cacheLock);

    recentClips.erase (std::remove_if (recentClips.begin(), recentClips.end(),
                                       [&] (const std::shared_ptr<Clip>& c) { return c->key == clip->key; }),
                       recentClips.end());
    recentClips.push_back (clip);

    if (recentClips.size() > maxRecentClips)
    {
        dropped = recentClips.front();
        recentClips.erase (recentClips.begin());
    }
}

bool PreviewPlayer::cachedStream (const juce::String& videoId, Stream& out) const
{
    const juce::ScopedLock cl (cacheLock);
    if (auto it = streams.find (videoId); it != streams.end() && nowMs() - it->second.resolvedAt < streamLifetimeMs)
    {
        out = it->second;
        return true;
    }
    return false;
}

void PreviewPlayer::forgetStream (const juce::String& videoId)
{
    const juce::ScopedLock cl (cacheLock);
    streams.erase (videoId);
}

int PreviewPlayer::runTool (const juce::StringArray& args, const std::function<bool()>& isStaleFn, int timeoutMs,
                            const std::function<void (const juce::String&)>& onLine)
{
    juce::ChildProcess proc;

    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1000;

    {
        const juce::ScopedLock sl (procLock);
        currentProc = &proc;
    }

    const auto deadline = tick() + (juce::uint32) timeoutMs;
    std::string line;
    char buffer[32];
    int result = 0, exitCode = 0;
    bool finished = false;

    auto emit = [&]
    {
        if (! line.empty())
            onLine (juce::String::fromUTF8 (line.data(), (int) line.size()));
        line.clear();
    };

    for (;;)
    {
        if (isStaleFn() || tick() > deadline)
        {
            proc.kill();
            result = -2000;
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

            juce::Thread::sleep (10);
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            if (buffer[i] == '\n' || buffer[i] == '\r') emit();
            else                                        line.push_back (buffer[i]);
        }
    }

    emit();

    {
        const juce::ScopedLock sl (procLock);
        currentProc = nullptr;
    }

    // El código de salida se toma apenas termina: en Linux/Mac, volver a preguntar lo pisa con 0.
    if (! finished)
    {
        proc.waitForProcessToFinish (3000);
        exitCode = (int) proc.getExitCode();
    }

    return result != 0 ? result : exitCode;
}
}
