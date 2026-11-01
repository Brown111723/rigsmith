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
    addAndMakeVisible (slider);

    nameLabel.setText (name, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (font (12.0f, true));
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (mono (11.0f));
    valueLabel.setColour (juce::Label::textColourId, muted);
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
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
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
    subtitle.setFont (font (12.0f, true));
    subtitle.setColour (juce::Label::textColourId, muted);
    info.setFont (font (12.0f));
    info.setColour (juce::Label::textColourId, muted);
    info.setJustificationType (juce::Justification::topLeft);
    for (auto* l : { &title, &subtitle, &info }) addAndMakeVisible (l);

    openButton.onClick = [this] { if (node != nullptr && onOpen) onOpen (*node); };
    addChildComponent (openButton);

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

    autoGain.setTooltip ("In Parallel mode, turn the sum down when the paths add up to more than 100%");
    autoGain.onClick = [this]
    {
        if (auto* s = dynamic_cast<Split*> (node)) { s->autoGain = autoGain.getToggleState(); host.getEngine().markDirty(); }
    };
    addChildComponent (autoGain);

    addPathButton.onClick = [this] { if (auto* s = dynamic_cast<Split*> (node)) host.getEngine().addBranch (*s); };
    addChildComponent (addPathButton);

    setNode (nullptr);
    startTimerHz (20);
}

Inspector::~Inspector() { stopTimer(); }

void Inspector::clearControls()
{
    knobs.clear();
    toggles.clear();
    knobParam.clear();
    toggleParam.clear();
    modeButtons.clear();
    strips.clear();
    openButton.setVisible (false);
    bypassButton.setVisible (false);
    autoGain.setVisible (false);
    addPathButton.setVisible (false);
}

void Inspector::setNode (Node* n)
{
    node = n;
    clearControls();

    if (node == nullptr)
    {
        title.setText ("Rigsmith", juce::dontSendNotification);
        subtitle.setText ("NOTHING SELECTED", juce::dontSendNotification);
        info.setText ("Click a block to edit it. Drag blocks to reorder or into another path. Right-click for options, "
                      "double-click to load a model or IR, and drop .nam / .wav files anywhere.",
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
    for (int i = 0; i < b.numParams(); ++i)
    {
        const auto& sp = b.getSpecs()[(size_t) i];
        if (sp.toggle)
        {
            auto* t = toggles.add (new juce::ToggleButton (sp.name));
            t->onClick = [this, &b, i, t] { b.set (i, t->getToggleState() ? 1.0f : 0.0f); host.getEngine().markDirty(); };
            addAndMakeVisible (t);
            toggleParam.push_back (i);
            continue;
        }

        auto* k = knobs.add (new Knob (sp.name, c));
        k->slider.setRange (sp.min, sp.max, 0.0);
        if (sp.centre > sp.min && sp.centre < sp.max)
            k->slider.setSkewFactorFromMidPoint (sp.centre);
        k->slider.setDoubleClickReturnValue (true, sp.def);
        k->slider.setValue (b.get (i), juce::dontSendNotification);
        k->formatValue = [&b, i] { return b.formatValue (i); };
        k->slider.onValueChange = [this, &b, i, k]
        {
            b.set (i, (float) k->slider.getValue());
            k->refreshText();
            host.getEngine().markDirty();
        };
        k->onRightClick = [this, &b, i] { if (onLearnParam) onLearnParam (b, i); };
        k->refreshText();
        addAndMakeVisible (k);
        knobParam.push_back (i);
    }

    switch (b.getType())
    {
        case NodeType::Nam:    openButton.setButtonText ("Load model..."); openButton.setVisible (true); break;
        case NodeType::Ir:     openButton.setButtonText ("Load IR...");    openButton.setVisible (true); break;
        case NodeType::Plugin: openButton.setButtonText ("Open plug-in");  openButton.setVisible (true); break;
        default: break;
    }
}

void Inspector::buildSplit (Split& s)
{
    for (int m = 0; m < 4; ++m)
    {
        auto* btn = modeButtons.add (new juce::TextButton (splitModeName ((SplitMode) m)));
        btn->setClickingTogglesState (false);
        btn->onClick = [this, &s, m]
        {
            s.setMode ((SplitMode) m);
            host.getEngine().storeScene();
            host.getEngine().markDirty();
            needsRebuild = true;  // crossover knobs appear/disappear (rebuilt from the timer, not inside this click)
        };
        addAndMakeVisible (btn);
    }

    autoGain.setVisible (s.getMode() == SplitMode::Parallel);
    autoGain.setToggleState (s.autoGain.load(), juce::dontSendNotification);

    if (s.getMode() == SplitMode::Crossover)
    {
        auto addX = [this, &s] (const juce::String& name, std::atomic<float>& target, float lo, float hi, float mid)
        {
            auto* k = knobs.add (new Knob (name, accent));
            k->slider.setRange (lo, hi, 1.0);
            k->slider.setSkewFactorFromMidPoint (mid);
            k->slider.setValue (target.load(), juce::dontSendNotification);
            k->formatValue = [k] { return juce::String (juce::roundToInt (k->slider.getValue())) + " Hz"; };
            k->slider.onValueChange = [this, &target, k] { target = (float) k->slider.getValue(); k->refreshText(); host.getEngine().markDirty(); };
            k->refreshText();
            addAndMakeVisible (k);
            knobParam.push_back (-1);
            juce::ignoreUnused (s);
        };
        addX ("Low X-over", s.xoverLow, 60.0f, 1000.0f, 250.0f);
        addX ("High X-over", s.xoverHigh, 500.0f, 8000.0f, 2500.0f);
    }

    for (int i = 0; i < (int) s.branches.size(); ++i)
    {
        auto* st = strips.add (new Strip (*this, s, i));
        addAndMakeVisible (st);
    }
    addPathButton.setVisible ((int) s.branches.size() < kMaxBranches);
}

void Inspector::timerCallback()
{
    if (node == nullptr) return;
    if (needsRebuild)
    {
        needsRebuild = false;
        setNode (node);
        return;
    }

    title.setText (node->getName(), juce::dontSendNotification);
    bypassButton.setToggleState (node->bypassed.load(), juce::dontSendNotification);

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
        sub = "VST3 - " + pb->getDescription().manufacturerName.toUpperCase();
        if (pb->isMissing()) inf = "This plug-in isn't installed or hasn't been scanned. Its settings are kept.";
        else inf << pb->getDescription().version << (pb->getLatency() > 0 ? "\nLatency " + juce::String (pb->getLatency()) + " samples" : juce::String());
    }
    else if (auto* gb = dynamic_cast<GateBlock*> (node))
    {
        inf << "Reduction " << juce::String (gb->getReduction(), 0) << " dB";
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
    g.setColour (border);
    g.drawVerticalLine (290, 12.0f, (float) getHeight() - 12.0f);
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto left = r.removeFromLeft (262);
    r.removeFromLeft (28);

    title.setBounds (left.removeFromTop (34).withTrimmedLeft (14));
    subtitle.setBounds (left.removeFromTop (18));
    auto buttons = left.removeFromBottom (32);
    if (openButton.isVisible()) openButton.setBounds (buttons.removeFromLeft (130));
    buttons.removeFromLeft (8);
    bypassButton.setBounds (buttons.removeFromLeft (90));
    left.removeFromBottom (6);
    info.setBounds (left.reduced (0, 4));

    if (! modeButtons.isEmpty())
    {
        auto top = r.removeFromTop (30);
        for (auto* b : modeButtons) { b->setBounds (top.removeFromLeft (104)); top.removeFromLeft (6); }
        top.removeFromLeft (10);
        autoGain.setBounds (top.removeFromLeft (130));
        r.removeFromTop (10);
    }

    auto row = r;
    for (auto* k : knobs) { k->setBounds (row.removeFromLeft (86).withHeight (juce::jmin (row.getHeight(), 118))); row.removeFromLeft (4); }
    if (! toggles.isEmpty())
    {
        auto col = row.removeFromLeft (130).withTrimmedTop (20);
        for (auto* t : toggles) { t->setBounds (col.removeFromTop (28)); col.removeFromTop (6); }
    }
    for (auto* s : strips) { s->setBounds (row.removeFromLeft (112)); row.removeFromLeft (8); }
    if (addPathButton.isVisible())
        addPathButton.setBounds (row.removeFromLeft (100).withSizeKeepingCentre (100, 32));
}

} // namespace rig
