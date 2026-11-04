#include "PluginEditor.h"
#include "Resolver.h"
#include "Tools.h"

using grab::TR;

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

        const auto stripe = it->kind == K::active  ? brand::red
                          : it->kind == K::failed  ? brand::yellow.withAlpha (0.8f)
                          : it->kind == K::doneJob ? brand::white
                                                   : brand::line;
        g.setColour (stripe);
        g.fillRoundedRectangle (rf.withWidth (4.0f), 2.0f);

        auto content = r.reduced (16, 8);
        auto right = content.removeFromRight (it->draggable() ? 96 : 84);
        content.removeFromRight (8);

        g.setColour (brand::white);
        g.setFont (f.bold (14.5f));
        g.drawFittedText (it->title, content.removeFromTop (21), juce::Justification::centredLeft, 1, 0.9f);

        const bool warn = it->kind == K::failed || it->warning.isNotEmpty();
        g.setColour (warn ? brand::yellow : brand::grey);
        g.setFont (f.semi (12.0f));
        g.drawFittedText (it->warning.isNotEmpty() ? it->warning : it->sub,
                          content.removeFromTop (17), juce::Justification::centredLeft, 1, 0.85f);

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
                // Sin porcentaje: un bloque que va y viene.
                const auto t = (float) (juce::Time::getMillisecondCounter() % 1400) / 1400.0f;
                const auto w = bar.getWidth() * 0.25f;
                const auto x = bar.getX() + (bar.getWidth() - w) * (0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi));
                g.fillRoundedRectangle (bar.withX (x).withWidth (w), 2.0f);
            }
        }

        // Zona derecha
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
GrabEditor::GrabEditor (GrabProcessor& p)
    : AudioProcessorEditor (p), processorRef (p), engine (p.engine)
{
    setLookAndFeel (&look);
    formats.registerBasicFormats();

    input.setFont (fonts->semi (15.0f));
    input.setTextToShowWhenEmpty (TR ("Pegá un link o escribí artista y tema"), brand::grey);
    input.setJustification (juce::Justification::centredLeft);
    input.setIndents (12, 0);
    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.setSelectAllWhenFocused (true);
    input.onReturnKey  = [this] { startFromUI(); };
    input.onTextChange = [this] { goButton.setButtonText (input.isEmpty() ? TR ("PEGAR Y BAJAR") : TR ("BAJAR")); };
    addAndMakeVisible (input);

    goButton.setButtonText (TR ("PEGAR Y BAJAR"));
    goButton.getProperties().set ("primary", true);
    goButton.onClick = [this] { startFromUI(); };
    addAndMakeVisible (goButton);

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

    banner = std::make_unique<ToolsBanner> (*this);
    addChildComponent (*banner);

    list.setModel (this);
    list.setRowHeight (60);
    list.getViewport()->setScrollBarThickness (8);
    addAndMakeVisible (list);

    setResizable (true, true);
    setResizeLimits (460, 420, 1100, 1100);
    setSize (580, 540);

    updateBanner();
    rescanFolder();
    rebuildItems();
    startTimerHz (12);
}

GrabEditor::~GrabEditor()
{
    stopTimer();
    list.setModel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void GrabEditor::paint (juce::Graphics& g)
{
    g.fillAll (brand::bg);

    // Título: CLARABOYA + GRAB sobre bordó (como "AL MASTER" en los reels)
    {
        const auto font = fonts->title (40.0f, 0.06f);
        const juce::String a ("CLARABOYA"), b ("GRAB");
        const auto wa = juce::GlyphArrangement::getStringWidth (font, a);
        const auto wb = juce::GlyphArrangement::getStringWidth (font, b);

        const auto x = (float) headerArea.getX();
        const auto y = (float) headerArea.getY() + 16.0f;
        const auto bx = x + wa + 14.0f;

        g.setFont (font);
        g.setColour (brand::bordo);
        g.fillRect (juce::Rectangle<float> (bx - 7.0f, y + 1.0f, wb + 14.0f, 38.0f));
        g.setColour (brand::white);
        g.drawText (a, juce::Rectangle<float> (x, y, wa + 4.0f, 40.0f), juce::Justification::centredLeft, false);
        g.drawText (b, juce::Rectangle<float> (bx, y, wb + 4.0f, 40.0f), juce::Justification::centredLeft, false);

        auto tag = headerArea.withTrimmedLeft ((int) (bx + wb + 24.0f) - headerArea.getX()).withTrimmedTop (16).withHeight (40);
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
                                    : TR ("YouTube · SoundCloud · Bandcamp · Spotify · Apple Music · Deezer · TIDAL · y más"),
                          hintArea, juce::Justification::centredLeft, 1, 0.8f);
    }

    // Lista vacía
    if (items.empty())
    {
        auto r = list.getBounds().withTrimmedTop (list.getHeight() / 4);
        g.setColour (brand::grey.withAlpha (0.5f));
        g.setFont (fonts->title (34.0f, 0.06f));
        g.drawText (TR ("COPIÁ UN LINK Y TOCÁ PEGAR"), r.removeFromTop (40), juce::Justification::centred, false);
        g.setFont (fonts->semi (12.5f));
        g.drawFittedText (TR ("Lo bajo como WAV y lo arrastrás a un track de Cubase."),
                          r.removeFromTop (22), juce::Justification::centred, 1);
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

    headerArea = r.removeFromTop (68);

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

    footerArea = r.removeFromBottom (50);
    auto f = footerArea.withTrimmedTop (1).reduced (0, 11);
    settingsButton.setBounds (f.removeFromRight (92));
    f.removeFromRight (12);
    footerInfoArea = f.removeFromRight (120);
    f.removeFromRight (8);
    folderButton.setBounds (f);

    list.setBounds (r.withTrimmedBottom (6));
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
        m.addSeparator();
        m.addItem (TR ("Mandar a la papelera"), [safe, item]
        {
            item.file.moveToTrash();
            if (safe == nullptr) return;
            if (item.jobId != 0) safe->engine.dismiss (item.jobId);
            safe->rescanFolder();
            safe->rebuildItems();
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

//==============================================================================
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

    if (! engine.getToolStatus().installed)
    {
        toast (TR ("Primero tocá INSTALAR (es una sola vez)."), true);
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
                input.setText (text.substring (0, 300), false);
                toast (TR ("Lo copiado no parece un link. Revisalo en el campo y tocá BAJAR."), true);
                goButton.setButtonText (TR ("BAJAR"));
                return;
            }
        }
    }

    for (auto& l : lines)
        engine.enqueueDownload (l);

    input.clear();
    goButton.setButtonText (TR ("PEGAR Y BAJAR"));
    toast (lines.size() == 1 ? TR ("En camino…") : TR ("En camino: ") + juce::String (lines.size()) + TR (" temas"));
    rebuildItems();
}

void GrabEditor::showSettings()
{
    const auto st = engine.getToolStatus();
    juce::Component::SafePointer<GrabEditor> safe (this);
    juce::PopupMenu m;

    m.addSectionHeader ("CLARABOYA GRAB 1.0" + (st.version.isNotEmpty() ? "   yt-dlp " + st.version : juce::String()));
    m.addItem (TR ("Cambiar carpeta de descarga…"), [safe] { if (safe != nullptr) safe->chooseFolder(); });
    m.addItem (TR ("Abrir carpeta"), [safe]
    {
        if (safe == nullptr) return;
        auto d = safe->engine.getOutputFolder();
        d.createDirectory();
        d.startAsProcess();
    });
    m.addSeparator();
    m.addItem (TR ("Usar sesión de Firefox (si YouTube pide iniciar sesión)"), true, engine.getUseFirefoxCookies(), [safe]
    {
        if (safe != nullptr)
            safe->engine.setUseFirefoxCookies (! safe->engine.getUseFirefoxCookies());
    });
    m.addSeparator();
    m.addItem (TR ("Actualizar yt-dlp"), st.installed && ! st.busy, false, [safe] { if (safe != nullptr) safe->engine.enqueueUpdateYtdlp(); });
   #if JUCE_WINDOWS
    m.addItem (TR ("Reinstalar herramientas"), ! st.busy, false, [safe] { if (safe != nullptr) safe->engine.enqueueInstallTools (true); });
   #endif
    m.addItem (TR ("Limpiar la lista de recientes"), [safe] { if (safe != nullptr) safe->engine.clearFinished(); });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton).withMinimumWidth (320));
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
                              safe->rescanFolder();
                              safe->rebuildItems();
                              safe->toast (TR ("Carpeta cambiada"));
                          });
}

//==============================================================================
void GrabEditor::rescanFolder()
{
    const auto dir = engine.getOutputFolder();
    auto files = dir.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.wav");

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });

    if (files.size() > 80)
        files.removeRange (80, files.size() - 80);

    folderFiles = files;
    lastFolder = dir;
    lastScan = juce::Time::getMillisecondCounter();

    // Ruta corta: …\Carpeta\Subcarpeta
    auto path = dir.getFullPathName();
    if (path.length() > 46)
        path = juce::String::fromUTF8 ("…") + juce::File::getSeparatorString()
             + dir.getParentDirectory().getFileName() + juce::File::getSeparatorString() + dir.getFileName();

    folderButton.setButtonText (TR ("Carpeta:  ") + path);
}

juce::String GrabEditor::fileInfo (const juce::File& f)
{
    const auto modified = f.getLastModificationTime();
    const auto key = f.getFullPathName() + "@" + juce::String (modified.toMilliseconds());

    if (auto found = infoCache.find (key); found != infoCache.end())
        return found->second;

    juce::String s;

    if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f)); r != nullptr && r->sampleRate > 0)
        s = grab::formatDuration ((double) r->lengthInSamples / r->sampleRate) + TR (" · ")
          + grab::formatRate (r->sampleRate) + " / " + juce::String ((int) r->bitsPerSample) + " bit";

    const auto now = juce::Time::getCurrentTime();
    const bool today = modified.getYear() == now.getYear() && modified.getDayOfYear() == now.getDayOfYear();
    const auto when = today ? "hoy " + modified.formatted ("%H:%M") : modified.formatted ("%d/%m %H:%M");

    s = s.isNotEmpty() ? s + TR (" · ") + when : when;
    infoCache[key] = s;
    return s;
}

void GrabEditor::rebuildItems()
{
    std::vector<Item> next;
    juce::Array<juce::File> fromJobs;

    const auto jobs = engine.getJobs();

    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
    {
        Item i;
        i.jobId = it->id;
        i.title = it->title;
        i.sub = it->detail;
        i.warning = it->warning;
        i.progress = it->progress;

        if (it->state == grab::Job::State::done)
        {
            if (! it->result.existsAsFile())
            {
                engine.dismiss (it->id);
                continue;
            }

            i.kind = Item::Kind::doneJob;
            i.file = it->result;
            fromJobs.add (it->result);
        }
        else
        {
            i.kind = it->state == grab::Job::State::failed ? Item::Kind::failed : Item::Kind::active;
        }

        next.push_back (i);
    }

    for (auto& f : folderFiles)
    {
        if (fromJobs.contains (f) || ! f.existsAsFile())
            continue;

        Item i;
        i.kind = Item::Kind::file;
        i.file = f;
        i.title = f.getFileNameWithoutExtension();
        i.sub = fileInfo (f);
        next.push_back (i);
    }

    juce::String sig;
    for (auto& i : next)
        sig << (int) i.kind << '|' << i.title << '|' << i.sub << '|' << i.warning << '|' << (int) (i.progress * 100.0f) << '\n';

    const bool emptinessChanged = next.empty() != items.empty();
    items = std::move (next);

    if (sig != lastSignature)
    {
        lastSignature = sig;
        list.updateContent();
        list.repaint();
    }

    if (emptinessChanged)
        repaint();
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
    const auto rev = engine.getRevision();

    if (engine.getOutputFolder() != lastFolder || now - lastScan > 1500)
    {
        rescanFolder();
        rebuildItems();
    }

    if (rev != lastRevision)
    {
        lastRevision = rev;
        rebuildItems();
        updateBanner();
        repaint (footerInfoArea);
    }

    // Animación de las barras sin porcentaje.
    for (auto& i : items)
        if (i.kind == Item::Kind::active && i.progress < 0.0f)
            { list.repaint(); break; }

    if (toastUntil != 0 && now >= toastUntil)
    {
        toastUntil = 0;
        repaint (hintArea);
    }
}
