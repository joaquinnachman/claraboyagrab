#include "Preview.h"
#include "GrabEngine.h"
#include "Tools.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <string>
#include <iostream>

namespace grab
{
namespace
{
    juce::String safeId (const juce::String& id)
    {
        auto s = id.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
        return s.isEmpty() ? juce::String ("preview") : s.substring (0, 40);
    }

    juce::String secs (double s)
    {
        return juce::String (s, 2);
    }
}

//==============================================================================
juce::File PreviewPlayer::cacheFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Claraboya Grab - previews");
}

double PreviewPlayer::chooseStart (double duration)
{
    // Temas cortos: desde el principio. Si no, desde el 30 % (suele caer cerca del estribillo).
    if (duration <= clipSeconds + 15.0)
        return 0.0;

    return juce::jlimit (0.0, duration - clipSeconds, std::round (duration * 0.3));
}

PreviewPlayer::PreviewPlayer (Engine& e) : juce::Thread ("Claraboya Grab - preview"), engine (e)
{
    startThread();
}

PreviewPlayer::~PreviewPlayer()
{
    ++latestGen;
    signalThreadShouldExit();
    killCurrentProcess();
    notify();
    stopThread (8000);
}

void PreviewPlayer::killCurrentProcess()
{
    const juce::ScopedLock sl (procLock);
    if (currentProc != nullptr)
        currentProc->kill();
}

//==============================================================================
void PreviewPlayer::play (const juce::String& videoId, const juce::String& url, const juce::String& title, double duration)
{
    const auto outputError = ensureOutput ? ensureOutput() : juce::String();
    std::unique_ptr<Clip> old;

    {
        const juce::ScopedLock sl (lock);
        const int gen = ++latestGen;

        status = {};
        status.videoId = videoId;
        status.title = title;
        status.startSec = chooseStart (duration);
        status.lengthSec = duration > 0.0 ? juce::jmin (clipSeconds, duration - status.startSec) : clipSeconds;

        if (outputError.isNotEmpty())
        {
            status.state = State::failed;
            status.error = TR ("no pude abrir la salida de audio (") + outputError + ")";
            hasPending = false;
        }
        else
        {
            status.state = State::loading;
            pending = { videoId, url, title, duration, gen };
            hasPending = true;
        }

        const juce::SpinLock::ScopedLockType csl (clipLock);
        playing = false;
        std::swap (old, clip);
    }

    killCurrentProcess();   // si estaba cargando otro, se corta ya
    ++revision;
    notify();
}

void PreviewPlayer::stop()
{
    std::unique_ptr<Clip> old;

    {
        const juce::ScopedLock sl (lock);
        ++latestGen;
        hasPending = false;
        status = {};

        const juce::SpinLock::ScopedLockType csl (clipLock);
        playing = false;
        std::swap (old, clip);
    }

    killCurrentProcess();
    ++revision;
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
            const auto now = juce::Time::getMillisecondCounter();
            const auto last = juce::jmax (lastRenderMs.load(), playStartedMs.load());
            s.stalled = now - last > 1500;
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
    if (! tl.isLocked() || clip == nullptr)
        return;

    const auto& a = clip->audio;
    const int len = a.getNumSamples();
    const int srcChannels = a.getNumChannels();
    const int outChannels = out.getNumChannels();

    if (len < 2 || srcChannels == 0 || outChannels == 0 || numSamples <= 0)
    {
        playing = false;
        return;
    }

    const double rate = playbackRate.load();
    const double step = rate > 0.0 ? clip->sampleRate / rate : 1.0;
    int produced = numSamples;

    for (int ch = 0; ch < outChannels; ++ch)
    {
        const float* src = a.getReadPointer (juce::jmin (ch, srcChannels - 1));
        float* dst = out.getWritePointer (ch, startSample);
        double p = clip->pos;

        for (int i = 0; i < numSamples; ++i)
        {
            const int idx = (int) p;

            if (idx >= len - 1)
            {
                produced = juce::jmin (produced, i);
                break;
            }

            const float frac = (float) (p - (double) idx);
            dst[i] += src[idx] + (src[idx + 1] - src[idx]) * frac;
            p += step;
        }
    }

    clip->pos += step * (double) produced;

    if (produced < numSamples)
        playing = false;

    progress.store ((float) juce::jlimit (0.0, 1.0, clip->pos / (double) len));
    lastRenderMs.store (juce::Time::getMillisecondCounter());
}

//==============================================================================
void PreviewPlayer::run()
{
    // Fragmentos viejos de otras sesiones
    for (auto& f : cacheFolder().findChildFiles (juce::File::findFilesAndDirectories, false))
        if (f.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (2))
            f.deleteRecursively();

    while (! threadShouldExit())
    {
        Request req;
        bool have = false;

        {
            const juce::ScopedLock sl (lock);
            if (hasPending)
            {
                req = pending;
                hasPending = false;
                have = true;
            }
        }

        if (! have)
        {
            wait (500);
            continue;
        }

        if (isStale (req.generation))
            continue;

        std::unique_ptr<Clip> loaded;
        juce::String error;
        const bool ok = loadClip (req, loaded, error);

        if (isStale (req.generation))
            continue;

        if (! ok)
        {
            setStatus (req.generation, [&] (Status& s) { s.state = State::failed; s.error = error; });
            continue;
        }

        {
            const juce::ScopedLock sl (lock);

            if (req.generation == latestGen.load())
            {
                {
                    const juce::SpinLock::ScopedLockType csl (clipLock);
                    std::swap (clip, loaded);
                    progress = 0.0f;
                    lastRenderMs = 0;
                    playStartedMs = juce::Time::getMillisecondCounter();
                    playing = true;
                }

                status.state = State::playing;
                status.progress = 0.0f;
            }
        }

        ++revision;
        // "loaded" (el clip anterior o el que no se usó) se libera acá, nunca en el hilo de audio.
    }
}

int PreviewPlayer::runTool (const juce::StringArray& args, int generation, int timeoutMs,
                            const std::function<void (const juce::String&)>& onLine)
{
    juce::ChildProcess proc;

    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1000;

    {
        const juce::ScopedLock sl (procLock);
        currentProc = &proc;
    }

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    std::string line;
    char buffer[32];
    int result = 0;

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
        if (isStale (generation) || juce::Time::getMillisecondCounter() > deadline)
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

bool PreviewPlayer::loadClip (const Request& req, std::unique_ptr<Clip>& out, juce::String& error)
{
    if (! Tools::ytdlp().existsAsFile() || ! Tools::ffmpeg().existsAsFile())
    {
        error = TR ("faltan las herramientas: tocá INSTALAR.");
        return false;
    }

    const double rate  = playbackRate.load() > 0.0 ? playbackRate.load() : 48000.0;
    const double start = chooseStart (req.duration);
    const double len   = req.duration > 0.0 ? juce::jmin (clipSeconds, req.duration - start) : clipSeconds;

    const auto dir = cacheFolder();
    dir.createDirectory();

    const auto cached = dir.getChildFile (safeId (req.videoId) + "-" + juce::String ((int) start) + "-"
                                          + juce::String ((int) std::lround (rate)) + ".wav");

    if (! cached.existsAsFile())
    {
        const auto tmp = dir.getChildFile (safeId (req.videoId) + "-"
                                           + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        tmp.createDirectory();

        // 1) Solo el fragmento, sin bajar el tema entero (YouTube lo permite).
        //    Si la plataforma no deja bajar un pedazo, se baja el audio completo y se recorta después.
        auto ytdlpArgs = [&] (bool onlySection)
        {
            juce::StringArray a { Tools::ytdlp().getFullPathName(), "--newline", "--color", "never", "--encoding", "utf-8",
                                  "--no-playlist", "--no-mtime", "-f", "bestaudio/best", "--ffmpeg-location",
                                  Tools::ffmpeg().getParentDirectory().getFullPathName() };

            if (auto deno = Tools::deno(); deno.existsAsFile())
                a.addArray (juce::StringArray { "--js-runtimes", "deno:" + deno.getFullPathName() });

            if (engine.getUseFirefoxCookies())
                a.addArray (juce::StringArray { "--cookies-from-browser", "firefox" });

            a.addArray (engine.getExtraYtdlpArgs());

            if (onlySection)
                a.addArray (juce::StringArray { "--download-sections", "*" + secs (start) + "-" + secs (start + len) });

            a.addArray (juce::StringArray { "-o", tmp.getFullPathName() + juce::File::getSeparatorString() + "%(id)s.%(ext)s",
                                            "--print", "after_move:FILE|%(filepath)s", "--", req.url });
            return a;
        };

        juce::String printed, lastError;
        auto onLine = [&] (const juce::String& l)
        {
            if (juce::SystemStats::getEnvironmentVariable ("CLARABOYA_GRAB_DEBUG", {}).isNotEmpty())
                std::cerr << "[preview] " << l << std::endl;

            if (l.startsWith ("FILE|"))        printed = l.substring (5).trim();
            else if (l.startsWith ("ERROR:"))  lastError = l.substring (6).trim();
        };

        bool wholeFile = false;
        int code = runTool (ytdlpArgs (true), req.generation, 120000, onLine);

        if (code != 0 && ! isStale (req.generation) && lastError.containsIgnoreCase ("partially downloaded"))
        {
            wholeFile = true;
            printed = lastError = {};
            code = runTool (ytdlpArgs (false), req.generation, 180000, onLine);
        }

        if (isStale (req.generation))
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

        // 2) WAV nivelado (-16 LUFS) con fundidos cortos, a la frecuencia a la que va a sonar.
        const auto part = dir.getChildFile (cached.getFileNameWithoutExtension() + ".tmp.wav");
        const auto fadeOut = juce::jmax (0.0, len - 0.6);

        juce::StringArray ff { Tools::ffmpeg().getFullPathName(), "-hide_banner", "-nostdin", "-loglevel", "error", "-y" };

        if (wholeFile)
            ff.addArray (juce::StringArray { "-ss", secs (start), "-t", secs (len) });

        ff.addArray (juce::StringArray { "-i", section.getFullPathName(), "-vn", "-sn", "-dn", "-ac", "2",
                               "-ar", juce::String ((int) std::lround (rate)),
                               // asetpts: el pedazo puede venir con la hora original (ej. 1:28); se lleva a 0
                               "-af", "asetpts=PTS-STARTPTS,loudnorm=I=-16:TP=-1.5:LRA=11,afade=t=in:d=0.2,afade=t=out:st="
                                      + secs (fadeOut) + ":d=0.6",
                               "-t", secs (len), "-c:a", "pcm_f32le", part.getFullPathName() });

        juce::String ffError;
        const int ffCode = runTool (ff, req.generation, 60000, [&] (const juce::String& l) { ffError = l; });
        tmp.deleteRecursively();

        if (ffCode != 0 || ! part.existsAsFile() || ! part.moveFileTo (cached))
        {
            part.deleteFile();
            error = isStale (req.generation) ? juce::String() : TR ("no pude preparar el audio") + (ffError.isNotEmpty() ? ": " + ffError.substring (0, 100) : juce::String());
            return false;
        }
    }

    // 3) A memoria
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (cached), true));

    if (reader == nullptr || reader->lengthInSamples < 100)
    {
        cached.deleteFile();
        error = TR ("el fragmento vino vacío.");
        return false;
    }

    auto c = std::make_unique<Clip>();
    c->sampleRate = reader->sampleRate;
    c->audio.setSize (juce::jmax (1, (int) reader->numChannels), (int) reader->lengthInSamples);
    reader->read (&c->audio, 0, (int) reader->lengthInSamples, 0, true, true);

    out = std::move (c);
    return true;
}
}
