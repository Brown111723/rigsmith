#pragma once

#include "GraphView.h"
#include "FxDisplay.h"

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

/** Bottom panel: every control for the selected block, split or plug-in,
    plus a live visual of what the controls are doing. */
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
    juce::TextButton openButton, replaceButton { "Replace" }, bypassButton, autoGainButton { "Auto gain" };
    juce::TextButton prevFile { "<" }, nextFile { ">" };
    std::unique_ptr<FxDisplay> display;

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
