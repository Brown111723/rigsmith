#pragma once

#include "GraphView.h"

namespace rig
{
/** Rotary knob with its name above and value below; right-click for MIDI learn. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, juce::Colour colour);
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    std::function<juce::String()> formatValue;
    std::function<void()> onRightClick;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refreshText();
};

/** Bottom panel: every control for the selected block, split or plug-in. */
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
    void buildSplit (Split&);
    void clearControls();

    GraphHost& host;
    Node* node = nullptr;
    bool needsRebuild = false;

    juce::Label title, subtitle, info;
    juce::TextButton openButton, bypassButton;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ToggleButton> toggles;
    std::vector<int> knobParam, toggleParam;

    // split controls
    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::ToggleButton autoGain { "Auto gain" };
    struct Strip;
    juce::OwnedArray<Strip> strips;
    juce::TextButton addPathButton { "+ Add path" };
};

} // namespace rig
