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
    /** A stepped selector: a dot round the knob for each position (the capture knob). */
    int detents = 0;
    juce::Colour detentColour;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override { if (onHover) onHover (false); }
    void refreshText();
};

/** A level meter with a trim handle: the bar shows the signal (dBFS), the handle sets the trim
    (drag it, double-click for 0 dB, right-click for MIDI learn where it's a parameter). */
class TrimMeter : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    TrimMeter (const juce::String& label, float minDb, float maxDb);
    std::function<float()> getTrim;
    std::function<void (float)> setTrim;
    std::function<void()> onRightClick;
    void setLevel (float linearPeak);   // call ~20 times a second

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> track() const;
    float trimToY (float db) const;
    juce::String label;
    float minDb, maxDb, shown = 0.0f, held = 0.0f, dragStart = 0.0f;
    int holdTicks = 0;
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
    void buildCaptureKnob (Block&);
    void buildMeters (Block&);
    void showCaptureEditor();
    void updateCaptureText();
    void showCaptureMenu();
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
               prevFile { Icon::Prev, "Previous gear in the library (the Capture knob switches captures of this one)" },
               nextFile { Icon::Next, "Next gear in the library (the Capture knob switches captures of this one)" },
               autoGainButton { Icon::AutoGain, "", theme::teal },
               diceButton { Icon::Dice, "Randomise this block's settings (R). Ctrl+Z undoes it" },
               presetButton { Icon::Preset, "Block presets: save, load, init" },
               lockButton { Icon::Unlock, "Lock: keep this block when randomising the rig", theme::accent };
    FootswitchButton bypassButton;
    std::unique_ptr<FxDisplay> display;
    void layoutIcons (juce::Rectangle<int> row);

    juce::Viewport controlsView;
    juce::Component controls;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<ChoiceControl> choices;
    std::vector<int> knobParam, toggleParam, choiceParam;

    // NAM / IR: every capture of the loaded gear on one knob
    std::unique_ptr<Knob> captureKnob;
    CaptureGroupPtr captureGroup;   // IRs: step through the pack
    juce::Array<juce::File> namCaptureFiles;   // NAM: the sweep shown on the knob
    juce::StringArray namCaptureLabels;
    juce::Label captureName;
    juce::TextButton editCaptures;

    // IN / OUT level meters with trim handles
    struct Meters : public juce::Component
    {
        TrimMeter in { "IN", -24.0f, 24.0f }, out { "OUT", -24.0f, 24.0f };
        Meters() { addAndMakeVisible (in); addAndMakeVisible (out); }
        void resized() override
        {
            auto r = getLocalBounds();
            in.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 0));
            out.setBounds (r.reduced (2, 0));
        }
    };
    std::unique_ptr<Meters> meters;
    juce::Label autoNote;   // "Auto is overriding Normalize"

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
