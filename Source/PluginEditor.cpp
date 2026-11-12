#include "PluginEditor.h"
#include <algorithm>
#include "Resolver.h"
#include "Tools.h"

using grab::TR;

namespace
{
    void drawPill (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour bg, juce::Colour fg,
                   const juce::Font& font, const juce::String& text)
    {
        g.setColour (bg);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (fg);
        g.setFont (font);
        g.drawText (text, r, juce::Justification::centred, false);
    }

    juce::String whenText (juce::int64 ms)
    {
        if (ms <= 0)
            return {};

        const juce::Time t (ms), now = juce::Time::getCurrentTime();
        const bool today = t.getYear() == now.getYear() && t.getDayOfYear() == now.getDayOfYear();
        return today ? "hoy " + t.formatted ("%H:%M") : t.formatted ("%d/%m %H:%M");
    }
}

//==============================================================================
/** Una fila de la lista: descarga en curso, error, o WAV listo para arrastrar. */
class GrabEditor::Row final : public juce::Component
{
public:
    explicit Row (GrabEditor& o) : owner (o) {}

    void setRow (int r) { row = r; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto* it = owner.getItem (row);
        if (it == nullptr)
            return;

        using K = Item::Kind;
        const auto& f = owner.getFonts();

        auto r = getLocalBounds().reduced (0, 3);
        auto rf = r.toFloat();

        g.setColour (hover && it->draggable() ? brand::rowHi : brand::row);
        g.fillRoundedRectangle (rf, 5.0f);

        const auto stripe = it->kind == K::active ? brand::red
                          : it->kind == K::failed ? brand::yellow.withAlpha (0.8f)
                                                  : brand::line;
        g.setColour (stripe);
        g.fillRoundedRectangle (rf.withWidth (4.0f), 2.0f);

        auto content = r.reduced (16, 8);
        auto right = content.removeFromRight (it->draggable() ? 96 : 84);
        content.removeFromRight (8);

        g.setColour (brand::white);
        g.setFont (f.bold (14.5f));
        g.drawFittedText (it->title, content.removeFromTop (21), juce::Justification::centredLeft, 1, 0.9f);

        g.setColour (it->kind == K::failed ? brand::yellow : brand::grey);
        g.setFont (f.semi (12.0f));
        g.drawFittedText (it->sub, content.removeFromTop (17), juce::Justification::centredLeft, 1, 0.85f);

        if (it->kind == K::active)
        {
            auto bar = content.removeFromTop (8).withTrimmedTop (4).toFloat();
            g.setColour (brand::line);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (brand::red);

            if (it->progress >= 0.0f)
            {
                g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * it->progress)), 2.0f);
            }
            else
            {
                const auto t = (float) (juce::Time::getMillisecondCounter() % 1400) / 1400.0f;
                const auto w = bar.getWidth() * 0.25f;
                const auto x = bar.getX() + (bar.getWidth() - w) * (0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi));
                g.fillRoundedRectangle (bar.withX (x).withWidth (w), 2.0f);
            }
        }

        if (it->draggable())
        {
            auto grip = right.removeFromLeft (14).toFloat();
            g.setColour (hover ? brand::white : brand::grey);
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 3; ++k)
                    g.fillEllipse (grip.getX() + 2.0f + (float) c * 6.0f, grip.getCentreY() - 8.0f + (float) k * 6.5f, 3.0f, 3.0f);

            g.setFont (f.title (19.0f, 0.06f));
            g.drawText (TR ("ARRASTRÁ"), right, juce::Justification::centredRight, false);
        }
        else
        {
            g.setColour (hover ? brand::white : brand::grey);
            g.setFont (f.bold (10.5f, 0.1f));
            g.drawText (it->kind == K::active ? "CANCELAR" : "QUITAR", right, juce::Justification::centredRight, false);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

    void mouseMove (const juce::MouseEvent&) override
    {
        const auto* it = owner.getItem (row);
        setMouseCursor (it != nullptr && it->draggable() ? juce::MouseCursor::DraggingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (e.mods.isPopupMenu())
            owner.showRowMenu (row, this);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
            return;

        auto file = owner.dragFileForRow (row);
        if (! file.existsAsFile())
            return;

        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && ! e.mods.isPopupMenu() && e.mouseWasClicked())
            owner.rowClicked (row, e.x > getWidth() - 110);
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { owner.rowDoubleClicked (row); }

private:
    GrabEditor& owner;
    int row = -1;
    bool hover = false, dragging = false;
};

//==============================================================================
/** Aviso de "falta instalar" / progreso de la instalación. */
class GrabEditor::ToolsBanner final : public juce::Component
{
public:
    explicit ToolsBanner (GrabEditor& o) : owner (o)
    {
        button.setButtonText ("INSTALAR");
        button.getProperties().set ("primary", true);
        button.onClick = [this] { owner.getEngine().enqueueInstallTools (false); };
        addAndMakeVisible (button);
    }

    void setStatus (const grab::ToolStatus& s)
    {
        status = s;
        button.setVisible (grab::Tools::canAutoInstall() && ! s.busy);
        button.setButtonText (s.lastFailed ? "REINTENTAR" : "INSTALAR");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& f = owner.getFonts();
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff2a1b1c));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (brand::bordo.brighter (0.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto text = getLocalBounds().reduced (14, 10);
        if (button.isVisible())
            text.removeFromRight (button.getWidth() + 14);

        const auto headline = status.busy       ? (status.installed ? TR ("Actualizando") : TR ("Instalando herramientas"))
                            : status.lastFailed ? TR ("No se pudo instalar")
                                                : TR ("Falta un paso (una sola vez)");

        g.setColour (brand::white);
        g.setFont (f.bold (14.0f));
        g.drawFittedText (headline, text.removeFromTop (20), juce::Justification::centredLeft, 1);

        const auto sub = (status.busy || status.lastFailed)
                       ? status.text
                       : (grab::Tools::canAutoInstall()
                            ? TR ("Baja yt-dlp, FFmpeg y Deno (unos 200 MB) a la carpeta del plugin. Después no hace falta más.")
                            : status.text);

        g.setColour (status.lastFailed ? brand::yellow : brand::grey);
        g.setFont (f.semi (12.0f));
        g.drawFittedText (sub, text.removeFromTop (30), juce::Justification::topLeft, 2, 0.9f);

        if (status.busy)
        {
            auto bar = getLocalBounds().reduced (14, 0).removeFromBottom (10).withHeight (3).toFloat();
            g.setColour (brand::line);
            g.fillRoundedRectangle (bar, 1.5f);
            g.setColour (brand::red);
            if (status.progress >= 0.0f)
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, status.progress)), 1.5f);
        }
    }

    void resized() override
    {
        button.setBounds (getLocalBounds().reduced (14, 16).removeFromRight (130));
    }

private:
    GrabEditor& owner;
    juce::TextButton button;
    grab::ToolStatus status;
};

//==============================================================================
/** Aviso de versión nueva de Claraboya Grab. */
class GrabEditor::UpdateBanner final : public juce::Component
{
public:
    explicit UpdateBanner (GrabEditor& o) : owner (o)
    {
        updateButton.setButtonText ("ACTUALIZAR");
        updateButton.getProperties().set ("primary", true);
        updateButton.onClick = [this] { owner.getUpdater().install(); };
        addAndMakeVisible (updateButton);

        laterButton.setButtonText (TR ("MÁS TARDE"));
        laterButton.onClick = [this] { owner.getUpdater().dismiss(); };
        addAndMakeVisible (laterButton);
    }

    void setStatus (const grab::Updater::Status& s)
    {
        using U = grab::Updater::State;
        status = s;
        const bool busy = s.state == U::downloading || s.state == U::launching;
        updateButton.setVisible (! busy);
        updateButton.setButtonText (s.state == U::failed ? "REINTENTAR" : "ACTUALIZAR");
        laterButton.setVisible (! busy);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using U = grab::Updater::State;
        const auto& f = owner.getFonts();
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff2a1b1c));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (brand::red.withAlpha (0.8f));
        g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);
        g.setColour (brand::bordo.brighter (0.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto text = getLocalBounds().reduced (16, 10);
        if (updateButton.isVisible())
            text.removeFromRight (buttonsWidth + 14);

        g.setColour (brand::white);
        g.setFont (f.bold (14.0f));
        g.drawFittedText (TR ("Hay una versión nueva: ") + status.latest.label, text.removeFromTop (20),
                          juce::Justification::centredLeft, 1, 0.85f);

        juce::String sub;
        bool warn = false;

        if (status.state == U::downloading)
            sub = TR ("Bajando el instalador… ") + juce::String (juce::roundToInt (juce::jmax (0.0f, status.progress) * 100.0f)) + " %";
        else if (status.state == U::launching)
            sub = owner.previewGoesThroughHost()
                ? TR ("Se abrió el instalador. Si te pide cerrar Cubase, guardá el proyecto antes.")
                : TR ("Abriendo el instalador… la app se cierra para poder actualizarse.");
        else if (status.state == U::failed)
            { sub = status.error; warn = true; }
        else if (! status.latest.changes.isEmpty())
            sub = TR ("Novedades: ") + status.latest.changes.joinIntoString (TR ("  ·  "));
        else
            sub = TR ("Tocá ACTUALIZAR: se baja el instalador y se abre solo.");

        g.setColour (warn ? brand::yellow : brand::grey);
        g.setFont (f.semi (12.0f));
        g.drawFittedText (sub, text.removeFromTop (32), juce::Justification::topLeft, 2, 0.9f);

        if (status.state == U::downloading || status.state == U::launching)
        {
            auto bar = getLocalBounds().reduced (16, 0).removeFromBottom (10).withHeight (3).toFloat();
            g.setColour (brand::line);
            g.fillRoundedRectangle (bar, 1.5f);
            g.setColour (brand::red);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, status.progress)), 1.5f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10).removeFromRight (buttonsWidth);
        updateButton.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        laterButton.setBounds (r.removeFromTop (22).reduced (14, 0));
    }

private:
    static constexpr int buttonsWidth = 130;
    GrabEditor& owner;
    juce::TextButton updateButton, laterButton;
    grab::Updater::Status status;
};

//==============================================================================
/** Panel de opciones del buscador: elegís cuál bajar. */
class GrabEditor::ResultsPanel final : public juce::Component,
                                       private juce::ListBoxModel
{
public:
    std::function<void()> onClose, onSkip;
    std::function<void (grab::SearchKind)> onKind;

    explicit ResultsPanel (GrabEditor& o) : owner (o)
    {
        for (auto* b : { &songsTab, &videosTab })
        {
            b->setClickingTogglesState (false);
            b->getProperties().set ("tab", true);
            addAndMakeVisible (*b);
        }

        songsTab.setButtonText ("CANCIONES");
        videosTab.setButtonText ("VIDEOS");
        songsTab.onClick  = [this] { if (onKind) onKind (grab::SearchKind::songs); };
        videosTab.onClick = [this] { if (onKind) onKind (grab::SearchKind::videos); };

        closeButton.setButtonText ("CERRAR");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        skipButton.setButtonText ("SALTEAR");
        skipButton.onClick = [this] { if (onSkip) onSkip(); };
        addChildComponent (skipButton);

        list.setModel (this);
        list.setRowHeight (64);
        list.getViewport()->setScrollBarThickness (8);
        list.setWantsKeyboardFocus (false);
        addAndMakeVisible (list);
    }

    ~ResultsPanel() override { list.setModel (nullptr); }

    void setQueueInfo (int index, int total)
    {
        queueIndex = index;
        queueTotal = total;
        skipButton.setVisible (total > 1 && index < total);
        resized();
        repaint();
    }

    void refresh()
    {
        const auto& r = owner.getSearchResult();
        const bool videos = r.request.kind == grab::SearchKind::videos;
        songsTab.setToggleState (! videos, juce::dontSendNotification);
        videosTab.setToggleState (videos, juce::dontSendNotification);
        list.updateContent();
        list.repaint();
        repaint();
    }

    void repaintRows()                        { list.repaint(); }
    void scrollToRow (int row)                { if (row >= 0) list.scrollToEnsureRowIsOnscreen (row); }

    void paint (juce::Graphics& g) override
    {
        using State = grab::Searcher::Result::State;
        const auto& f = owner.getFonts();
        const auto& r = owner.getSearchResult();

        g.setColour (brand::bgDeep.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (brand::line);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

        // Título
        auto t = titleArea;
        const juce::String prefix;

        if (queueTotal > 1)
        {
            g.setColour (brand::grey);
            g.setFont (f.bold (10.5f, 0.12f));
            g.drawText ("TEMA " + juce::String (queueIndex) + " DE " + juce::String (queueTotal), queueArea,
                        juce::Justification::centredRight, false);
        }

        if (r.state == State::resolving)
        {
            g.setColour (brand::white);
            g.setFont (f.title (21.0f, 0.05f));
            g.drawFittedText (prefix + TR ("LEYENDO EL TEMA EN ") + r.request.platform.toUpperCase() + TR ("…"),
                              t, juce::Justification::centredLeft, 1, 0.8f);
        }
        else if (r.hasSource())
        {
            const auto platform = (r.request.platform.isNotEmpty() ? r.request.platform : juce::String ("LINK")).toUpperCase();
            const auto lead = prefix + platform;
            const auto leadFont = f.title (21.0f, 0.05f);
            const auto leadW = juce::GlyphArrangement::getStringWidth (leadFont, lead) + 10.0f;

            g.setColour (brand::red.brighter (0.25f));
            g.setFont (leadFont);
            g.drawText (lead, t.removeFromLeft ((int) leadW), juce::Justification::centredLeft, false);

            g.setColour (brand::white);
            g.setFont (f.bold (14.0f));
            auto line = r.source.displayName();
            if (r.source.durationSec > 0)
                line << TR (" · ") << grab::formatDuration (r.source.durationSec);
            g.drawFittedText (line, t, juce::Justification::centredLeft, 1, 0.85f);
        }
        else
        {
            g.setColour (brand::white);
            g.setFont (f.title (21.0f, 0.05f));
            g.drawFittedText (prefix + TR ("RESULTADOS PARA «") + r.query.toUpperCase() + TR ("»"),
                              t, juce::Justification::centredLeft, 1, 0.75f);
        }

        // Línea de ayuda
        juce::String help;
        bool warn = false;

        using PS = grab::PreviewPlayer::State;
        const auto& ps = owner.getPreviewStatus();
        const bool previewHere = ps.state != PS::idle
                              && std::any_of (r.items.begin(), r.items.end(), [&] (const grab::Candidate& c) { return c.videoId == ps.videoId; });

        if (previewHere && ps.state == PS::loading)
        {
            help = TR ("Cargando 30 s de «") + ps.title + TR ("»…");
        }
        else if (previewHere && ps.state == PS::playing && ps.stalled)
        {
            help = owner.previewGoesThroughHost()
                 ? TR ("No llega sonido: Cubase no está procesando este canal. Probá en un canal activo o con la app suelta.")
                 : TR ("No llega sonido a la salida de audio de Windows.");
            warn = true;
        }
        else if (previewHere && ps.state == PS::playing)
        {
            help = TR ("Escuchando «") + ps.title + "»"
                 + (ps.startSec >= 1.0 ? TR (" desde ") + grab::formatDuration (ps.startSec) : juce::String())
                 + (owner.previewGoesThroughHost() ? TR (" · suena por este canal") : juce::String())
                 + TR (" · tocá la tapa para parar");
        }
        else if (previewHere && ps.state == PS::failed)
        {
            help = TR ("No pude cargar el preview: ") + ps.error;
            warn = true;
        }
        else switch (r.state)
        {
            case State::resolving: help = TR ("Leyendo título, artista y duración del link…"); break;
            case State::searching: help = TR ("Buscando en YouTube Music…"); break;
            case State::failed:    help = r.error; warn = true; break;
            case State::done:
                if (r.items.empty())       { help = TR ("No encontré nada. Probá con otras palabras o en VIDEOS."); warn = true; }
                else if (r.usedFallback)   { help = TR ("YouTube Music no respondió: son resultados de YouTube. Elegí cuál bajar."); warn = true; }
                else if (r.hasSource())    help = TR ("Tocá la tapa para escuchar 30 s · clic en la fila para bajar · «MISMA DURACIÓN» = igual al original");
                else                       help = TR ("Tocá la tapa para escuchar 30 s · clic en la fila (o flechas y Enter) para bajar");
                break;
            case State::idle:
            default: break;
        }

        g.setColour (warn ? brand::yellow : brand::grey);
        g.setFont (f.semi (12.0f));
        g.drawFittedText (help, helpArea, juce::Justification::centredLeft, 1, 0.85f);

        // Animación mientras busca
        if (r.state == State::resolving || r.state == State::searching)
        {
            auto bar = list.getBounds().removeFromTop (3).toFloat();
            g.setColour (brand::line);
            g.fillRect (bar);
            const auto ph = (float) (juce::Time::getMillisecondCounter() % 1200) / 1200.0f;
            const auto w = bar.getWidth() * 0.3f;
            g.setColour (brand::red);
            g.fillRect (bar.withX (bar.getX() + (bar.getWidth() + w) * ph - w).withWidth (w).getIntersection (bar));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);

        // 1) De dónde viene / qué se buscó  ·  CERRAR
        auto top = r.removeFromTop (30);
        closeButton.setBounds (top.removeFromRight (78).reduced (0, 2));
        top.removeFromRight (10);
        titleArea = top;

        // 2) CANCIONES | VIDEOS  ·  TEMA 1 DE 3  SALTEAR
        r.removeFromTop (6);
        auto tabs = r.removeFromTop (28);
        songsTab.setBounds (tabs.removeFromLeft (98));
        tabs.removeFromLeft (6);
        videosTab.setBounds (tabs.removeFromLeft (74));

        if (skipButton.isVisible())
        {
            skipButton.setBounds (tabs.removeFromRight (84));
            tabs.removeFromRight (10);
        }
        queueArea = tabs;

        // 3) Ayuda
        r.removeFromTop (6);
        helpArea = r.removeFromTop (20);
        r.removeFromTop (6);
        list.setBounds (r);
    }

private:
    class ResultRow;

    int getNumRows() override { return (int) owner.getSearchResult().items.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override;

    GrabEditor& owner;
    juce::TextButton songsTab, videosTab, closeButton, skipButton;
    juce::ListBox list;
    juce::Rectangle<int> titleArea, helpArea, queueArea;
    int queueIndex = 0, queueTotal = 0;
};

class GrabEditor::ResultsPanel::ResultRow final : public juce::Component
{
public:
    explicit ResultRow (GrabEditor& o) : owner (o) {}

    void setRow (int r) { row = r; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& found = owner.getSearchResult().items;
        if (! juce::isPositiveAndBelow (row, (int) found.size()))
            return;

        const auto& c = found[(size_t) row];
        const auto& f = owner.getFonts();
        const bool hot = hover || owner.getHighlighted() == row;

        auto r = getLocalBounds().reduced (0, 3);
        g.setColour (hot ? brand::rowHi : brand::row);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);

        if (owner.getHighlighted() == row)
        {
            g.setColour (brand::red);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.2f);
        }

        auto content = r.reduced (8, 6);

        // Tapa
        auto thumb = content.removeFromLeft (46).withSizeKeepingCentre (46, 46).toFloat();
        content.removeFromLeft (12);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (thumb, 4.0f);
            g.reduceClipRegion (clip);

            if (auto img = owner.getThumb (c.thumbUrl); img.isValid())
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (img, thumb, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
            }
            else
            {
                g.setColour (brand::bgDeep);
                g.fillRect (thumb);
                g.setColour (brand::line);
                g.fillRect (thumb.reduced (15.0f, 13.0f).withWidth (5.0f).translated (2.0f, 0.0f));
                g.fillEllipse (thumb.getCentreX() - 9.0f, thumb.getCentreY() + 2.0f, 9.0f, 7.0f);
            }

            // Preview: ▶ al pasar por la tapa, ■ mientras suena, ruedita mientras carga
            using PS = grab::PreviewPlayer::State;
            const auto& ps = owner.getPreviewStatus();
            const bool isThis = ps.videoId == c.videoId && (ps.state == PS::loading || ps.state == PS::playing);

            if (overThumb || isThis)
            {
                g.setColour (juce::Colours::black.withAlpha (overThumb ? 0.6f : 0.5f));
                g.fillRect (thumb);

                const auto cx = thumb.getCentreX(), cy = thumb.getCentreY();
                g.setColour (brand::white);

                if (isThis && ps.state == PS::loading)
                {
                    const auto a = (float) (juce::Time::getMillisecondCounter() % 1000) / 1000.0f * juce::MathConstants<float>::twoPi;
                    juce::Path arc;
                    arc.addCentredArc (cx, cy, 10.0f, 10.0f, 0.0f, a, a + 4.3f, true);
                    g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else if (isThis)
                {
                    g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ cx, cy }), 2.0f);
                }
                else
                {
                    juce::Path tri;
                    tri.addTriangle (cx - 5.0f, cy - 8.0f, cx - 5.0f, cy + 8.0f, cx + 9.0f, cy);
                    g.fillPath (tri);
                }
            }
            else
            {
                // Pista de que la tapa se puede escuchar: ▶ chiquito en la esquina
                const auto bx = thumb.getRight() - 10.0f, by = thumb.getBottom() - 10.0f;
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                g.fillEllipse (bx - 8.0f, by - 8.0f, 16.0f, 16.0f);
                juce::Path tri;
                tri.addTriangle (bx - 2.5f, by - 4.0f, bx - 2.5f, by + 4.0f, bx + 4.5f, by);
                g.setColour (brand::white);
                g.fillPath (tri);
            }
        }

        // Barra del preview a lo largo de la fila
        {
            const auto& ps = owner.getPreviewStatus();
            if (ps.videoId == c.videoId && ps.state == grab::PreviewPlayer::State::playing)
            {
                auto bar = r.toFloat().reduced (6.0f, 0.0f).removeFromBottom (3.0f);
                g.setColour (brand::line);
                g.fillRoundedRectangle (bar, 1.5f);
                g.setColour (ps.stalled ? brand::yellow : brand::red);
                g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * ps.progress)), 1.5f);
            }
        }

        // Derecha: duración + BAJAR
        auto right = content.removeFromRight (hot ? 132 : 56);
        content.removeFromRight (8);

        if (hot)
        {
            auto pill = right.removeFromRight (70).withSizeKeepingCentre (70, 28).toFloat();
            drawPill (g, pill, brand::bordo.brighter (hover && ! overThumb ? 0.25f : 0.0f),
                      brand::white.withAlpha (overThumb ? 0.6f : 1.0f), f.title (17.0f, 0.06f), "BAJAR");
            right.removeFromRight (6);
        }

        g.setColour (c.durationMatch ? brand::white : brand::grey);
        g.setFont (f.bold (12.5f));
        g.drawText (grab::formatDuration (c.duration), right, juce::Justification::centredRight, false);

        // Texto
        g.setColour (brand::white);
        g.setFont (f.bold (14.0f));
        g.drawFittedText (c.title, content.removeFromTop (content.getHeight() / 2).withTrimmedBottom (1),
                          juce::Justification::bottomLeft, 1, 0.85f);

        auto line2 = content.withTrimmedTop (2);

        if (c.durationMatch)
        {
            const auto font = f.bold (9.5f, 0.06f);
            const auto label = TR ("MISMA DURACIÓN");
            const auto w = juce::GlyphArrangement::getStringWidth (font, label) + 14.0f;
            drawPill (g, line2.removeFromLeft ((int) w).withHeight (16).toFloat(), brand::yellow, brand::bg, font, label);
            line2.removeFromLeft (8);
        }

        juce::String sub = c.artist;
        if (c.extra.isNotEmpty())
            sub << (sub.isNotEmpty() ? TR (" · ") : juce::String()) << c.extra;

        g.setColour (brand::grey);
        g.setFont (f.semi (12.0f));
        g.drawFittedText (sub, line2.withHeight (16), juce::Justification::centredLeft, 1, 0.85f);
    }

    void mouseEnter (const juce::MouseEvent& e) override { hover = true; mouseMove (e); repaint(); }
    void mouseExit  (const juce::MouseEvent&) override
    {
        if (overThumb)
            owner.hoverThumb (row, false);
        hover = overThumb = false;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool over = e.x < 8 + 46 + 8;   // la tapa y un poquito alrededor
        if (over != overThumb)
        {
            overThumb = over;
            owner.hoverThumb (row, over);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked() || e.mods.isPopupMenu())
            return;

        if (e.x < 8 + 46 + 8)
            owner.togglePreview (row);
        else
            owner.pick (row);
    }

private:
    GrabEditor& owner;
    int row = -1;
    bool hover = false, overThumb = false;
};

juce::Component* GrabEditor::ResultsPanel::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    auto* r = dynamic_cast<ResultRow*> (existing);

    if (r == nullptr)
    {
        delete existing;
        r = new ResultRow (owner);
        r->setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    r->setRow (row);
    return r;
}

//==============================================================================
GrabEditor::GrabEditor (GrabProcessor& p)
    : AudioProcessorEditor (p), processorRef (p), engine (p.engine), searcher (p.searcher)
{
    setLookAndFeel (&look);
    formats.registerBasicFormats();
    logo = juce::ImageCache::getFromMemory (BinaryData::logocabecera_png, BinaryData::logocabecera_pngSize);

    input.setFont (fonts->semi (15.0f));
    input.setTextToShowWhenEmpty (TR ("Pegá un link o escribí artista y tema"), brand::grey);
    input.setJustification (juce::Justification::centredLeft);
    input.setIndents (12, 0);
    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.setSelectAllWhenFocused (true);
    input.onReturnKey  = [this]
    {
        const auto& r = searchResult;
        if (resultsVisible() && highlighted >= 0 && r.state == grab::Searcher::Result::State::done)
            pick (highlighted);
        else
            startFromUI();
    };
    input.onEscapeKey  = [this] { if (resultsVisible()) closeResults(); };
    input.onTextChange = [this] { handleTyping(); };
    input.addKeyListener (this);
    addAndMakeVisible (input);

    goButton.getProperties().set ("primary", true);
    goButton.onClick = [this] { startFromUI(); };
    addAndMakeVisible (goButton);
    updateGoButton();

    folderButton.getProperties().set ("flat", true);
    folderButton.onClick = [this]
    {
        auto d = engine.getOutputFolder();
        d.createDirectory();
        d.startAsProcess();
    };
    addAndMakeVisible (folderButton);

    settingsButton.setButtonText ("AJUSTES");
    settingsButton.onClick = [this] { showSettings(); };
    addAndMakeVisible (settingsButton);

    clearButton.setButtonText ("LIMPIAR LISTA");
    clearButton.setTooltip (TR ("Saca todo de la lista. Los WAV siguen en la carpeta."));
    clearButton.onClick = [this]
    {
        engine.clearList();
        toast (TR ("Lista limpia. Los archivos siguen en la carpeta."));
    };
    addAndMakeVisible (clearButton);

    banner = std::make_unique<ToolsBanner> (*this);
    addChildComponent (*banner);

    versionBanner = std::make_unique<UpdateBanner> (*this);
    addChildComponent (*versionBanner);

    results = std::make_unique<ResultsPanel> (*this);
    results->onClose = [this] { closeResults(); };
    results->onSkip  = [this] { advanceAfterPick(); };
    results->onKind  = [this] (grab::SearchKind k) { setSearchKind (k); };
    addChildComponent (*results);

    list.setModel (this);
    list.setRowHeight (60);
    list.getViewport()->setScrollBarThickness (8);
    addAndMakeVisible (list);

    setResizable (true, true);
    setResizeLimits (480, 460, 1200, 1200);
    setSize (600, 600);

    searcher.clear();
    searchResult = searcher.getResult();
    lastSearchRevision = searcher.getRevision();

    updateBanner();
    updateFolderLabel();
    rebuildItems();
    startTimerHz (15);
}

GrabEditor::~GrabEditor()
{
    stopTimer();
    processorRef.preview.stop();
    input.removeKeyListener (this);
    list.setModel (nullptr);
    results.reset();
    setLookAndFeel (nullptr);
}

//==============================================================================
void GrabEditor::paint (juce::Graphics& g)
{
    g.fillAll (brand::bg);

    // Cabecera: logo (claraboya + diskette) · CLARABOYA · GRAB sobre bordó
    {
        auto x = (float) headerArea.getX();
        const auto y = (float) headerArea.getY() + 14.0f;

        if (logo.isValid())
        {
            const float h = 46.0f;
            const float w = h * (float) logo.getWidth() / (float) logo.getHeight();
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, juce::Rectangle<float> (x, y - 3.0f, w, h), juce::RectanglePlacement::centred);
            x += w + 14.0f;
        }

        const auto font = fonts->title (40.0f, 0.06f);
        const juce::String a ("CLARABOYA"), b ("GRAB");
        const auto wa = juce::GlyphArrangement::getStringWidth (font, a);
        const auto wb = juce::GlyphArrangement::getStringWidth (font, b);
        const auto bx = x + wa + 14.0f;

        g.setFont (font);
        g.setColour (brand::bordo);
        g.fillRect (juce::Rectangle<float> (bx - 7.0f, y + 1.0f, wb + 14.0f, 38.0f));
        g.setColour (brand::white);
        g.drawText (a, juce::Rectangle<float> (x, y, wa + 4.0f, 40.0f), juce::Justification::centredLeft, false);
        g.drawText (b, juce::Rectangle<float> (bx, y, wb + 4.0f, 40.0f), juce::Justification::centredLeft, false);

        auto tag = headerArea.withTrimmedLeft ((int) (bx + wb + 24.0f) - headerArea.getX()).withTrimmedTop (14).withHeight (40);
        g.setColour (brand::grey);
        g.setFont (fonts->bold (10.5f, 0.16f));
        g.drawFittedText ("DEL LINK AL WAV", tag, juce::Justification::centredRight, 1, 0.8f);
    }

    // Línea de ayuda / avisos
    {
        const bool showToast = juce::Time::getMillisecondCounter() < toastUntil;
        g.setColour (showToast ? (toastError ? brand::yellow : brand::white) : brand::grey.withAlpha (0.75f));
        g.setFont (fonts->semi (11.5f));
        g.drawFittedText (showToast ? toastText
                                    : TR ("YouTube, SoundCloud, Bandcamp: baja directo   ·   Spotify, Apple Music o texto: elegís entre opciones"),
                          hintArea, juce::Justification::centredLeft, 1, 0.75f);
    }

    if (! resultsVisible())
    {
        // Encabezado de la lista
        g.setColour (brand::grey);
        g.setFont (fonts->bold (10.5f, 0.16f));
        g.drawText ("RECIENTES", listHeaderArea, juce::Justification::centredLeft, false);

        if (items.empty())
        {
            auto r = list.getBounds().withTrimmedTop (list.getHeight() / 4);
            g.setColour (brand::grey.withAlpha (0.5f));
            g.setFont (fonts->title (32.0f, 0.06f));
            g.drawText (TR ("COPIÁ UN LINK O ESCRIBÍ UN TEMA"), r.removeFromTop (40), juce::Justification::centred, false);
            g.setFont (fonts->semi (12.5f));
            g.drawFittedText (TR ("Lo bajo como WAV y lo arrastrás a un track de Cubase."),
                              r.removeFromTop (22), juce::Justification::centred, 1);
        }
    }

    // Pie
    g.setColour (brand::line);
    g.drawHorizontalLine (footerArea.getY(), (float) footerArea.getX(), (float) footerArea.getRight());
    g.setColour (brand::grey);
    g.setFont (fonts->bold (10.0f, 0.1f));
    g.drawFittedText ("WAV 24 BIT  " + grab::formatRate (engine.getTargetSampleRate()).toUpperCase(),
                      footerInfoArea, juce::Justification::centredRight, 1, 0.8f);
}

void GrabEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 0);

    headerArea = r.removeFromTop (72);

    auto row = r.removeFromTop (46);
    goButton.setBounds (row.removeFromRight (168));
    row.removeFromRight (8);
    input.setBounds (row);

    r.removeFromTop (4);
    hintArea = r.removeFromTop (22);
    r.removeFromTop (6);

    if (banner->isVisible())
    {
        banner->setBounds (r.removeFromTop (74));
        r.removeFromTop (10);
    }

    if (versionBanner->isVisible())
    {
        versionBanner->setBounds (r.removeFromTop (74));
        r.removeFromTop (10);
    }

    footerArea = r.removeFromBottom (50);
    auto f = footerArea.withTrimmedTop (1).reduced (0, 11);
    settingsButton.setBounds (f.removeFromRight (92));
    f.removeFromRight (12);
    footerInfoArea = f.removeFromRight (120);
    f.removeFromRight (8);
    folderButton.setBounds (f);

    auto body = r.withTrimmedBottom (8);
    results->setBounds (body);

    listHeaderArea = body.removeFromTop (26);
    clearButton.setBounds (listHeaderArea.removeFromRight (124).reduced (0, 1));
    body.removeFromTop (2);
    list.setBounds (body);
}

//==============================================================================
const GrabEditor::Item* GrabEditor::getItem (int row) const
{
    return juce::isPositiveAndBelow (row, (int) items.size()) ? &items[(size_t) row] : nullptr;
}

juce::File GrabEditor::dragFileForRow (int row) const
{
    if (const auto* it = getItem (row); it != nullptr && it->draggable())
        return it->file;
    return {};
}

void GrabEditor::rowClicked (int row, bool onRightZone)
{
    const auto* it = getItem (row);
    if (it == nullptr || ! onRightZone)
        return;

    if (it->kind == Item::Kind::active)       engine.cancel (it->jobId);
    else if (it->kind == Item::Kind::failed)  engine.dismiss (it->jobId);
}

void GrabEditor::rowDoubleClicked (int row)
{
    if (const auto* it = getItem (row); it != nullptr && it->draggable())
        it->file.revealToUser();
}

void GrabEditor::showRowMenu (int row, juce::Component* target)
{
    const auto* it = getItem (row);
    if (it == nullptr)
        return;

    const auto item = *it;
    juce::Component::SafePointer<GrabEditor> safe (this);
    juce::PopupMenu m;

    if (item.draggable())
    {
        m.addItem (TR ("Mostrar en la carpeta"), [f = item.file] { f.revealToUser(); });
        m.addItem (TR ("Copiar ruta"), [f = item.file] { juce::SystemClipboard::copyTextToClipboard (f.getFullPathName()); });
        m.addItem (TR ("Sacar de la lista (no borra el archivo)"), [safe, f = item.file]
        {
            if (safe != nullptr) safe->engine.forget (f);
        });
        m.addSeparator();
        m.addItem (TR ("Mandar el archivo a la papelera"), [safe, f = item.file]
        {
            f.moveToTrash();
            if (safe != nullptr) safe->engine.forget (f);
        });
    }
    else if (item.kind == Item::Kind::failed)
    {
        m.addItem (TR ("Reintentar"), [safe, id = item.jobId] { if (safe != nullptr) safe->engine.retry (id); });
        m.addItem (TR ("Quitar de la lista"), [safe, id = item.jobId] { if (safe != nullptr) safe->engine.dismiss (id); });
    }
    else
    {
        m.addItem (TR ("Cancelar descarga"), [safe, id = item.jobId] { if (safe != nullptr) safe->engine.cancel (id); });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

int GrabEditor::getNumRows()
{
    return (int) items.size();
}

juce::Component* GrabEditor::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    auto* r = dynamic_cast<Row*> (existing);

    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }

    r->setRow (row);
    return r;
}

//==============================================================================
// Buscador

void GrabEditor::showResultsForTesting (const grab::Searcher::Result& r, int queueIndex, int queueTotal, int highlight)
{
    searcher.injectForTesting (r);
    searchResult = searcher.getResult();
    lastSearchRevision = searcher.getRevision();
    pendingIndex = queueIndex;
    pendingTotal = queueTotal;
    results->setQueueInfo (queueIndex, queueTotal);
    results->refresh();
    highlighted = highlight;
    showResults (true);
}

bool GrabEditor::resultsVisible() const
{
    return results != nullptr && results->isVisible();
}

void GrabEditor::showResults (bool show)
{
    if (show == resultsVisible())
        return;

    results->setVisible (show);
    list.setVisible (! show);
    clearButton.setVisible (! show && ! items.empty());
    repaint();
}

void GrabEditor::setInputSilently (const juce::String& t)
{
    const juce::ScopedValueSetter<bool> svs (suppressTyping, true);
    input.setText (t, false);
    updateGoButton();
}

void GrabEditor::updateGoButton()
{
    const auto text = input.getText().trim();
    juce::String platform;

    if (text.isEmpty())
        goButton.setButtonText (TR ("PEGAR Y BAJAR"));
    else if (grab::classify (text, platform) == grab::LinkKind::direct)
        goButton.setButtonText (TR ("BAJAR"));
    else
        goButton.setButtonText (TR ("BUSCAR"));
}

void GrabEditor::requestSearch (const juce::String& text, bool resolveLink, const grab::TrackQuery& expected)
{
    grab::Searcher::Request req;
    req.text = text;
    req.kind = searchKind;
    req.resolveLink = resolveLink;
    req.expected = expected;

    if (resolveLink)
    {
        grab::classify (text, req.platform);
        lastLinkRequested = text;
    }
    else if (expected.title.isNotEmpty())
    {
        req.platform = searchResult.request.platform;
    }

    searcher.request (req);
    highlighted = -1;
    showResults (true);
}

void GrabEditor::setSearchKind (grab::SearchKind k)
{
    searchKind = k;
    const auto& r = searchResult;

    if (r.query.isNotEmpty())
        requestSearch (r.query, false, r.source);
    else if (r.request.resolveLink && r.request.text.isNotEmpty())
        requestSearch (r.request.text, true);
}

void GrabEditor::handleTyping()
{
    updateGoButton();

    if (suppressTyping)
        return;

    const auto text = input.getText().trim();
    searchDue = 0;

    if (text.isEmpty())
    {
        if (resultsVisible() && pending.isEmpty())
            closeResults();
        return;
    }

    juce::String platform;
    const auto kind = grab::classify (text, platform);

    if (kind == grab::LinkKind::textSearch)
    {
        // Busca solo cuando dejás de escribir un ratito.
        if (text.length() >= 2)
            searchDue = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter() + 550);
    }
    else if (kind == grab::LinkKind::streaming)
    {
        if (text != lastLinkRequested)
            requestSearch (text, true);
    }
    else if (resultsVisible() && pending.isEmpty())
    {
        closeResults();
        setInputSilently (text);
    }
}

void GrabEditor::setHighlighted (int i)
{
    const auto n = (int) searchResult.items.size();
    highlighted = n == 0 ? -1 : juce::jlimit (0, n - 1, i);
    results->scrollToRow (highlighted);
    results->repaintRows();
}

bool GrabEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (! resultsVisible() || searchResult.items.empty())
        return false;

    if (key == juce::KeyPress::downKey) { setHighlighted (highlighted + 1); return true; }
    if (key == juce::KeyPress::upKey)   { setHighlighted (highlighted <= 0 ? 0 : highlighted - 1); return true; }
    return false;
}

void GrabEditor::togglePreview (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) searchResult.items.size()))
        return;

    const auto& c = searchResult.items[(size_t) index];
    const auto st = processorRef.preview.getStatus();
    using PS = grab::PreviewPlayer::State;

    if (st.videoId == c.videoId && (st.state == PS::loading || st.state == PS::playing))
    {
        processorRef.preview.stop();
    }
    else if (! engine.getToolStatus().installed)
    {
        toast (TR ("Primero tocá INSTALAR (es una sola vez)."), true);
    }
    else
    {
        processorRef.preview.play (c.videoId, c.url(), c.niceName(), c.duration);
        highlighted = index;
    }

    previewStatus = processorRef.preview.getStatus();
    results->repaint();
}

void GrabEditor::hoverThumb (int index, bool over)
{
    if (over)
    {
        hoverRow = index;
        hoverSince = juce::Time::getMillisecondCounter();
    }
    else if (hoverRow == index)
    {
        hoverRow = -1;
    }
}

void GrabEditor::prefetchItem (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) searchResult.items.size()))
        return;

    const auto& c = searchResult.items[(size_t) index];

    if (c.videoId != lastPrefetchId)
    {
        lastPrefetchId = c.videoId;
        processorRef.preview.prefetch (c.videoId, c.url());
    }
}

void GrabEditor::pick (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) searchResult.items.size()))
        return;

    if (! engine.getToolStatus().installed)
    {
        toast (TR ("Primero tocá INSTALAR (es una sola vez)."), true);
        return;
    }

    const auto c = searchResult.items[(size_t) index];
    processorRef.preview.stop();
    engine.enqueueDownload (c.url(), c.title, c.artist);
    toast (TR ("Bajando: ") + c.niceName());
    advanceAfterPick();
}

void GrabEditor::advanceAfterPick()
{
    if (! pending.isEmpty())
        openNextPending();
    else
        closeResults();
}

void GrabEditor::openNextPending()
{
    if (pending.isEmpty())
        return;

    const auto text = pending[0];
    pending.remove (0);
    ++pendingIndex;

    setInputSilently (text);
    results->setQueueInfo (pendingIndex, pendingTotal);

    juce::String platform;
    requestSearch (text, grab::classify (text, platform) == grab::LinkKind::streaming);
}

void GrabEditor::closeResults()
{
    processorRef.preview.stop();
    pending.clear();
    pendingTotal = pendingIndex = 0;
    searchDue = 0;
    highlighted = -1;
    lastLinkRequested = {};
    searcher.clear();
    results->setQueueInfo (0, 0);
    setInputSilently ({});
    showResults (false);
}

void GrabEditor::startFromUI()
{
    auto text = input.getText().trim();
    const bool fromClipboard = text.isEmpty();

    if (fromClipboard)
        text = juce::SystemClipboard::getTextFromClipboard().trim();

    if (text.isEmpty())
    {
        toast (TR ("No hay nada copiado. Copiá un link y tocá de nuevo."), true);
        return;
    }

    auto lines = juce::StringArray::fromLines (text);
    lines.trim();
    lines.removeEmptyStrings();

    if (fromClipboard)
    {
        // Lo copiado puede ser cualquier cosa: solo links o textos cortos.
        for (auto& l : lines)
        {
            juce::String platform;
            if (lines.size() > 25 || (grab::classify (l, platform) == grab::LinkKind::textSearch && l.length() > 90))
            {
                setInputSilently (text.substring (0, 300));
                toast (TR ("Lo copiado no parece un link. Revisalo en el campo y tocá BUSCAR."), true);
                return;
            }
        }
    }

    juce::StringArray direct, toChoose;

    for (auto& l : lines)
    {
        juce::String platform;
        (grab::classify (l, platform) == grab::LinkKind::direct ? direct : toChoose).add (l);
    }

    if (! direct.isEmpty())
    {
        if (! engine.getToolStatus().installed)
        {
            toast (TR ("Primero tocá INSTALAR (es una sola vez)."), true);
            return;
        }

        for (auto& l : direct)
            engine.enqueueDownload (l);

        toast (direct.size() == 1 ? TR ("En camino…") : TR ("En camino: ") + juce::String (direct.size()) + TR (" links"));
    }

    if (toChoose.isEmpty())
    {
        setInputSilently ({});
        if (resultsVisible() && pending.isEmpty())
            closeResults();
        return;
    }

    // Spotify, Apple, texto: uno por uno, eligiendo la opción.
    searchDue = 0;
    pending = toChoose;
    pendingTotal = toChoose.size();
    pendingIndex = 0;
    lastLinkRequested = {};
    openNextPending();
}

//==============================================================================
void GrabEditor::showSettings()
{
    const auto st = engine.getToolStatus();
    juce::Component::SafePointer<GrabEditor> safe (this);
    juce::PopupMenu m;

    m.addSectionHeader ("CLARABOYA GRAB " + grab::Updater::currentLabel().toUpperCase()
                        + (st.version.isNotEmpty() ? "   yt-dlp " + st.version : juce::String()));
    m.addItem (TR ("Buscar versión nueva de Claraboya Grab"), [safe]
    {
        if (safe == nullptr) return;
        safe->awaitingUpdateCheck = true;
        safe->getUpdater().checkNow();
        safe->toast (TR ("Buscando versión nueva…"));
    });
    m.addSeparator();
    m.addItem (TR ("Cambiar carpeta de descarga…"), [safe] { if (safe != nullptr) safe->chooseFolder(); });
    m.addItem (TR ("Abrir carpeta"), [safe]
    {
        if (safe == nullptr) return;
        auto d = safe->engine.getOutputFolder();
        d.createDirectory();
        d.startAsProcess();
    });
    m.addSeparator();

    if (engine.followsHostRate())
    {
        m.addItem (TR ("Frecuencia del WAV: la de la sesión (") + grab::formatRate (engine.getTargetSampleRate()) + ")",
                   false, true, [] {});
    }
    else
    {
        juce::PopupMenu rates;
        for (auto sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
            rates.addItem (grab::formatRate (sr), true, std::abs (engine.getAppSampleRate() - sr) < 1.0,
                           [safe, sr] { if (safe != nullptr) { safe->engine.setAppSampleRate (sr); safe->repaint(); } });
        m.addSubMenu (TR ("Frecuencia del WAV (") + grab::formatRate (engine.getAppSampleRate()) + ")", rates);
    }

    m.addItem (TR ("Usar sesión de Firefox (si YouTube pide iniciar sesión)"), true, engine.getUseFirefoxCookies(), [safe]
    {
        if (safe != nullptr)
            safe->engine.setUseFirefoxCookies (! safe->engine.getUseFirefoxCookies());
    });
    m.addSeparator();
    m.addItem (TR ("Actualizar yt-dlp"), st.installed && ! st.busy, false, [safe] { if (safe != nullptr) safe->engine.enqueueUpdateYtdlp(); });
    if (grab::Tools::canAutoInstall())
        m.addItem (TR ("Reinstalar herramientas"), ! st.busy, false, [safe] { if (safe != nullptr) safe->engine.enqueueInstallTools (true); });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton).withMinimumWidth (340));
}

void GrabEditor::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (TR ("Elegí dónde guardar los WAV"), engine.getOutputFolder());
    juce::Component::SafePointer<GrabEditor> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (! f.isDirectory()) return;

                              safe->engine.setOutputFolder (f);
                              safe->updateFolderLabel();
                              safe->toast (TR ("Carpeta cambiada. Lo nuevo se guarda ahí."));
                          });
}

void GrabEditor::updateFolderLabel()
{
    const auto dir = engine.getOutputFolder();
    lastFolder = dir;

    auto path = dir.getFullPathName();
    if (path.length() > 46)
        path = juce::String::fromUTF8 ("…") + juce::File::getSeparatorString()
             + dir.getParentDirectory().getFileName() + juce::File::getSeparatorString() + dir.getFileName();

    folderButton.setButtonText (TR ("Carpeta:  ") + path);
}

juce::String GrabEditor::fileInfo (const juce::File& f)
{
    const auto key = f.getFullPathName() + "@" + juce::String (f.getLastModificationTime().toMilliseconds());

    if (auto found = infoCache.find (key); found != infoCache.end())
        return found->second;

    juce::String s;

    if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f)); r != nullptr && r->sampleRate > 0)
        s = grab::formatDuration ((double) r->lengthInSamples / r->sampleRate) + TR (" · ")
          + grab::formatRate (r->sampleRate) + " / " + juce::String ((int) r->bitsPerSample) + " bit";

    infoCache[key] = s;
    return s;
}

void GrabEditor::rebuildItems()
{
    std::vector<Item> next;

    const auto jobs = engine.getJobs();

    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
    {
        Item i;
        i.jobId = it->id;
        i.title = it->title;
        i.sub = it->detail;
        i.progress = it->progress;
        i.kind = it->state == grab::Job::State::failed ? Item::Kind::failed : Item::Kind::active;
        next.push_back (i);
    }

    for (auto& h : engine.getHistory())
    {
        if (! h.file.existsAsFile())
            continue;

        Item i;
        i.kind = Item::Kind::file;
        i.file = h.file;
        i.title = h.title.isNotEmpty() ? h.title : h.file.getFileNameWithoutExtension();

        auto info = h.detail.isNotEmpty() ? h.detail : fileInfo (h.file);
        const auto when = whenText (h.time);
        i.sub = info.isNotEmpty() && when.isNotEmpty() ? info + TR (" · ") + when : info + when;
        next.push_back (i);
    }

    juce::String sig;
    for (auto& i : next)
        sig << (int) i.kind << '|' << i.title << '|' << i.sub << '|' << (int) (i.progress * 100.0f) << '\n';

    const bool emptinessChanged = next.empty() != items.empty();
    items = std::move (next);

    if (sig != lastSignature)
    {
        lastSignature = sig;
        list.updateContent();
        list.repaint();
    }

    clearButton.setVisible (! resultsVisible() && ! items.empty());

    if (emptinessChanged)
        repaint();
}

void GrabEditor::updateUpdateBanner()
{
    using U = grab::Updater::State;
    const auto st = getUpdater().getStatus();

    // Resultado de "Buscar versión nueva" (a mano)
    if (awaitingUpdateCheck && st.state != U::checking)
    {
        if (st.state == U::upToDate)
        {
            awaitingUpdateCheck = false;
            toast (TR ("Tenés la última versión (") + grab::Updater::currentLabel() + ").");
        }
        else if (st.state == U::failed || st.state == U::available)
        {
            awaitingUpdateCheck = false;
            if (st.state == U::failed)
                toast (st.error, true);
        }
    }

    const bool show = (st.state == U::available && ! st.dismissed)
                   || st.state == U::downloading || st.state == U::launching
                   || (st.state == U::failed && st.latest.build > grab::Updater::currentBuild() && ! st.dismissed);

    versionBanner->setStatus (st);

    if (show != versionBanner->isVisible())
    {
        versionBanner->setVisible (show);
        resized();
        repaint();
    }
}

void GrabEditor::updateBanner()
{
    const auto st = engine.getToolStatus();
    const bool show = ! st.installed || st.busy;

    if (toolsWereBusy && ! st.busy && st.text.isNotEmpty() && st.installed)
        toast (st.text, st.lastFailed);

    toolsWereBusy = st.busy;
    banner->setStatus (st);

    if (show != banner->isVisible())
    {
        banner->setVisible (show);
        resized();
        repaint();
    }
}

void GrabEditor::toast (const juce::String& s, bool isError)
{
    toastText = s;
    toastError = isError;
    toastUntil = juce::Time::getMillisecondCounter() + 4000;
    repaint (hintArea);
}

void GrabEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    // Búsqueda mientras escribís
    if (searchDue != 0 && now >= searchDue)
    {
        searchDue = 0;
        const auto text = input.getText().trim();
        juce::String platform;

        if (text.length() >= 2 && grab::classify (text, platform) == grab::LinkKind::textSearch)
            requestSearch (text, false);
    }

    if (const auto rev = searcher.getRevision(); rev != lastSearchRevision)
    {
        lastSearchRevision = rev;
        searchResult = searcher.getResult();

        if (highlighted >= (int) searchResult.items.size())
            highlighted = -1;

        resultsStableSince = 0;
        results->refresh();
    }

    // Preview más rápido: resolver por adelantado la opción bajo el mouse (a los 150 ms)
    // y la primera de cada búsqueda (cuando los resultados quedan quietos 0,7 s).
    if (resultsVisible() && searchResult.state == grab::Searcher::Result::State::done && ! searchResult.items.empty()
        && engine.getToolStatus().installed)
    {
        if (hoverRow >= 0 && now - hoverSince > 150)
            prefetchItem (hoverRow);

        if (searchResult.generation != prefetchedGeneration)
        {
            if (resultsStableSince == 0)
                resultsStableSince = juce::jmax ((juce::uint32) 1, now);
            else if (now - resultsStableSince > 700)
            {
                prefetchedGeneration = searchResult.generation;
                prefetchItem (0);
            }
        }
    }

    // Preview: estado, progreso, y cortarlo si su opción ya no está en la lista
    {
        using PS = grab::PreviewPlayer::State;
        const auto rev = processorRef.preview.getRevision();
        const bool active = previewStatus.state == PS::loading || previewStatus.state == PS::playing;

        if (rev != lastPreviewRevision || active)
        {
            lastPreviewRevision = rev;
            previewStatus = processorRef.preview.getStatus();
            const bool nowActive = previewStatus.state == PS::loading || previewStatus.state == PS::playing;

            if (nowActive && (! resultsVisible()
                              || std::none_of (searchResult.items.begin(), searchResult.items.end(),
                                               [this] (const grab::Candidate& c) { return c.videoId == previewStatus.videoId; })))
            {
                processorRef.preview.stop();
                previewStatus = processorRef.preview.getStatus();
            }

            if (resultsVisible())
                results->repaint();   // fila, barra y línea de ayuda
        }
    }

    if (const auto rev = processorRef.thumbs.getRevision(); rev != lastThumbRevision)
    {
        lastThumbRevision = rev;
        if (resultsVisible())
            results->repaintRows();
    }

    if (resultsVisible())
    {
        using State = grab::Searcher::Result::State;
        if (searchResult.state == State::resolving || searchResult.state == State::searching)
            results->repaint();
    }

    if (engine.getOutputFolder() != lastFolder)
        updateFolderLabel();

    if (const auto rev = getUpdater().getRevision(); rev != lastUpdaterRevision)
    {
        lastUpdaterRevision = rev;
        updateUpdateBanner();
    }

    if (const auto rev = engine.getRevision(); rev != lastRevision || now - lastRebuild > 3000)
    {
        lastRevision = rev;
        lastRebuild = now;
        rebuildItems();
        updateBanner();
        repaint (footerInfoArea);
    }

    for (auto& i : items)
        if (i.kind == Item::Kind::active && i.progress < 0.0f)
            { list.repaint(); break; }

    if (toastUntil != 0 && now >= toastUntil)
    {
        toastUntil = 0;
        repaint (hintArea);
    }
}
