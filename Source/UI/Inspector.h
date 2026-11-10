#pragma once

#include "GraphView.h"
#include "FxDisplay.h"
#include "Icons.h"

namespace rig
{
/** Rotary knob with its name above and value below; right-click for options. */
class Knob : public juce::Component,
             public juce::DragAndDropTarget,
             private juce::Timer
{
public:
    Knob (const juce::String& name, juce::Colour colour);
    ~Knob() override;
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    std::function<juce::String()> formatValue;
    std::function<void()> onRightClick;
    std::function<void (bool)> onHover;
    /** A stepped selector: a dot round the knob for each position (the capture knob). */
    int detents = 0;
    juce::Colour detentColour;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override { if (onHover) onHover (false); }
    void refreshText();

    /** This knob is a block parameter: modulation sources can be dropped on it, and it draws
        a ring for each source routed to it plus a dot where the sound actually is right now.
        target: the block's id, or "mod:N" for a modulator's own knob. */
    void setModTarget (Engine&, Block&, const juce::String& target, int param);

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

protected:
    juce::String dropHint;   // shown while something is dragged over
    juce::Colour dropColour;

private:
    void timerCallback() override;
    Engine* modEngine = nullptr;
    Block* modBlock = nullptr;
    juce::String modTarget;
    int modParam = -1;
    float lastLive = -1.0f;
    size_t lastRoutes = 0;
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

/** A knob things can be dropped on (the Capture knob: drop captures to add them to the sweep). */
class DropKnob : public Knob,
                 public juce::FileDragAndDropTarget
{
public:
    using Knob::Knob;
    std::function<void (const juce::String& description)> onDrop;
    std::function<void (const juce::StringArray& paths)> onFilesDrop;

    static bool isCaptureDrag (const juce::String& s)
    {
        return s.startsWith ("file:") || s.startsWith ("files:") || s.startsWith ("folder:");
    }
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return isCaptureDrag (d.description.toString()) || Knob::isInterestedInDragSource (d);
    }
    void itemDragEnter (const SourceDetails& d) override
    {
        if (! isCaptureDrag (d.description.toString())) { Knob::itemDragEnter (d); return; }
        dropHint = "Add to the sweep"; dropColour = theme::teal; repaint();
    }
    void itemDropped (const SourceDetails& d) override
    {
        if (! isCaptureDrag (d.description.toString())) { Knob::itemDropped (d); return; }
        dropHint.clear(); repaint();
        if (onDrop) onDrop (d.description.toString());
    }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHint = "Add to the sweep"; dropColour = theme::teal; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHint.clear(); repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { dropHint.clear(); repaint(); if (onFilesDrop) onFilesDrop (f); }
};

/** Right-click on any modulatable knob: MIDI learn, "Modulate with", and the routes into it. */
void showKnobMenu (Engine&, Block&, const juce::String& target, int param, juce::Component* anchor, std::function<void()> learn);

/** A small segmented selector for choice parameters (delay mode, reverb type...). */
class ChoiceControl : public juce::Component
{
public:
    ChoiceControl (const juce::String& name, const juce::StringArray& choices, juce::Colour colour,
                   const std::vector<std::pair<int, juce::String>>& sections = {});
    void setSelected (int index);
    std::function<void (int)> onChange;
    void resized() override;
    int preferredWidth() const;

private:
    juce::Label label;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ComboBox> combo;   // many options (a pedal's models): a drop-down instead of buttons
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
               lockButton { Icon::Unlock, "Lock: keep this block when randomising the rig" };
    FootswitchButton bypassButton;
    std::unique_ptr<FxDisplay> display;
    void layoutIcons (juce::Rectangle<int> row);

    juce::Viewport controlsView;
    juce::Component controls;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<ChoiceControl> choices;
    std::vector<int> knobParam, toggleParam, choiceParam;
    std::vector<int> builtOrder;
    juce::String builtNames;      // what the knobs were called (a pedal model renames them)
    static juce::String panelSignature (const Block&);   // the block's panelOrder() when built (the gate's View changes it)

    // NAM / IR: every capture of the loaded gear on one knob
    std::unique_ptr<Knob> captureKnob;
    CaptureGroupPtr captureGroup;
    juce::Array<juce::File> namCaptureFiles;   // the sweep shown on the knob (NAM or IR)
    juce::StringArray namCaptureLabels;
    juce::Label captureName;
    juce::TextButton editCaptures;

    // IN / OUT level meters with trim handles
    struct Meters : public juce::Component
    {
        TrimMeter in { "IN", -24.0f, 24.0f }, out { "OUT", -24.0f, 24.0f };
        Meters() { addAndMakeVisible (in); addAndMakeVisible (out); }
        void paint (juce::Graphics& g) override
        {
            // framed like the display beside it
            theme::drawGlass (g, getLocalBounds().toFloat(), 7.0f);
            g.setColour (theme::border.withAlpha (0.7f));
            g.fillRect (getWidth() / 2, 10, 1, getHeight() - 20);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (1, 2);
            in.setBounds (r.removeFromLeft (r.getWidth() / 2));
            out.setBounds (r);
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
