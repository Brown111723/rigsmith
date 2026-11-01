#pragma once

#include "Engine/Engine.h"

namespace rig::theme
{
inline const juce::Colour bg       { 0xff0e0f11 };
inline const juce::Colour panel    { 0xff17181c };
inline const juce::Colour card     { 0xff1f2025 };
inline const juce::Colour cardHi   { 0xff272930 };
inline const juce::Colour border   { 0xff2a2c32 };
inline const juce::Colour borderHi { 0xff464a53 };
inline const juce::Colour text     { 0xffececee };
inline const juce::Colour muted    { 0xffa3a6ae };
inline const juce::Colour dim      { 0xff6b6e76 };
inline const juce::Colour accent   { 0xfff2a33a };
inline const juce::Colour teal     { 0xff6fd6c6 };
inline const juce::Colour red      { 0xffe86a5c };
inline const juce::Colour blue     { 0xff8ea7ff };
inline const juce::Colour purple   { 0xffc9a6ff };
inline const juce::Colour green    { 0xff3dbe7a };

juce::Colour colourFor (const Node&);
juce::Colour branchColour (int index);
juce::String kindLabel (const Node&);
juce::Font font (float size, bool bold = false);
juce::Font mono (float size);
}

namespace rig
{
class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (13.0f, true); }
    juce::Font getPopupMenuFont() override { return theme::font (14.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (13.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
};
}
