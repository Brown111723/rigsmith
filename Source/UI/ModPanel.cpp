#include "ModPanel.h"

namespace rig
{
using namespace theme;

namespace
{
    juce::String noteName (float hz)
    {
        if (hz <= 0.0f) return "-";
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = juce::roundToInt (12.0f * std::log2 (hz / 440.0f)) + 69;
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }

    juce::Image dragImage (const juce::String& name, juce::Colour c)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (font (12.0f, true), name) + 34;
        juce::Image img (juce::Image::ARGB, w, 24, true);
        juce::Graphics g (img);
        g.setColour (card.withAlpha (0.95f));
        g.fillRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, 23.0f, 12.0f);
        g.setColour (c);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, 23.0f, 12.0f, 1.5f);
        g.fillEllipse (9.0f, 8.0f, 8.0f, 8.0f);
        g.setColour (text);
        g.setFont (font (12.0f, true));
        g.drawText (name, juce::Rectangle<int> (22, 0, w - 26, 24), juce::Justification::centredLeft);
        return img;
    }

    /** The trace of a source's last few seconds, in a box (-1..1 bottom to top). */
    void drawTrace (juce::Graphics& g, const std::vector<float>& h, juce::Rectangle<float> r, int lastN, juce::Colour c, float thickness, bool fill)
    {
        if (h.empty()) return;
        const int n = juce::jmin ((int) h.size(), lastN);
        const int start = (int) h.size() - n;
        auto yOf = [&] (float v) { return r.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * r.getHeight() * 0.5f; };
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) juce::jmax (1, n - 1);
            if (i == 0) p.startNewSubPath (x, yOf (h[(size_t) (start + i)]));
            else p.lineTo (x, yOf (h[(size_t) (start + i)]));
        }
        if (fill)
        {
            juce::Path f (p);
            f.lineTo (r.getRight(), r.getCentreY());
            f.lineTo (r.getX(), r.getCentreY());
            f.closeSubPath();
            g.setColour (c.withAlpha (0.13f));
            g.fillPath (f);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
/** One source: name, live trace, value bar, how many knobs it drives. Click to edit, drag to route. */
struct ModPanel::Chip : public juce::Component, public juce::SettableTooltipClient
{
    Chip (ModPanel& o, int i) : owner (o), index (i)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setRepaintsOnMouseActivity (true);
        auto& m = owner.engine.mods.source (i);
        setTooltip (m.getName() + ": " + m.describe() + ". Click to edit; drag onto any knob to modulate it");
    }

    void paint (juce::Graphics& g) override
    {
        auto& m = owner.engine.mods.source (index);
        const auto c = m.getColour();
        const bool sel = owner.selected == index;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (sel ? cardHi : (isMouseOver() ? card.brighter (0.04f) : card));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (sel ? c : border);
        g.drawRoundedRectangle (r, 7.0f, sel ? 1.6f : 1.0f);

        auto top = r.reduced (8.0f, 5.0f).removeFromTop (16.0f);
        g.setColour (c);
        g.fillEllipse (top.getX(), top.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (text);
        g.setFont (font (11.5f, true).withExtraKerningFactor (0.05f));
        g.drawText (m.getName(), top.withTrimmedLeft (12.0f), juce::Justification::centredLeft);

        int routes = 0;
        for (auto& rt : owner.engine.mods.routes) routes += rt.source == index;
        if (routes > 0)
        {
            const auto t = juce::String (routes) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92"));
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (font (10.0f, true), t) + 10.0f;
            auto b = top.removeFromRight (w).withSizeKeepingCentre (w, 14.0f);
            g.setColour (c.withAlpha (0.2f));
            g.fillRoundedRectangle (b, 7.0f);
            g.setColour (c);
            g.setFont (font (10.0f, true));
            g.drawText (t, b, juce::Justification::centred);
        }

        auto body = r.reduced (8.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (9.0f);
        m.readHistory (hist);
        g.setColour (juce::Colour (0xff111215));
        g.fillRoundedRectangle (body.expanded (2.0f, 1.0f), 3.0f);
        drawTrace (g, hist, body.reduced (0.0f, 2.0f), 160, c.withAlpha (0.9f), 1.4f, true);

        // the value now
        const float v = m.value();
        auto bar = r.reduced (8.0f, 0.0f).removeFromBottom (6.0f).withTrimmedBottom (2.0f);
        g.setColour (juce::Colour (0xff111215));
        g.fillRoundedRectangle (bar, 2.0f);
        const float mid = m.isBipolar() ? bar.getCentreX() : bar.getX();
        const float x = m.isBipolar() ? bar.getCentreX() + v * bar.getWidth() * 0.5f : bar.getX() + v * bar.getWidth();
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), bar.getY(), juce::jmax (mid, x) + 1.0f, bar.getBottom()), 2.0f);
    }

    // click to edit; dragging leaves the editor alone (so you can drop onto the knobs showing there)
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown()) owner.select (index); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 5) return;
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this); dc != nullptr && ! dc->isDragAndDropActive())
        {
            auto& m = owner.engine.mods.source (index);
            dc->startDragging ("modsrc:" + juce::String (index), this, juce::ScaledImage (dragImage (m.getName(), m.getColour())), false);
        }
    }

    ModPanel& owner;
    const int index;
    std::vector<float> hist;
};

//==============================================================================
/** A picture of what the selected source does: waveform, ADSR, the steps, the level window, the note range. */
struct ModPanel::Display : public juce::Component
{
    explicit Display (ModPanel& o) : owner (o) {}

    Modulator& mod() const { return owner.engine.mods.source (owner.selected); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff111215));
        g.setColour (border);
        g.drawRect (getLocalBounds());
        auto r = getLocalBounds().toFloat().reduced (10.0f, 10.0f);
        auto& m = mod();
        const auto c = m.getColour();
        switch (m.getKind())
        {
            case ModKind::Lfo:   paintLfo (g, r, m, c); break;
            case ModKind::Env:   paintEnv (g, r, dynamic_cast<EnvMod&> (m), c); break;
            case ModKind::Seq:   paintSeq (g, r, dynamic_cast<SeqMod&> (m), c); break;
            case ModKind::Level: paintLevel (g, r, dynamic_cast<LevelMod&> (m), c); break;
            case ModKind::Pitch: paintPitch (g, r, dynamic_cast<PitchMod&> (m), c); break;
        }
    }

    static void label (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Justification j, juce::Colour c = muted)
    {
        g.setColour (c);
        g.setFont (mono (10.0f));
        g.drawText (t, r, j);
    }

    void paintLfo (juce::Graphics& g, juce::Rectangle<float> r, Modulator& m, juce::Colour c)
    {
        auto& lfo = dynamic_cast<LfoMod&> (m);
        const bool bi = lfo.isBipolar();
        auto plot = r.withTrimmedBottom (14.0f);
        g.setColour (juce::Colour (0xff22242a));
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
        auto yOf = [&] (float v)   // v: -1..1 shape; unipolar shown 0..1 from the bottom
        {
            const float o = bi ? v : (v + 1.0f) * 0.5f;
            return bi ? plot.getCentreY() - o * plot.getHeight() * 0.45f : plot.getBottom() - o * plot.getHeight() * 0.9f;
        };
        juce::Path p;
        const int steps = 200;
        for (int i = 0; i <= steps; ++i)
        {
            const double ph = 2.0 * i / steps;   // two cycles
            const float y = yOf (lfo.lfoShape (ph));
            const float x = plot.getX() + plot.getWidth() * (float) i / steps;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (c.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // where it is now
        const float ph = m.livePos.load();
        const float x = plot.getX() + plot.getWidth() * ph * 0.5f;
        const float v = m.value() / juce::jmax (0.01f, m.get (LfoMod::pDepth) * 0.01f);
        const float y = bi ? plot.getCentreY() - v * plot.getHeight() * 0.45f : plot.getBottom() - v * plot.getHeight() * 0.9f;
        g.setColour (c.withAlpha (0.25f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (text);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        const int sync = lfo.getChoice (LfoMod::pSync);
        label (g, r.removeFromBottom (12.0f), (sync > 0 ? lfo.getSpecs()[LfoMod::pSync].choices[sync] + " (" + juce::String (owner.tempoNow(), 0) + " BPM)"
                                                        : juce::String (lfo.get (LfoMod::pRate), 2) + " Hz")
                                                + (bi ? "   bipolar" : "   unipolar"), juce::Justification::centredLeft);
    }

    void paintEnv (juce::Graphics& g, juce::Rectangle<float> r, EnvMod& m, juce::Colour c)
    {
        auto plot = r.withTrimmedBottom (14.0f);
        // the input level against the threshold, down the left
        auto meter = plot.removeFromLeft (10.0f);
        plot.removeFromLeft (10.0f);
        const float lvl = gainToDb (owner.levelNow());
        auto yDb = [&] (float db) { return meter.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 70.0f) * meter.getHeight(); };
        g.setColour (juce::Colour (0xff1c1d22));
        g.fillRect (meter);
        g.setColour (lvl > m.get (EnvMod::pThreshold) ? c : dim);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (meter.getX(), yDb (lvl), meter.getRight(), meter.getBottom()));
        g.setColour (text);
        g.fillRect (meter.getX() - 2.0f, yDb (m.get (EnvMod::pThreshold)) - 1.0f, meter.getWidth() + 4.0f, 2.0f);

        // ADSR, each stage's width by its time (gently compressed so short ones stay visible)
        auto w = [] (float ms) { return std::sqrt (juce::jmax (1.0f, ms)); };
        const float a = w (m.get (EnvMod::pAttack)), d = w (m.get (EnvMod::pDecay)), s = 18.0f, rl = w (m.get (EnvMod::pRelease));
        const float total = a + d + s + rl;
        const float sus = m.get (EnvMod::pSustain) * 0.01f;
        auto X = [&] (float t) { return plot.getX() + plot.getWidth() * t / total; };
        auto Y = [&] (float v) { return plot.getBottom() - v * plot.getHeight(); };
        juce::Path p;
        p.startNewSubPath (X (0), Y (0));
        p.lineTo (X (a), Y (1));
        p.quadraticTo (X (a + d * 0.3f), Y (sus + (1.0f - sus) * 0.15f), X (a + d), Y (sus));
        p.lineTo (X (a + d + s), Y (sus));
        p.quadraticTo (X (a + d + s + rl * 0.25f), Y (sus * 0.15f), X (total), Y (0));
        juce::Path f (p);
        f.closeSubPath();
        g.setColour (c.withAlpha (0.12f));
        g.fillPath (f);
        g.setColour (c.withAlpha (0.9f));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // the stage lit, the level now
        const int st = m.liveStage.load();
        const float lv = m.livePos.load();
        static const char* stages[] { "idle", "attack", "decay", "sustain", "release" };
        g.setColour (text.withAlpha (0.8f));
        g.drawHorizontalLine ((int) Y (lv), plot.getX(), plot.getRight());
        const float sx = st == EnvMod::Attack ? X (a * lv) : st == EnvMod::Decay ? X (a + d * 0.5f) : st == EnvMod::Sustain ? X (a + d + s * 0.5f)
                       : st == EnvMod::Release ? X (a + d + s + rl * (1.0f - lv / juce::jmax (0.01f, sus))) : X (0);
        g.setColour (text);
        g.fillEllipse (sx - 4.0f, Y (lv) - 4.0f, 8.0f, 8.0f);
        label (g, r.removeFromBottom (12.0f), juce::String ("in ") + (lvl > -99.0f ? juce::String (lvl, 0) : juce::String ("-inf")) + " dB   " + stages[juce::jlimit (0, 4, st)],
               juce::Justification::centredLeft);
    }

    juce::Rectangle<float> stepArea() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f).withTrimmedBottom (14.0f); }

    void paintSeq (juce::Graphics& g, juce::Rectangle<float> r, SeqMod& m, juce::Colour c)
    {
        auto area = stepArea();
        const int n = m.numSteps();
        const int cur = m.liveStage.load();
        const float bw = area.getWidth() / SeqMod::maxSteps;
        for (int i = 0; i < SeqMod::maxSteps; ++i)
        {
            auto col = juce::Rectangle<float> (area.getX() + bw * i, area.getY(), bw, area.getHeight()).reduced (1.5f, 0.0f);
            const bool on = i < n;
            g.setColour (juce::Colour (on ? 0xff1c1d22 : 0xff151619));
            g.fillRoundedRectangle (col, 2.0f);
            const float v = m.getBase (SeqMod::pStep1 + i);
            auto bar = col.withTop (col.getBottom() - v * col.getHeight());
            g.setColour (! on ? dim.withAlpha (0.3f) : (i == cur ? c : c.withAlpha (0.55f)));
            g.fillRoundedRectangle (bar, 2.0f);
            if (i == cur && on)
            {
                g.setColour (text);
                g.fillRect (col.getX(), col.getBottom() + 3.0f, col.getWidth(), 2.0f);
            }
        }
        const int sync = m.getChoice (SeqMod::pSync);
        label (g, r.removeFromBottom (12.0f).translated (0.0f, 2.0f), juce::String (n) + " steps   " + (sync > 0 ? m.getSpecs()[SeqMod::pSync].choices[sync] : juce::String (m.get (SeqMod::pRate), 1) + " Hz")
                                                + "   draw the steps", juce::Justification::centredLeft);
    }

    void paintLevel (juce::Graphics& g, juce::Rectangle<float> r, LevelMod& m, juce::Colour c)
    {
        auto plot = r.withTrimmedBottom (14.0f);
        auto meter = plot.removeFromTop (plot.getHeight() * 0.5f).reduced (0.0f, 8.0f);
        auto xDb = [&] (float db) { return meter.getX() + juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 70.0f) * meter.getWidth(); };
        g.setColour (juce::Colour (0xff1c1d22));
        g.fillRoundedRectangle (meter, 3.0f);
        const float lo = m.get (LevelMod::pFloor), hi = m.get (LevelMod::pCeiling);
        g.setColour (c.withAlpha (0.15f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xDb (lo), meter.getY(), xDb (hi), meter.getBottom()));
        const float db = m.liveDb.load();
        g.setColour (dim);
        g.fillRoundedRectangle (meter.withRight (xDb (db)), 3.0f);
        g.setColour (c);
        g.fillRect (xDb (lo) - 1.0f, meter.getY() - 4.0f, 2.0f, meter.getHeight() + 8.0f);
        g.fillRect (xDb (hi) - 1.0f, meter.getY() - 4.0f, 2.0f, meter.getHeight() + 8.0f);
        label (g, { meter.getX(), meter.getBottom() + 2.0f, meter.getWidth(), 12.0f }, "floor " + juce::String (lo, 0) + "  ceiling " + juce::String (hi, 0) + " dB",
               juce::Justification::centred);
        // output 0..1
        auto out = plot.reduced (0.0f, 14.0f).withTrimmedTop (4.0f);
        g.setColour (juce::Colour (0xff1c1d22));
        g.fillRoundedRectangle (out, 3.0f);
        g.setColour (c);
        g.fillRoundedRectangle (out.withWidth (out.getWidth() * juce::jlimit (0.0f, 1.0f, m.livePos.load())), 3.0f);
        label (g, r.removeFromBottom (12.0f), "in " + (db > -99.0f ? juce::String (db, 0) : juce::String ("-inf")) + " dB   out " + juce::String (juce::roundToInt (m.livePos.load() * 100.0f)) + "%",
               juce::Justification::centredLeft);
    }

    void paintPitch (juce::Graphics& g, juce::Rectangle<float> r, PitchMod& m, juce::Colour c)
    {
        const float hz = m.liveHz.load();
        auto plot = r.withTrimmedBottom (14.0f);
        g.setColour (hz > 0.0f ? c : dim);
        g.setFont (font (30.0f, true));
        g.drawText (noteName (hz), plot.removeFromTop (plot.getHeight() * 0.55f), juce::Justification::centred);
        auto line = plot.withSizeKeepingCentre (plot.getWidth(), 8.0f);
        g.setColour (juce::Colour (0xff1c1d22));
        g.fillRoundedRectangle (line, 4.0f);
        const float pos = m.livePos.load();
        g.setColour (c);
        g.fillRoundedRectangle (line.withWidth (juce::jmax (8.0f, line.getWidth() * pos)), 4.0f);
        label (g, { line.getX(), line.getBottom() + 3.0f, line.getWidth(), 12.0f }, noteName (m.get (PitchMod::pLow)), juce::Justification::centredLeft);
        label (g, { line.getX(), line.getBottom() + 3.0f, line.getWidth(), 12.0f }, noteName (m.get (PitchMod::pHigh)), juce::Justification::centredRight);
        label (g, r.removeFromBottom (12.0f), hz > 0.0f ? juce::String (hz, 1) + " Hz" : juce::String ("play a note"), juce::Justification::centredLeft);
    }

    // drawing the sequencer's steps
    void mouseDown (const juce::MouseEvent& e) override { drawStep (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drawStep (e); }
    void drawStep (const juce::MouseEvent& e)
    {
        auto* seq = dynamic_cast<SeqMod*> (&mod());
        if (seq == nullptr) return;
        const auto a = stepArea();
        const int i = juce::jlimit (0, SeqMod::maxSteps - 1, (int) ((e.position.x - a.getX()) / a.getWidth() * SeqMod::maxSteps));
        float v = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        if (e.mods.isShiftDown()) v = std::round (v * 4.0f) / 4.0f;
        seq->set (SeqMod::pStep1 + i, v);
        owner.engine.markDirty();
        repaint();
    }

    ModPanel& owner;
};

//==============================================================================
/** Every source's last five seconds; the selected one bold. */
struct ModPanel::Scope : public juce::Component
{
    explicit Scope (ModPanel& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff111215));
        g.setColour (border);
        g.drawRect (getLocalBounds());
        auto r = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
        auto plot = r.withTrimmedBottom (12.0f);
        g.setColour (juce::Colour (0xff1c1d22));
        for (float f : { 0.25f, 0.5f, 0.75f })
            g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * f), plot.getX(), plot.getRight());
        for (int s = 1; s < 5; ++s)
            g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * s / 5.0f), plot.getY(), plot.getBottom());

        auto& mods = owner.engine.mods;
        const bool all = owner.showAll.getToggleState();
        for (int i = 0; i < ModMatrix::numSources; ++i)
        {
            if (i == owner.selected) continue;
            bool used = false;
            for (auto& rt : mods.routes) used |= rt.source == i;
            if (! all && ! used) continue;
            mods.source (i).readHistory (hist);
            drawTrace (g, hist, plot, Modulator::historySize, mods.source (i).getColour().withAlpha (0.35f), 1.0f, false);
        }
        auto& sel = mods.source (owner.selected);
        sel.readHistory (hist);
        drawTrace (g, hist, plot, Modulator::historySize, sel.getColour(), 2.0f, true);

        auto tag = [&] (const juce::String& t, juce::Font f, bool right)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (f, t) + 10;
            auto row = plot.toNearestInt().removeFromTop (16).reduced (2, 1);
            auto box = right ? row.removeFromRight (w) : row.removeFromLeft (w);
            g.setColour (juce::Colour (0xff111215).withAlpha (0.85f));
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
            g.setColour (sel.getColour());
            g.setFont (f);
            g.drawText (t, box, juce::Justification::centred);
        };
        const float v = sel.value();
        tag (sel.getName(), font (11.0f, true), false);
        tag ((v >= 0 ? "+" : "") + juce::String (v, 2), mono (11.0f), true);
        g.setColour (dim);
        g.setFont (mono (9.5f));
        g.drawText ("-5 s", r.removeFromBottom (11.0f), juce::Justification::centredLeft);
        g.drawText ("now", juce::Rectangle<float> (r.getX(), r.getBottom() + 0.0f, r.getWidth(), 11.0f), juce::Justification::centredRight);
    }

    ModPanel& owner;
    std::vector<float> hist;
};

//==============================================================================
/** One destination of the selected source: what, how far (drag), remove. */
struct ModPanel::RouteRow : public juce::Component
{
    RouteRow (ModPanel& o, int routeIndex) : owner (o), index (routeIndex)
    {
        auto& r = owner.engine.mods.routes[(size_t) index];
        name.setText (owner.engine.describeModTarget (r.target, r.param), juce::dontSendNotification);
        name.setFont (font (11.5f));
        name.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (name);
        amount.setSliderStyle (juce::Slider::LinearBar);
        amount.setRange (-100.0, 100.0, 1.0);
        amount.setValue (r.amount * 100.0, juce::dontSendNotification);
        amount.setDoubleClickReturnValue (true, 50.0);
        amount.setTextValueSuffix ("%");
        amount.setColour (juce::Slider::trackColourId, owner.engine.mods.source (r.source).getColour().withAlpha (0.55f));
        amount.setTooltip ("How far " + owner.engine.mods.source (r.source).getName() + " moves this knob (negative: the other way)");
        amount.onValueChange = [this]
        {
            owner.engine.mods.setAmount (index, (float) amount.getValue() * 0.01f);
            owner.engine.markDirty();
        };
        addAndMakeVisible (amount);
        remove.onClick = [this]
        {
            auto& e = owner.engine;
            const int i = index;
            juce::MessageManager::callAsync ([&e, i, sp = juce::Component::SafePointer<ModPanel> (&owner)]
            {
                e.removeModRoute (i);
                if (sp != nullptr) sp->rebuildRoutes();
            });
        };
        addAndMakeVisible (remove);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        remove.setBounds (r.removeFromRight (r.getHeight()));
        r.removeFromRight (4);
        amount.setBounds (r.removeFromRight (juce::jmin (120, r.getWidth() / 2)));
        r.removeFromRight (6);
        name.setBounds (r);
    }

    ModPanel& owner;
    const int index;
    juce::Label name;
    juce::Slider amount;
    IconButton remove { Icon::Close, "Remove this modulation" };
};

//==============================================================================
ModPanel::ModPanel (Engine& e) : engine (e)
{
    for (int i = 0; i < ModMatrix::numSources; ++i)
        addAndMakeVisible (chips.add (new Chip (*this, i)));
    display = std::make_unique<Display> (*this);
    addAndMakeVisible (*display);
    scope = std::make_unique<Scope> (*this);
    addAndMakeVisible (*scope);
    addAndMakeVisible (editor);

    editorTitle.setFont (font (14.0f, true));
    editorBlurb.setFont (font (11.5f));
    editorBlurb.setColour (juce::Label::textColourId, muted);
    editorBlurb.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (editorTitle);
    addAndMakeVisible (editorBlurb);

    routesTitle.setFont (font (11.0f, true));
    routesTitle.setColour (juce::Label::textColourId, muted);
    addAndMakeVisible (routesTitle);
    routesView.setViewedComponent (&routesHolder, false);
    routesView.setScrollBarsShown (true, false);
    routesView.setScrollBarThickness (6);
    addAndMakeVisible (routesView);

    showAll.setClickingTogglesState (true);
    showAll.setToggleState (false, juce::dontSendNotification);
    showAll.setTooltip ("Show every source in the analyzer, or only the selected one and those in use");
    showAll.setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.35f));
    addAndMakeVisible (showAll);

    tempo.setSliderStyle (juce::Slider::LinearBar);
    tempo.setRange (40.0, 240.0, 1.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.setColour (juce::Slider::trackColourId, purple.withAlpha (0.3f));
    tempo.setValue (engine.mods.bpm.load(), juce::dontSendNotification);
    tempo.setTooltip ("Tempo for synced LFOs and the sequencer. In a DAW the host's tempo is used instead");
    tempo.onValueChange = [this] { engine.mods.bpm = (float) tempo.getValue(); engine.markDirty(); };
    addAndMakeVisible (tempo);
    tempoLabel.setFont (font (11.0f, true));
    tempoLabel.setColour (juce::Label::textColourId, muted);
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    buildEditor();
}

ModPanel::~ModPanel()
{
    stopTimer();
    engine.mods.watching = false;
}

void ModPanel::visibilityChanged()
{
    engine.mods.watching = isVisible();
    if (isVisible()) { startTimerHz (30); rebuildRoutes(); }
    else stopTimer();
}

float ModPanel::tempoNow() const
{
    const double h = engine.mods.hostBpm.load();
    return (float) (h > 0.0 ? h : engine.mods.bpm.load());
}

float ModPanel::levelNow() const
{
    return dbToGain (dynamic_cast<LevelMod&> (engine.mods.source (7)).liveDb.load());
}

void ModPanel::select (int s)
{
    if (! juce::isPositiveAndBelow (s, ModMatrix::numSources)) return;
    selected = s;
    buildEditor();
    rebuildRoutes();
    repaint();
}

void ModPanel::buildEditor()
{
    knobs.clear(); knobParam.clear();
    combos.clear(); comboLabels.clear(); comboParam.clear();
    toggles.clear(); toggleParam.clear();

    auto& m = engine.mods.source (selected);
    const auto c = m.getColour();
    editorTitle.setText (m.getName(), juce::dontSendNotification);
    editorTitle.setColour (juce::Label::textColourId, c);
    editorBlurb.setText (m.describe(), juce::dontSendNotification);
    const auto target = ModMatrix::targetFor (selected);

    for (int i = 0; i < m.numParams(); ++i)
    {
        if (m.getKind() == ModKind::Seq && i >= SeqMod::pStep1) break;   // drawn in the display
        const auto& sp = m.getSpecs()[(size_t) i];
        if (sp.toggle)
        {
            auto* t = toggles.add (new juce::ToggleButton (sp.name));
            t->setToggleState (m.getBase (i) > 0.5f, juce::dontSendNotification);
            t->onClick = [this, &m, i, t] { m.set (i, t->getToggleState() ? 1.0f : 0.0f); engine.markDirty(); };
            editor.addAndMakeVisible (t);
            toggleParam.push_back (i);
            continue;
        }
        if (sp.isChoice())
        {
            auto* l = comboLabels.add (new juce::Label ({}, sp.name));
            l->setFont (font (11.0f, true));
            l->setColour (juce::Label::textColourId, muted);
            editor.addAndMakeVisible (l);
            auto* cb = combos.add (new juce::ComboBox());
            cb->addItemList (sp.choices, 1);
            cb->setSelectedItemIndex (juce::roundToInt (m.getBase (i)), juce::dontSendNotification);
            cb->onChange = [this, &m, i, cb] { m.set (i, (float) cb->getSelectedItemIndex()); engine.markDirty(); };
            editor.addAndMakeVisible (cb);
            comboParam.push_back (i);
            continue;
        }
        auto* k = knobs.add (new Knob (sp.name, c));
        k->slider.setRange (sp.min, sp.max, sp.decimals == 0 && sp.unit.isEmpty() ? 1.0 : 0.0);
        if (sp.hasCentre && sp.centre > sp.min && sp.centre < sp.max)
            k->slider.setSkewFactorFromMidPoint (sp.centre);
        k->slider.setDoubleClickReturnValue (true, sp.def);
        k->slider.setValue (m.getBase (i), juce::dontSendNotification);
        k->formatValue = [&m, i] { return m.formatValue (i, m.getBase (i)); };
        k->setModTarget (engine, m, target, i);
        k->slider.onValueChange = [this, &m, i, k] { m.set (i, (float) k->slider.getValue()); k->refreshText(); engine.markDirty(); };
        k->onRightClick = [this, &m, target, i, k] { showKnobMenu (engine, m, target, i, k, nullptr); };
        k->slider.setTooltip (sp.name + ". Drop another source here to modulate it (an envelope speeding up this LFO...). Right-click: modulation");
        k->refreshText();
        editor.addAndMakeVisible (k);
        knobParam.push_back (i);
    }
    display->setMouseCursor (m.getKind() == ModKind::Seq ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    resized();
}

juce::String ModPanel::routesSignature() const
{
    juce::String s (selected);
    for (auto& r : engine.mods.routes)
        if (r.source == selected) s << "|" << r.target << ":" << r.param;
    s << "#" << (int) engine.mods.routes.size();
    return s;
}

void ModPanel::rebuildRoutes()
{
    routeRows.clear();
    shownRoutes = routesSignature();
    for (size_t i = 0; i < engine.mods.routes.size(); ++i)
        if (engine.mods.routes[i].source == selected)
            routesHolder.addAndMakeVisible (routeRows.add (new RouteRow (*this, (int) i)));
    const auto& m = engine.mods.source (selected);
    routesTitle.setText (routeRows.isEmpty() ? "Drag " + m.getName() + " onto any knob: a block's, or another source's"
                                             : m.getName() + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + juce::String (routeRows.size())
                                                 + (routeRows.size() == 1 ? " knob" : " knobs"),
                         juce::dontSendNotification);
    const int rowH = 26;
    routesHolder.setSize (juce::jmax (10, routesView.getWidth() - (routeRows.size() * rowH > routesView.getHeight() ? 8 : 0)), routeRows.size() * rowH);
    for (int i = 0; i < routeRows.size(); ++i)
        routeRows[i]->setBounds (0, i * rowH, routesHolder.getWidth(), rowH);
}

void ModPanel::timerCallback()
{
    for (auto* c : chips) c->repaint();
    display->repaint();
    scope->repaint();
    if (routesSignature() != shownRoutes) rebuildRoutes();

    // knobs follow MIDI, presets, undo
    auto& m = engine.mods.source (selected);
    for (size_t i = 0; i < knobParam.size(); ++i)
        if (! knobs[(int) i]->slider.isMouseButtonDown() && knobs[(int) i]->slider.getValue() != m.getBase (knobParam[i]))
        {
            knobs[(int) i]->slider.setValue (m.getBase (knobParam[i]), juce::dontSendNotification);
            knobs[(int) i]->refreshText();
        }
    for (size_t i = 0; i < comboParam.size(); ++i)
        if (combos[(int) i]->getSelectedItemIndex() != juce::roundToInt (m.getBase (comboParam[i])))
            combos[(int) i]->setSelectedItemIndex (juce::roundToInt (m.getBase (comboParam[i])), juce::dontSendNotification);
    for (size_t i = 0; i < toggleParam.size(); ++i)
        toggles[(int) i]->setToggleState (m.getBase (toggleParam[i]) > 0.5f, juce::dontSendNotification);

    const double host = engine.mods.hostBpm.load();
    tempo.setVisible (host <= 0.0);
    tempoLabel.setText (host > 0.0 ? "Host tempo " + juce::String (host, 1) + " BPM" : juce::String ("Tempo"), juce::dontSendNotification);
    if (! tempo.isMouseButtonDown() && std::abs (tempo.getValue() - engine.mods.bpm.load()) > 0.01)
        tempo.setValue (engine.mods.bpm.load(), juce::dontSendNotification);

    if (engine.mods.pickedFlash.exchange (false)) pickGlow = 1.0f;
    else pickGlow *= 0.8f;
    repaint (headerArea);
}

void ModPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto h = headerArea;
    g.setColour (text);
    g.setFont (font (12.0f, true).withExtraKerningFactor (0.12f));
    const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "MODULATION") + 6;
    drawIcon (g, Icon::Mod, h.removeFromLeft (22).toFloat().withSizeKeepingCentre (18.0f, 18.0f), purple);
    h.removeFromLeft (6);
    g.drawText ("MODULATION", h.removeFromLeft (tw), juce::Justification::centredLeft);
    h.removeFromLeft (12);

    // the pick light: flashes on every note the envelopes see
    auto light = h.removeFromLeft (12).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (juce::Colour (0xff2a2c32).interpolatedWith (accent, pickGlow));
    g.fillEllipse (light);
    h.removeFromLeft (4);
    g.setColour (muted);
    g.setFont (font (11.0f));
    g.drawText ("pick", h.removeFromLeft (30), juce::Justification::centredLeft);
    h.removeFromLeft (14);
    g.setColour (dim);
    g.drawText ("Drag a source onto any knob to modulate it. Right-click a knob for more", h.withTrimmedRight (300), juce::Justification::centredLeft);

    // section frames
    g.setColour (border);
    g.drawVerticalLine (chipsArea.getRight() + 9, (float) chipsArea.getY(), (float) chipsArea.getBottom());
    g.drawVerticalLine (scopeArea.getX() - 10, (float) scopeArea.getY(), (float) scopeArea.getBottom());
}

void ModPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    headerArea = r.removeFromTop (32).withTrimmedTop (2);
    {
        auto h = headerArea;
        closeButton.setBounds (h.removeFromRight (26).withSizeKeepingCentre (24, 24));
        h.removeFromRight (10);
        tempo.setBounds (h.removeFromRight (96).withSizeKeepingCentre (96, 22));
        h.removeFromRight (6);
        tempoLabel.setBounds (h.removeFromRight (tempo.isVisible() ? 50 : 170));
    }
    r.removeFromBottom (10);

    // left: the sources, 3 x 3
    chipsArea = r.removeFromLeft (juce::jlimit (240, 320, getWidth() / 4));
    {
        const int gap = 6;
        const int cw = (chipsArea.getWidth() - 2 * gap) / 3, ch = (chipsArea.getHeight() - 2 * gap) / 3;
        for (int i = 0; i < chips.size(); ++i)
            chips[i]->setBounds (chipsArea.getX() + (i % 3) * (cw + gap), chipsArea.getY() + (i / 3) * (ch + gap), cw, ch);
    }
    r.removeFromLeft (20);

    // right: analyzer and destinations
    scopeArea = r.removeFromRight (juce::jlimit (260, 380, getWidth() / 4));
    {
        auto s = scopeArea;
        auto sc = s.removeFromTop (s.getHeight() * 52 / 100);
        scope->setBounds (sc);
        showAll.setBounds (sc.getRight() - 50, sc.getBottom() - 24, 44, 18);
        showAll.toFront (false);
        s.removeFromTop (4);
        routesTitle.setBounds (s.removeFromTop (18));
        routesView.setBounds (s);
        rebuildRoutes();
    }
    r.removeFromRight (20);

    // middle: the selected source
    editorArea = r;
    auto e = r;
    auto titleRow = e.removeFromTop (20);
    editorTitle.setBounds (titleRow.removeFromLeft (64));
    editorBlurb.setBounds (titleRow);
    e.removeFromTop (6);
    auto& m = engine.mods.source (selected);
    const int dw = juce::jlimit (170, m.getKind() == ModKind::Seq ? 300 : 230, e.getWidth() * 2 / 5);
    display->setBounds (e.removeFromLeft (dw));
    e.removeFromLeft (14);
    editor.setBounds (e);

    // controls: choices and switches on top, knobs underneath
    auto c = editor.getLocalBounds();
    auto row = c.removeFromTop (44);
    const int nChoice = combos.size() + toggles.size();
    const int cw = nChoice > 0 ? juce::jmin (120, (row.getWidth() - 6 * (nChoice - 1)) / juce::jmax (1, nChoice)) : 0;
    for (int i = 0; i < combos.size(); ++i)
    {
        auto cell = row.removeFromLeft (cw);
        row.removeFromLeft (6);
        comboLabels[i]->setBounds (cell.removeFromTop (16));
        combos[i]->setBounds (cell.removeFromTop (24));
    }
    for (auto* t : toggles)
    {
        auto cell = row.removeFromLeft (cw);
        row.removeFromLeft (6);
        t->setBounds (cell.withTrimmedTop (16).removeFromTop (24));
    }
    c.removeFromTop (6);
    const int n = knobs.size();
    if (n > 0)
    {
        const int kw = juce::jmin (88, c.getWidth() / n);
        const int kh = juce::jmin (c.getHeight(), 116);
        for (int i = 0; i < n; ++i)
            knobs[i]->setBounds (c.getX() + i * kw, c.getY(), kw, kh);
    }
}

} // namespace rig
