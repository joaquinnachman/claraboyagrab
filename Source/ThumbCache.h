#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <map>

/** Tapas de los resultados: se bajan en segundo plano y se decodifican al dibujarlas. */
class ThumbCache
{
public:
    ThumbCache() : pool (juce::ThreadPoolOptions{}.withThreadName ("Claraboya Grab - tapas").withNumberOfThreads (3)) {}

    ~ThumbCache()
    {
        alive->store (false);
        pool.removeAllJobs (true, 4000);
    }

    /** Llamar desde el hilo de la interfaz. Devuelve una imagen vacía mientras baja. */
    juce::Image get (const juce::String& url)
    {
        if (url.isEmpty())
            return {};

        const juce::ScopedLock sl (lock);

        if (entries.size() > 250)
            entries.clear();

        auto [it, inserted] = entries.try_emplace (url);
        auto& e = it->second;

        if (inserted)
        {
            auto flag = alive;
            pool.addJob ([this, url, flag]
            {
                juce::MemoryBlock data;

                if (flag->load())
                {
                    int status = 0;
                    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                    .withConnectionTimeoutMs (6000)
                                    .withNumRedirectsToFollow (5)
                                    .withStatusCode (&status);

                    if (auto in = juce::URL::createWithoutParsing (url).createInputStream (opts); in != nullptr && status < 400)
                        in->readIntoMemoryBlock (data, 2 * 1024 * 1024);
                }

                if (! flag->load())
                    return;

                {
                    const juce::ScopedLock sl2 (lock);
                    if (auto found = entries.find (url); found != entries.end())
                    {
                        found->second.data = std::move (data);
                        found->second.fetched = true;
                    }
                }
                ++revision;
            });
        }

        if (e.fetched && ! e.decoded)
        {
            e.decoded = true;
            if (e.data.getSize() > 0)
                e.image = juce::ImageFileFormat::loadFrom (e.data.getData(), e.data.getSize());
            e.data.reset();
        }

        return e.image;
    }

    juce::uint32 getRevision() const noexcept { return revision.load(); }

    /** Solo para capturas/pruebas. */
    void putForTesting (const juce::String& url, const juce::Image& img)
    {
        const juce::ScopedLock sl (lock);
        auto& e = entries[url];
        e.image = img;
        e.fetched = e.decoded = true;
        ++revision;
    }

private:
    struct Entry
    {
        juce::MemoryBlock data;
        juce::Image image;
        bool fetched = false, decoded = false;
    };

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::CriticalSection lock;
    std::map<juce::String, Entry> entries;
    std::atomic<juce::uint32> revision { 0 };
    juce::ThreadPool pool;   // último: se destruye primero y espera a los trabajos

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThumbCache)
};
