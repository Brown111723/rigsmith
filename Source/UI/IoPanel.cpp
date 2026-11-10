#include "IoPanel.h"

namespace rig
{
using namespace theme;

static constexpr float kFloorDb = -60.0f;

static juce::String dbText (float gain)
{
    if (gain < 1.0e-5f) return "-inf";
    const float db = gainToDb (gain);
    return (db > 0.05f ? "+" : "") + juce::String (db, 1);
}

IoPanel::IoPanel (GraphHost& h) : host (h), engine (h.getEngine())
{
    title.setFont (font (18.0f, true));
    subtitle.setFont (font (11.0f, true));
    subtitle.setColour (juce::Label::textColourId, muted);
    subtitle.setJustificationType (juce::Justification::centredRight);
    info.setFont (font (12.0f));
    info.setColour (juce::Label::textColourId, muted);
    for (auto* l : { &title, &subtitle, &info }) addAndMakeVisible (l);
    addAndMakeVisible (controls);

    phase.onClick = [this] { engine.inputPhase = phase.getToggleState(); };
    phase.setTooltip ("Flip the input's polarity (phase)");
    limiter.onClick = [this] { engine.limiterOn = limiter.getToggleState(); refreshControls(); resized(); repaint(); };
    limiter.setTooltip ("Zero-latency brickwall limiter on the master output: nothing gets past the ceiling");
    tunerMute.onClick = [this] { engine.tunerMute = tunerMute.getToggleState(); };
    tunerMute.setTooltip ("Silence the output while the full-screen tuner is open");
    limiterNote.setFont (font (11.0f));
    limiterNote.setColour (juce::Label::textColourId, dim);
    limiterNote.setJustificationType (juce::Justification::topLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &phase, &limiter, &tunerMute, &limiterNote })
        controls.addChildComponent (c);

    history.assign (240, kFloorDb);
    grHistory.assign (240, 0.0f);
    build();
}

void IoPanel::setOutput (bool o)
{
    if (o == output && ! knobs.isEmpty()) return;
    output = o;
    history.assign (240, kFloorDb);
    grHistory.assign (240, 0.0f);
    holdL = holdR = holdGr = {};
    build();
}

Knob* IoPanel::addKnob (const juce::String& name, juce::Colour c, double lo, double hi, double def, double mid,
                        std::function<float()> get, std::function<void (float)> set, std::function<juce::String (double)> fmt)
{
    auto* k = knobs.add (new Knob (name, c));
    k->slider.setRange (lo, hi, 0.0);
    if (mid > lo && mid < hi) k->slider.setSkewFactorFromMidPoint (mid);
    k->slider.setDoubleClickReturnValue (true, def);
    k->slider.setValue (get(), juce::dontSendNotification);
    k->formatValue = [k, fmt] { return fmt (k->slider.getValue()); };
    k->slider.onValueChange = [k, set] { set ((float) k->slider.getValue()); k->refreshText(); };
    k->refreshText();
    knobGetters.push_back (get);
    controls.addAndMakeVisible (k);
    return k;
}

void IoPanel::build()
{
    knobs.clear();
    knobGetters.clear();
    source.reset();

    auto dbFmt = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto panFmt = [] (double v)
    {
        const int p = juce::roundToInt (v * 100.0);
        return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
    };

    if (! output)
    {
        title.setText ("Input", juce::dontSendNotification);
        subtitle.setText ("AUDIO IN", juce::dontSendNotification);
        addKnob ("Gain", teal, -24.0, 24.0, 0.0, 0.0,
                 [this] { return engine.inputGainDb.load(); }, [this] (float v) { engine.inputGainDb = v; }, dbFmt);
        addKnob ("Balance", teal, -1.0, 1.0, 0.0, 0.0,
                 [this] { return engine.inputPan.load(); }, [this] (float v) { engine.inputPan = v; }, panFmt);

        source = std::make_unique<ChoiceControl> ("Source", juce::StringArray { "Input 1", "Input 2", "1 + 2", "Stereo" }, teal);
        source->onChange = [this] (int i) { engine.inputMode = i; };
        controls.addAndMakeVisible (*source);
    }
    else
    {
        title.setText ("Output", juce::dontSendNotification);
        subtitle.setText ("MASTER OUT", juce::dontSendNotification);
        auto* master = addKnob ("Master", accent, -40.0, 12.0, 0.0, -6.0,
                                [this] { return engine.outputGainDb.load(); }, [this] (float v) { engine.outputGainDb = v; }, dbFmt);
        master->onRightClick = [this]
        {
            MidiMapping m;
            m.target = MidiMapping::Target::MasterVolume;
            engine.startLearn (m);
        };
        addKnob ("Balance", accent, -1.0, 1.0, 0.0, 0.0,
                 [this] { return engine.outputPan.load(); }, [this] (float v) { engine.outputPan = v; }, panFmt);
        addKnob ("Width", accent, 0.0, 200.0, 100.0, 100.0,
                 [this] { return engine.outputWidth.load(); }, [this] (float v) { engine.outputWidth = v; },
                 [] (double v) { return juce::roundToInt (v) == 0 ? juce::String ("Mono") : juce::String (juce::roundToInt (v)) + "%"; });
        addKnob ("Ceiling", red, -12.0, 0.0, -0.3, -3.0,
                 [this] { return engine.limiterCeilingDb.load(); }, [this] (float v) { engine.limiterCeilingDb = v; },
                 [] (double v) { return juce::String (v, 1) + " dB"; });
        addKnob ("Release", red, 5.0, 500.0, 60.0, 60.0,
                 [this] { return engine.limiterReleaseMs.load(); }, [this] (float v) { engine.limiterReleaseMs = v; },
                 [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; });
    }

    phase.setVisible (! output);
    limiter.setVisible (output);
    tunerMute.setVisible (output);
    limiterNote.setVisible (output);
    limiterNote.setText ("Instant attack, no added latency. The ceiling is the loudest any sample can be.", juce::dontSendNotification);
    refreshControls();
    resized();
    repaint();
}

void IoPanel::refreshControls()
{
    for (size_t i = 0; i < knobGetters.size(); ++i)
        if (! knobs[(int) i]->slider.isMouseButtonDown())
        {
            knobs[(int) i]->slider.setValue (knobGetters[i](), juce::dontSendNotification);
            knobs[(int) i]->refreshText();
        }
    if (source != nullptr) source->setSelected (engine.inputMode.load());
    phase.setToggleState (engine.inputPhase.load(), juce::dontSendNotification);
    limiter.setToggleState (engine.limiterOn.load(), juce::dontSendNotification);
    tunerMute.setToggleState (engine.tunerMute.load(), juce::dontSendNotification);

    // limiter knobs dim when it's off
    if (output && knobs.size() >= 5)
        for (int i = 3; i < 5; ++i) knobs[i]->setAlpha (engine.limiterOn.load() ? 1.0f : 0.4f);

    juce::String inf;
    if (! output)
    {
        static const char* modes[] = { "Mono guitar on input 1", "Mono guitar on input 2", "Inputs 1 and 2 summed to mono", "Stereo (1 = left, 2 = right)" };
        inf << modes[juce::jlimit (0, 3, engine.inputMode.load())];
        if (engine.inputPhase.load()) inf << "   -   polarity flipped";
    }
    else
    {
        inf << (engine.limiterOn.load() ? "Limiter on, ceiling " + juce::String (engine.limiterCeilingDb.load(), 1) + " dBFS" : juce::String ("Limiter off"));
        if (engine.outputWidth.load() < 0.5f) inf << "   -   mono";
    }
    if (idle) inf = "Nothing selected: this is the master. Click a block to edit it.";
    info.setText (inf, juce::dontSendNotification);
}

void IoPanel::setLevels (const IoLevels& l)
{
    levels = l;
    const auto now = juce::Time::getMillisecondCounter();
    auto hold = [now] (Hold& h, float v)
    {
        if (v >= h.value) { h.value = v; h.at = now; }
        else if (now - h.at > 1500) h.value *= 0.92f;
    };
    const float pl = output ? l.outPeakL : l.inPeakL, pr = output ? l.outPeakR : l.inPeakR;
    hold (holdL, pl);
    hold (holdR, pr);
    if (l.grDb <= holdGr.value) { holdGr.value = l.grDb; holdGr.at = now; }
    else if (now - holdGr.at > 1500) holdGr.value *= 0.92f;

    std::move (history.begin() + 1, history.end(), history.begin());
    history.back() = juce::jmax (kFloorDb, gainToDb (juce::jmax (pl, pr)));
    std::move (grHistory.begin() + 1, grHistory.end(), grHistory.begin());
    grHistory.back() = output && engine.limiterOn.load() ? l.grDb : 0.0f;

    refreshControls();
    repaint (meterArea.getUnion (historyArea).getUnion (oversArea).expanded (4));
}

void IoPanel::mouseUp (const juce::MouseEvent& e)
{
    if (oversArea.contains (e.getPosition()) && onResetOvers)
        onResetOvers();
}

//==============================================================================
void IoPanel::drawMeters (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = output ? accent : teal;
    const float pl = output ? levels.outPeakL : levels.inPeakL, pr = output ? levels.outPeakR : levels.inPeakR;
    const float rl = output ? levels.outRmsL : levels.inRmsL, rr = output ? levels.outRmsR : levels.inRmsR;
    const bool lim = output && engine.limiterOn.load();
    const float ceilingDb = engine.limiterCeilingDb.load();

    auto xFor = [&] (juce::Rectangle<float> bar, float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb); };

    auto labels = r.removeFromLeft (18.0f);
    auto values = r.removeFromRight (104.0f);
    r.removeFromRight (8.0f);
    const int rows = lim ? 3 : 2;
    const float rowH = juce::jmin (22.0f, (r.getHeight() - 18.0f - (float) (rows - 1) * 6.0f) / (float) rows);

    auto bar = [&] (int row, const juce::String& name, float peak, float rms, float holdV, const juce::String& readout, const juce::String& readout2)
    {
        auto b = juce::Rectangle<float> (r.getX(), r.getY() + (float) row * (rowH + 6.0f), r.getWidth(), rowH);
        g.setColour (juce::Colour (0xff0c0d10));
        g.fillRoundedRectangle (b, 4.0f);
        // rms (solid) and peak (lighter)
        const float xp = xFor (b, gainToDb (peak)), xr = xFor (b, gainToDb (rms));
        juce::ColourGradient grad (c.darker (0.3f), b.getX(), 0, red, b.getRight(), 0, false);
        grad.addColour (0.80, c);
        grad.addColour (0.95, accent);
        g.setGradientFill (grad);
        g.setOpacity (0.45f);
        g.fillRoundedRectangle (b.withRight (xp), 4.0f);
        g.setOpacity (1.0f);
        g.fillRoundedRectangle (b.withRight (xr).reduced (0, 3.0f), 3.0f);
        // peak hold
        if (holdV > 1.0e-5f)
        {
            const float xh = xFor (b, gainToDb (holdV));
            g.setColour (gainToDb (holdV) > -0.5f ? red : text);
            g.fillRect (xh - 1.0f, b.getY(), 2.0f, b.getHeight());
        }
        if (lim)
        {
            const float xc = xFor (b, ceilingDb);
            g.setColour (red.withAlpha (0.8f));
            g.fillRect (xc - 0.75f, b.getY() - 2.0f, 1.5f, b.getHeight() + 4.0f);
        }
        g.setColour (muted);
        g.setFont (font (11.0f, true));
        g.drawText (name, labels.withY (b.getY()).withHeight (b.getHeight()).toNearestInt(), juce::Justification::centredLeft);
        g.setFont (mono (11.0f));
        g.setColour (text);
        auto v = values.withY (b.getY()).withHeight (b.getHeight());
        g.drawText (readout, v.removeFromLeft (52.0f).toNearestInt(), juce::Justification::centredRight);
        g.setColour (dim);
        g.drawText (readout2, v.toNearestInt(), juce::Justification::centredRight);
    };

    bar (0, "L", pl, rl, holdL.value, dbText (holdL.value), dbText (rl) + " rms");
    bar (1, "R", pr, rr, holdR.value, dbText (holdR.value), dbText (rr) + " rms");

    if (lim)
    {
        // gain reduction grows from the right
        auto b = juce::Rectangle<float> (r.getX(), r.getY() + 2.0f * (rowH + 6.0f), r.getWidth(), rowH);
        g.setColour (juce::Colour (0xff0c0d10));
        g.fillRoundedRectangle (b, 4.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, -levels.grDb / 24.0f);
        g.setColour (red.withAlpha (0.85f));
        g.fillRoundedRectangle (b.withLeft (b.getRight() - b.getWidth() * frac), 4.0f);
        const float fh = juce::jlimit (0.0f, 1.0f, -holdGr.value / 24.0f);
        if (fh > 0.002f) { g.setColour (text); g.fillRect (b.getRight() - b.getWidth() * fh - 1.0f, b.getY(), 2.0f, b.getHeight()); }
        g.setColour (muted);
        g.setFont (font (11.0f, true));
        g.drawText ("GR", labels.withY (b.getY()).withHeight (b.getHeight()).toNearestInt(), juce::Justification::centredLeft);
        g.setFont (mono (11.0f));
        g.setColour (levels.grDb < -0.1f ? red : text);
        auto v = values.withY (b.getY()).withHeight (b.getHeight());
        g.drawText (juce::String (levels.grDb, 1), v.removeFromLeft (52.0f).toNearestInt(), juce::Justification::centredRight);
        g.setColour (dim);
        g.drawText ("max " + juce::String (holdGr.value, 1), v.toNearestInt(), juce::Justification::centredRight);
    }

    // scale
    const float sy = r.getY() + (float) rows * (rowH + 6.0f);
    g.setFont (mono (9.5f));
    g.setColour (dim);
    for (float db : { -60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f })
    {
        const float x = xFor (r, db);
        g.fillRect (x - 0.5f, sy - 3.0f, 1.0f, 3.0f);
        if (db != -3.0f || r.getWidth() > 300.0f)
            g.drawText (juce::String ((int) db), (int) x - 14, (int) sy, 28, 12, juce::Justification::centred);
    }
}

void IoPanel::drawHistory (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = output ? accent : teal;
    g.setColour (juce::Colour (0xff111215));
    g.fillRect (r);
    g.setColour (border);
    g.drawRect (r);
    r = r.reduced (1.0f);

    auto yFor = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb); };
    for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, 0.0f })
    {
        g.setColour (juce::Colour (db == 0.0f ? 0xff2e3038 : 0xff1e2025));
        g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());
        g.setColour (dim);
        g.setFont (mono (9.0f));
        g.drawText (juce::String ((int) db), (int) r.getRight() - 30, (int) yFor (db) - 1, 26, 11, juce::Justification::topRight);
    }

    juce::Path p;
    const int n = (int) history.size();
    p.startNewSubPath (r.getX(), r.getBottom());
    for (int i = 0; i < n; ++i)
        p.lineTo (r.getX() + r.getWidth() * (float) i / (float) (n - 1), yFor (history[(size_t) i]));
    p.lineTo (r.getRight(), r.getBottom());
    p.closeSubPath();
    g.setColour (c.withAlpha (0.22f));
    g.fillPath (p);
    g.setColour (c.withAlpha (0.9f));
    juce::Path line;
    for (int i = 0; i < n; ++i)
    {
        const juce::Point<float> pt (r.getX() + r.getWidth() * (float) i / (float) (n - 1), yFor (history[(size_t) i]));
        if (i == 0) line.startNewSubPath (pt); else line.lineTo (pt);
    }
    g.strokePath (line, juce::PathStrokeType (1.4f));

    if (output && engine.limiterOn.load())
    {
        const float yc = yFor (engine.limiterCeilingDb.load());
        g.setColour (red.withAlpha (0.6f));
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine ({ r.getX(), yc, r.getRight(), yc }, dashes, 2, 1.0f);

        // gain reduction hangs from the top, like a mastering limiter
        juce::Path gr;
        gr.startNewSubPath (r.getX(), r.getY());
        for (int i = 0; i < n; ++i)
            gr.lineTo (r.getX() + r.getWidth() * (float) i / (float) (n - 1),
                       r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -grHistory[(size_t) i] / 24.0f));
        gr.lineTo (r.getRight(), r.getY());
        gr.closeSubPath();
        g.setColour (red.withAlpha (0.55f));
        g.fillPath (gr);
    }

    g.setColour (muted);
    g.setFont (mono (9.5f));
    g.drawText (output && engine.limiterOn.load() ? "level (fill)   limiting (red, from top)" : "level, last 8 s",
                r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::topLeft);
}

void IoPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    const auto c = output ? accent : teal;
    g.setColour (c);
    g.fillRoundedRectangle ((float) title.getX() - 12.0f, (float) title.getY() + 5.0f, 5.0f, (float) title.getHeight() - 10.0f, 2.5f);

    if (! meterArea.isEmpty()) drawMeters (g, meterArea.toFloat());
    if (! historyArea.isEmpty()) drawHistory (g, historyArea.toFloat());

    // overs / clip counter (click to reset)
    if (! oversArea.isEmpty())
    {
        const int overs = output ? levels.outOvers : levels.inOvers;
        const auto r = oversArea.toFloat();
        g.setColour (overs > 0 ? red.withAlpha (0.2f) : card);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (overs > 0 ? red : border);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (overs > 0 ? red : muted);
        g.setFont (font (11.0f, true));
        const auto label = output && engine.limiterOn.load() ? juce::String ("LIMITED") : juce::String ("CLIPS");
        g.drawText (label + "  " + juce::String (overs), oversArea.reduced (8, 0), juce::Justification::centred);
    }
}

void IoPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto left = r.removeFromLeft (juce::jlimit (380, 640, (int) ((float) getWidth() * 0.42f)));
    r.removeFromLeft (20);

    auto top = left.removeFromTop (28);
    subtitle.setBounds (top.removeFromRight (160));
    title.setBounds (top.withTrimmedLeft (12));
    left.removeFromTop (4);
    auto infoRow = left.removeFromTop (22);
    oversArea = infoRow.removeFromRight (output ? 120 : 92).reduced (0, 1);
    info.setBounds (infoRow);
    left.removeFromTop (8);

    const bool lim = output && engine.limiterOn.load();
    meterArea = left.removeFromTop (lim ? 104 : 74);
    left.removeFromTop (10);
    historyArea = left.getHeight() > 40 ? left : juce::Rectangle<int>();

    // controls on the right
    controls.setBounds (r);
    int x = 0;
    const int kw = 84, kh = juce::jlimit (100, 128, r.getHeight() - 70);
    if (source != nullptr)
    {
        source->setBounds (0, 0, juce::jmax (source->preferredWidth(), 300), 52);
        phase.setBounds (0, 64, 180, 28);
        x = 0;
        for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (juce::jmax (320, source->preferredWidth() + 24) + i * (kw + 6), 0, kw, kh);
        return;
    }
    for (int i = 0; i < knobs.size(); ++i)
    {
        if (i == 3) { x += 14; limiter.setBounds (x, 0, 190, 26); limiterNote.setBounds (x, kh + 28, 300, 34); }
        knobs[i]->setBounds (x, i >= 3 ? 28 : 0, kw, kh);
        x += kw + 6;
    }
    tunerMute.setBounds (0, kh + 10, 190, 28);
}

} // namespace rig
