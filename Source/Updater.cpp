#include "Updater.h"

#include <juce_cryptography/juce_cryptography.h>

#ifndef CLARABOYA_GRAB_BUILD
 #define CLARABOYA_GRAB_BUILD 0
#endif

#ifndef CLARABOYA_GRAB_VERSION
 #define CLARABOYA_GRAB_VERSION "0.0.0"
#endif

namespace grab
{
namespace
{
    constexpr juce::uint32 checkEveryMs = 6u * 60u * 60u * 1000u;

    juce::String mb (juce::int64 bytes)
    {
        return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    }
}

//==============================================================================
int Updater::currentBuild() noexcept
{
    return CLARABOYA_GRAB_BUILD;
}

juce::String Updater::currentLabel()
{
    return juce::String (CLARABOYA_GRAB_VERSION) + (currentBuild() > 0 ? TR (" · build ") + juce::String (currentBuild())
                                                                        : TR (" · de prueba"));
}

bool Updater::parseLatest (const juce::var& json, Release& out)
{
    const auto tag = json.getProperty ("tag_name", {}).toString();

    if (! tag.startsWithIgnoreCase ("build-"))
        return false;

    Release r;
    r.build = tag.fromFirstOccurrenceOf ("-", false, false).getIntValue();

    if (r.build <= 0)
        return false;

    // "Claraboya Grab 1.3.0 · build 12" -> "1.3.0 · build 12"
    r.label = json.getProperty ("name", {}).toString().trim();
    if (r.label.startsWithIgnoreCase ("Claraboya Grab "))
        r.label = r.label.substring (15).trim();
    if (r.label.isEmpty())
        r.label = "build " + juce::String (r.build);

    r.pageUrl = json.getProperty ("html_url", juce::String (pageUrl)).toString();

    // Novedades: las líneas "- ..." del texto de la Release
    for (auto line : juce::StringArray::fromLines (json.getProperty ("body", {}).toString()))
    {
        line = line.trim();
        if (line.startsWith ("- ") || line.startsWith ("* "))
            r.changes.add (line.substring (2).trim());
    }

    const auto assets = json.getProperty ("assets", {});
    if (auto* arr = assets.getArray())
    {
        for (auto& a : *arr)
        {
            const auto name = a.getProperty ("name", {}).toString();
            const auto url  = a.getProperty ("browser_download_url", {}).toString();

            if (! url.startsWith ("https://github.com/joaquinnachman/claraboyagrab/"))
                continue;   // solo descargas de este repo

            if (name.endsWithIgnoreCase ("Instalador.exe"))
            {
                r.installerUrl = url;
                r.installerSize = (juce::int64) a.getProperty ("size", 0);
                r.installerDigest = a.getProperty ("digest", {}).toString();
            }
            else if (name.endsWithIgnoreCase (".zip"))
            {
                r.zipUrl = url;
            }
        }
    }

    out = r;
    return true;
}

bool Updater::download (const juce::String& url, juce::int64 expectedSize, const juce::String& digest,
                        const juce::File& dest, const std::function<void (float)>& onProgress,
                        const std::function<bool()>& shouldStop, juce::String& error)
{
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (20000)
                    .withNumRedirectsToFollow (10)
                    .withStatusCode (&status)
                    .withExtraHeaders ("User-Agent: ClaraboyaGrab/" CLARABOYA_GRAB_VERSION "\r\n");

    auto in = juce::URL::createWithoutParsing (url).createInputStream (opts);

    if (in == nullptr || status >= 400)
    {
        error = TR ("no pude conectar con GitHub") + (status >= 400 ? " (HTTP " + juce::String (status) + ")" : juce::String());
        return false;
    }

    const auto total = expectedSize > 0 ? expectedSize : in->getTotalLength();
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    part.deleteFile();

    {
        juce::FileOutputStream out (part);
        if (! out.openedOk())
        {
            error = TR ("no pude escribir en ") + part.getParentDirectory().getFullPathName();
            return false;
        }

        juce::HeapBlock<char> buffer (1 << 16);
        juce::int64 done = 0;

        for (;;)
        {
            if (shouldStop && shouldStop())
            {
                out.flush();
                part.deleteFile();
                error = TR ("cancelado");
                return false;
            }

            const auto n = in->read (buffer.get(), 1 << 16);
            if (n <= 0)
                break;

            if (! out.write (buffer.get(), (size_t) n))
            {
                error = TR ("no hay lugar en el disco");
                return false;
            }

            done += n;
            if (onProgress && total > 0)
                onProgress ((float) done / (float) total);
        }

        out.flush();

        if (done == 0 || (expectedSize > 0 && done != expectedSize))
        {
            part.deleteFile();
            error = TR ("la descarga quedó cortada (") + mb (done) + (expectedSize > 0 ? " de " + mb (expectedSize) : juce::String()) + ")";
            return false;
        }
    }

    // Integridad: GitHub informa el SHA-256 de cada archivo publicado
    if (digest.startsWithIgnoreCase ("sha256:"))
    {
        const auto expected = digest.substring (7).trim().toLowerCase();
        const auto actual = juce::SHA256 (part).toHexString().toLowerCase();

        if (expected != actual)
        {
            part.deleteFile();
            error = TR ("el archivo no coincide con el publicado (SHA-256)");
            return false;
        }
    }

    dest.deleteFile();
    if (! part.moveFileTo (dest))
    {
        part.deleteFile();
        error = TR ("no pude guardar ") + dest.getFileName();
        return false;
    }

    return true;
}

//==============================================================================
Updater::Updater() : juce::Thread ("Claraboya Grab - actualizaciones")
{
    startThread();
}

Updater::~Updater()
{
    signalThreadShouldExit();
    notify();
    stopThread (15000);
}

void Updater::checkNow()
{
    checkRequested = true;
    notify();
}

void Updater::install()
{
    {
        const juce::ScopedLock sl (lock);
        if (status.state == State::downloading || status.state == State::launching)
            return;
    }

    installRequested = true;
    notify();
}

void Updater::dismiss()
{
    setStatus ([] (Status& s) { s.dismissed = true; });
}

Updater::Status Updater::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void Updater::setStatus (const std::function<void (Status&)>& fn)
{
    {
        const juce::ScopedLock sl (lock);
        fn (status);
    }
    ++revision;
}

void Updater::setStatusForTesting (const Status& s)
{
    setStatus ([&s] (Status& st) { st = s; });
}

void Updater::run()
{
    // Las compilaciones de prueba (build 0) no se fijan solas: solo con "Buscar actualizaciones".
    const bool autoCheck = currentBuild() > 0;

    if (autoCheck)
    {
        wait (4000);   // que primero abra todo tranquilo
        if (! threadShouldExit())
            doCheck (false);
    }

    while (! threadShouldExit())
    {
        wait (30000);

        if (threadShouldExit())
            break;

        if (installRequested.exchange (false))
            doInstall();
        else if (checkRequested.exchange (false))
            doCheck (true);
        else if (autoCheck && juce::Time::getMillisecondCounter() - lastCheckMs > checkEveryMs)
            doCheck (false);
    }
}

void Updater::doCheck (bool manual)
{
    lastCheckMs = juce::Time::getMillisecondCounter();

    State before;
    {
        const juce::ScopedLock sl (lock);
        before = status.state;
        if (before == State::downloading || before == State::launching)
            return;
    }

    if (manual)
        setStatus ([] (Status& s) { s.state = State::checking; s.error = {}; });

    int httpStatus = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (10000)
                    .withNumRedirectsToFollow (5)
                    .withStatusCode (&httpStatus)
                    .withExtraHeaders ("Accept: application/vnd.github+json\r\n"
                                       "X-GitHub-Api-Version: 2022-11-28\r\n"
                                       "User-Agent: ClaraboyaGrab/" CLARABOYA_GRAB_VERSION "\r\n");

    Release r;
    bool ok = false;

    if (auto in = juce::URL::createWithoutParsing (apiUrl).createInputStream (opts); in != nullptr && httpStatus == 200)
        ok = parseLatest (juce::JSON::parse (in->readEntireStreamAsString()), r);

    if (! ok)
    {
        // Sin internet o GitHub no responde: en el chequeo automático no se molesta a nadie.
        setStatus ([&] (Status& s)
        {
            s.state = manual ? State::failed : (before == State::checking ? State::idle : before);
            s.error = manual ? TR ("No pude consultar si hay versión nueva. Revisá internet.") : juce::String();
        });
        return;
    }

    const bool newer = r.build > currentBuild();

    setStatus ([&] (Status& s)
    {
        if (s.latest.build != r.build)
            s.dismissed = false;   // otra versión nueva: avisar de nuevo

        s.latest = r;
        s.error = {};
        s.progress = -1.0f;
        s.state = newer ? State::available : State::upToDate;
    });
}

void Updater::doInstall()
{
    const auto st = getStatus();

    if (st.latest.build <= 0)
        return;

    if (st.latest.installerUrl.isEmpty())
    {
        // Release vieja sin instalador: se abre la página para bajarla a mano.
        juce::MessageManager::callAsync ([url = st.latest.pageUrl] { juce::URL (url).launchInDefaultBrowser(); });
        return;
    }

    setStatus ([] (Status& s) { s.state = State::downloading; s.progress = 0.0f; s.error = {}; });

    const auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ClaraboyaGrab-Instalador-build-" + juce::String (st.latest.build) + ".exe");
    juce::String error;
    float lastShown = -1.0f;

    const bool ok = download (st.latest.installerUrl, st.latest.installerSize, st.latest.installerDigest, dest,
                              [&] (float p)
                              {
                                  if (p - lastShown >= 0.01f)
                                  {
                                      lastShown = p;
                                      setStatus ([p] (Status& s) { s.progress = p; });
                                  }
                              },
                              [this] { return threadShouldExit(); }, error);

    if (! ok)
    {
        setStatus ([&] (Status& s) { s.state = State::failed; s.progress = -1.0f; s.error = TR ("No se pudo bajar el instalador: ") + error; });
        return;
    }

    setStatus ([] (Status& s) { s.state = State::launching; s.progress = 1.0f; });

    // Abre el instalador (Windows pide permiso). En la app suelta, además, se cierra para poder reemplazarla.
    juce::MessageManager::callAsync ([dest]
    {
        dest.startAsProcess();

        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    });
}
}
