#pragma once

#include "Theme.h"

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

/** Top bar: logo, presets, scenes A-D, tuner readout, in/out gain, status, menu. */
class Header : public juce::Component
{
public:
    explicit Header (Engine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh (const Tuner::Reading&, const juce::String& status);

    std::function<void()> onPresetMenu, onSave, onMenu, onTuner;
    std::function<void (int)> onSceneRightClick;

    juce::TextButton prev { "<" }, next { ">" }, presetButton, save { "Save" }, menu;
    SceneButton scenes[kNumScenes];
    juce::Slider inGain, outGain;

private:
    Engine& engine;
    juce::Rectangle<int> logoArea, tunerArea, statusArea, inArea, outArea;
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
