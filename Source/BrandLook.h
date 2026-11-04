#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

// Paleta de Claraboya (brand kit).
namespace brand
{
    const juce::Colour bg     { 0xff1f2426 };   // gris oscuro
    const juce::Colour bgDeep { 0xff161a1b };
    const juce::Colour row    { 0xff262c2e };
    const juce::Colour rowHi  { 0xff2f3639 };
    const juce::Colour line   { 0xff3a4245 };
    const juce::Colour bordo  { 0xff551112 };
    const juce::Colour red    { 0xffb0242a };   // rojo de acento
    const juce::Colour white  { 0xfff2f2f2 };
    const juce::Colour grey   { 0xff9aa3a6 };
    const juce::Colour yellow { 0xffffe48c };   // amarillo suave de los reels
}

/** Bebas Neue + Montserrat embebidas en el plugin (licencia OFL). */
struct BrandFonts
{
    BrandFonts()
    {
        bebas    = juce::Typeface::createSystemTypefaceFor (BinaryData::BebasNeueRegular_ttf,
                                                            (size_t) BinaryData::BebasNeueRegular_ttfSize);
        montSemi = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf,
                                                            (size_t) BinaryData::MontserratSemiBold_ttfSize);
        montBold = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratBold_ttf,
                                                            (size_t) BinaryData::MontserratBold_ttfSize);
    }

    juce::Font title (float h, float tracking = 0.04f) const { return juce::Font (juce::FontOptions (bebas).withHeight (h).withKerningFactor (tracking)); }
    juce::Font semi  (float h) const                         { return juce::Font (juce::FontOptions (montSemi).withHeight (h)); }
    juce::Font bold  (float h, float tracking = 0.0f) const  { return juce::Font (juce::FontOptions (montBold).withHeight (h).withKerningFactor (tracking)); }

    juce::Typeface::Ptr bebas, montSemi, montBold;
};

class BrandLook final : public juce::LookAndFeel_V4
{
public:
    BrandLook()
    {
        using namespace brand;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextEditor::backgroundColourId, bgDeep);
        setColour (juce::TextEditor::textColourId, white);
        setColour (juce::TextEditor::highlightColourId, red.withAlpha (0.45f));
        setColour (juce::TextEditor::highlightedTextColourId, white);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, red);
        setColour (juce::CaretComponent::caretColourId, white);
        setColour (juce::TextButton::buttonColourId, row);
        setColour (juce::TextButton::buttonOnColourId, red);
        setColour (juce::TextButton::textColourOffId, white);
        setColour (juce::TextButton::textColourOnId, white);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, grey.withAlpha (0.35f));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff23292b));
        setColour (juce::PopupMenu::textColourId, white);
        setColour (juce::PopupMenu::headerTextColourId, grey);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, bordo);
        setColour (juce::PopupMenu::highlightedTextColourId, white);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        if ((bool) b.getProperties()["flat"])
            return;

        auto r = b.getLocalBounds().toFloat().reduced (0.5f);

        if ((bool) b.getProperties()["primary"])
        {
            auto c = down ? brand::bordo.darker (0.25f) : (over ? brand::red : brand::bordo);
            if (! b.isEnabled()) c = c.withSaturation (0.15f).withMultipliedAlpha (0.6f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 4.0f);
            return;
        }

        g.setColour (down ? brand::bgDeep : (over ? brand::rowHi : brand::row));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (over ? brand::grey.withAlpha (0.6f) : brand::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if ((bool) b.getProperties()["primary"]) return fonts->title ((float) h * 0.56f, 0.06f);
        if ((bool) b.getProperties()["flat"])    return fonts->semi (12.5f);
        return fonts->bold (11.0f, 0.08f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));

        if ((bool) b.getProperties()["flat"])
        {
            g.setColour (over ? brand::white : brand::grey);
            g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centredLeft, 1, 0.85f);
            return;
        }

        g.setColour (brand::white.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        g.setColour (te.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        const bool focus = te.hasKeyboardFocus (true);
        g.setColour (focus ? brand::red : brand::line);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, focus ? 1.5f : 1.0f);
    }

    juce::Font getPopupMenuFont() override { return fonts->semi (15.0f); }

    juce::SharedResourcePointer<BrandFonts> fonts;
};
