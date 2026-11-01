#pragma once

#include "Theme.h"
#include "Icons.h"

namespace rig
{
class SceneButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
};

/** Top bar: logo, presets, undo/redo, scenes A-D, tuner readout, status, randomise,
    library and full-screen toggles, in/out gain, menu. */
class Header : public juce::Component
{
public:
    explicit Header (Engine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh (const Tuner::Reading&, const juce::String& status);

    std::function<void()> onPresetMenu, onSave, onMenu, onTuner, onUndo, onRedo, onRandom, onRandomMenu, onLibrary, onFullScreen;
    std::function<void (int)> onSceneRightClick;

    juce::TextButton presetButton;
    IconButton prev { Icon::Prev, "Previous preset (Page Up)" }, next { Icon::Next, "Next preset (Page Down)" },
               save { Icon::Save, "Save preset (Ctrl+S)" },
               undo { Icon::Undo, "Undo (Ctrl+Z)" }, redo { Icon::Redo, "Redo (Ctrl+Y)" },
               random { Icon::Dice, "Randomise the whole rig: amps, cabs and effects (locked blocks stay). Ctrl+Z undoes it. Right-click for options" },
               randomMore { Icon::Caret, "More randomise options" },
               library { Icon::Library, "Show / hide the library (L)", theme::teal },
               fullScreen { Icon::FullScreen, "Full screen (F11)", theme::teal },
               menu { Icon::Menu, "Menu" };
    SceneButton scenes[kNumScenes];
    juce::Slider inGain, outGain;

private:
    Engine& engine;
    juce::Rectangle<int> logoArea, tunerArea, statusArea, inArea, outArea;
public:
    bool showFullScreenButton = true;
    Tuner::Reading reading;
    juce::String status;
};

/** Full-screen tuner shown over the rig. */
class TunerOverlay : public juce::Component
{
public:
    explicit TunerOverlay (Engine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setReading (const Tuner::Reading& r) { reading = r; repaint(); }
    std::function<void()> onClose;

private:
    Engine& engine;
    Tuner::Reading reading;
    juce::ToggleButton muteToggle { "Mute output while tuning" };
    juce::TextButton close { "Close tuner" };
};

} // namespace rig
