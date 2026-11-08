// Saca capturas de la ventana del plugin sin abrir un DAW (solo desarrollo).
//   ClaraboyaGrabSnapshot <carpeta de salida> <carpeta con WAVs de prueba>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Tools.h"

static void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

static void save (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

static juce::Image fakeCover (juce::Colour a, juce::Colour b, bool wide)
{
    juce::Image img (juce::Image::RGB, wide ? 320 : 120, wide ? 180 : 120, true);
    juce::Graphics g (img);
    g.setGradientFill (juce::ColourGradient (a, 0, 0, b, (float) img.getWidth(), (float) img.getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillEllipse (img.getBounds().toFloat().reduced (img.getHeight() * 0.28f));
    return img;
}

static grab::Candidate cand (const char* id, const char* title, const char* artist, const char* extra, double dur,
                             bool match, bool video = false)
{
    grab::Candidate c;
    c.videoId = id;
    c.title = juce::String::fromUTF8 (title);
    c.artist = juce::String::fromUTF8 (artist);
    c.extra = juce::String::fromUTF8 (extra);
    c.duration = dur;
    c.durationMatch = match;
    c.isVideo = video;
    c.thumbUrl = "test://" + juce::String (id);
    return c;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const juce::File wavDir (argc > 2 ? juce::String (argv[2]) : outDir.getChildFile ("wavs").getFullPathName());

    // 1) Lista con historial y un error
    {
        GrabProcessor p;
        const auto salida = outDir.getChildFile ("salida-lista");
        salida.deleteRecursively();
        p.engine.setOutputFolder (salida);
        p.engine.setExtraYtdlpArgs ({ "--enable-file-urls" });
        p.engine.clearList();
        p.prepareToPlay (48000.0, 512);

        // Historial: los WAV de la carpeta de prueba, bajados de verdad con el motor.
        auto wavs = wavDir.findChildFiles (juce::File::findFiles, false, "*.wav");
        wavs.sort();
        for (auto& f : wavs)
        {
            p.engine.enqueueDownload ("file://" + juce::URL::addEscapeChars (f.getFullPathName(), false, true).replace ("%2F", "/"),
                                      f.getFileNameWithoutExtension().fromFirstOccurrenceOf (" - ", false, false),
                                      f.getFileNameWithoutExtension().upToFirstOccurrenceOf (" - ", false, false));
            p.engine.waitUntilIdle (60000);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        p.engine.enqueueDownload ("https://www.youtube.com/watch?v=no-existe");
        pump (2500);
        save (*ed, outDir.getChildFile ("1-lista.png"));
    }

    // 2) Opciones para un link de Spotify
    {
        GrabProcessor p;
        p.engine.setOutputFolder (wavDir);
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<GrabEditor*> (ed.get());

        p.thumbs.putForTesting ("test://a", fakeCover (juce::Colour (0xff7a5a3a), juce::Colour (0xff2b1d14), false));
        p.thumbs.putForTesting ("test://b", fakeCover (juce::Colour (0xff3a4a6a), juce::Colour (0xff141b2b), false));
        p.thumbs.putForTesting ("test://c", fakeCover (juce::Colour (0xff6a3a5a), juce::Colour (0xff2b142b), false));
        p.thumbs.putForTesting ("test://d", fakeCover (juce::Colour (0xff4a6a3a), juce::Colour (0xff1b2b14), false));

        grab::Searcher::Result r;
        r.state = grab::Searcher::Result::State::done;
        r.request.platform = "Spotify";
        r.request.resolveLink = true;
        r.source.title = "Tema de Prueba";
        r.source.artist = "Banda de Ejemplo";
        r.source.durationSec = 294.0;
        r.query = "Banda de Ejemplo Tema de Prueba";
        r.items = { cand ("a", "Tema de Prueba", "Banda de Ejemplo", "Disco Debut", 294.0, true),
                    cand ("b", "Tema de Prueba (Remasterizado)", "Banda de Ejemplo", "Grandes Éxitos", 290.0, false),
                    cand ("c", "Tema de Prueba (En Vivo)", "Banda de Ejemplo", "En Vivo en el Club", 331.0, false),
                    cand ("d", "Tema de Prueba", "Grupo Tributo", "Covers", 291.0, false) };

        editor->setInputTextForTesting ("Banda de Ejemplo Tema de Prueba");
        editor->showResultsForTesting (r, 1, 2, 0);
        pump (600);
        save (*ed, outDir.getChildFile ("2-opciones-spotify.png"));

        // Escuchando la primera opción
        grab::PreviewPlayer::Status ps;
        ps.videoId = "a";
        ps.title = "Banda de Ejemplo - Tema de Prueba";
        ps.state = grab::PreviewPlayer::State::playing;
        ps.progress = 0.38f;
        ps.startSec = 88.0;
        ps.lengthSec = 30.0;
        p.preview.setStatusForTesting (ps);
        pump (600);
        save (*ed, outDir.getChildFile ("4-escuchando.png"));

        ps.videoId = "c";
        ps.title = "Banda de Ejemplo - Tema de Prueba (En Vivo)";
        ps.state = grab::PreviewPlayer::State::loading;
        p.preview.setStatusForTesting (ps);
        pump (400);
        save (*ed, outDir.getChildFile ("5-cargando-preview.png"));
    }

    // 3) Buscando, y la app sin herramientas
    {
        GrabProcessor p;
        p.engine.setOutputFolder (wavDir);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<GrabEditor*> (ed.get());

        grab::Searcher::Result r;
        r.state = grab::Searcher::Result::State::searching;
        r.query = juce::String::fromUTF8 ("charly garcía rezo por vos");
        editor->setInputTextForTesting (r.query);
        editor->showResultsForTesting (r, 0, 0, -1);
        pump (500);
        save (*ed, outDir.getChildFile ("3-buscando.png"));
    }

    // 6) Aviso de versión nueva
    {
        GrabProcessor p;
        p.engine.setOutputFolder (outDir.getChildFile ("salida-lista"));
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());

        grab::Updater::Status u;
        u.state = grab::Updater::State::available;
        u.latest.build = 12;
        u.latest.label = juce::String::fromUTF8 ("1.3.0 · build 12");
        u.latest.changes = { "Preview: escuchar 30 s de cada opción antes de bajarla", "Aviso de versión nueva" };
        u.latest.changes.set (0, juce::String::fromUTF8 ("Preview: escuchar 30 s de cada opción antes de bajarla"));
        u.latest.changes.set (1, juce::String::fromUTF8 ("Aviso de versión nueva dentro de la app"));
        p.updater->setStatusForTesting (u);
        pump (800);
        save (*ed, outDir.getChildFile ("6-aviso-version-nueva.png"));

        u.state = grab::Updater::State::downloading;
        u.progress = 0.42f;
        p.updater->setStatusForTesting (u);
        pump (500);
        save (*ed, outDir.getChildFile ("7-bajando-instalador.png"));
    }

    return 0;
}
