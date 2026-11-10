#pragma once

#include "Engine/Engine.h"

namespace rig::theme
{
// "Obsidian": near-black neutrals, one accent, colour only where it means something
inline const juce::Colour bg       { 0xff09090b };
inline const juce::Colour panel    { 0xff0f0f12 };
inline const juce::Colour card     { 0xff17171b };
inline const juce::Colour cardHi   { 0xff202025 };
inline const juce::Colour border   { 0xff26262c };
inline const juce::Colour borderHi { 0xff3d3d45 };
inline const juce::Colour text     { 0xfff1f1f3 };
inline const juce::Colour muted    { 0xff9c9ca6 };
inline const juce::Colour dim      { 0xff5f5f69 };
/** The one accent (Settings > Accent colour). Read at paint time: it can change. */
inline juce::Colour accent         { 0xffffa826 };
inline const juce::Colour teal     { 0xff5fd4c4 };
inline const juce::Colour red      { 0xffff5a4d };
inline const juce::Colour blue     { 0xff8aa4ff };
inline const juce::Colour purple   { 0xffc4a2ff };
inline const juce::Colour green    { 0xff45c486 };
/** Text on top of an accent fill. */
inline const juce::Colour onAccent { 0xff0b0b0d };

struct AccentChoice { const char* name; juce::uint32 argb; };
const std::vector<AccentChoice>& accentChoices();
void setAccent (int index);
int getAccentIndex();

juce::Colour colourFor (const Node&);
juce::Colour branchColour (int index);
juce::String kindLabel (const Node&);
/** Inter (regular, or semibold for "bold"). */
juce::Font font (float size, bool bold = false);
juce::Font medium (float size);
/** Numbers and readouts. */
juce::Font mono (float size);
/** The wide display face for wordmarks ("OVERDRIVE"); tracking spreads the letters. */
juce::Font display (float size, float tracking = 0.12f);

//--- hardware drawing, shared by the cards, the panel and the knobs ---------------
/** A piece of gear's faceplate: dark anodised metal with a soft top light and a hairline edge. */
void drawPlate (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour edge = border, float lift = 0.0f);
/** An inset glass screen (displays, meters). */
void drawGlass (juce::Graphics&, juce::Rectangle<float>, float radius);
/** A machined black knob with a white pointer. angle in radians (0 = up). */
void drawKnobBody (juce::Graphics&, juce::Point<float> centre, float radius, float angle, bool enabled = true, bool lit = false);
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, bool on);
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius);
/** A soft shadow under something (cheap: a few stacked translucent shapes). */
void drawShadow (juce::Graphics&, juce::Rectangle<float>, float radius, float size = 10.0f, float strength = 0.5f);
}

namespace rig
{
class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel();
    /** After the accent changes. */
    void refreshColours();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::medium (13.0f); }
    juce::Font getPopupMenuFont() override { return theme::font (14.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (13.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    juce::Font getAlertWindowTitleFont() override { return theme::font (17.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return theme::font (14.0f); }
    juce::Font getAlertWindowFont() override { return theme::font (13.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};
}
