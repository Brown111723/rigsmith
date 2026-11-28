#include "EffectPanel.h"
#include "Inspector.h"
#include "../Engine/Engine.h"
#include "../Engine/FactoryPresets.h"

namespace rig
{
using namespace theme;

namespace
{
bool pitchLike (const juce::String& id)
{
    const auto l = id.toLowerCase();
    for (auto* w : { "semi", "pitch", "tune", "fine", "interval", "octave", "transpose", "key", "scale", "harmony", "shift", "voice1", "voice2" })
        if (l.contains (w)) return true;
    return false;
}

juce::Colour lit (juce::Colour c) { return c.withMultipliedSaturation (1.05f).brighter (0.15f); }
}

void rollKeepingPitch (Block& b, juce::Random& rng, float amount)
{
    std::vector<std::pair<int, float>> keep;
    for (int i = 0; i < b.numParams(); ++i)
        if (pitchLike (b.getSpecs()[(size_t) i].id) || i == b.getModelParam()) keep.push_back ({ i, b.getBase (i) });
    b.randomise (rng, amount);
    for (auto [i, v] : keep) b.set (i, v);
    b.presetName.clear();
    b.touched();
}

//==============================================================================
/** A fader on a card: its name, its value, a track you drag (double-click: back to its default). */
class EffectPanel::Fader : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    juce::String name;
    juce::Colour colour;
    bool horizontal = false;
    std::function<float()> getP, getLiveP;
    std::function<void (float)> setP;
    std::function<juce::String()> valueText;
    std::function<void()> begin, end, reset, rightClick;

    juce::Rectangle<float> track() const
    {
        auto r = getLocalBounds().toFloat();
        if (horizontal) return r.withTrimmedLeft (r.getWidth() * 0.36f).withTrimmedRight (52.0f).withSizeKeepingCentre (r.getWidth() * 0.64f - 52.0f, 6.0f);
        r = r.reduced (6.0f, 0.0f).withTrimmedTop (40.0f).withTrimmedBottom (10.0f);
        return r.withSizeKeepingCentre (6.0f, r.getHeight());
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool over = isMouseOverOrDragging();
        g.setColour (over ? cardHi : card);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const float p = juce::jlimit (0.0f, 1.0f, getP ? getP() : 0.0f);
        auto t = track();
        if (horizontal)
        {
            g.setColour (muted);
            g.setFont (font (10.0f, true));
            g.drawText (name.toUpperCase(), r.withWidth (r.getWidth() * 0.36f).reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
            g.setColour (theme::text);
            g.setFont (mono (10.5f));
            g.drawText (valueText ? valueText() : juce::String(), r.removeFromRight (50.0f).reduced (2.0f, 0.0f), juce::Justification::centredRight, true);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (t, 3.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (t.withWidth (t.getWidth() * p), 3.0f);
            g.setColour (theme::text);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ t.getX() + t.getWidth() * p, t.getCentreY() }));
            return;
        }
        // name and value on top
        g.setColour (muted);
        g.setFont (font (9.5f, true));
        g.drawText (name.toUpperCase(), r.withHeight (18.0f).withTrimmedTop (4.0f), juce::Justification::centred, true);
        g.setColour (theme::text);
        g.setFont (mono (11.0f));
        g.drawText (valueText ? valueText() : juce::String(), r.withTrimmedTop (18.0f).withHeight (16.0f), juce::Justification::centred, true);
        // the scale
        g.setColour (border.brighter (0.2f));
        for (int k = 0; k <= 10; ++k)
        {
            const float y = t.getBottom() - t.getHeight() * (float) k / 10.0f;
            const float w = k % 5 == 0 ? 7.0f : 4.0f;
            g.drawHorizontalLine ((int) y, t.getX() - 5.0f - w, t.getX() - 5.0f);
            g.drawHorizontalLine ((int) y, t.getRight() + 5.0f, t.getRight() + 5.0f + w);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (t, 3.0f);
        const float hy = t.getBottom() - t.getHeight() * p;
        juce::ColourGradient fill (lit (colour), t.getX(), hy, colour.withAlpha (0.55f), t.getX(), t.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (t.withTop (hy), 3.0f);
        if (getLiveP)
        {
            const float lp = juce::jlimit (0.0f, 1.0f, getLiveP());
            if (std::abs (lp - p) > 0.01f)
            {
                g.setColour (theme::text.withAlpha (0.8f));
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ t.getRight() + 9.0f, t.getBottom() - t.getHeight() * lp }));
            }
        }
        // the handle
        auto h = juce::Rectangle<float> (juce::jmin (28.0f, r.getWidth() - 14.0f), 12.0f).withCentre ({ t.getCentreX(), hy });
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (h.translated (0.0f, 1.5f), 3.0f);
        juce::ColourGradient cap (juce::Colour (0xff4a4a52), h.getX(), h.getY(), juce::Colour (0xff2a2a30), h.getX(), h.getBottom(), false);
        g.setGradientFill (cap);
        g.fillRoundedRectangle (h, 3.0f);
        g.setColour (over ? theme::text : muted);
        g.drawHorizontalLine ((int) h.getCentreY() - 2, h.getX() + 6.0f, h.getRight() - 6.0f);
        g.drawHorizontalLine ((int) h.getCentreY() + 1, h.getX() + 6.0f, h.getRight() - 6.0f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (rightClick) rightClick(); return; }
        if (begin) begin();
        startP = getP ? getP() : 0.0f;
        // a click on the track jumps there
        auto t = track();
        const bool onHandle = horizontal ? std::abs (e.position.x - (t.getX() + t.getWidth() * startP)) < 10.0f
                                         : std::abs (e.position.y - (t.getBottom() - t.getHeight() * startP)) < 10.0f;
        if (! onHandle && t.expanded (14.0f, 4.0f).contains (e.position))
        {
            startP = horizontal ? (e.position.x - t.getX()) / t.getWidth() : (t.getBottom() - e.position.y) / t.getHeight();
            if (setP) setP (juce::jlimit (0.0f, 1.0f, startP));
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! setP) return;
        auto t = track();
        const float scale = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float d = horizontal ? (float) e.getDistanceFromDragStartX() / t.getWidth() : -(float) e.getDistanceFromDragStartY() / t.getHeight();
        setP (juce::jlimit (0.0f, 1.0f, startP + d * scale));
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu() && end) end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (begin) begin(); if (reset) reset(); if (end) end(); repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (! setP || ! getP) return;
        if (begin) begin();
        setP (juce::jlimit (0.0f, 1.0f, getP() + (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * 0.08f));
        if (end) end();
        repaint();
    }

private:
    float startP = 0.0f;
};

//==============================================================================
/** Two controls on one pad: left / right is one, down / up the other. */
class EffectPanel::XYPad : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    juce::String xName, yName;
    juce::Colour colour;
    std::function<float()> getX, getY;
    std::function<void (float, float)> set;
    std::function<juce::String()> valueText;
    std::function<void()> begin, end;

    juce::Rectangle<float> inner() const { return getLocalBounds().toFloat().reduced (8.0f).withTrimmedTop (16.0f).withTrimmedLeft (10.0f).withTrimmedBottom (12.0f); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (card);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (muted);
        g.setFont (font (9.5f, true));
        g.drawText ((xName + " / " + yName).toUpperCase(), r.withHeight (18.0f).withTrimmedTop (3.0f), juce::Justification::centred, true);
        auto in = inner();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (in, 4.0f);
        g.setColour (border);
        g.drawRoundedRectangle (in, 4.0f, 1.0f);
        g.setColour (border.brighter (0.15f).withAlpha (0.7f));
        g.drawHorizontalLine ((int) in.getCentreY(), in.getX() + 2, in.getRight() - 2);
        g.drawVerticalLine ((int) in.getCentreX(), in.getY() + 2, in.getBottom() - 2);
        const float px = in.getX() + in.getWidth() * juce::jlimit (0.0f, 1.0f, getX ? getX() : 0.5f);
        const float py = in.getBottom() - in.getHeight() * juce::jlimit (0.0f, 1.0f, getY ? getY() : 0.5f);
        g.setColour (colour.withAlpha (0.18f));
        g.drawHorizontalLine ((int) py, in.getX() + 2, in.getRight() - 2);
        g.drawVerticalLine ((int) px, in.getY() + 2, in.getBottom() - 2);
        for (float k : { 26.0f, 16.0f })
        {
            g.setColour (colour.withAlpha (k > 20.0f ? 0.12f : 0.28f));
            g.fillEllipse (juce::Rectangle<float> (k, k).withCentre ({ px, py }));
        }
        g.setColour (lit (colour));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ px, py }));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ px, py }));
        g.setColour (dim);
        g.setFont (font (9.0f));
        g.drawText (xName, in.withY (in.getBottom() + 1.0f).withHeight (11.0f), juce::Justification::centred, true);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, in.getX() - 6.0f, in.getCentreY()));
            g.drawText (yName, juce::Rectangle<float> (in.getX() - 6.0f - in.getHeight() * 0.5f, in.getCentreY() - 6.0f, in.getHeight(), 11.0f), juce::Justification::centred, true);
        }
        if (isMouseButtonDown() && valueText)
        {
            g.setColour (theme::text.withAlpha (0.9f));
            g.setFont (mono (10.0f));
            g.drawText (valueText(), in.reduced (6.0f, 4.0f), juce::Justification::topRight, true);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (begin) begin(); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto in = inner();
        if (set) set (juce::jlimit (0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth()), juce::jlimit (0.0f, 1.0f, (in.getBottom() - e.position.y) / in.getHeight()));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (end) end(); repaint(); }
};

//==============================================================================
EffectPanel::EffectPanel (Block& b, Host h, std::unique_ptr<FxDisplay> d)
    : block (b), host (std::move (h)), colour (categoryColour (b.getCategory())), display (std::move (d))
{
    if (display != nullptr)
    {
        display->onEdited = [this] { block.presetName.clear(); done(); };
        addAndMakeVisible (*display);
    }
    inMeter = std::make_unique<TrimMeter> ("IN", -24.0f, 24.0f);
    outMeter = std::make_unique<TrimMeter> ("OUT", -24.0f, 24.0f);
    inMeter->getTrim = [this] { return block.inTrimDb.load(); };
    inMeter->setTrim = [this] (float db) { block.inTrimDb = db; done(); };
    outMeter->getTrim = [this] { return block.outTrimDb.load(); };
    outMeter->setTrim = [this] (float db) { block.outTrimDb = db; done(); };
    inMeter->setTooltip ("Level in: drag the handle to trim it (double-click: 0 dB)");
    outMeter->setTooltip ("Level out: drag the handle to trim it (double-click: 0 dB)");
    addAndMakeVisible (*inMeter);
    addAndMakeVisible (*outMeter);

    diceBtn.onClick = [this]
    {
        if (host.isLocked ? host.isLocked() : block.locked.load()) return;
        begin();
        if (host.roll) host.roll();
        else { juce::Random rng; rollKeepingPitch (block, rng); }
        done();
        build();
    };
    lockBtn.setClickingTogglesState (false);
    lockBtn.onClick = [this]
    {
        const bool now = ! (host.isLocked ? host.isLocked() : block.locked.load());
        if (host.setLocked) host.setLocked (now); else block.locked = now;
        if (host.edited) host.edited();
        timerCallback();
    };
    closeBtn.onClick = [this] { if (host.onRemove) juce::MessageManager::callAsync ([cb = host.onRemove] { cb(); }); };
    addAndMakeVisible (diceBtn);
    addAndMakeVisible (lockBtn);
    addChildComponent (closeBtn);
    closeBtn.setVisible (host.onRemove != nullptr);

    presets.setTooltip ("Presets: starting points" + juce::String (host.engine != nullptr ? ", yours, and Save" : ""));
    presets.onChange = [this]
    {
        if (syncing) return;
        const int r = presets.getSelectedId();
        const auto& f = factoryPresets (block.getType());
        if (r == 1)
        {
            begin();
            for (int i = 0; i < block.numParams(); ++i)
                if (block.getSpecs()[(size_t) i].id != "capture") block.set (i, block.getSpecs()[(size_t) i].def);
            block.presetName.clear();
            block.mixPct = 100.0f;
            done();
        }
        else if (r >= 100 && r - 100 < (int) f.size())
        {
            begin();
            applyFactoryPreset (block, f[(size_t) (r - 100)]);
            block.presetName = f[(size_t) (r - 100)].name;
            done();
        }
        else if (r >= 1000 && host.engine != nullptr && r - 1000 < userFiles.size())
        {
            const auto file = userFiles[r - 1000];
            const auto id = block.id;
            auto* eng = host.engine;
            juce::MessageManager::callAsync ([eng, id, file] { if (auto* n = eng->loadBlockPreset (id, file)) n->presetName = file.getFileNameWithoutExtension(); });
            return;
        }
        else if (r == 2 && host.engine != nullptr)
        {
            auto* w = new juce::AlertWindow ("Save preset", "Name this " + block.getName() + " setting", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", block.presetName, "Name");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<EffectPanel> safe (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int res)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                if (safe == nullptr || res != 1 || name.isEmpty()) return;
                if (safe->host.engine->saveBlockPreset (safe->block, name)) { safe->block.presetName = name; safe->buildPresets(); }
            }), true);
        }
        buildPresets();
        build();
    };
    addAndMakeVisible (presets);

    model.setTooltip ("Model: the kind of unit it behaves like");
    model.onChange = [this]
    {
        if (syncing || modelParam < 0) return;
        begin();
        block.set (modelParam, (float) (model.getSelectedId() - 1));
        block.onChoiceChanged (modelParam);
        if (block.hasModels()) block.applyModelStock();
        block.presetName.clear();
        done();
        build();
    };
    addChildComponent (model);

    build();
    startTimerHz (20);
}

EffectPanel::~EffectPanel()
{
    stopTimer();
}

bool EffectPanel::on() const { return host.isOn ? host.isOn() : ! block.bypassed.load(); }
void EffectPanel::begin() { if (host.beginEdit) host.beginEdit(); }
void EffectPanel::done()
{
    block.touched();
    if (host.edited) host.edited();
    repaint();
}

juce::String EffectPanel::signature() const
{
    juce::String s;
    for (int i : block.panelOrder()) s << block.getParamName (i) << ";";
    s << "|" << block.numParams() << "|" << (modelParam >= 0 ? block.getChoice (modelParam) : -1);
    return s;
}

juce::String EffectPanel::displayTitle() const
{
    switch (block.getCategory())
    {
        case Category::Dynamics:   return "Transfer curve";
        case Category::Drive:      return "Saturation curve";
        case Category::Modulation: return "Modulation wave";
        case Category::Delay:      return "Echo timeline";
        case Category::Reverb:     return "Decay";
        case Category::Filter:
        case Category::Eq:         return "Response";
        default:                   return "Live view";
    }
}

juce::String EffectPanel::controlsTitle() const
{
    switch (block.getCategory())
    {
        case Category::Dynamics:   return "Dynamics";
        case Category::Drive:      return "Drive & tone";
        case Category::Modulation: return "Wave & character";
        case Category::Delay:      return "Echo & feedback";
        case Category::Reverb:     return "Space & decay";
        case Category::Filter:
        case Category::Eq:         return "Tone & character";
        default:                   return "Controls";
    }
}

void EffectPanel::buildPresets()
{
    const juce::ScopedValueSetter<bool> sv (syncing, true);
    presets.clear (juce::dontSendNotification);
    presets.addItem ("Init (defaults)", 1);
    factoryIds.clear();
    const auto& f = factoryPresets (block.getType());
    if (! f.empty())
    {
        presets.addSectionHeading ("Starting points");
        for (size_t i = 0; i < f.size(); ++i) presets.addItem (f[i].name, 100 + (int) i);
    }
    userFiles.clear();
    if (host.engine != nullptr)
    {
        userFiles = host.engine->getBlockPresets (block);
        presets.addSectionHeading ("Yours");
        for (int i = 0; i < userFiles.size(); ++i) presets.addItem (userFiles[i].getFileNameWithoutExtension(), 1000 + i);
        presets.addSeparator();
        presets.addItem ("Save preset...", 2);
    }
    presets.setText (block.presetName.isNotEmpty() ? block.presetName : juce::String ("Default"), juce::dontSendNotification);
}

void EffectPanel::build()
{
    built = signature();
    faders.clear();
    pad.reset();
    switches.clear();
    switchLabels.clear();

    modelParam = block.getModelParam() >= 0 ? block.getModelParam() : block.indexOf ("model");
    if (modelParam >= 0 && ! block.getSpecs()[(size_t) modelParam].isChoice()) modelParam = -1;
    {
        const juce::ScopedValueSetter<bool> sv (syncing, true);
        model.clear (juce::dontSendNotification);
        if (modelParam >= 0)
        {
            const auto names = block.getChoiceNames (modelParam);
            for (int i = 0; i < names.size(); ++i) model.addItem (names[i], i + 1);
            model.setSelectedId (block.getChoice (modelParam) + 1, juce::dontSendNotification);
        }
        model.setVisible (modelParam >= 0);
    }
    buildPresets();

    // what to show: the panel's own order (a model hides what the real unit doesn't have)
    std::vector<int> order = block.panelOrder();
    if (order.empty()) for (int i = 0; i < block.numParams(); ++i) order.push_back (i);
    std::vector<int> cont, choice, toggle;
    int mixParam = -1;
    for (int i : order)
    {
        if (i == modelParam || ! juce::isPositiveAndBelow (i, block.numParams())) continue;
        const auto& s = block.getSpecs()[(size_t) i];
        if (s.id == "capture") continue;
        if (s.id.equalsIgnoreCase ("mix") && ! s.isChoice() && ! s.toggle) { mixParam = i; continue; }
        if (s.toggle) toggle.push_back (i);
        else if (s.isChoice()) choice.push_back (i);
        else cont.push_back (i);
    }

    auto paramFader = [this] (int i)
    {
        auto* f = faders.add (new Fader());
        f->name = block.getParamName (i);
        f->colour = colour;
        const auto& s = block.getSpecs()[(size_t) i];
        f->getP = [this, i] { return block.getSpecs()[(size_t) i].toProportion (block.getBase (i)); };
        f->getLiveP = [this, i] { return block.getSpecs()[(size_t) i].toProportion (block.get (i)); };
        f->setP = [this, i] (float p) { block.set (i, block.getSpecs()[(size_t) i].fromProportion (p)); block.presetName.clear(); done(); };
        f->valueText = [this, i] { return block.formatValue (i); };
        f->begin = [this] { begin(); };
        f->end = [] {};
        f->reset = [this, i] { block.set (i, block.getSpecs()[(size_t) i].def); done(); };
        f->rightClick = [this, i, f] { if (host.paramMenu) host.paramMenu (i, f); };
        f->setTooltip (s.name + ": drag (Shift: fine), double-click for its default" + juce::String (host.paramMenu ? ", right-click to learn or modulate" : ""));
        addAndMakeVisible (f);
        return f;
    };

    // Mix: always first, the block's own or the panel's dry / wet
    if (mixParam >= 0) paramFader (mixParam)->name = "Mix";
    else
    {
        auto* f = faders.add (new Fader());
        f->name = "Mix";
        f->colour = colour;
        f->getP = [this] { return block.mixPct.load() * 0.01f; };
        f->setP = [this] (float p) { block.mixPct = p * 100.0f; done(); };
        f->valueText = [this] { return juce::String (juce::roundToInt (block.mixPct.load())) + " %"; };
        f->begin = [this] { begin(); };
        f->reset = [this] { block.mixPct = 100.0f; done(); };
        f->setTooltip ("Mix: the dry sound against the effect (double-click: 100 %)");
        addAndMakeVisible (f);
    }

    // the pair that belongs on a pad
    static const std::vector<std::pair<const char*, const char*>> pairs {
        { "rate", "depth" }, { "speed", "depth" }, { "time", "feedback" }, { "size", "decay" }, { "decay", "damping" }, { "decay", "tone" },
        { "cutoff", "res" }, { "cutoff", "resonance" }, { "freq", "q" }, { "centre", "feedback" }, { "manual", "feedback" },
        { "threshold", "ratio" }, { "drive", "tone" }, { "gain", "tone" }, { "fuzz", "tone" }, { "low", "high" }, { "attack", "release" }
    };
    int px = -1, py = -1;
    if (cont.size() >= 3)
        for (auto& [a, b] : pairs)
        {
            int ia = -1, ib = -1;
            for (int i : cont) { const auto& id = block.getSpecs()[(size_t) i].id; if (id.equalsIgnoreCase (a)) ia = i; if (id.equalsIgnoreCase (b)) ib = i; }
            if (ia >= 0 && ib >= 0) { px = ia; py = ib; break; }
        }
    for (int i : cont)
        if (i != px && i != py) paramFader (i);
    if (px >= 0)
    {
        pad = std::make_unique<XYPad>();
        pad->xName = block.getParamName (px);
        pad->yName = block.getParamName (py);
        pad->colour = colour;
        pad->getX = [this, px] { return block.getSpecs()[(size_t) px].toProportion (block.getBase (px)); };
        pad->getY = [this, py] { return block.getSpecs()[(size_t) py].toProportion (block.getBase (py)); };
        pad->set = [this, px, py] (float x, float y)
        {
            block.set (px, block.getSpecs()[(size_t) px].fromProportion (x));
            block.set (py, block.getSpecs()[(size_t) py].fromProportion (y));
            block.presetName.clear();
            done();
        };
        pad->valueText = [this, px, py] { return block.formatValue (px) + "  /  " + block.formatValue (py); };
        pad->begin = [this] { begin(); };
        pad->setTooltip ("Drag: " + pad->xName + " left / right, " + pad->yName + " down / up");
        addAndMakeVisible (*pad);
    }

    // switches: a few options as buttons, many as a drop-down, on / off as a lit button
    for (int i : choice)
    {
        const auto names = block.getChoiceNames (i);
        auto* l = switchLabels.add (new juce::Label ({}, block.getParamName (i).toUpperCase()));
        l->setFont (font (9.5f, true));
        l->setColour (juce::Label::textColourId, muted);
        addAndMakeVisible (l);
        int longest = 0;
        for (auto& n : names) longest = juce::jmax (longest, n.length());
        if (names.size() <= 4 && longest <= 10)
        {
            auto* row = switches.add (new juce::Component());
            row->getProperties().set ("param", i);
            row->getProperties().set ("label", switchLabels.size() - 1);
            for (int k = 0; k < names.size(); ++k)
            {
                auto* b = new juce::TextButton (names[k]);
                b->setClickingTogglesState (false);
                b->setToggleState (block.getChoice (i) == k, juce::dontSendNotification);
                b->setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.85f));
                b->onClick = [this, i, k] { begin(); block.set (i, (float) k); block.onChoiceChanged (i); block.presetName.clear(); done(); build(); };
                row->addAndMakeVisible (b);
                b->setConnectedEdges ((k > 0 ? juce::Button::ConnectedOnLeft : 0) | (k < names.size() - 1 ? juce::Button::ConnectedOnRight : 0));
            }
            row->setSize (names.size() * 64, 24);
            addAndMakeVisible (row);
        }
        else
        {
            auto* c = new juce::ComboBox();
            c->getProperties().set ("param", i);
            c->getProperties().set ("label", switchLabels.size() - 1);
            for (int k = 0; k < names.size(); ++k) c->addItem (names[k], k + 1);
            c->setSelectedId (block.getChoice (i) + 1, juce::dontSendNotification);
            c->onChange = [this, i, c] { begin(); block.set (i, (float) (c->getSelectedId() - 1)); block.onChoiceChanged (i); block.presetName.clear(); done(); juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<EffectPanel> (this)] { if (safe != nullptr) safe->build(); }); };
            c->setSize (150, 24);
            switches.add (c);
            addAndMakeVisible (c);
        }
    }
    for (int i : toggle)
    {
        auto* b = new juce::TextButton (block.getParamName (i));
        b->getProperties().set ("param", i);
        b->getProperties().set ("label", -1);
        b->setClickingTogglesState (false);
        b->setToggleState (block.getBase (i) > 0.5f, juce::dontSendNotification);
        b->setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.85f));
        b->onClick = [this, i, b] { begin(); block.set (i, block.getBase (i) > 0.5f ? 0.0f : 1.0f); b->setToggleState (block.getBase (i) > 0.5f, juce::dontSendNotification); block.presetName.clear(); done(); };
        b->setSize (juce::jmax (70, juce::GlyphArrangement::getStringWidthInt (font (12.0f), b->getButtonText()) + 24), 24);
        switches.add (b);
        addAndMakeVisible (b);
    }
    resized();
    repaint();
}

void EffectPanel::refresh()
{
    const juce::ScopedValueSetter<bool> sv (syncing, true);
    if (modelParam >= 0) model.setSelectedId (block.getChoice (modelParam) + 1, juce::dontSendNotification);
    for (auto* s : switches)
    {
        const int i = (int) s->getProperties()["param"];
        if (! juce::isPositiveAndBelow (i, block.numParams())) continue;
        if (auto* c = dynamic_cast<juce::ComboBox*> (s)) c->setSelectedId (block.getChoice (i) + 1, juce::dontSendNotification);
        else if (auto* b = dynamic_cast<juce::TextButton*> (s)) b->setToggleState (block.getBase (i) > 0.5f, juce::dontSendNotification);
        else
            for (int k = 0; k < s->getNumChildComponents(); ++k)
                if (auto* bb = dynamic_cast<juce::TextButton*> (s->getChildComponent (k))) bb->setToggleState (block.getChoice (i) == k, juce::dontSendNotification);
    }
    presets.setText (block.presetName.isNotEmpty() ? block.presetName : juce::String ("Default"), juce::dontSendNotification);
    repaint();
}

void EffectPanel::timerCallback()
{
    if (signature() != built) { build(); return; }
    inMeter->setLevel (block.peakIn.exchange (0.0f));
    outMeter->setLevel (block.peakOut.exchange (0.0f));
    const bool locked = host.isLocked ? host.isLocked() : block.locked.load();
    lockBtn.setIcon (locked ? Icon::Lock : Icon::Unlock);
    lockBtn.setHighlighted (locked);
    diceBtn.setEnabled (! locked);
    for (auto* f : faders) f->repaint();
    if (pad != nullptr) pad->repaint();
    static int tick = 0;
    if (++tick % 10 == 0) refresh();
    repaint (titleBar.getUnion (toolBar.removeFromLeft (110)));
}

//==============================================================================
void EffectPanel::resized()
{
    auto r = getLocalBounds().reduced (1);
    titleBar = r.removeFromTop (34);
    toolBar = r.removeFromTop (40);
    {
        auto t = titleBar.reduced (12, 0);
        if (host.onRemove) closeBtn.setBounds (t.removeFromRight (26).withSizeKeepingCentre (24, 24));
        tagR = t.removeFromRight (96).withSizeKeepingCentre (96, 20);
        gripR = titleBar.withWidth (juce::jmax (0, tagR.getX() - titleBar.getX() - 8));
    }
    {
        auto t = toolBar.reduced (12, 6);
        powerR = t.removeFromLeft (62);
        t.removeFromLeft (14);
        diceBtn.setBounds (t.removeFromLeft (28));
        t.removeFromLeft (4);
        lockBtn.setBounds (t.removeFromLeft (28));
        t.removeFromLeft (20);
        t.removeFromLeft (62);   // "PRESETS"
        presets.setBounds (t.removeFromLeft (juce::jmin (190, juce::jmax (110, t.getWidth() / 3))));
        t.removeFromLeft (20);
        if (modelParam >= 0)
        {
            t.removeFromLeft (52);   // "MODEL"
            model.setBounds (t.removeFromLeft (juce::jmin (190, juce::jmax (100, t.getWidth()))));
        }
    }
    r.reduce (12, 8);
    const int leftW = juce::jmax (200, juce::roundToInt ((float) r.getWidth() * (pad != nullptr ? 0.5f : 0.54f)));
    leftArea = r.removeFromLeft (leftW);
    r.removeFromLeft (14);
    rightArea = r;
    {
        auto l = leftArea.withTrimmedTop (20);
        auto meters = l.removeFromRight (56);
        inMeter->setBounds (meters.removeFromLeft (26));
        meters.removeFromLeft (4);
        outMeter->setBounds (meters.removeFromLeft (26));
        l.removeFromRight (8);
        if (display != nullptr) display->setBounds (l);
    }
    layoutBody (rightArea.withTrimmedTop (20));
}

void EffectPanel::layoutBody (juce::Rectangle<int> r)
{
    // the switches: a row (or two) along the bottom
    std::vector<juce::Component*> sw (switches.begin(), switches.end());
    if (! sw.empty())
    {
        int rows = 1, x = 0;
        for (auto* s : sw) { const int w = s->getWidth() + 10; if (x + w > r.getWidth() && x > 0) { ++rows; x = 0; } x += w; }
        auto area = r.removeFromBottom (rows * 40);
        r.removeFromBottom (6);
        int cx = area.getX(), cy = area.getY();
        for (auto* s : sw)
        {
            const int w = juce::jmin (s->getWidth(), area.getWidth());
            if (cx + w > area.getRight() && cx > area.getX()) { cx = area.getX(); cy += 40; }
            const int li = (int) s->getProperties()["label"];
            if (juce::isPositiveAndBelow (li, switchLabels.size())) switchLabels[li]->setBounds (cx - 4, cy, w + 8, 14);
            s->setBounds (cx, cy + 14, w, 24);
            if (! dynamic_cast<juce::ComboBox*> (s) && ! dynamic_cast<juce::TextButton*> (s))
            {
                const int n = s->getNumChildComponents();
                for (int k = 0; k < n; ++k) s->getChildComponent (k)->setBounds (k * w / n, 0, w / n, 24);
            }
            cx += w + 10;
        }
    }
    // the pad on the right, square
    if (pad != nullptr)
    {
        const int side = juce::jmin (r.getHeight(), juce::jmax (120, r.getWidth() / 3));
        pad->setBounds (r.removeFromRight (side).withSizeKeepingCentre (side, juce::jmin (side, r.getHeight())));
        r.removeFromRight (10);
    }
    // the faders: as many as fit at a comfortable width; the rest as bars below
    const int minW = 54, gap = 8;
    const int fit = juce::jmax (1, (r.getWidth() + gap) / (minW + gap));
    const int n = faders.size();
    const int tall = juce::jmin (n, n > fit ? fit : n);
    const int extra = n - tall;
    const int cols = extra > 2 ? 2 : 1;
    auto bars = r.removeFromBottom (extra > 0 ? ((extra + cols - 1) / cols) * 30 : 0);
    if (extra > 0) r.removeFromBottom (6);
    const int w = tall > 0 ? juce::jmin (78, (r.getWidth() - gap * (tall - 1)) / tall) : 0;
    for (int k = 0; k < tall; ++k)
    {
        faders[k]->horizontal = false;
        faders[k]->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
    for (int k = 0; k < extra; ++k)
    {
        auto* f = faders[tall + k];
        f->horizontal = true;
        const int bw = (bars.getWidth() - 8 * (cols - 1)) / cols;
        f->setBounds (bars.getX() + (k % cols) * (bw + 8), bars.getY() + (k / cols) * 30, bw, 26);
    }
}

void EffectPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff111216));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (border);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
    const bool isOn = on();
    // the title bar: its light and name, its kind
    {
        auto t = titleBar.toFloat().reduced (14.0f, 0.0f);
        const auto led = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ t.getX() + 4.5f, t.getCentreY() });
        if (isOn) { g.setColour (colour.withAlpha (0.3f)); g.fillEllipse (led.expanded (4.0f)); }
        g.setColour (isOn ? lit (colour) : dim);
        g.fillEllipse (led);
        g.setColour (isOn ? theme::text : muted);
        g.setFont (theme::display (14.0f, 0.14f));
        g.drawText (block.getName().toUpperCase(), t.withTrimmedLeft (18.0f), juce::Justification::centredLeft, true);
        auto tag = tagR.toFloat();
        const auto kind = categoryName (block.getCategory()).toUpperCase();
        const float tw = juce::jmin (tag.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (font (9.5f, true), kind) + 16.0f);
        tag = tag.withLeft (tag.getRight() - tw);
        g.setColour (colour.withAlpha (0.12f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (colour.withAlpha (0.5f));
        g.drawRoundedRectangle (tag, 4.0f, 1.0f);
        g.setColour (lit (colour));
        g.setFont (font (9.5f, true));
        g.drawText (kind, tag, juce::Justification::centred, false);
        g.setColour (border);
        g.drawHorizontalLine (titleBar.getBottom(), r.getX(), r.getRight());
    }
    // the toolbar
    {
        auto p = powerR.toFloat();
        const auto ring = juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ p.getX() + 12.0f, p.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (ring);
        g.setColour (isOn ? colour : borderHi);
        g.drawEllipse (ring.reduced (1.0f), 2.0f);
        if (isOn) { g.setColour (colour.withAlpha (0.35f)); g.fillEllipse (ring.reduced (5.0f)); }
        g.setColour (isOn ? theme::text : muted);
        g.setFont (font (12.5f));
        g.drawText (isOn ? "On" : "Off", p.withTrimmedLeft (30.0f), juce::Justification::centredLeft, false);
        g.setColour (border);
        g.drawVerticalLine (powerR.getRight() + 7, (float) toolBar.getY() + 10.0f, (float) toolBar.getBottom() - 10.0f);
        g.drawVerticalLine (lockBtn.getRight() + 10, (float) toolBar.getY() + 10.0f, (float) toolBar.getBottom() - 10.0f);
        g.setColour (muted);
        g.setFont (font (10.5f, true));
        g.drawText ("PRESETS", presets.getBounds().withX (presets.getX() - 62).withWidth (58), juce::Justification::centredLeft, false);
        if (modelParam >= 0)
        {
            g.setColour (border);
            g.drawVerticalLine (presets.getRight() + 10, (float) toolBar.getY() + 10.0f, (float) toolBar.getBottom() - 10.0f);
            g.setColour (muted);
            g.drawText ("MODEL", model.getBounds().withX (model.getX() - 52).withWidth (48), juce::Justification::centredLeft, false);
        }
        g.setColour (border);
        g.drawHorizontalLine (toolBar.getBottom(), r.getX(), r.getRight());
    }
    // the two halves
    g.setColour (muted);
    g.setFont (font (11.5f));
    g.drawText (displayTitle(), leftArea.withHeight (18), juce::Justification::centredLeft, true);
    g.drawText (controlsTitle(), rightArea.withHeight (18), juce::Justification::centredLeft, true);
    g.setColour (border);
    g.drawVerticalLine (rightArea.getX() - 7, (float) toolBar.getBottom() + 1.0f, r.getBottom() - 1.0f);
    if (display == nullptr)
    {
        auto d = leftArea.withTrimmedTop (20).withTrimmedRight (64).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (d, 6.0f);
    }
    if (! isOn)
    {
        g.setColour (juce::Colour (0xff111216).withAlpha (0.45f));
        g.fillRect (getLocalBounds().withTrimmedTop (toolBar.getBottom() + 1).reduced (2));
    }
}

void EffectPanel::mouseDown (const juce::MouseEvent& e)
{
    if (powerR.contains (e.getPosition()))
    {
        const bool now = ! on();
        if (host.setOn) host.setOn (now); else block.bypassed = ! now;
        if (host.edited) host.edited();
        repaint();
    }
}

void EffectPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (host.dragDescription.isEmpty() || ! gripR.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6) return;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this); c != nullptr && ! c->isDragAndDropActive())
        c->startDragging (host.dragDescription, this);
}

} // namespace rig
