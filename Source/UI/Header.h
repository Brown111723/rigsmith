#pragma once

#include "Theme.h"
#include "Icons.h"

namespace rig
{
class SceneButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick, onDoubleClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
};

/** Top bar: logo, presets, undo/redo, scenes A-D, then icon buttons for randomise,
    tuner, library, full screen and the menu. (In / out gain live on the IN / OUT blocks.) */
class Header : public juce::Component
{
public:
    explicit Header (Engine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh (const Tuner::Reading&, const juce::String& status);

    std::function<void()> onPresetMenu, onSave, onMenu, onTuner, onUndo, onRedo, onRandom, onRandomMenu, onLibrary, onFullScreen, onTooltips, onMod;
    std::function<void (int)> onSceneRightClick;

    juce::TextButton presetButton;
    IconButton prev { Icon::Prev, "Previous preset (Page Up)" }, next { Icon::Next, "Next preset (Page Down)" },
               save { Icon::Save, "Save preset (Ctrl+S)" },
               undo { Icon::Undo, "Undo (Ctrl+Z)" }, redo { Icon::Redo, "Redo (Ctrl+Y)" },
               random { Icon::Dice, "Randomise the whole rig: amps, cabs and effects (locked blocks stay). Ctrl+Z undoes it. Right-click for options" },
               tunerButton { Icon::TuningFork, "Tuner (T)", theme::teal },
               modButton { Icon::Mod, "Modulation (M): 3 LFOs, 3 envelopes, a step sequencer, input level and pitch - drag them onto any knob", theme::purple },
               library { Icon::Library, "Show / hide the library (L)", theme::teal },
               fullScreen { Icon::FullScreen, "Full screen (F11)", theme::teal },
               tips { Icon::Tooltip, "Tooltips on / off: hover anything for a hint", theme::teal },
               menu { Icon::Menu, "Menu" };
    SceneButton scenes[kNumScenes];
    /** Double-click a scene: type a new name in place (Enter keeps it, Esc cancels). */
    void renameScene (int index);

private:
    Engine& engine;
    juce::Rectangle<int> logoArea, statusArea;
    std::unique_ptr<juce::TextEditor> sceneEditor;
    void drawStatus (juce::Graphics&);
public:
    bool showFullScreenButton = true;
    Tuner::Reading reading;
    juce::String status;
    float cpu = 0;
    double sampleRate = 0;
    int blockSize = 0, latency = 0;
    void setStatus (double sr, int block, int latencySamples, float cpuLoad);
};

/** Full-screen tuner shown over the rig. Mono (one string), Poly (strum all the open strings
    and see every one at once, PolyTune-style), or Auto (Poly when several strings ring, Mono
    when one does). */
class TunerOverlay : public juce::Component
{
public:
    explicit TunerOverlay (Engine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setReading (const Tuner::Reading& r) { reading = r; repaint(); }
    void setPolyReading (const PolyTuner::Reading& r) { poly = r; repaint(); }
    std::function<void()> onClose;

    enum Mode { Auto = 0, Mono, Poly };
    Mode getMode() const noexcept { return mode; }
    bool showingPoly() const noexcept { return mode == Poly || (mode == Auto && poly.strummed); }

private:
    void paintMono (juce::Graphics&, juce::Rectangle<int>);
    void paintPoly (juce::Graphics&, juce::Rectangle<int>);
    void setMode (Mode);

    Engine& engine;
    Tuner::Reading reading;
    PolyTuner::Reading poly;
    Mode mode = Auto;
    juce::TextButton modeButtons[3] { juce::TextButton ("Auto"), juce::TextButton ("Mono"), juce::TextButton ("Poly") };
    juce::ComboBox tuningBox;
    juce::Slider reference;
    juce::Label referenceLabel;
    juce::ToggleButton muteToggle { "Mute output while tuning" };
    juce::TextButton close { "Close tuner" };
};

} // namespace rig
