#pragma once

#include "Inspector.h"

namespace rig
{
/** The MOD section, shown between the rig and the block panel (the wave button in the header, or M).

    Left: the nine sources (3 LFOs, 3 envelopes, the step sequencer, the input level and the input
    pitch), each a chip with a live trace - drag one onto any knob to route it there.
    Middle: the selected source's editor - a picture of what it does (waveform, ADSR, steps you can
    draw, level window, note range) and its knobs. Those knobs take sources too, so an envelope can
    speed up an LFO or an LFO can sweep the sequencer's glide.
    Right: the analyzer - the last five seconds of every source, the selected one bold - and where
    the selected source goes, with a depth slider per destination. */
class ModPanel : public juce::Component,
                 private juce::Timer
{
public:
    explicit ModPanel (Engine&);
    ~ModPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    void select (int source);
    int getSelected() const noexcept { return selected; }
    std::function<void()> onClose;

    static constexpr int preferredHeight = 262;

private:
    void timerCallback() override;
    void buildEditor();
    void rebuildRoutes();
    juce::String routesSignature() const;
    float tempoNow() const;
    float levelNow() const;

    Engine& engine;
    int selected = 0;

    struct Chip;
    struct Display;
    struct Scope;
    struct RouteRow;
    juce::OwnedArray<Chip> chips;
    std::unique_ptr<Display> display;
    std::unique_ptr<Scope> scope;

    // the selected source's controls
    juce::Component editor;
    juce::OwnedArray<Knob> knobs;
    std::vector<int> knobParam;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label> comboLabels;
    std::vector<int> comboParam;
    juce::OwnedArray<juce::ToggleButton> toggles;
    std::vector<int> toggleParam;
    juce::Label editorTitle, editorBlurb;

    // where it goes
    juce::Label routesTitle;
    juce::Viewport routesView;
    juce::Component routesHolder;
    juce::OwnedArray<RouteRow> routeRows;
    juce::String shownRoutes;
    juce::TextButton showAll { "All" };

    // tempo
    juce::Slider tempo;
    juce::Label tempoLabel;
    IconButton closeButton { Icon::Close, "Hide the MOD section (M)" };

    juce::Rectangle<int> headerArea, chipsArea, editorArea, scopeArea;
    float pickGlow = 0.0f;
};

} // namespace rig
