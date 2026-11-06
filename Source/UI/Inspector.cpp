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

Knob::~Knob() { stopTimer(); }

void Knob::setModTarget (Engine& e, Block& b, const juce::String& target, int param)
{
    modEngine = &e;
    modBlock = &b;
    modTarget = target;
    modParam = param;
    startTimerHz (30);
}

void Knob::timerCallback()
{
    if (modBlock == nullptr) return;
    const size_t n = modEngine->mods.routesTo (modTarget, modParam).size();
    const float live = modBlock->get (modParam);
    if (n != lastRoutes || (n > 0 && live != lastLive))
    {
        lastRoutes = n;
        lastLive = live;
        repaint();
    }
}

static bool isModSourceDrag (const juce::String& s) { return s.startsWith ("modsrc:"); }

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    if (modEngine == nullptr || ! isModSourceDrag (d.description.toString())) return false;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    return juce::isPositiveAndBelow (src, ModMatrix::numSources) && &modEngine->mods.source (src) != modBlock;   // not onto its own knobs
}

void Knob::itemDragEnter (const SourceDetails& d)
{
    if (modEngine == nullptr) return;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (! juce::isPositiveAndBelow (src, ModMatrix::numSources)) return;
    dropHint = "+ " + modEngine->mods.source (src).getName();
    dropColour = modEngine->mods.source (src).getColour();
    repaint();
}

void Knob::itemDragExit (const SourceDetails&) { dropHint.clear(); repaint(); }

void Knob::itemDropped (const SourceDetails& d)
{
    dropHint.clear();
    repaint();
    if (modEngine == nullptr || ! isModSourceDrag (d.description.toString())) return;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (! juce::isPositiveAndBelow (src, ModMatrix::numSources)) return;
    const bool bi = modEngine->mods.source (src).isBipolar();
    // start where it's audible but not wild: half the travel (bipolar: a quarter each way)
    const float base = modBlock->getSpecs()[(size_t) modParam].toProportion (modBlock->getBase (modParam));
    float amount = bi ? 0.25f : 0.5f;
    if (! bi && base > 0.6f) amount = -0.5f;   // near the top: modulate downwards
    modEngine->addModRoute (src, modTarget, modParam, amount);
    modEngine->message (modEngine->mods.source (src).getName() + " now moves " + modEngine->describeModTarget (modTarget, modParam)
                        + ". Set how far in the MOD panel (or right-click the knob)");
    lastRoutes = 0;
    repaint();
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    // modulation: a ring per source showing how far it can move the knob, and a dot where it is now
    if (modEngine != nullptr && modBlock != nullptr)
    {
        const auto routes = modEngine->mods.routesTo (modTarget, modParam);
        if (! routes.empty())
        {
            const auto& sp = modBlock->getSpecs()[(size_t) modParam];
            const auto b = slider.getBounds().toFloat();
            const auto c = b.getCentre();
            const float a0 = juce::degreesToRadians (225.0f), a1 = juce::degreesToRadians (495.0f);
            const float base = sp.toProportion (modBlock->getBase (modParam));
            float rad = b.getWidth() * 0.5f - 1.5f;
            for (auto* r : routes)
            {
                const auto& src = modEngine->mods.source (r->source);
                const float amt = r->amount;
                const float lo = juce::jlimit (0.0f, 1.0f, src.isBipolar() ? base - std::abs (amt) : base + juce::jmin (0.0f, amt));
                const float hi = juce::jlimit (0.0f, 1.0f, src.isBipolar() ? base + std::abs (amt) : base + juce::jmax (0.0f, amt));
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0 + (a1 - a0) * lo, a0 + (a1 - a0) * juce::jmax (hi, lo + 0.004f), true);
                g.setColour (src.getColour().withAlpha (0.85f));
                g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                rad += 3.2f;
            }
            const float live = sp.toProportion (modBlock->get (modParam));
            const auto p = c.getPointOnCircumference (b.getWidth() * 0.5f - 1.5f, a0 + (a1 - a0) * live);
            g.setColour (theme::text);
            g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            g.setColour (modEngine->mods.source (routes.front()->source).getColour());
            g.drawEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f, 1.2f);
        }
    }

    if (dropHint.isNotEmpty())
    {
        g.setColour (dropColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 10.0f, 2.0f);
        g.setFont (theme::font (11.0f, true));
        g.drawText (dropHint, getLocalBounds().removeFromBottom (18), juce::Justification::centred);
    }
}

//==============================================================================
void showKnobMenu (Engine& engine, Block& block, const juce::String& target, int param, juce::Component* anchor, std::function<void()> learn)
{
    juce::PopupMenu m, with;
    const auto routes = engine.mods.routesTo (target, param);
    const auto name = block.getSpecs()[(size_t) param].name;
    m.addSectionHeader (name);
    if (learn) m.addItem (1, "MIDI learn...");
    for (int s = 0; s < ModMatrix::numSources; ++s)
    {
        auto& src = engine.mods.source (s);
        if (&src == &block) continue;   // a modulator can't usefully drive its own knobs from here
        bool on = false;
        for (auto* r : routes) on |= r->source == s;
        with.addColouredItem (100 + s, src.getName() + "  -  " + src.describe(), src.getColour(), true, on);
    }
    m.addSubMenu ("Modulate with", with);
    if (! routes.empty())
    {
        m.addSeparator();
        for (auto* r : routes)
        {
            const int pct = juce::roundToInt (r->amount * 100.0f);
            m.addItem (200 + r->source, "Remove " + engine.mods.source (r->source).getName() + " (" + (pct > 0 ? "+" : "") + juce::String (pct) + "%)");
        }
        if (routes.size() > 1) m.addItem (300, "Remove all modulation");
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                     [&engine, &block, target, param, learn, sp = juce::Component::SafePointer<juce::Component> (anchor)] (int r)
    {
        if (r == 1 && learn) learn();
        else if (r >= 100 && r < 100 + ModMatrix::numSources)
        {
            const int s = r - 100;
            const bool bi = engine.mods.source (s).isBipolar();
            engine.addModRoute (s, target, param, bi ? 0.25f : 0.5f);
            engine.message (engine.mods.source (s).getName() + " now moves " + engine.describeModTarget (target, param));
            juce::ignoreUnused (block);
        }
        else if (r >= 200 && r < 200 + ModMatrix::numSources)
        {
            for (int i = (int) engine.mods.routes.size(); --i >= 0;)
            {
                const auto& rt = engine.mods.routes[(size_t) i];
                if (rt.source == r - 200 && rt.target == target && rt.param == param) engine.removeModRoute (i);
            }
        }
        else if (r == 300) engine.removeModRoutesTo (target, param);
        if (sp != nullptr) sp->repaint();
    });
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
    r.removeFromTop (36.0f);      // label + live level
    r.removeFromBottom (20.0f);   // trim
    return r.withSizeKeepingCentre (8.0f, r.getHeight()).translated (-4.0f, 0.0f);
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
    auto top = getLocalBounds().removeFromTop (36);
    g.setColour (muted);
    g.setFont (font (10.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (label, top.removeFromTop (18), juce::Justification::centredBottom);

    // the live level in dBFS (peak, held for a moment)
    const float heldDb = gainToDb (held);
    g.setColour (held >= 1.0f ? red : heldDb > -6.0f ? accent : text.withAlpha (0.9f));
    g.setFont (mono (10.5f));
    g.drawText (held > 1.0e-5f ? juce::String (heldDb, 1) : juce::String ("-inf"), top, juce::Justification::centred);

    // the signal: -60 .. +6 dBFS up the track, with a scale beside it
    g.setColour (juce::Colour (0xff0b0c0e));
    g.fillRoundedRectangle (t, 2.5f);
    auto levelY = [&t] (float lin)
    {
        const float db = juce::jlimit (-60.0f, 6.0f, gainToDb (lin));
        return t.getBottom() - (db + 60.0f) / 66.0f * t.getHeight();
    };
    g.setColour (border.brighter (0.15f));
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        g.fillRect (t.getRight() + 2.0f, levelY (dbToGain (db)), db == 0.0f ? 5.0f : 3.0f, 1.0f);

    const float y = levelY (shown);
    const float zeroY = levelY (1.0f);
    const auto fill = juce::Rectangle<float> (t.getX() + 1.5f, y, t.getWidth() - 3.0f, t.getBottom() - y - 1.5f);
    if (fill.getHeight() > 0.5f)
    {
        juce::ColourGradient grad (teal, 0.0f, t.getBottom(), red, 0.0f, zeroY, false);
        grad.addColour (0.8, accent);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 1.5f);
    }
    if (held > 1.0e-4f)
    {
        g.setColour (held >= 1.0f ? red : text.withAlpha (0.75f));
        g.fillRect (t.getX() + 1.5f, levelY (held) - 0.75f, t.getWidth() - 3.0f, 1.5f);
    }

    // the trim handle: a bar across the track with a pointer
    const float trim = getTrim ? getTrim() : 0.0f;
    const float hy = trimToY (trim);
    const bool over = isMouseOverOrDragging();
    const auto hc = over ? accent : (std::abs (trim) < 0.05f ? text.withAlpha (0.8f) : accent.withAlpha (0.9f));
    juce::Path h;
    h.addTriangle (t.getRight() + 9.0f, hy, t.getRight() + 16.0f, hy - 5.0f, t.getRight() + 16.0f, hy + 5.0f);
    g.setColour (hc);
    g.fillPath (h);
    g.fillRoundedRectangle (t.getX() - 3.0f, hy - 1.25f, t.getWidth() + 6.0f, 2.5f, 1.0f);
    g.setColour (std::abs (trim) < 0.05f ? muted : accent);
    g.setFont (mono (10.0f));
    g.drawText ((trim > 0.05f ? "+" : "") + juce::String (trim, 1), getLocalBounds().removeFromBottom (18), juce::Justification::centred);
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
    builtOrder.clear();
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
    builtOrder = order;
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
            ch->setSelected (juce::roundToInt (b.getBase (i)));
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
        k->slider.setValue (b.getBase (i), juce::dontSendNotification);
        k->formatValue = [&b, i] { return b.formatValue (i, b.getBase (i)); };
        k->setModTarget (host.getEngine(), b, b.id, i);
        k->slider.onValueChange = [this, &b, i, k]
        {
            b.set (i, (float) k->slider.getValue());
            k->refreshText();
            if (display) display->setHighlight (b.getSpecs()[(size_t) i].name + "  " + b.formatValue (i, b.getBase (i)));
            host.getEngine().markDirty();
        };
        k->onHover = [this, &b, i] (bool over)
        {
            if (display) display->setHighlight (over ? b.getSpecs()[(size_t) i].name + "  " + b.formatValue (i, b.getBase (i)) : juce::String());
        };
        k->onRightClick = [this, &b, i, k]
        {
            showKnobMenu (host.getEngine(), b, b.id, i, k, [this, &b, i] { if (onLearnParam) onLearnParam (b, i); });
        };
        k->slider.setTooltip (b.getType() == NodeType::Nam && i == NamBlock::pGain
                                  ? juce::String ("Gain: drives the amp harder, like the gain knob on the real thing. "
                                                  "Rigsmith measured this capture when it loaded and trims the output, so mostly the drive changes, not the volume. "
                                                  "Double-click: 5 (as captured). Right-click: MIDI learn")
                                  : sp.name + ": drag up / down (Shift for fine). Double-click: default. "
                                    "Drop a MOD source here to modulate it. Right-click: MIDI learn, modulation");
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

void Inspector::buildMeters (Block& b)
{
    if (b.getType() == NodeType::Tuner) return;
    meters = std::make_unique<Meters>();
    auto& eng = host.getEngine();
    if (auto* nb = dynamic_cast<NamBlock*> (&b))
    {
        // NAM: the handles are its Input / Output trims (scene- and MIDI-learnable parameters)
        meters->in.getTrim = [nb] { return nb->getBase (NamBlock::pInput); };
        meters->in.setTrim = [nb, &eng] (float v) { nb->set (NamBlock::pInput, v); eng.markDirty(); };
        meters->out.getTrim = [nb] { return nb->getBase (NamBlock::pOutput); };
        meters->out.setTrim = [nb, &eng] (float v) { nb->set (NamBlock::pOutput, v); eng.markDirty(); };
        meters->in.onRightClick = [this, nb] { if (onLearnParam) onLearnParam (*nb, NamBlock::pInput); };
        meters->out.onRightClick = [this, nb] { if (onLearnParam) onLearnParam (*nb, NamBlock::pOutput); };
        meters->in.setTooltip ("Level into the amp (dBFS peak). Drag the handle to trim it: hotter or quieter pickups, or to calibrate a capture. "
                               "Gain is the knob for drive. Double-click: 0 dB. Right-click: MIDI learn");
    }
    else
    {
        auto* n = &b;
        meters->in.getTrim = [n] { return n->inTrimDb.load(); };
        meters->in.setTrim = [n, &eng] (float v) { n->inTrimDb = v; n->touched(); eng.markDirty(); };
        meters->out.getTrim = [n] { return n->outTrimDb.load(); };
        meters->out.setTrim = [n, &eng] (float v) { n->outTrimDb = v; eng.markDirty(); };
        meters->in.setTooltip ("Level into this block (dBFS peak). Drag the handle to trim it (how hard it's driven). Double-click: 0 dB");
    }
    meters->out.setTooltip ("Level out of this block (dBFS peak). Drag the handle to trim it. Double-click: 0 dB");
    addAndMakeVisible (*meters);
}

//==============================================================================
/** Choose which captures the Capture knob sweeps through, and in what order. The sweep is at the
    top (drag or use the arrows to reorder, untick to take one out); below, every other capture of
    the same gear, under the gear's name, ready to tick in. Names are shortened to the part that
    tells captures of one gear apart. Changes apply as you make them. */
class CaptureEditor : public juce::Component
{
public:
    CaptureEditor (Engine& e, Node& n, std::function<void()> changedFn)
        : engine (e), node (n), sweep (*dynamic_cast<Sweepable*> (&n)), onChanged (std::move (changedFn))
    {
        const auto list = sweep.getCaptureFiles();
        for (int i = 0; i < list.size(); ++i)
            inItems.push_back (describe (list[i], sweep.getCaptureToneType (i)));

        // the rest of each gear in the sweep, under its name
        juce::StringArray seen;
        for (auto& f : list)
            if (auto g = engine.captures.groupFor (f); g != nullptr && ! seen.contains (g->key))
            {
                seen.add (g->key);
                Gear section;
                section.title = g->title;
                for (int i = 0; i < g->size(); ++i)
                    if (! list.contains (g->files[i]))
                        section.items.push_back (describe (g->files[i], g->fileToneTypes[i]));
                if (! section.items.empty()) more.push_back (section);
            }

        for (auto* btn : { &allButton, &clearButton })
        {
            btn->setColour (juce::TextButton::buttonColourId, card);
            addAndMakeVisible (btn);
        }
        allButton.onClick = [this]
        {
            for (auto& g : more) for (auto& it : g.items) if ((int) inItems.size() < Sweepable::maxCaptures) inItems.push_back (it);
            for (auto& g : more) g.items.erase (std::remove_if (g.items.begin(), g.items.end(), [this] (const Item& it) { return contains (it.file); }), g.items.end());
            apply();
        };
        clearButton.onClick = [this]
        {
            // keep just the capture the knob is on
            const auto current = sweep.getCaptureFiles()[sweep.getNearestCapture()];
            for (auto& it : inItems) if (it.file != current) putBack (it);
            inItems.erase (std::remove_if (inItems.begin(), inItems.end(), [&] (const Item& it) { return it.file != current; }), inItems.end());
            apply();
        };
        allButton.setTooltip ("Every capture of these gear in the sweep");
        clearButton.setTooltip ("Only the capture the knob is on");
        rebuildRows();
        setSize (440, juce::jmin (580, headerH + (int) rows.size() * rowH + footerH));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel);
        g.setColour (text);
        g.setFont (font (14.0f, true));
        g.drawText ("Captures to sweep", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (font (11.0f));
        g.drawText ("Tick to add or remove. Drag the sweep to reorder; captures can come from different gear.",
                    16, 28, getWidth() - 32, 14, juce::Justification::centredLeft, true);

        const bool mixed = gearCount() > 1;
        g.saveState();
        g.reduceClipRegion (0, headerH, getWidth(), getHeight() - headerH - footerH);
        for (int r = firstRow; r < (int) rows.size(); ++r)
        {
            const auto area = rowArea (r);
            if (area.getY() >= getHeight() - footerH) break;
            const auto& row = rows[(size_t) r];
            if (row.kind == Row::Section || row.kind == Row::Heading)
            {
                const bool section = row.kind == Row::Section;
                g.setColour (section ? accent : text.withAlpha (0.85f));
                g.setFont (section ? font (10.5f, true).withExtraKerningFactor (0.08f) : font (12.0f, true));
                g.drawText (row.text, area.getX() + 16, area.getBottom() - 20, area.getWidth() - 32, 18, juce::Justification::centredLeft, true);
                if (section)
                {
                    g.setColour (border);
                    g.fillRect (16, area.getBottom() - 1, getWidth() - 32, 1);
                }
                continue;
            }

            const auto& it = item (row);
            const bool in = row.kind == Row::In;
            if (in && row.index == dragRow) { g.setColour (cardHi); g.fillRoundedRectangle (area.toFloat().reduced (8, 1), 5.0f); }

            const auto box = tickArea (r).toFloat();
            if (in)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (bg);
                juce::Path t;
                t.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
                t.lineTo (box.getX() + 7.5f, box.getBottom() - 4.5f);
                t.lineTo (box.getRight() - 4.0f, box.getY() + 4.5f);
                g.strokePath (t, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (muted);
                g.setFont (mono (10.0f));
                g.drawText (juce::String (row.index + 1), area.getX() + 38, area.getY(), 18, area.getHeight(), juce::Justification::centredRight);
            }
            else
            {
                g.setColour (borderHi);
                g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.3f);
            }

            const int nameX = area.getX() + (in ? 62 : 44);
            const int rightW = in ? 150 : 90;
            g.setColour (in ? text : text.withAlpha (0.75f));
            g.setFont (font (12.5f, in));
            g.drawText (it.shortName, nameX, area.getY(), area.getRight() - nameX - rightW, area.getHeight(), juce::Justification::centredLeft, true);

            // tone, and the gear when the sweep mixes gear
            juce::String side = it.tone;
            if (in && mixed) side = it.gear + (it.tone.isNotEmpty() ? "  -  " + it.tone : juce::String());
            g.setColour (muted);
            g.setFont (font (10.5f));
            g.drawText (side, area.getRight() - rightW, area.getY(), rightW - (in ? 64 : 16), area.getHeight(), juce::Justification::centredRight, true);

            if (in && inItems.size() > 1)
            {
                juce::Path up, down;
                const auto u = upArea (r).toFloat().reduced (6.0f, 8.0f), d = downArea (r).toFloat().reduced (6.0f, 8.0f);
                up.addTriangle (u.getX(), u.getBottom(), u.getRight(), u.getBottom(), u.getCentreX(), u.getY());
                down.addTriangle (d.getX(), d.getY(), d.getRight(), d.getY(), d.getCentreX(), d.getBottom());
                g.setColour (row.index > 0 ? muted : dim.withAlpha (0.35f));
                g.fillPath (up);
                g.setColour (row.index < (int) inItems.size() - 1 ? muted : dim.withAlpha (0.35f));
                g.fillPath (down);
            }
        }
        g.restoreState();

        if (maxFirstRow() > 0)
        {
            // a thin scroll indicator
            const float track = (float) (getHeight() - headerH - footerH);
            const float h = track * (float) visibleRows() / (float) rows.size();
            const float y = (float) headerH + (track - h) * (float) firstRow / (float) maxFirstRow();
            g.setColour (borderHi);
            g.fillRoundedRectangle ((float) getWidth() - 6.0f, y, 3.0f, h, 1.5f);
        }
        g.setColour (border);
        g.fillRect (0, getHeight() - footerH, getWidth(), 1);
        g.setColour (muted);
        g.setFont (font (11.0f));
        g.drawText (juce::String ((int) inItems.size()) + " in the sweep", getWidth() - 140, getHeight() - footerH, 124, footerH,
                    juce::Justification::centredRight);
    }

    void resized() override
    {
        auto f = getLocalBounds().removeFromBottom (footerH).reduced (16, 9);
        allButton.setBounds (f.removeFromLeft (120));
        f.removeFromLeft (8);
        clearButton.setBounds (f.removeFromLeft (120));
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        firstRow = juce::jlimit (0, maxFirstRow(), firstRow - (w.deltaY > 0 ? 2 : -2));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragRow = -1;
        const int r = rowAt (e.y);
        if (r < 0) return;
        const auto row = rows[(size_t) r];
        if (row.kind != Row::In && row.kind != Row::Out) return;

        if (row.kind == Row::Out)
        {
            // tick in: to the end of the sweep
            if ((int) inItems.size() >= Sweepable::maxCaptures) return;
            auto it = more[(size_t) row.group].items[(size_t) row.index];
            more[(size_t) row.group].items.erase (more[(size_t) row.group].items.begin() + row.index);
            inItems.push_back (it);
            apply();
            return;
        }
        if (tickArea (r).expanded (4).contains (e.getPosition()))
        {
            if (inItems.size() == 1) return;   // keep at least one
            putBack (inItems[(size_t) row.index]);
            inItems.erase (inItems.begin() + row.index);
            apply();
            return;
        }
        const int i = row.index;
        if (upArea (r).contains (e.getPosition()) && i > 0) { std::swap (inItems[(size_t) i], inItems[(size_t) i - 1]); apply(); return; }
        if (downArea (r).contains (e.getPosition()) && i + 1 < (int) inItems.size()) { std::swap (inItems[(size_t) i], inItems[(size_t) i + 1]); apply(); return; }
        dragRow = i;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragRow < 0) return;
        const int r = rowAt (juce::jlimit (headerH, getHeight() - footerH - 1, e.y));
        int target = dragRow;
        if (r >= 0 && rows[(size_t) r].kind == Row::In) target = rows[(size_t) r].index;
        else if (r >= 0 && r < firstInRow()) target = 0;
        else if (r >= 0) target = (int) inItems.size() - 1;
        if (target != dragRow)
        {
            auto it = inItems[(size_t) dragRow];
            inItems.erase (inItems.begin() + dragRow);
            inItems.insert (inItems.begin() + target, it);
            dragRow = target;
            moved = true;
            rebuildRows();
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
    struct Item { juce::File file; juce::String shortName, gear, tone; };
    struct Gear { juce::String title; std::vector<Item> items; };
    struct Row
    {
        enum Kind { Section, Heading, In, Out } kind;
        int group = -1, index = -1;
        juce::String text;
    };
    static constexpr int headerH = 50, rowH = 26, footerH = 46;

    Item describe (const juce::File& f, const juce::String& tone) const
    {
        Item it;
        it.file = f;
        it.tone = toneTypeName (tone);
        it.shortName = f.getFileNameWithoutExtension();
        if (auto g = engine.captures.groupFor (f))
        {
            it.gear = g->title;
            if (const int i = g->indexOf (f); i >= 0)
            {
                it.shortName = g->shortLabels[i];
                if (it.tone.isEmpty()) it.tone = toneTypeName (g->fileToneTypes[i]);
            }
        }
        return it;
    }

    bool contains (const juce::File& f) const
    {
        for (auto& it : inItems) if (it.file == f) return true;
        return false;
    }

    void putBack (const Item& it)
    {
        for (auto& g : more)
            if (g.title == it.gear) { g.items.insert (g.items.begin(), it); return; }
        Gear g;
        g.title = it.gear.isNotEmpty() ? it.gear : juce::String ("Other");
        g.items.push_back (it);
        more.push_back (g);
    }

    int gearCount() const
    {
        juce::StringArray gears;
        for (auto& it : inItems) gears.addIfNotAlreadyThere (it.gear);
        return gears.size();
    }

    void rebuildRows()
    {
        rows.clear();
        rows.push_back ({ Row::Section, -1, -1, "IN THE SWEEP" });
        for (int i = 0; i < (int) inItems.size(); ++i) rows.push_back ({ Row::In, -1, i, {} });
        bool any = false;
        for (auto& g : more) any |= ! g.items.empty();
        if (any)
        {
            rows.push_back ({ Row::Section, -1, -1, "MORE CAPTURES" });
            for (int gi = 0; gi < (int) more.size(); ++gi)
            {
                if (more[(size_t) gi].items.empty()) continue;
                rows.push_back ({ Row::Heading, gi, -1, more[(size_t) gi].title });
                for (int i = 0; i < (int) more[(size_t) gi].items.size(); ++i) rows.push_back ({ Row::Out, gi, i, {} });
            }
        }
        firstRow = juce::jlimit (0, maxFirstRow(), firstRow);
    }

    const Item& item (const Row& r) const
    {
        return r.kind == Row::In ? inItems[(size_t) r.index] : more[(size_t) r.group].items[(size_t) r.index];
    }

    int visibleRows() const { return juce::jmax (1, (getHeight() - headerH - footerH) / rowH); }
    int maxFirstRow() const { return juce::jmax (0, (int) rows.size() - visibleRows()); }
    int firstInRow() const { for (int r = 0; r < (int) rows.size(); ++r) if (rows[(size_t) r].kind == Row::In) return r; return 0; }
    juce::Rectangle<int> rowArea (int r) const { return { 0, headerH + (r - firstRow) * rowH, getWidth(), rowH }; }
    juce::Rectangle<int> tickArea (int r) const { auto a = rowArea (r); return { 18, a.getCentreY() - 8, 16, 16 }; }
    juce::Rectangle<int> upArea (int r) const { auto a = rowArea (r); return { getWidth() - 58, a.getY(), 20, a.getHeight() }; }
    juce::Rectangle<int> downArea (int r) const { auto a = rowArea (r); return { getWidth() - 36, a.getY(), 20, a.getHeight() }; }
    int rowAt (int y) const
    {
        if (y < headerH || y >= getHeight() - footerH) return -1;
        const int r = firstRow + (y - headerH) / rowH;
        return r < (int) rows.size() ? r : -1;
    }

    void apply()
    {
        // keep the knob on the capture it was on, wherever that ends up
        const auto files = sweep.getCaptureFiles();
        const auto current = files.isEmpty() ? juce::File() : files[sweep.getNearestCapture()];
        juce::Array<juce::File> list;
        for (auto& it : inItems) list.add (it.file);
        const int at = juce::jmax (0, list.indexOf (current));
        juce::String err;
        sweep.loadCaptures (list, (float) at, err);
        engine.markDirty();
        rebuildRows();
        repaint();
        if (onChanged) onChanged();
    }

    Engine& engine;
    Node& node;
    Sweepable& sweep;
    std::function<void()> onChanged;
    std::vector<Item> inItems;
    std::vector<Gear> more;
    std::vector<Row> rows;
    juce::TextButton allButton { "Add them all" }, clearButton { "Just this one" };
    int firstRow = 0, dragRow = -1;
    bool moved = false;
};

//==============================================================================
void Inspector::buildCaptureKnob (Block& b)
{
    // NAM and IR blocks: a sweep through the block's captures, blending between neighbours
    auto* sw = dynamic_cast<Sweepable*> (&b);
    if (sw == nullptr) return;
    namCaptureFiles = sw->getCaptureFiles();
    const int n = namCaptureFiles.size();
    if (n == 0) return;
    auto group = host.getEngine().captures.groupFor (namCaptureFiles[0]);
    const bool more = group != nullptr && group->size() > 1;

    editCaptures.setButtonText (n < 2 ? "Sweep captures..." : "Edit captures...");
    editCaptures.setTooltip ("Choose which captures the Capture knob sweeps through, and in what order. "
                             "Drop more captures (from any gear) on the knob to add them");
    editCaptures.onClick = [this] { showCaptureEditor(); };
    if (n >= 2 || more) controls.addAndMakeVisible (editCaptures);
    if (n < 2) return;

    namCaptureLabels = host.getEngine().captures.shortNames (namCaptureFiles, sw->getCaptureNames());
    const auto colour = gearColour (sw->getSweepGear());
    auto dk = std::make_unique<DropKnob> ("Capture", colour);
    dk->onDrop = [this] (const juce::String& description)
    {
        if (node != nullptr) host.dropCaptures (node, nullptr, -1, CaptureDrop::fromDescription (description), GraphHost::DropMode::Add);
    };
    dk->onFilesDrop = [this] (const juce::StringArray& paths)
    {
        if (node != nullptr) host.dropCaptures (node, nullptr, -1, CaptureDrop::fromPaths (paths), GraphHost::DropMode::Add);
    };
    captureKnob = std::move (dk);
    auto* k = captureKnob.get();
    k->nameLabel.setFont (font (12.0f, true));
    k->slider.setRange (0.0, (double) (n - 1), 0.0);
    k->slider.setMouseDragSensitivity (juce::jlimit (160, 420, n * 60));
    k->slider.setDoubleClickReturnValue (false, 0);
    k->slider.setValue (sw->getCapturePosition(), juce::dontSendNotification);
    k->detents = n <= 48 ? n : 0;
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
        if (auto* s = dynamic_cast<Sweepable*> (node))
        {
            s->setCapturePosition ((float) k->slider.getValue());
            host.getEngine().markDirty();
        }
        k->refreshText();
        updateCaptureText();
    };
    k->onRightClick = [this] { showCaptureMenu(); };
    if (auto* blk = dynamic_cast<Block*> (node))
        k->setModTarget (host.getEngine(), *blk, blk->id, sw->getCaptureParam());
    k->slider.setTooltip ("Sweeps through the captures, morphing smoothly from one to the next. Drop captures (from any gear) here to add them. "
                          "Right-click to jump to one, MIDI-learn it (an expression pedal works well) or edit the list");
    k->refreshText();
    controls.addAndMakeVisible (k);

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
    if (auto* sw = dynamic_cast<Sweepable*> (node))
    {
        const int n = sw->getNumCaptures();
        const float pos = sw->getCapturePosition();
        const int a = juce::jlimit (0, n - 1, (int) std::floor (pos));
        const float f = pos - (float) a;
        juce::String line = toneTypeName (sw->getCaptureToneType (sw->getNearestCapture()));
        if (f >= 0.02f && f <= 0.98f && a + 1 < n)
            line = juce::String (juce::roundToInt ((1.0f - f) * 100.0f)) + "% / " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
        captureKnob->nameLabel.setText ("CAPTURE  " + juce::String (sw->getNearestCapture() + 1) + " / " + juce::String (n),
                                        juce::dontSendNotification);
        captureName.setText (line, juce::dontSendNotification);
    }
    captureKnob->repaint();
}

void Inspector::showCaptureEditor()
{
    if (dynamic_cast<Sweepable*> (node) == nullptr) return;
    auto ed = std::make_unique<CaptureEditor> (host.getEngine(), *node, [sp = juce::Component::SafePointer<Inspector> (this)]
    {
        if (sp != nullptr) sp->needsRebuild = true;
    });
    auto* anchor = captureKnob != nullptr ? static_cast<juce::Component*> (captureKnob.get()) : &editCaptures;
    juce::CallOutBox::launchAsynchronously (std::move (ed), anchor->getScreenBounds(), nullptr);
}

void Inspector::showCaptureMenu()
{
    auto* sw = dynamic_cast<Sweepable*> (node);
    if (sw == nullptr) return;
    juce::PopupMenu m;
    const auto names = sw->getCaptureNames();
    m.addSectionHeader (node->getName());
    const int nearest = sw->getNearestCapture();
    for (int i = 0; i < names.size(); ++i)
    {
        const auto tone = toneTypeName (sw->getCaptureToneType (i));
        m.addItem (i + 1, names[i] + (tone.isNotEmpty() ? "   (" + tone + ")" : juce::String()), true, i == nearest);
    }
    m.addSeparator();
    m.addItem (1001, "Edit captures...");
    m.addItem (1002, "MIDI learn (expression pedal)");
    m.addItem (1003, "Modulate (an envelope morphing clean to crunch as you dig in...)");
    juce::Component::SafePointer<Inspector> sp (this);
    auto* block = node;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (captureKnob.get()), [sp, block] (int r)
    {
        if (sp == nullptr || sp->node != block) return;
        if (r == 1001) { sp->showCaptureEditor(); return; }
        if (r == 1003)
        {
            if (auto* s = dynamic_cast<Sweepable*> (block))
                if (auto* blk = dynamic_cast<Block*> (block))
                    showKnobMenu (sp->host.getEngine(), *blk, blk->id, s->getCaptureParam(), sp->captureKnob.get(),
                                  [sp, block, p = s->getCaptureParam()] { if (sp != nullptr && sp->onLearnParam) sp->onLearnParam (*block, p); });
            return;
        }
        if (r == 1002)
        {
            if (auto* s = dynamic_cast<Sweepable*> (block); s != nullptr && sp->onLearnParam) sp->onLearnParam (*block, s->getCaptureParam());
            return;
        }
        if (r > 0 && sp->captureKnob != nullptr) sp->captureKnob->slider.setValue (r - 1, juce::sendNotificationSync);
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
    if (auto* blk = dynamic_cast<Block*> (node); blk != nullptr && blk->panelOrder() != builtOrder)
    {
        needsRebuild = true;   // e.g. the gate switched to Advanced: different controls
        return;
    }
    if (auto* sw = dynamic_cast<Sweepable*> (node))
    {
        // the sweep changed somewhere else (the card's knob, MIDI, a scene, the dice, a drop...)
        if (sw->getCaptureFiles() != namCaptureFiles) { needsRebuild = true; return; }
        // where the knob is set (modulation moves the sound, not the knob)
        float basePos = sw->getCapturePosition();
        if (auto* blk = dynamic_cast<Block*> (node); blk != nullptr && sw->getNumCaptures() > 1)
            basePos = blk->getBase (sw->getCaptureParam()) * (float) (sw->getNumCaptures() - 1);
        if (captureKnob != nullptr && ! captureKnob->slider.isMouseButtonDown()
            && std::abs (captureKnob->slider.getValue() - basePos) > 1.0e-4)
        {
            captureKnob->slider.setValue (basePos, juce::dontSendNotification);
            captureKnob->refreshText();
            updateCaptureText();
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
                knobs[(int) k]->slider.setValue (b->getBase (knobParam[k]), juce::dontSendNotification);
                knobs[(int) k]->refreshText();
            }
        for (size_t t = 0; t < toggleParam.size(); ++t)
            if (toggleParam[t] >= 0)
                toggles[(int) t]->setToggleState (b->getBase (toggleParam[t]) > 0.5f, juce::dontSendNotification);
        for (size_t c = 0; c < choiceParam.size(); ++c)
            choices[(int) c]->setSelected (juce::roundToInt (b->getBase (choiceParam[c])));
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
        // read left to right, then the next row; smaller knobs when the space is short
        int r = juce::jmax (1, (availH + 6) / (96 + 6));
        if (ks.size() <= 10) r = juce::jmin (r, 2);   // two rows read best for a handful of knobs
        const int kH = juce::jlimit (92, 128, (availH - (r - 1) * 6) / r);
        const int kW = kH < 104 ? 74 : kw;
        const int cols = juce::jmax (1, (ks.size() + r - 1) / r);
        for (int i = 0; i < ks.size(); ++i)
            ks[i]->setBounds (startX + (i % cols) * (kW + 4), startY + (i / cols) * (kH + 6), kW, kH);
        return startX + (ks.isEmpty() ? 0 : cols * (kW + 4));
    };
    juce::ignoreUnused (kh);

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

    int knobTop = 0, topRight = 0;
    if (! choices.isEmpty())
    {
        // selectors (Type, Mode...) side by side along the top, the knobs underneath
        const int avail = juce::jmax (400, controlsView.getWidth() - 10);
        int cx = 0, cy = 0;
        for (auto* c : choices)
        {
            const int w = c->preferredWidth();
            if (cx > 0 && cx + w > avail) { cx = 0; cy += 58; }
            c->setBounds (cx, cy, w, 52);
            cx += w + 20;
            topRight = juce::jmax (topRight, cx);
        }
        knobTop = cy + 60;
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

    x = placeGrid (knobs, x, knobTop, h - knobTop);

    if (! toggles.isEmpty())
    {
        int y = knobTop + 22, tw = 150;
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

    controls.setSize (juce::jmax (juce::jmax (x, topRight), 10), h);
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
    const int meterW = meters != nullptr ? 84 : 0;
    auto left = r.removeFromLeft (node == nullptr || isSplit ? 300 : juce::jlimit (330, 600, (int) ((float) getWidth() * 0.36f)) + meterW);
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
        if (meters != nullptr)
        {
            // IN / OUT beside the display, the same height
            meters->setBounds (left.removeFromRight (76));
            left.removeFromRight (8);
        }
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
