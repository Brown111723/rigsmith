#pragma once

#include "GraphView.h"
#include "FxDisplay.h"
#include "Icons.h"

namespace rig
{
/** Rotary knob with its name above and value below; right-click for options. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, juce::Colour colour);
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    std::function<juce::String()> formatValue;
    std::function<void()> onRightClick;
    std::function<void (bool)> onHover;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override { if (onHover) onHover (false); }
    void refreshText();
};

/** A small segmented selector for choice parameters (delay mode, reverb type...). */
class ChoiceControl : public juce::Component
{
public:
    ChoiceControl (const juce::String& name, const juce::StringArray& choices, juce::Colour colour);
    void setSelected (int index);
    std::function<void (int)> onChange;
    void resized() override;
    int preferredWidth() const;

private:
    juce::Label label;
    juce::OwnedArray<juce::TextButton> buttons;
};

/** Bottom panel. Left: the block's name, a row of icon buttons (open, bypass, gain
    compensation, randomise, presets, lock) and the live display. Right: every control. */
class Inspector : public juce::Component,
                  private juce::Timer
{
public:
    explicit Inspector (GraphHost&);
    ~Inspector() override;

    void setNode (Node*);
    Node* getNode() const noexcept { return node; }

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (Node&)> onOpen;            // load model/IR, open plug-in UI
    std::function<void (Node&, int param)> onLearnParam;

private:
    void timerCallback() override;
    void buildBlock (Block&);
    void buildPluginParams (PluginBlock&);
    void buildSplit (Split&);
    void clearControls();
    void layoutControls();
    Knob* addKnob (const juce::String& name, juce::Colour c);

    GraphHost& host;
    Node* node = nullptr;
    bool needsRebuild = false;
    bool showAllPluginParams = true;

    juce::Label title, subtitle, info;
    IconButton openButton { Icon::Open, "Open" },
               prevFile { Icon::Prev, "Previous file in this folder" }, nextFile { Icon::Next, "Next file in this folder" },
               bypassButton { Icon::Power, "On / bypass (Space)", theme::teal },
               autoGainButton { Icon::AutoGain, "", theme::teal },
               diceButton { Icon::Dice, "Randomise this block's settings (R). Right-click for more" },
               shuffleButton { Icon::Shuffle, "Swap for a random block of the same kind (Shift+R)" },
               presetButton { Icon::Preset, "Block presets: save, load, init" },
               lockButton { Icon::Unlock, "Lock: keep this block when randomising the rig", theme::accent };
    std::unique_ptr<FxDisplay> display;
    void layoutIcons (juce::Rectangle<int> row);

    juce::Viewport controlsView;
    juce::Component controls;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<ChoiceControl> choices;
    std::vector<int> knobParam, toggleParam, choiceParam;

    // plug-in parameters
    juce::Label pluginHeader;
    juce::TextButton pluginScope { "All" };
    juce::OwnedArray<Knob> pluginKnobs;
    std::vector<int> pluginParamIndex;

    // split controls
    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::ToggleButton autoGainSum { "Auto gain on sum" };
    struct Strip;
    juce::OwnedArray<Strip> strips;
    juce::TextButton addPathButton { "+ Add path" };
};

} // namespace rig
