#pragma once

#include "Inspector.h"

namespace rig
{
/** The MOD section, between the rig and the block panel.

    Collapsed (the default) it's a slim bar: click it (or the wave button in the top bar, or M)
    to open it. The bar still shows which sources are busy, with a live meter for each.

    Open - left: the nine sources (3 LFOs, 3 envelopes, the step sequencer, the input level, the
    input pitch), each a chip with a live trace; drag one onto any knob to route it there.
    Middle: the selected source's editor - a live picture of what it does and its knobs, which take
    sources too (an envelope speeding up an LFO...). Right: where it goes - every knob it moves,
    with a depth bar, a live meter and a remove button - or, under "All", every routing in the
    rig grouped by source. */
class ModPanel : public juce::Component,
                 private juce::Timer
{
public:
    explicit ModPanel (Engine&);
    ~ModPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void select (int source);
    int getSelected() const noexcept { return selected; }

    void setCollapsed (bool);
    bool isCollapsed() const noexcept { return collapsed; }
    int getPreferredHeight() const noexcept { return collapsed ? collapsedHeight : expandedHeight; }
    /** Show every routing (the top bar's button does this when there are some). */
    void showAllRoutes (bool);

    std::function<void (bool collapsed)> onCollapsedChanged;
    /** A destination was clicked: select that block in the rig. */
    std::function<void (const juce::String& nodeId)> onSelectBlock;

    static constexpr int collapsedHeight = 34, expandedHeight = 262;

private:
    void timerCallback() override;
    void buildEditor();
    void rebuildRoutes();
    juce::String routesSignature() const;
    float tempoNow() const;
    float levelNow() const;
    void updateWatching();

    Engine& engine;
    int selected = 0;
    bool collapsed = true, allRoutes = false;

    struct Chip;
    struct Display;
    struct DepthBar;
    struct RouteRow;
    struct GroupRow;
    juce::OwnedArray<Chip> chips;
    std::unique_ptr<Display> display;

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
    juce::TextButton thisTab, allTab;
    juce::Viewport routesView;
    juce::Component routesHolder;
    juce::OwnedArray<juce::Component> routeRows;
    juce::Label routesHint;
    juce::String shownRoutes;

    // tempo
    juce::Slider tempo;
    juce::Label tempoLabel;

    juce::Rectangle<int> headerArea, chipsArea, editorArea, routesArea;
    float pickGlow = 0.0f;
    bool headerHover = false;
};

} // namespace rig
