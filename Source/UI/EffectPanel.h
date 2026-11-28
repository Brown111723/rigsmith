#pragma once

#include "FxDisplay.h"
#include "Icons.h"

namespace rig
{
class Engine;
class TrimMeter;

/** One effect, laid out the same way everywhere (Rigsmith's inspector, a Tracksmith insert, Keysmith's rack):
    a title bar (its light and name, its kind on the right), a toolbar (on / off, dice, lock, presets, model),
    the live display with IN / OUT meters on the left, and faders on the right - Mix always first - with an XY pad
    for the two controls that belong together and buttons for its switches. Few knobs: faders, pads and buttons. */
class EffectPanel : public juce::Component,
                    private juce::Timer
{
public:
    struct Host
    {
        Engine* engine = nullptr;                    // your presets, the modulation menu (optional)
        std::function<bool()> isOn;                  // default: not bypassed
        std::function<void (bool)> setOn;
        std::function<void()> roll;                  // the dice (default: the block's own randomise, pitch kept)
        std::function<bool()> isLocked;              // default: the block's lock
        std::function<void (bool)> setLocked;
        std::function<void()> beginEdit;             // an undo step starts
        std::function<void()> edited;                // something changed (dirty, repaint elsewhere)
        std::function<void (int param, juce::Component*)> paramMenu;   // right-click a control (MIDI learn, modulate)
        std::function<void()> onRemove;              // shows a close button when set
        juce::String dragDescription;                // set: drag the title bar to reorder (a DragAndDropContainer above)
    };

    EffectPanel (Block&, Host, std::unique_ptr<FxDisplay> display = nullptr);
    ~EffectPanel() override;

    Block& getBlock() noexcept { return block; }
    static int preferredHeight() noexcept { return 320; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    /** Values changed from elsewhere (an undo, a scene): redraw them. */
    void refresh();

    class Fader;
    class XYPad;

private:
    void timerCallback() override;
    void build();
    void buildPresets();
    void layoutBody (juce::Rectangle<int>);
    juce::String signature() const;
    void begin();
    void done();
    bool on() const;
    juce::String displayTitle() const;
    juce::String controlsTitle() const;

    Block& block;
    Host host;
    juce::Colour colour;
    std::unique_ptr<FxDisplay> display;
    std::unique_ptr<TrimMeter> inMeter, outMeter;

    juce::Rectangle<int> titleBar, toolBar, leftArea, rightArea, powerR, tagR, gripR;
    IconButton diceBtn { Icon::Dice, "Roll this effect's settings (tuning stays)" },
               lockBtn { Icon::Unlock, "Lock: the dice leave it alone" },
               closeBtn { Icon::Close, "Remove this effect" };
    juce::ComboBox presets, model;
    int modelParam = -1;
    std::vector<int> factoryIds;
    juce::Array<juce::File> userFiles;

    juce::OwnedArray<Fader> faders;
    std::unique_ptr<XYPad> pad;
    juce::OwnedArray<juce::Component> switches;   // choice buttons, combos and toggles
    juce::OwnedArray<juce::Label> switchLabels;
    juce::String built;
    bool syncing = false;
};

/** The default dice: the block's randomise with anything pitch-like (semitones, intervals, keys) put back. */
void rollKeepingPitch (Block&, juce::Random&, float amount = 1.0f);

} // namespace rig
