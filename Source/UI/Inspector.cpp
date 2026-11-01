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
    slider.setBounds (r.withSizeKeepingCentre (s, s));
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
    title.setFont (font (19.0f, true));
    title.setMinimumHorizontalScale (0.7f);
    subtitle.setFont (font (12.0f, true));
    subtitle.setColour (juce::Label::textColourId, muted);
    info.setFont (font (12.0f));
    info.setColour (juce::Label::textColourId, muted);
    info.setJustificationType (juce::Justification::topLeft);
    for (auto* l : { &title, &subtitle, &info }) addAndMakeVisible (l);

    openButton.onClick = [this] { if (node != nullptr && onOpen) onOpen (*node); };
    addChildComponent (openButton);
    replaceButton.setTooltip ("Swap this block for another in the same place (keeps scenes and footswitches)");
    replaceButton.onClick = [this] { if (node != nullptr) host.showReplaceMenu (*node, &replaceButton); };
    addChildComponent (replaceButton);

    prevFile.setTooltip ("Previous file in this folder");
    nextFile.setTooltip ("Next file in this folder");
    prevFile.onClick = [this] { if (node != nullptr) host.browseFile (*node, -1); };
    nextFile.onClick = [this] { if (node != nullptr) host.browseFile (*node, 1); };
    addChildComponent (prevFile);
    addChildComponent (nextFile);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setButtonText ("Bypass");
    bypassButton.setColour (juce::TextButton::buttonOnColourId, red);
    bypassButton.onClick = [this]
    {
        if (node == nullptr) return;
        node->bypassed = bypassButton.getToggleState();
        host.getEngine().storeScene();
        host.getEngine().markDirty();
    };
    addChildComponent (bypassButton);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setColour (juce::TextButton::buttonOnColourId, teal);
    autoGainButton.setTooltip ("Match this block's output loudness to its input, so switching it on doesn't jump in volume");
    autoGainButton.onClick = [this]
    {
        if (node == nullptr) return;
        node->autoGain = autoGainButton.getToggleState();
        host.getEngine().markDirty();
    };
    addChildComponent (autoGainButton);

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
    for (auto* c : std::initializer_list<juce::Component*> { &openButton, &replaceButton, &bypassButton, &autoGainButton, &prevFile, &nextFile,
                                                              &autoGainSum, &addPathButton, &pluginHeader, &pluginScope })
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
        info.setText ("Click a block to edit it. Drag blocks to reorder or into another path. Right-click for options "
                      "(replace, auto gain, MIDI learn), double-click to load a model or IR, and drop .nam / .wav files anywhere.",
                      juce::dontSendNotification);
        resized();
        repaint();
        return;
    }

    bypassButton.setVisible (true);
    bypassButton.setToggleState (node->bypassed.load(), juce::dontSendNotification);

    if (auto* s = dynamic_cast<Split*> (node)) buildSplit (*s);
    else if (auto* b = dynamic_cast<Block*> (node)) buildBlock (*b);

    timerCallback();
    resized();
    repaint();
}

void Inspector::buildBlock (Block& b)
{
    const auto c = colourFor (b);
    replaceButton.setVisible (true);
    autoGainButton.setVisible (true);
    autoGainButton.setToggleState (b.autoGain.load(), juce::dontSendNotification);

    if (b.getVisual() != Visual::None)
    {
        display = std::make_unique<FxDisplay> (host.getEngine(), b);
        display->onEdited = [this] { host.getEngine().markDirty(); };
        addAndMakeVisible (*display);
    }

    for (int i = 0; i < b.numParams(); ++i)
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
        k->refreshText();
        knobParam.push_back (i);
    }

    switch (b.getType())
    {
        case NodeType::Nam:    openButton.setButtonText ("Load model..."); openButton.setVisible (true); prevFile.setVisible (true); nextFile.setVisible (true); break;
        case NodeType::Ir:     openButton.setButtonText ("Load IR...");    openButton.setVisible (true); prevFile.setVisible (true); nextFile.setVisible (true); break;
        case NodeType::Plugin: openButton.setButtonText ("Open plug-in");  openButton.setVisible (true); break;
        default: break;
    }

    if (auto* pb = dynamic_cast<PluginBlock*> (&b))
        buildPluginParams (*pb);
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
    autoGainButton.setVisible (true);
    autoGainButton.setToggleState (s.autoGain.load(), juce::dontSendNotification);

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
    bypassButton.setToggleState (node->bypassed.load(), juce::dontSendNotification);
    autoGainButton.setToggleState (node->autoGain.load(), juce::dontSendNotification);
    autoGainButton.setButtonText (node->autoGain.load() ? "Auto " + juce::String (node->autoGainDb.load(), 1) + " dB" : "Auto gain");

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
            if (i.gear.isNotEmpty()) inf << i.gear << "\n";
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
            inf << "\nRight-click a parameter knob to pin it here.";
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

    subtitle.setText (sub, juce::dontSendNotification);
    info.setText (inf, juce::dontSendNotification);

    if (auto* b = dynamic_cast<Block*> (node))
    {
        for (size_t k = 0; k < knobParam.size(); ++k)
            if (knobParam[k] >= 0 && ! knobs[(int) k]->slider.isMouseButtonDown())
            {
                knobs[(int) k]->slider.setValue (b->get (knobParam[k]), juce::dontSendNotification);
                knobs[(int) k]->refreshText();
            }
        for (size_t t = 0; t < toggleParam.size(); ++t)
            toggles[(int) t]->setToggleState (b->get (toggleParam[t]) > 0.5f, juce::dontSendNotification);
        for (size_t c = 0; c < choiceParam.size(); ++c)
            choices[(int) c]->setSelected (b->getChoice (choiceParam[c]));
    }
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    if (node != nullptr)
    {
        g.setColour (colourFor (*node));
        g.fillRoundedRectangle (16.0f, 18.0f, 6.0f, 26.0f, 3.0f);
    }
}

void Inspector::layoutControls()
{
    const int h = juce::jmax (120, controlsView.getHeight() - 10);
    int x = 0;

    if (! modeButtons.isEmpty())
    {
        int bx = 0;
        for (auto* b : modeButtons) { b->setBounds (bx, 0, 104, 30); bx += 110; }
        autoGainSum.setBounds (bx + 6, 0, 160, 30);
        int y = 40;
        for (auto* k : knobs) { k->setBounds (x, y, 80, juce::jmin (h - y, 118)); x += 84; }
        for (auto* s : strips) { s->setBounds (x, y, 112, h - y); x += 120; }
        if (addPathButton.isVisible()) { addPathButton.setBounds (x, y + (h - y) / 2 - 16, 100, 32); x += 108; }
        controls.setSize (juce::jmax (x, bx + 170), h);
        return;
    }

    if (! choices.isEmpty())
    {
        int cw = 0;
        for (auto* c : choices) cw = juce::jmax (cw, c->preferredWidth());
        int y = 0;
        for (auto* c : choices) { c->setBounds (x, y, cw, 52); y += 58; }
        x += cw + 14;
    }

    for (auto* k : knobs) { k->setBounds (x, 0, 78, juce::jmin (h, 118)); x += 80; }

    if (! toggles.isEmpty())
    {
        int y = 22;
        for (auto* t : toggles) { t->setBounds (x + 4, y, 150, 28); y += 34; }
        x += 160;
    }

    if (pluginHeader.isVisible())
    {
        x += 10;
        const int startX = x;
        pluginHeader.setBounds (x, 0, 180, 18);
        pluginScope.setBounds (x + 180, 0, 100, 22);
        int px = x, py = 26;
        const int rows = juce::jmax (1, (h - 26) / 104);
        int row = 0;
        for (auto* k : pluginKnobs)
        {
            k->setBounds (px, py, 76, 100);
            if (++row >= rows) { row = 0; px += 80; py = 26; }
            else py += 104;
        }
        x = juce::jmax (startX + 290, px + (row > 0 ? 80 : 0));
    }

    controls.setSize (juce::jmax (x, 10), h);
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto left = r.removeFromLeft (262);
    r.removeFromLeft (16);

    title.setBounds (left.removeFromTop (34).withTrimmedLeft (14));
    subtitle.setBounds (left.removeFromTop (18));
    auto row2 = left.removeFromBottom (30);
    left.removeFromBottom (6);
    auto row1 = left.removeFromBottom (30);
    left.removeFromBottom (6);
    info.setBounds (left.reduced (0, 4));

    if (openButton.isVisible()) { openButton.setBounds (row1.removeFromLeft (118)); row1.removeFromLeft (6); }
    if (prevFile.isVisible())   { prevFile.setBounds (row1.removeFromLeft (32)); row1.removeFromLeft (4); nextFile.setBounds (row1.removeFromLeft (32)); row1.removeFromLeft (6); }
    if (replaceButton.isVisible()) replaceButton.setBounds (row1.removeFromLeft (juce::jmin (90, row1.getWidth())));
    bypassButton.setBounds (row2.removeFromLeft (84));
    row2.removeFromLeft (6);
    autoGainButton.setBounds (row2.removeFromLeft (130));

    if (display != nullptr)
    {
        display->setBounds (r.removeFromLeft (juce::jmin (420, r.getWidth() / 2)));
        r.removeFromLeft (16);
    }
    controlsView.setBounds (r);
    layoutControls();
}

} // namespace rig
