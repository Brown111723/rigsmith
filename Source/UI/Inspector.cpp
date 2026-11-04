#include "Inspector.h"

namespace rig
{
using namespace theme;

//==============================================================================
Knob::Knob (const juce::String& name, juce::Colour colour)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setDoubleClickReturnValue (false, 0);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);

    nameLabel.setText (name, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (font (12.0f, true));
    nameLabel.setMinimumHorizontalScale (0.7f);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (mono (11.0f));
    valueLabel.setColour (juce::Label::textColourId, muted);
    valueLabel.setMinimumHorizontalScale (0.6f);
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    slider.onValueChange = [this] { refreshText(); };
    slider.addMouseListener (this, false);
}

void Knob::refreshText()
{
    valueLabel.setText (formatValue ? formatValue() : juce::String (slider.getValue(), 1), juce::dontSendNotification);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick)
        onRightClick();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    nameLabel.setBounds (r.removeFromTop (18));
    valueLabel.setBounds (r.removeFromBottom (18));
    const int s = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (s, s).reduced (detents > 1 ? 9 : 0));
}

void Knob::paint (juce::Graphics& g)
{
    if (detents < 2) return;
    // a dot for every position, the current one lit
    const auto b = slider.getBounds().toFloat();
    const auto c = b.getCentre();
    const float rad = b.getWidth() * 0.5f + 6.0f;
    const int at = juce::roundToInt (slider.getValue() - slider.getMinimum());
    const float a0 = juce::degreesToRadians (225.0f), a1 = juce::degreesToRadians (495.0f);
    const float dot = detents > 24 ? 2.0f : 3.0f;
    for (int i = 0; i < detents; ++i)
    {
        const auto p = c.getPointOnCircumference (rad, a0 + (a1 - a0) * (float) i / (float) (detents - 1));
        g.setColour (i == at ? detentColour : theme::dim.withAlpha (0.8f));
        const float d = i == at ? dot + 2.0f : dot;
        g.fillEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d);
    }
}

//==============================================================================
TrimMeter::TrimMeter (const juce::String& l, float mn, float mx) : label (l), minDb (mn), maxDb (mx)
{
    setRepaintsOnMouseActivity (true);
}

void TrimMeter::setLevel (float peak)
{
    const float next = juce::jmax (peak, shown * 0.82f);
    if (peak >= held) { held = peak; holdTicks = 30; }
    else if (--holdTicks <= 0) held *= 0.9f;
    if (std::abs (next - shown) > 1.0e-4f || peak > 0.0f) { shown = next; repaint(); }
}

juce::Rectangle<float> TrimMeter::track() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (18.0f);
    r.removeFromBottom (18.0f);
    return r.withSizeKeepingCentre (10.0f, r.getHeight() - 8.0f);
}

float TrimMeter::trimToY (float db) const
{
    const auto t = track();
    return t.getBottom() - (db - minDb) / (maxDb - minDb) * t.getHeight();
}

void TrimMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto t = track();
    g.setColour (muted);
    g.setFont (font (10.5f, true));
    g.drawText (label, getLocalBounds().removeFromTop (16), juce::Justification::centred);

    // the signal: -60 .. +6 dBFS up the track
    g.setColour (bg);
    g.fillRoundedRectangle (t, 3.0f);
    auto levelY = [&t] (float lin)
    {
        const float db = juce::jlimit (-60.0f, 6.0f, gainToDb (lin));
        return t.getBottom() - (db + 60.0f) / 66.0f * t.getHeight();
    };
    const float y = levelY (shown);
    const float zeroY = levelY (1.0f);
    const auto fill = juce::Rectangle<float> (t.getX() + 2.0f, y, t.getWidth() - 4.0f, t.getBottom() - y - 1.0f);
    if (fill.getHeight() > 0.5f)
    {
        juce::ColourGradient grad (teal, 0.0f, t.getBottom(), red, 0.0f, zeroY, false);
        grad.addColour (0.8, accent);
        g.setGradientFill (grad);
        g.fillRect (fill);
    }
    if (held > 1.0e-4f)
    {
        g.setColour (held >= 1.0f ? red : text.withAlpha (0.7f));
        g.fillRect (t.getX() + 2.0f, levelY (held) - 1.0f, t.getWidth() - 4.0f, 1.5f);
    }
    g.setColour (border.brighter (0.2f));
    g.drawHorizontalLine ((int) zeroY, t.getX() - 3.0f, t.getRight() + 3.0f);   // 0 dBFS

    // the trim handle
    const float trim = getTrim ? getTrim() : 0.0f;
    const float hy = trimToY (trim);
    const bool over = isMouseOverOrDragging();
    juce::Path h;
    h.addTriangle (t.getRight() + 2.0f, hy, t.getRight() + 10.0f, hy - 5.0f, t.getRight() + 10.0f, hy + 5.0f);
    g.setColour (over ? accent : text.withAlpha (0.85f));
    g.fillPath (h);
    g.fillRect (t.getX() - 4.0f, hy - 1.0f, t.getWidth() + 6.0f, 2.0f);
    g.setColour (std::abs (trim) < 0.05f ? muted : accent);
    g.setFont (mono (10.0f));
    g.drawText ((trim > 0.05f ? "+" : "") + juce::String (trim, 1), getLocalBounds().removeFromBottom (16), juce::Justification::centred);
}

void TrimMeter::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    dragStart = getTrim ? getTrim() : 0.0f;
}

void TrimMeter::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! setTrim) return;
    // fine control with shift; about 0.25 dB a pixel otherwise
    const float perPx = (maxDb - minDb) / track().getHeight() * (e.mods.isShiftDown() ? 0.2f : 1.0f);
    float v = dragStart - (float) e.getDistanceFromDragStartY() * perPx;
    v = juce::jlimit (minDb, maxDb, std::round (v * 10.0f) / 10.0f);
    if (std::abs (v) < 0.3f) v = 0.0f;   // a little detent at 0 dB
    setTrim (v);
    repaint();
}

void TrimMeter::mouseDoubleClick (const juce::MouseEvent&)
{
    if (setTrim) { setTrim (0.0f); repaint(); }
}

//==============================================================================
ChoiceControl::ChoiceControl (const juce::String& name, const juce::StringArray& options, juce::Colour colour)
{
    label.setText (name, juce::dontSendNotification);
    label.setFont (font (12.0f, true));
    addAndMakeVisible (label);
    for (int i = 0; i < options.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (options[i]));
        b->setColour (juce::TextButton::buttonOnColourId, colour);
        b->onClick = [this, i] { setSelected (i); if (onChange) onChange (i); };
        addAndMakeVisible (b);
    }
}

void ChoiceControl::setSelected (int index)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

int ChoiceControl::preferredWidth() const
{
    int w = 0;
    for (auto* b : buttons) w += juce::GlyphArrangement::getStringWidthInt (font (13.0f, true), b->getButtonText()) + 22;
    return juce::jmax (120, w + 4 * (buttons.size() - 1));
}

void ChoiceControl::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (20));
    r = r.removeFromTop (30);
    for (auto* b : buttons)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (font (13.0f, true), b->getButtonText()) + 22;
        b->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (4);
    }
}

//==============================================================================
struct Inspector::Strip : public juce::Component
{
    Strip (Inspector& o, Split& s, int i) : owner (o), split (s), index (i), level ("Level", branchColour (i))
    {
        const auto c = branchColour (i);
        name.setText ("Path " + juce::String::charToString ((juce::juce_wchar) ('A' + i)), juce::dontSendNotification);
        name.setFont (font (13.0f, true));
        name.setColour (juce::Label::textColourId, c);
        name.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (name);

        level.nameLabel.setVisible (false);
        level.slider.setRange (0.0, 100.0, 0.1);
        level.formatValue = [this] { return juce::String (juce::roundToInt (level.slider.getValue())) + "%"; };
        level.slider.onValueChange = [this]
        {
            level.refreshText();
            if (updating) return;
            split.setLevel (index, (float) level.slider.getValue() / 100.0f, true);
            owner.host.getEngine().storeScene();
            owner.host.getEngine().markDirty();
        };
        level.onRightClick = [this]
        {
            MidiMapping m; m.target = MidiMapping::Target::BranchLevel; m.nodeId = split.id; m.index = index;
            owner.host.getEngine().startLearn (m);
        };
        addAndMakeVisible (level);

        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setColour (juce::Slider::trackColourId, c.withAlpha (0.6f));
        pan.setTooltip ("Pan (double-click to centre)");
        pan.onValueChange = [this]
        {
            if (updating) return;
            split.branches[(size_t) index]->pan = (float) pan.getValue();
            owner.host.getEngine().markDirty();
        };
        addAndMakeVisible (pan);

        phase.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x00d8));
        phase.setTooltip ("Invert polarity");
        phase.setClickingTogglesState (true);
        phase.setColour (juce::TextButton::buttonOnColourId, red);
        phase.onClick = [this]
        {
            split.branches[(size_t) index]->phase = phase.getToggleState();
            owner.host.getEngine().markDirty();
        };
        addAndMakeVisible (phase);

        mute.setButtonText ("M");
        mute.setTooltip ("Mute path (stored per scene)");
        mute.setClickingTogglesState (true);
        mute.onClick = [this]
        {
            split.branches[(size_t) index]->mute = mute.getToggleState();
            owner.host.getEngine().storeScene();
            owner.host.getEngine().markDirty();
        };
        addAndMakeVisible (mute);
        refresh();
    }

    void refresh()
    {
        if (index >= (int) split.branches.size()) return;
        auto& b = *split.branches[(size_t) index];
        updating = true;
        if (! level.slider.isMouseButtonDown()) level.slider.setValue (b.level.load() * 100.0, juce::dontSendNotification);
        if (! pan.isMouseButtonDown()) pan.setValue (b.pan.load(), juce::dontSendNotification);
        phase.setToggleState (b.phase.load(), juce::dontSendNotification);
        mute.setToggleState (b.mute.load(), juce::dontSendNotification);
        level.refreshText();
        updating = false;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (card);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        name.setBounds (r.removeFromTop (18));
        auto bottom = r.removeFromBottom (26);
        phase.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (2, 0));
        mute.setBounds (bottom.reduced (2, 0));
        pan.setBounds (r.removeFromBottom (22));
        level.setBounds (r);
    }

    Inspector& owner;
    Split& split;
    int index;
    bool updating = false;
    juce::Label name;
    Knob level;
    juce::Slider pan;
    juce::TextButton phase, mute;
};

//==============================================================================
Inspector::Inspector (GraphHost& h) : host (h)
{
    title.setFont (font (18.0f, true));
    title.setMinimumHorizontalScale (0.7f);
    subtitle.setFont (font (11.0f, true));
    subtitle.setColour (juce::Label::textColourId, muted);
    subtitle.setJustificationType (juce::Justification::centredRight);
    subtitle.setMinimumHorizontalScale (0.7f);
    info.setFont (font (12.0f));
    info.setColour (juce::Label::textColourId, muted);
    info.setJustificationType (juce::Justification::topLeft);
    for (auto* l : { &title, &subtitle, &info }) addAndMakeVisible (l);

    openButton.onClick = [this] { if (node != nullptr && onOpen) onOpen (*node); };
    prevFile.onClick = [this] { if (node != nullptr) host.browseFile (*node, -1); };
    nextFile.onClick = [this] { if (node != nullptr) host.browseFile (*node, 1); };

    bypassButton.onClick = [this]
    {
        if (node == nullptr) return;
        node->bypassed = ! node->bypassed.load();
        host.getEngine().storeScene();
        host.getEngine().markDirty();
    };

    autoGainButton.setTooltip ("Gain compensation: like normalising this block. While you play it measures loudness in vs out, "
                               "then holds one fixed level match (no pumping). It re-measures when you change settings. "
                               "Right-click to measure again now.");
    autoGainButton.onClick = [this]
    {
        if (node == nullptr) return;
        node->autoGain = ! node->autoGain.load();
        if (node->autoGain.load()) node->requestRelearn();
        host.getEngine().markDirty();
    };
    autoGainButton.onRightClick = [this]
    {
        if (node == nullptr) return;
        node->autoGain = true;
        node->requestRelearn();
        host.getEngine().markDirty();
    };

    diceButton.onClick = [this] { if (node != nullptr) host.randomise (*node, GraphHost::RandomSettings); };
    presetButton.onClick = [this] { if (node != nullptr) host.showBlockPresetMenu (*node, &presetButton); };
    lockButton.onClick = [this] { if (node != nullptr) host.toggleLock (*node); };

    for (auto* b : std::initializer_list<juce::Component*> { &openButton, &prevFile, &nextFile, &bypassButton, &autoGainButton,
                                                             &diceButton, &presetButton, &lockButton })
        addChildComponent (b);

    autoGainSum.setTooltip ("In Parallel mode, turn the sum down when the paths add up to more than 100%");
    autoGainSum.onClick = [this]
    {
        if (auto* s = dynamic_cast<Split*> (node)) { s->autoGainSum = autoGainSum.getToggleState(); host.getEngine().markDirty(); }
    };
    controls.addChildComponent (autoGainSum);

    addPathButton.onClick = [this] { if (auto* s = dynamic_cast<Split*> (node)) host.getEngine().addBranch (*s); };
    controls.addChildComponent (addPathButton);

    pluginHeader.setFont (font (11.0f, true));
    pluginHeader.setColour (juce::Label::textColourId, muted);
    pluginHeader.setText ("PLUG-IN PARAMETERS", juce::dontSendNotification);
    pluginHeader.setTooltip ("Right-click a knob to pin it, so 'Pinned only' shows just your favourites");
    controls.addChildComponent (pluginHeader);
    pluginScope.setTooltip ("Show all of the plug-in's parameters, or only the ones you've pinned (right-click a knob to pin)");
    pluginScope.onClick = [this] { showAllPluginParams = ! showAllPluginParams; needsRebuild = true; };
    controls.addChildComponent (pluginScope);

    controlsView.setViewedComponent (&controls, false);
    controlsView.setScrollBarsShown (false, true);
    controlsView.setScrollBarThickness (8);
    addAndMakeVisible (controlsView);

    setNode (nullptr);
    startTimerHz (20);
}

Inspector::~Inspector()
{
    stopTimer();
    clearControls();
}

void Inspector::clearControls()
{
    display.reset();
    captureKnob.reset();
    captureGroup.reset();
    namCaptureFiles.clear();
    namCaptureLabels.clear();
    meters.reset();
    knobs.clear();
    toggles.clear();
    choices.clear();
    knobParam.clear();
    toggleParam.clear();
    choiceParam.clear();
    pluginKnobs.clear();
    pluginParamIndex.clear();
    modeButtons.clear();
    strips.clear();
    for (auto* c : std::initializer_list<juce::Component*> { &openButton, &bypassButton, &autoGainButton, &prevFile, &nextFile,
                                                              &diceButton, &presetButton, &lockButton,
                                                              &autoGainSum, &addPathButton, &pluginHeader, &pluginScope, &captureName, &editCaptures })
        c->setVisible (false);
}

Knob* Inspector::addKnob (const juce::String& name, juce::Colour c)
{
    auto* k = knobs.add (new Knob (name, c));
    controls.addAndMakeVisible (k);
    return k;
}

void Inspector::setNode (Node* n)
{
    if (n != node)
    {
        host.getEngine().setAnalyzedNode (n);
        if (auto* pb = dynamic_cast<PluginBlock*> (n))
            showAllPluginParams = pb->pinned.empty();
    }
    node = n;
    clearControls();

    if (node == nullptr)
    {
        title.setText ("Rigsmith", juce::dontSendNotification);
        subtitle.setText ("NOTHING SELECTED", juce::dontSendNotification);
        info.setText ("Click a block to edit it. Drag blocks to reorder, or drop a block, model, IR or plug-in onto the middle "
                      "of another block to replace it. Right-click for options (lock, presets, MIDI learn). "
                      "Roll the dice at the top for a whole new rig - Ctrl+Z takes it back.",
                      juce::dontSendNotification);
        resized();
        repaint();
        return;
    }

    bypassButton.setVisible (true);
    bypassButton.setColour (colourFor (*node));
    bypassButton.setToggleState (! node->bypassed.load(), juce::dontSendNotification);
    const bool isTuner = node->getType() == NodeType::Tuner;
    autoGainButton.setVisible (! isTuner);
    diceButton.setVisible (! isTuner);
    lockButton.setVisible (! isTuner);

    if (auto* s = dynamic_cast<Split*> (node)) buildSplit (*s);
    else if (auto* b = dynamic_cast<Block*> (node)) buildBlock (*b);

    timerCallback();
    resized();
    repaint();
}

void Inspector::buildBlock (Block& b)
{
    const auto c = colourFor (b);
    presetButton.setVisible (b.getType() != NodeType::Tuner);

    display = std::make_unique<FxDisplay> (host.getEngine(), b);
    display->onEdited = [this] { host.getEngine().markDirty(); };
    addAndMakeVisible (*display);

    // the parameters, in the block's panel order (NAM: Gain first; Input / Output are the meters)
    std::vector<int> order = b.panelOrder();
    if (order.empty())
        for (int i = 0; i < b.numParams(); ++i) order.push_back (i);
    for (int i : order)
    {
        const auto& sp = b.getSpecs()[(size_t) i];
        if (sp.toggle)
        {
            auto* t = toggles.add (new juce::ToggleButton (sp.name));
            t->onClick = [this, &b, i, t] { b.set (i, t->getToggleState() ? 1.0f : 0.0f); host.getEngine().markDirty(); };
            controls.addAndMakeVisible (t);
            toggleParam.push_back (i);
            continue;
        }
        if (sp.isChoice())
        {
            auto* ch = choices.add (new ChoiceControl (sp.name, sp.choices, c));
            ch->setSelected (b.getChoice (i));
            ch->onChange = [this, &b, i] (int v) { b.set (i, (float) v); host.getEngine().markDirty(); };
            controls.addAndMakeVisible (ch);
            choiceParam.push_back (i);
            continue;
        }

        auto* k = addKnob (sp.name, c);
        k->slider.setRange (sp.min, sp.max, 0.0);
        if (sp.hasCentre && sp.centre > sp.min && sp.centre < sp.max)
            k->slider.setSkewFactorFromMidPoint (sp.centre);
        k->slider.setDoubleClickReturnValue (true, sp.def);
        k->slider.setValue (b.get (i), juce::dontSendNotification);
        k->formatValue = [&b, i] { return b.formatValue (i); };
        k->slider.onValueChange = [this, &b, i, k]
        {
            b.set (i, (float) k->slider.getValue());
            k->refreshText();
            if (display) display->setHighlight (b.getSpecs()[(size_t) i].name + "  " + b.formatValue (i));
            host.getEngine().markDirty();
        };
        k->onHover = [this, &b, i] (bool over)
        {
            if (display) display->setHighlight (over ? b.getSpecs()[(size_t) i].name + "  " + b.formatValue (i) : juce::String());
        };
        k->onRightClick = [this, &b, i] { if (onLearnParam) onLearnParam (b, i); };
        k->slider.setTooltip (b.getType() == NodeType::Nam && i == NamBlock::pGain
                                  ? juce::String ("Gain: drives the amp harder, like the gain knob on the real thing. "
                                                  "Rigsmith measured this capture when it loaded and trims the output, so mostly the drive changes, not the volume. "
                                                  "Double-click: 5 (as captured). Right-click: MIDI learn")
                                  : sp.name + ": drag up / down (Shift for fine). Double-click: default. Right-click: MIDI learn");
        k->refreshText();
        knobParam.push_back (i);
    }

    buildCaptureKnob (b);
    buildMeters (b);

    switch (b.getType())
    {
        case NodeType::Nam:
            openButton.setIcon (Icon::Folder); openButton.setTooltip ("Load a NAM model (or drop a .nam file on the block)");
            openButton.setVisible (true); prevFile.setVisible (true); nextFile.setVisible (true);
            break;
        case NodeType::Ir:
            openButton.setIcon (Icon::Folder); openButton.setTooltip ("Load a cab IR (or drop a .wav file on the block)");
            openButton.setVisible (true); prevFile.setVisible (true); nextFile.setVisible (true);
            if (auto* ir = dynamic_cast<IrBlock*> (&b))
            {
                // not a parameter: a setting of this block, saved with the preset
                auto* t = toggles.add (new juce::ToggleButton ("Switch off after an Amp + Cab model"));
                t->setTooltip ("Amp + Cab captures already include a cabinet, so a second one would muffle the sound. "
                               "With this on, the IR turns itself off (\"Auto off\") whenever the amp before it is an Amp + Cab model.");
                t->setToggleState (ir->skipAfterFullRig.load(), juce::dontSendNotification);
                t->onClick = [this, ir, t]
                {
                    ir->skipAfterFullRig = t->getToggleState();
                    host.getEngine().updateAutoSkips();
                    host.getEngine().markDirty();
                };
                controls.addAndMakeVisible (t);
                toggleParam.push_back (-1);
            }
            break;
        case NodeType::Plugin:
            openButton.setIcon (Icon::Open); openButton.setTooltip ("Open the plug-in's window");
            openButton.setVisible (true);
            break;
        default: break;
    }

    if (auto* pb = dynamic_cast<PluginBlock*> (&b))
        buildPluginParams (*pb);
}

static juce::File loadedFile (const Node& n)
{
    if (auto* nb = dynamic_cast<const NamBlock*> (&n)) return nb->getFile();
    if (auto* ir = dynamic_cast<const IrBlock*> (&n)) return ir->getFile();
    return {};
}

//==============================================================================
/** Choose which captures of a gear the Capture knob sweeps through, and in what order:
    tick to include, drag (or use the arrows) to reorder. Changes apply as you make them. */
class CaptureEditor : public juce::Component
{
public:
    CaptureEditor (Engine& e, NamBlock& b, std::function<void()> changedFn)
        : engine (e), block (b), onChanged (std::move (changedFn))
    {
        const auto list = block.getCaptureFiles();
        const auto names = block.getCaptureNames();
        for (int i = 0; i < list.size(); ++i)
            rows.push_back ({ list[i], names[i], toneTypeName (block.getInfo (i).toneType), true });
        if (! list.isEmpty())
            if (auto g = engine.captures.groupFor (list[0]))
            {
                title = g->title;
                for (int i = 0; i < g->size(); ++i)
                    if (! list.contains (g->files[i]))
                        rows.push_back ({ g->files[i], g->labels[i], toneTypeName (g->fileToneTypes[i]), false });
            }
        if (title.isEmpty()) title = block.getName();

        for (auto* btn : { &allButton, &resetButton })
        {
            btn->setColour (juce::TextButton::buttonColourId, card);
            addAndMakeVisible (btn);
        }
        allButton.onClick = [this] { for (auto& r : rows) r.in = true; apply(); };
        resetButton.onClick = [this]
        {
            // the gear's own order, everything in
            if (auto g = engine.captures.groupFor (rows.empty() ? juce::File() : rows[0].file))
            {
                std::vector<Row> fresh;
                for (int i = 0; i < g->size(); ++i)
                    fresh.push_back ({ g->files[i], g->labels[i], toneTypeName (g->fileToneTypes[i]), true });
                rows = fresh;
                apply();
            }
        };
        allButton.setTooltip ("Sweep through every capture of this gear");
        resetButton.setTooltip ("Every capture, in the gear's own order");
        setSize (380, juce::jmin (560, headerH + (int) rows.size() * rowH + footerH));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel);
        g.setColour (text);
        g.setFont (font (14.0f, true));
        g.drawText ("Captures to sweep", 14, 8, getWidth() - 28, 18, juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (font (11.0f));
        g.drawText (title + "  -  tick to include, drag to reorder", 14, 26, getWidth() - 28, 14, juce::Justification::centredLeft, true);

        int included = 0;
        for (auto& r : rows) included += r.in ? 1 : 0;
        for (int i = firstRow; i < (int) rows.size(); ++i)
        {
            const auto area = rowArea (i);
            if (area.getY() >= getHeight() - footerH) break;
            const auto& r = rows[(size_t) i];
            if (i == dragRow) { g.setColour (cardHi); g.fillRoundedRectangle (area.toFloat().reduced (4, 1), 5.0f); }
            // tick box
            const auto box = tickArea (i).toFloat();
            g.setColour (r.in ? accent : borderHi);
            if (r.in)
            {
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (bg);
                juce::Path t;
                t.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
                t.lineTo (box.getX() + 7.5f, box.getBottom() - 4.5f);
                t.lineTo (box.getRight() - 4.0f, box.getY() + 4.5f);
                g.strokePath (t, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.3f);

            // position in the sweep
            g.setColour (r.in ? muted : dim);
            g.setFont (mono (10.0f));
            if (r.in) g.drawText (juce::String (positionOf (i) + 1), area.getX() + 34, area.getY(), 22, area.getHeight(), juce::Justification::centredRight);
            g.setColour (r.in ? text : muted);
            g.setFont (font (12.5f, r.in));
            g.drawText (r.label, area.getX() + 62, area.getY(), area.getWidth() - 62 - 120, area.getHeight(), juce::Justification::centredLeft, true);
            g.setColour (muted);
            g.setFont (font (11.0f));
            g.drawText (r.tone, area.getRight() - 118, area.getY(), 60, area.getHeight(), juce::Justification::centredRight, true);
            if (r.in && included > 1)
            {
                // up / down arrows
                juce::Path up, down;
                const auto u = upArea (i).toFloat().reduced (6.0f), d = downArea (i).toFloat().reduced (6.0f);
                up.addTriangle (u.getX(), u.getBottom(), u.getRight(), u.getBottom(), u.getCentreX(), u.getY());
                down.addTriangle (d.getX(), d.getY(), d.getRight(), d.getY(), d.getCentreX(), d.getBottom());
                g.setColour (positionOf (i) > 0 ? muted : dim.withAlpha (0.4f));
                g.fillPath (up);
                g.setColour (positionOf (i) < included - 1 ? muted : dim.withAlpha (0.4f));
                g.fillPath (down);
            }
        }
        if ((int) rows.size() * rowH > getHeight() - headerH - footerH)
        {
            g.setColour (muted);
            g.setFont (font (10.0f));
            g.drawText ("scroll for more", 0, getHeight() - footerH - 14, getWidth() - 14, 12, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (14, 8);
        allButton.setBounds (f.removeFromLeft (110));
        f.removeFromLeft (8);
        resetButton.setBounds (f.removeFromLeft (150));
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int visible = (getHeight() - headerH - footerH) / rowH;
        firstRow = juce::jlimit (0, juce::jmax (0, (int) rows.size() - visible), firstRow - (w.deltaY > 0 ? 1 : -1));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragRow = -1;
        const int i = rowAt (e.y);
        if (i < 0) return;
        auto& r = rows[(size_t) i];
        if (tickArea (i).expanded (4).contains (e.getPosition()))
        {
            int included = 0;
            for (auto& x : rows) included += x.in ? 1 : 0;
            if (r.in && included == 1) return;   // keep at least one
            r.in = ! r.in;
            if (r.in)
            {
                // newly included captures go to the end of the sweep
                auto row = r;
                rows.erase (rows.begin() + i);
                int at = 0;
                while (at < (int) rows.size() && rows[(size_t) at].in) ++at;
                rows.insert (rows.begin() + at, row);
            }
            else
            {
                auto row = r;
                rows.erase (rows.begin() + i);
                rows.push_back (row);
            }
            apply();
            return;
        }
        if (r.in && upArea (i).contains (e.getPosition()) && i > 0 && rows[(size_t) i - 1].in) { std::swap (rows[(size_t) i], rows[(size_t) i - 1]); apply(); return; }
        if (r.in && downArea (i).contains (e.getPosition()) && i + 1 < (int) rows.size() && rows[(size_t) i + 1].in) { std::swap (rows[(size_t) i], rows[(size_t) i + 1]); apply(); return; }
        if (r.in) { dragRow = i; repaint(); }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragRow < 0) return;
        int target = rowAt (juce::jlimit (headerH, getHeight() - footerH - 1, e.y));
        if (target < 0) return;
        int lastIn = 0;
        while (lastIn + 1 < (int) rows.size() && rows[(size_t) lastIn + 1].in) ++lastIn;
        target = juce::jlimit (0, lastIn, target);
        if (target != dragRow)
        {
            auto row = rows[(size_t) dragRow];
            rows.erase (rows.begin() + dragRow);
            rows.insert (rows.begin() + target, row);
            dragRow = target;
            moved = true;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (moved) apply();
        moved = false;
        dragRow = -1;
        repaint();
    }

private:
    struct Row { juce::File file; juce::String label, tone; bool in; };
    static constexpr int headerH = 48, rowH = 28, footerH = 44;

    juce::Rectangle<int> rowArea (int i) const { return { 0, headerH + (i - firstRow) * rowH, getWidth(), rowH }; }
    juce::Rectangle<int> tickArea (int i) const { auto a = rowArea (i); return { 14, a.getCentreY() - 8, 16, 16 }; }
    juce::Rectangle<int> upArea (int i) const { auto a = rowArea (i); return { getWidth() - 54, a.getY() + 2, 20, a.getHeight() - 4 }; }
    juce::Rectangle<int> downArea (int i) const { auto a = rowArea (i); return { getWidth() - 32, a.getY() + 2, 20, a.getHeight() - 4 }; }
    int rowAt (int y) const
    {
        if (y < headerH || y >= getHeight() - footerH) return -1;
        const int i = firstRow + (y - headerH) / rowH;
        return i < (int) rows.size() ? i : -1;
    }
    int positionOf (int i) const { int p = 0; for (int k = 0; k < i; ++k) p += rows[(size_t) k].in ? 1 : 0; return p; }

    void apply()
    {
        // keep the knob on the capture it was on, wherever that ends up
        const auto current = block.getFile();
        juce::Array<juce::File> files;
        for (auto& r : rows) if (r.in) files.add (r.file);
        const int at = juce::jmax (0, files.indexOf (current));
        juce::String err;
        block.loadCaptures (files, (float) at, err);
        engine.markDirty();
        repaint();
        if (onChanged) onChanged();
    }

    Engine& engine;
    NamBlock& block;
    std::function<void()> onChanged;
    std::vector<Row> rows;
    juce::String title;
    juce::TextButton allButton { "Use them all" }, resetButton { "All, in the gear's order" };
    int firstRow = 0, dragRow = -1;
    bool moved = false;
};

void Inspector::buildMeters (Block& b)
{
    if (b.getType() == NodeType::Tuner) return;
    meters = std::make_unique<Meters>();
    auto& eng = host.getEngine();
    if (auto* nb = dynamic_cast<NamBlock*> (&b))
    {
        // NAM: the handles are its Input / Output trims (scene- and MIDI-learnable parameters)
        meters->in.getTrim = [nb] { return nb->get (NamBlock::pInput); };
        meters->in.setTrim = [nb, &eng] (float v) { nb->set (NamBlock::pInput, v); eng.markDirty(); };
        meters->out.getTrim = [nb] { return nb->get (NamBlock::pOutput); };
        meters->out.setTrim = [nb, &eng] (float v) { nb->set (NamBlock::pOutput, v); eng.markDirty(); };
        meters->in.onRightClick = [this, nb] { if (onLearnParam) onLearnParam (*nb, NamBlock::pInput); };
        meters->out.onRightClick = [this, nb] { if (onLearnParam) onLearnParam (*nb, NamBlock::pOutput); };
        meters->in.setTooltip ("Level into the amp. Drag the handle to trim it: hotter or quieter pickups, or to calibrate a capture. "
                               "Gain is the knob for drive. Double-click: 0 dB. Right-click: MIDI learn");
    }
    else
    {
        auto* n = &b;
        meters->in.getTrim = [n] { return n->inTrimDb.load(); };
        meters->in.setTrim = [n, &eng] (float v) { n->inTrimDb = v; n->touched(); eng.markDirty(); };
        meters->out.getTrim = [n] { return n->outTrimDb.load(); };
        meters->out.setTrim = [n, &eng] (float v) { n->outTrimDb = v; eng.markDirty(); };
        meters->in.setTooltip ("Level into this block. Drag the handle to trim it (how hard it's driven). Double-click: 0 dB");
    }
    meters->out.setTooltip ("Level out of this block. Drag the handle to trim it. Double-click: 0 dB");
    controls.addAndMakeVisible (*meters);
}

//==============================================================================
void Inspector::buildCaptureKnob (Block& b)
{
    if (auto* nb = dynamic_cast<NamBlock*> (&b))
    {
        // NAM: a sweep through the block's captures, blending between neighbours
        namCaptureFiles = nb->getCaptureFiles();
        const int n = namCaptureFiles.size();
        auto group = n > 0 ? host.getEngine().captures.groupFor (namCaptureFiles[0]) : nullptr;
        const bool more = group != nullptr && group->size() > 1;
        if (n < 2 && ! more) return;

        editCaptures.setButtonText (n < 2 ? "Sweep captures..." : "Edit captures...");
        editCaptures.setTooltip ("Choose which captures of this gear the Capture knob sweeps through, and in what order");
        editCaptures.onClick = [this] { showCaptureEditor(); };
        controls.addAndMakeVisible (editCaptures);
        if (n < 2) return;

        namCaptureLabels = shortenCaptureLabels (nb->getCaptureNames());
        const auto colour = gearColour (nb->getGear());
        captureKnob = std::make_unique<Knob> ("Capture", colour);
        auto* k = captureKnob.get();
        k->nameLabel.setFont (font (12.0f, true));
        k->slider.setRange (0.0, (double) (n - 1), 0.0);
        k->slider.setMouseDragSensitivity (juce::jlimit (160, 420, n * 60));
        k->slider.setDoubleClickReturnValue (false, 0);
        k->slider.setValue (nb->getCapturePosition(), juce::dontSendNotification);
        k->detents = n;
        k->detentColour = colour;
        k->valueLabel.setFont (font (12.0f, true));
        k->valueLabel.setColour (juce::Label::textColourId, text);
        k->formatValue = [this, k]
        {
            const int count = namCaptureLabels.size();
            const float pos = (float) k->slider.getValue();
            const int a = juce::jlimit (0, count - 1, (int) std::floor (pos));
            const float f = pos - (float) a;
            if (f < 0.02f || a >= count - 1) return namCaptureLabels[a];
            if (f > 0.98f) return namCaptureLabels[a + 1];
            return namCaptureLabels[a] + " > " + namCaptureLabels[a + 1];
        };
        k->slider.onValueChange = [this, k]
        {
            if (auto* block = dynamic_cast<NamBlock*> (node))
            {
                block->setCapturePosition ((float) k->slider.getValue());
                host.getEngine().markDirty();
            }
            k->refreshText();
            updateCaptureText();
        };
        k->onRightClick = [this] { showCaptureMenu(); };
        k->slider.setTooltip ("Sweeps through the captures, morphing smoothly from one to the next. "
                              "Right-click to jump to one, MIDI-learn it (an expression pedal works well) or edit the list");
        k->refreshText();
        controls.addAndMakeVisible (k);
    }
    else
    {
        // IRs: step through every IR of the pack
        const auto file = loadedFile (b);
        if (file == juce::File()) return;
        captureGroup = host.getEngine().captures.groupFor (file);
        if (captureGroup == nullptr || captureGroup->size() < 2) return;

        auto group = captureGroup;
        captureKnob = std::make_unique<Knob> ("Capture", gearColour (group->gear));
        auto* k = captureKnob.get();
        k->nameLabel.setFont (font (12.0f, true));
        k->slider.setRange (0.0, (double) (group->size() - 1), 1.0);
        k->slider.setMouseDragSensitivity (juce::jlimit (90, 400, group->size() * 14));
        k->slider.setDoubleClickReturnValue (false, 0);
        k->slider.setValue (juce::jmax (0, group->indexOf (file)), juce::dontSendNotification);
        k->detents = group->size() <= 48 ? group->size() : 0;
        k->detentColour = gearColour (group->gear);
        k->valueLabel.setFont (font (12.0f, true));
        k->valueLabel.setColour (juce::Label::textColourId, text);
        k->formatValue = [k, group]
        {
            const int i = juce::jlimit (0, group->size() - 1, (int) k->slider.getValue());
            return group->shortLabels[i];
        };
        k->slider.onValueChange = [this, k, group]
        {
            k->refreshText();
            k->repaint();
            const int i = juce::jlimit (0, group->size() - 1, (int) k->slider.getValue());
            if (node != nullptr && loadedFile (*node) != group->files[i])
                host.switchCapture (*node, group->files[i]);
            updateCaptureText();
        };
        k->onRightClick = [this] { showCaptureMenu(); };
        k->slider.setTooltip ("Every IR of " + group->title + " (" + juce::String (group->size())
                              + "). Drag, scroll or use the arrow keys to switch; right-click for the list");
        k->refreshText();
        controls.addAndMakeVisible (k);
    }

    captureName.setFont (font (11.0f));
    captureName.setColour (juce::Label::textColourId, muted);
    captureName.setJustificationType (juce::Justification::centredTop);
    captureName.setMinimumHorizontalScale (0.75f);
    captureName.setInterceptsMouseClicks (false, false);
    controls.addAndMakeVisible (captureName);
    updateCaptureText();
}

void Inspector::updateCaptureText()
{
    if (captureKnob == nullptr) return;
    if (auto* nb = dynamic_cast<NamBlock*> (node))
    {
        const int n = nb->getNumCaptures();
        const float pos = nb->getCapturePosition();
        const int a = juce::jlimit (0, n - 1, (int) std::floor (pos));
        const float f = pos - (float) a;
        juce::String head = "CAPTURE  " + juce::String (nb->getNearestCapture() + 1) + " / " + juce::String (n);
        juce::String line = toneTypeName (nb->getInfo().toneType);
        if (f >= 0.02f && f <= 0.98f && a + 1 < n)
            line = juce::String (juce::roundToInt ((1.0f - f) * 100.0f)) + "% / " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
        captureKnob->nameLabel.setText (head, juce::dontSendNotification);
        captureName.setText (line, juce::dontSendNotification);
    }
    else if (captureGroup != nullptr)
    {
        const int i = juce::jlimit (0, captureGroup->size() - 1, (int) captureKnob->slider.getValue());
        captureKnob->nameLabel.setText ("CAPTURE  " + juce::String (i + 1) + " / " + juce::String (captureGroup->size()),
                                        juce::dontSendNotification);
        captureName.setText (toneTypeName (captureGroup->fileToneTypes[i]), juce::dontSendNotification);
    }
    captureKnob->repaint();
}

void Inspector::showCaptureEditor()
{
    auto* nb = dynamic_cast<NamBlock*> (node);
    if (nb == nullptr) return;
    auto ed = std::make_unique<CaptureEditor> (host.getEngine(), *nb, [sp = juce::Component::SafePointer<Inspector> (this)]
    {
        if (sp != nullptr) sp->needsRebuild = true;
    });
    auto* anchor = captureKnob != nullptr ? static_cast<juce::Component*> (captureKnob.get()) : &editCaptures;
    juce::CallOutBox::launchAsynchronously (std::move (ed), anchor->getScreenBounds(), nullptr);
}

void Inspector::showCaptureMenu()
{
    if (node == nullptr) return;
    juce::PopupMenu m;
    if (auto* nb = dynamic_cast<NamBlock*> (node))
    {
        const auto names = nb->getCaptureNames();
        m.addSectionHeader (nb->getName());
        const int nearest = nb->getNearestCapture();
        for (int i = 0; i < names.size(); ++i)
        {
            const auto tone = toneTypeName (nb->getInfo (i).toneType);
            m.addItem (i + 1, names[i] + (tone.isNotEmpty() ? "   (" + tone + ")" : juce::String()), true, i == nearest);
        }
        m.addSeparator();
        m.addItem (1001, "Edit captures...");
        m.addItem (1002, "MIDI learn (expression pedal)");
        juce::Component::SafePointer<Inspector> sp (this);
        auto* block = nb;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (captureKnob.get()), [sp, block] (int r)
        {
            if (sp == nullptr || sp->node != block) return;
            if (r == 1001) { sp->showCaptureEditor(); return; }
            if (r == 1002) { if (sp->onLearnParam) sp->onLearnParam (*block, NamBlock::pCapture); return; }
            if (r > 0 && sp->captureKnob != nullptr) sp->captureKnob->slider.setValue (r - 1, juce::sendNotificationSync);
        });
        return;
    }

    if (captureGroup == nullptr) return;
    auto group = captureGroup;
    const auto current = loadedFile (*node);
    m.addSectionHeader (group->title);
    for (int i = 0; i < group->size(); ++i)
    {
        auto tone = toneTypeName (group->fileToneTypes[i]);
        m.addItem (i + 1, group->labels[i] + (tone.isNotEmpty() ? "   (" + tone + ")" : juce::String()), true, group->files[i] == current);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (captureKnob.get()), [this, group] (int r)
    {
        if (r <= 0 || node == nullptr || captureGroup != group || captureKnob == nullptr) return;
        captureKnob->slider.setValue (r - 1, juce::sendNotificationSync);
    });
}

void Inspector::buildPluginParams (PluginBlock& pb)
{
    auto* inst = pb.getInstance();
    if (inst == nullptr) return;

    const auto params = inst->getParameters();
    pluginHeader.setVisible (true);
    pluginScope.setVisible (true);
    pluginScope.setButtonText (showAllPluginParams ? "Showing all" : "Pinned only");

    const auto c = colourFor (pb);
    int shown = 0;
    for (int i = 0; i < params.size() && shown < 160; ++i)
    {
        auto* p = params[i];
        if (! p->isAutomatable() && ! pb.isPinned (i)) continue;
        if (! showAllPluginParams && ! pb.isPinned (i)) continue;
        ++shown;

        auto* k = pluginKnobs.add (new Knob (p->getName (24), pb.isPinned (i) ? accent : c));
        const int steps = p->getNumSteps();
        k->slider.setRange (0.0, 1.0, steps > 1 && steps < 64 ? 1.0 / (steps - 1) : 0.0);
        k->slider.setValue (p->getValue(), juce::dontSendNotification);
        k->slider.setDoubleClickReturnValue (true, p->getDefaultValue());
        k->formatValue = [p, k] { return (p->getText ((float) k->slider.getValue(), 16) + " " + p->getLabel()).trim(); };
        k->slider.onDragStart = [p] { p->beginChangeGesture(); };
        k->slider.onDragEnd = [p] { p->endChangeGesture(); };
        k->slider.onValueChange = [this, p, k]
        {
            p->setValueNotifyingHost ((float) k->slider.getValue());
            k->refreshText();
            host.getEngine().markDirty();
        };
        k->onRightClick = [this, &pb, i]
        {
            juce::PopupMenu m;
            m.addItem (1, pb.isPinned (i) ? "Unpin from panel" : "Pin to panel");
            m.addItem (2, "MIDI learn");
            m.showMenuAsync ({}, [this, &pb, i] (int r)
            {
                if (r == 1) { pb.setPinned (i, ! pb.isPinned (i)); host.getEngine().markDirty(); needsRebuild = true; }
                if (r == 2 && onLearnParam) onLearnParam (pb, 1000 + i);
            });
        };
        k->refreshText();
        controls.addAndMakeVisible (k);
        pluginParamIndex.push_back (i);
    }
}

void Inspector::buildSplit (Split& s)
{
    for (int m = 0; m < 4; ++m)
    {
        auto* btn = modeButtons.add (new juce::TextButton (splitModeName ((SplitMode) m)));
        btn->onClick = [this, &s, m]
        {
            s.setMode ((SplitMode) m);
            host.getEngine().storeScene();
            host.getEngine().markDirty();
            needsRebuild = true;  // crossover knobs appear/disappear (rebuilt from the timer, not inside this click)
        };
        controls.addAndMakeVisible (btn);
    }

    autoGainSum.setVisible (s.getMode() == SplitMode::Parallel);
    autoGainSum.setToggleState (s.autoGainSum.load(), juce::dontSendNotification);

    if (s.getMode() == SplitMode::Crossover)
    {
        auto addX = [this] (const juce::String& name, std::atomic<float>& target, float lo, float hi, float mid)
        {
            auto* k = addKnob (name, accent);
            k->slider.setRange (lo, hi, 1.0);
            k->slider.setSkewFactorFromMidPoint (mid);
            k->slider.setValue (target.load(), juce::dontSendNotification);
            k->formatValue = [k] { return juce::String (juce::roundToInt (k->slider.getValue())) + " Hz"; };
            k->slider.onValueChange = [this, &target, k] { target = (float) k->slider.getValue(); k->refreshText(); host.getEngine().markDirty(); };
            k->refreshText();
            knobParam.push_back (-1);
        };
        addX ("Low X-over", s.xoverLow, 60.0f, 1000.0f, 250.0f);
        addX ("High X-over", s.xoverHigh, 500.0f, 8000.0f, 2500.0f);
    }

    for (int i = 0; i < (int) s.branches.size(); ++i)
        controls.addAndMakeVisible (strips.add (new Strip (*this, s, i)));
    addPathButton.setVisible ((int) s.branches.size() < kMaxBranches);
}

//==============================================================================
void Inspector::timerCallback()
{
    if (node == nullptr) return;
    if (needsRebuild)
    {
        needsRebuild = false;
        const auto pos = controlsView.getViewPosition();
        setNode (node);
        controlsView.setViewPosition (pos);
        return;
    }

    title.setText (node->getName(), juce::dontSendNotification);
    if (meters != nullptr)
    {
        float in = node->peakIn.exchange (0.0f), out = node->peakOut.exchange (0.0f);
        if (auto* nb = dynamic_cast<NamBlock*> (node)) in *= dbToGain (nb->get (NamBlock::pInput));   // what reaches the model
        meters->in.setLevel (in);
        meters->out.setLevel (out);
    }
    if (auto* nb = dynamic_cast<NamBlock*> (node))
    {
        // the sweep changed somewhere else (the card's knob, MIDI, a scene, the dice...)
        if (nb->getCaptureFiles() != namCaptureFiles) { needsRebuild = true; return; }
        if (captureKnob != nullptr && ! captureKnob->slider.isMouseButtonDown()
            && std::abs (captureKnob->slider.getValue() - nb->getCapturePosition()) > 1.0e-4)
        {
            captureKnob->slider.setValue (nb->getCapturePosition(), juce::dontSendNotification);
            captureKnob->refreshText();
            updateCaptureText();
        }
    }
    else if (node->getType() == NodeType::Ir)
    {
        // the IR changed somewhere else (the card's knob, the arrows, the dice...)
        const auto file = loadedFile (*node);
        const auto group = file == juce::File() ? nullptr : host.getEngine().captures.groupFor (file);
        const bool wantsKnob = group != nullptr && group->size() > 1;
        if (wantsKnob != (captureKnob != nullptr) || (wantsKnob && group != captureGroup))
        {
            needsRebuild = true;
            return;
        }
        if (captureKnob != nullptr && ! captureKnob->slider.isMouseButtonDown())
        {
            const int i = captureGroup->indexOf (file);
            if (i >= 0 && i != (int) captureKnob->slider.getValue())
            {
                captureKnob->slider.setValue (i, juce::dontSendNotification);
                captureKnob->refreshText();
                updateCaptureText();
            }
        }
    }
    bypassButton.setToggleState (! node->bypassed.load(), juce::dontSendNotification);
    lockButton.setToggleState (node->locked.load(), juce::dontSendNotification);
    lockButton.setIcon (node->locked.load() ? Icon::Lock : Icon::Unlock);
    {
        const bool ag = node->autoGain.load();
        autoGainButton.setToggleState (ag, juce::dontSendNotification);
        const float prog = node->autoGainProgress.load();
        const float db = node->autoGainDb.load();
        const auto label = ! ag ? juce::String ("Auto")
                         : (prog < 1.0f ? juce::String ("Play...") : (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB");
        if (label != autoGainButton.getLabel()) { autoGainButton.setLabel (label); resized(); }
        autoGainButton.setProgress (ag && prog < 1.0f ? prog : -1.0f);
    }

    juce::String sub = kindLabel (*node), inf;
    if (auto* s = dynamic_cast<Split*> (node))
    {
        sub = "SPLIT - " + splitModeName (s->getMode()).toUpperCase();
        for (int m = 0; m < modeButtons.size(); ++m)
            modeButtons[m]->setToggleState ((int) s->getMode() == m, juce::dontSendNotification);
        float total = 0;
        for (auto& b : s->branches) total += b->mute.load() ? 0.0f : b->level.load();
        inf << (int) s->branches.size() << " paths - total " << juce::roundToInt (total * 100.0f) << "%";
        if (s->getLatency() > 0) inf << "\nPaths time-aligned (" << s->getLatency() << " samples)";
        switch (s->getMode())
        {
            case SplitMode::Split:     inf << "\nLevels always add up to 100%."; break;
            case SplitMode::Parallel:  inf << "\nEvery path gets the full signal."; break;
            case SplitMode::Crossover: inf << "\nA = lows, B = mids, C = highs."; break;
            case SplitMode::Stereo:    inf << "\nPan paths for a wide rig."; break;
        }
        for (auto* st : strips) st->refresh();
    }
    else if (auto* nb = dynamic_cast<NamBlock*> (node))
    {
        const auto i = nb->getInfo();
        if (i.loaded)
        {
            sub = "NAM - " + i.architecture.toUpperCase();
            // gain compensation measures and overrides Normalize: say so rather than leave both fighting
            if (nb->autoGain.load() && nb->get (NamBlock::pNormalize) > 0.5f && i.hasLoudness)
                inf << "Auto is overriding Normalize - switch one off (Normalize is steadier for amps)\n";
            if (nb->getCaptureName() != nb->getName()) inf << nb->getCaptureName() << "\n";
            if (std::abs (nb->getGainDb()) > 0.05f)
                inf << "Gain " << (nb->getGainDb() > 0 ? "+" : "") << juce::String (nb->getGainDb(), 1) << " dB drive, level "
                    << (nb->getGainCompensationDb() > 0 ? "+" : "") << juce::String (nb->getGainCompensationDb(), 1) << " dB\n";
            if (i.toneType.isNotEmpty()) inf << toneTypeName (i.toneType) << "\n";
            if (i.modeledBy.isNotEmpty()) inf << "by " << i.modeledBy << "\n";
            inf << "Trained at " << juce::String (i.modelRate / 1000.0, 1) << " kHz";
            if (i.resampling) inf << " (resampling)";
            if (i.hasLoudness) inf << "\nLoudness " << juce::String (i.loudnessDb, 1) << " dB";
            inf << "\n" << i.file.getFileName();
        }
        else inf = "No model loaded. Click Load model, double-click the block, or drop a .nam file on it.";
    }
    else if (auto* ir = dynamic_cast<IrBlock*> (node))
    {
        if (ir->getFile().existsAsFile())
            inf << ir->getFile().getFileName() << "\n" << juce::String (ir->getIrLength() / juce::jmax (1.0, ir->getSampleRate()) * 1000.0, 0) << " ms";
        else inf = "No IR loaded. Click Load IR or drop a .wav file on it.";
    }
    else if (auto* pb = dynamic_cast<PluginBlock*> (node))
    {
        sub = "VST - " + pb->getDescription().manufacturerName.toUpperCase();
        if (pb->isMissing()) inf = "This plug-in isn't installed or hasn't been scanned. Its settings are kept.";
        else
        {
            inf << categoryName (pb->getCategory()) << "  " << pb->getDescription().version;
            if (pb->getLatency() > 0) inf << "\nLatency " << pb->getLatency() << " samples";
        }

        auto* inst = pb->getInstance();
        if (inst != nullptr)
        {
            const auto params = inst->getParameters();
            for (size_t k = 0; k < pluginParamIndex.size(); ++k)
                if (juce::isPositiveAndBelow (pluginParamIndex[k], params.size()) && ! pluginKnobs[(int) k]->slider.isMouseButtonDown())
                {
                    pluginKnobs[(int) k]->slider.setValue (params[pluginParamIndex[k]]->getValue(), juce::dontSendNotification);
                    pluginKnobs[(int) k]->refreshText();
                }
        }
    }
    else if (auto* gb = dynamic_cast<GateBlock*> (node))
    {
        inf << "Reduction " << juce::String (gb->getReduction(), 1) << " dB";
    }

    if (dynamic_cast<Split*> (node) == nullptr)
        inf = inf.replace ("\n", "   -   ");
    subtitle.setText (sub, juce::dontSendNotification);
    info.setColour (juce::Label::textColourId, inf.startsWith ("Auto is overriding") ? accent : muted);
    const bool hadInfo = info.getText().isNotEmpty();
    info.setText (inf, juce::dontSendNotification);
    title.setTooltip (inf.replace ("   -   ", "\n"));
    if (hadInfo != inf.isNotEmpty()) resized();

    if (auto* b = dynamic_cast<Block*> (node))
    {
        for (size_t k = 0; k < knobParam.size(); ++k)
            if (knobParam[k] >= 0 && ! knobs[(int) k]->slider.isMouseButtonDown())
            {
                knobs[(int) k]->slider.setValue (b->get (knobParam[k]), juce::dontSendNotification);
                knobs[(int) k]->refreshText();
            }
        for (size_t t = 0; t < toggleParam.size(); ++t)
            if (toggleParam[t] >= 0)
                toggles[(int) t]->setToggleState (b->get (toggleParam[t]) > 0.5f, juce::dontSendNotification);
        for (size_t c = 0; c < choiceParam.size(); ++c)
            choices[(int) c]->setSelected (b->getChoice (choiceParam[c]));
    }
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    if (node != nullptr)
    {
        g.setColour (colourFor (*node));
        g.fillRoundedRectangle ((float) title.getX() - 12.0f, (float) title.getY() + 5.0f, 5.0f, (float) title.getHeight() - 10.0f, 2.5f);
    }
}

void Inspector::layoutControls()
{
    const int h = juce::jmax (110, controlsView.getHeight() - 10);
    int x = 0;

    // knobs fill columns top to bottom when the panel is tall enough for more than one row
    const int rows = juce::jmax (1, (h + 6) / 118);
    const int kh = juce::jlimit (100, 128, (h - (rows - 1) * 6) / rows);
    const int kw = 80;
    auto placeGrid = [&] (juce::OwnedArray<Knob>& ks, int startX, int startY, int availH)
    {
        // read left to right, then the next row
        const int r = juce::jmax (1, (availH + 6) / (kh + 6));
        const int cols = juce::jmax (1, (ks.size() + r - 1) / r);
        for (int i = 0; i < ks.size(); ++i)
            ks[i]->setBounds (startX + (i % cols) * (kw + 4), startY + (i / cols) * (kh + 6), kw, kh);
        return startX + (ks.isEmpty() ? 0 : cols * (kw + 4));
    };

    if (! modeButtons.isEmpty())
    {
        int bx = 0;
        for (auto* b : modeButtons) { b->setBounds (bx, 0, 104, 30); bx += 110; }
        autoGainSum.setBounds (bx + 6, 0, 160, 30);
        int y = 40;
        for (auto* k : knobs) { k->setBounds (x, y, kw, juce::jmin (h - y, 118)); x += kw + 4; }
        for (auto* s : strips) { s->setBounds (x, y, 112, h - y); x += 120; }
        if (addPathButton.isVisible()) { addPathButton.setBounds (x, y + (h - y) / 2 - 16, 100, 32); x += 108; }
        controls.setSize (juce::jmax (x, bx + 170), h);
        return;
    }

    if (meters != nullptr)
    {
        // IN / OUT meters first, full height
        meters->setBounds (x, 0, 64, juce::jmin (h, 230));
        x += 64 + 18;
    }

    if (! choices.isEmpty())
    {
        int cw = 0;
        for (auto* c : choices) cw = juce::jmax (cw, c->preferredWidth());
        int y = 0;
        for (auto* c : choices)
        {
            if (y > 0 && y + 52 > h) { y = 0; x += cw + 14; }
            c->setBounds (x, y, cw, 52);
            y += 58;
        }
        x += cw + 16;
    }

    if (captureKnob != nullptr)
    {
        // the big one, first: which capture of this gear
        const int cw = 150, ch = juce::jmin (h - (editCaptures.isVisible() ? 30 : 0), 176);
        captureKnob->setBounds (x, 0, cw, ch - 18);
        captureName.setBounds (x - 10, ch - 18, cw + 20, 16);
        if (editCaptures.isVisible()) editCaptures.setBounds (x + 15, ch + 2, cw - 30, 24);
        x += cw + 22;
    }
    else if (editCaptures.isVisible())
    {
        editCaptures.setBounds (x, h / 2 - 14, 140, 28);
        x += 140 + 18;
    }

    x = placeGrid (knobs, x, 0, h);

    if (! toggles.isEmpty())
    {
        int y = 22, tw = 150;
        for (auto* t : toggles)
            tw = juce::jmax (tw, juce::GlyphArrangement::getStringWidthInt (font (14.0f), t->getButtonText()) + 44);
        for (auto* t : toggles) { t->setBounds (x + 8, y, tw, 28); y += 34; }
        x += tw + 16;
    }

    if (pluginHeader.isVisible())
    {
        x += 12;
        pluginHeader.setBounds (x, 0, 180, 20);
        pluginScope.setBounds (x + 184, 0, 104, 22);
        const int end = placeGrid (pluginKnobs, x, 28, h - 28);
        x = juce::jmax (x + 292, end);
    }

    controls.setSize (juce::jmax (x, 10), h);
}

void Inspector::layoutIcons (juce::Rectangle<int> row)
{
    const int h = row.getHeight();
    auto place = [&] (IconButton& b, int gapAfter = 4)
    {
        if (! b.isVisible()) return;
        const int w = juce::jmin (b.preferredWidth (h), row.getWidth());
        b.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (gapAfter);
    };
    // On first, so it's in the same place on every block
    if (bypassButton.isVisible())
    {
        bypassButton.setBounds (row.removeFromLeft (juce::jmin (bypassButton.preferredWidth (h), row.getWidth())));
        row.removeFromLeft (10);
    }
    place (openButton);
    place (prevFile, 2);
    place (nextFile, 10);
    place (autoGainButton, 10);
    place (diceButton, 10);
    place (presetButton);
    place (lockButton);
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    const bool isSplit = dynamic_cast<Split*> (node) != nullptr;
    auto left = r.removeFromLeft (node == nullptr || isSplit ? 300 : juce::jlimit (330, 600, (int) ((float) getWidth() * 0.38f)));
    r.removeFromLeft (16);

    auto top = left.removeFromTop (28);
    subtitle.setBounds (top.removeFromRight (juce::jmin (top.getWidth() / 2, 220)));
    title.setBounds (top.withTrimmedLeft (12));
    left.removeFromTop (6);
    if (node != nullptr)
    {
        layoutIcons (left.removeFromTop (30));
        left.removeFromTop (6);
    }

    if (display != nullptr)
    {
        if (info.getText().isNotEmpty()) { info.setBounds (left.removeFromTop (16)); left.removeFromTop (4); }
        else info.setBounds ({});
        display->setBounds (left);
    }
    else
    {
        info.setBounds (left.reduced (0, 2));
    }

    controlsView.setBounds (r);
    layoutControls();
}

} // namespace rig
