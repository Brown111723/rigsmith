#pragma once

#include "Theme.h"

namespace rig
{
enum class Icon
{
    Open, Folder, Power, AutoGain, Dice, Shuffle, Preset, Lock, Unlock,
    Prev, Next, FullScreen, ExitFullScreen, Library, Undo, Redo, Init, Menu, Save, Caret, TuningFork
};

/** Draws an icon into a box (stroked line art, filled where it reads better). */
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> box, juce::Colour);

/** A square (or icon + text) button drawn in the rig's style.
    Toggle buttons light up in their colour when on. */
class IconButton : public juce::Button
{
public:
    IconButton (Icon, const juce::String& tooltip, juce::Colour onColour = theme::accent);

    void setIcon (Icon i) { icon = i; repaint(); }
    /** Optional text drawn to the right of the icon (e.g. "+3.2 dB"). */
    void setLabel (const juce::String& t) { if (t != label) { label = t; repaint(); } }
    const juce::String& getLabel() const noexcept { return label; }
    /** 0..1 progress ring (e.g. gain compensation learning), < 0 hides it. */
    void setProgress (float p) { if (std::abs (p - progress) > 0.005f) { progress = p; repaint(); } }
    void setHighlighted (bool h) { if (h != highlighted) { highlighted = h; repaint(); } }
    int preferredWidth (int height) const;

    std::function<void()> onRightClick;

    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Icon icon;
    juce::String label;
    juce::Colour onColour;
    float progress = -1.0f;
    bool highlighted = false;
};

/** The block footswitch: an LED-lit round switch with a glow when on (cards and the panel share it). */
void drawFootswitch (juce::Graphics&, juce::Rectangle<float> sw, juce::Colour, bool on);

/** The panel's on / bypass control, drawn exactly like the footswitch on the block's card. */
class FootswitchButton : public juce::Button
{
public:
    FootswitchButton();
    void setColour (juce::Colour c) { if (c != colour) { colour = c; repaint(); } }
    int preferredWidth (int height) const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::Colour colour { theme::accent };
};

} // namespace rig
