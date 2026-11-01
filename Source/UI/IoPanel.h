#pragma once

#include "Inspector.h"

namespace rig
{
/** Live levels the editor gathers from the engine every frame. */
struct IoLevels
{
    float inPeakL = 0, inPeakR = 0, inRmsL = 0, inRmsR = 0;
    float outPeakL = 0, outPeakR = 0, outRmsL = 0, outRmsR = 0;
    float grDb = 0;                  // limiter gain reduction (negative, smoothed)
    int inOvers = 0, outOvers = 0;   // since the last reset
};

/** The bottom panel for the IN and OUT blocks: detailed stereo meters with peak hold,
    RMS and clip counters, a level history, and the input / master controls
    (gain, balance, polarity, source; width, brickwall limiter ceiling and release). */
class IoPanel : public juce::Component
{
public:
    explicit IoPanel (GraphHost&);

    void setOutput (bool isOutput);
    bool isOutput() const noexcept { return output; }
    void setLevels (const IoLevels&);
    std::function<void()> onResetOvers;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void build();
    void refreshControls();
    Knob* addKnob (const juce::String& name, juce::Colour, double lo, double hi, double def, double mid,
                   std::function<float()> get, std::function<void (float)> set, std::function<juce::String (double)> fmt);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);
    void drawHistory (juce::Graphics&, juce::Rectangle<float>);

    GraphHost& host;
    Engine& engine;
    bool output = false;
    IoLevels levels;

    struct Hold { float value = 0; juce::uint32 at = 0; };
    Hold holdL, holdR, holdGr;
    std::vector<float> history, grHistory;

    juce::Label title, subtitle, info;
    juce::Rectangle<int> meterArea, historyArea, oversArea;
    juce::Component controls;
    juce::OwnedArray<Knob> knobs;
    std::vector<std::function<float()>> knobGetters;
    std::unique_ptr<ChoiceControl> source;
    juce::ToggleButton phase { "Invert polarity" }, limiter { "Brickwall limiter" }, tunerMute { "Mute while tuning" };
    juce::Label limiterNote;
};

} // namespace rig
