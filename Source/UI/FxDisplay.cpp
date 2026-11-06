#include "FxDisplay.h"

namespace rig
{
using namespace theme;

static constexpr float minHz = 20.0f, maxHz = 20000.0f;

FxDisplay::FxDisplay (Engine& e, Block& b)
    : engine (e), block (b), visual (b.getVisual()), colour (colourFor (b))
{
    preHist.assign (fftSize, 0.0f);
    postHist.assign (fftSize, 0.0f);
    fftData.assign (fftSize * 2, 0.0f);
    scratch.assign (fftSize, 0.0f);
    envIn.assign (180, -100.0f);
    envGain.assign (180, 0.0f);
    setOpaque (true);
    startTimerHz (30);
}

FxDisplay::~FxDisplay() { stopTimer(); }

//==============================================================================
juce::Rectangle<float> FxDisplay::plot() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f).withTrimmedBottom (12.0f); }

float FxDisplay::xForHz (float hz) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
}

float FxDisplay::hzForX (float x) const
{
    const auto p = plot();
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
}

float FxDisplay::yForDb (float db, float range) const
{
    const auto p = plot();
    return p.getCentreY() - juce::jlimit (-range, range, db) / range * (p.getHeight() * 0.5f);
}

float FxDisplay::dbForY (float y, float range) const
{
    const auto p = plot();
    return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * range;
}

//==============================================================================
void FxDisplay::pullSpectrum()
{
    auto pull = [this] (bool post, std::vector<float>& hist)
    {
        const int got = engine.analyzer.read (post, scratch.data(), fftSize);
        if (got <= 0) return;
        std::move (hist.begin() + got, hist.end(), hist.begin());
        std::copy (scratch.begin(), scratch.begin() + got, hist.end() - got);
    };
    pull (false, preHist);
    pull (true, postHist);

    const int w = juce::jmax (1, (int) plot().getWidth());
    if ((int) preDisp.size() != w) { preDisp.assign ((size_t) w, -120.0f); postDisp.assign ((size_t) w, -120.0f); }

    auto analyse = [this, w] (const std::vector<float>& hist, std::vector<float>& disp)
    {
        std::copy (hist.begin(), hist.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const float sr = (float) juce::jmax (8000.0, engine.getSampleRate());
        const float norm = 4.0f / (float) fftSize;
        for (int x = 0; x < w; ++x)
        {
            const float f0 = hzForX (plot().getX() + (float) x), f1 = hzForX (plot().getX() + (float) x + 1.0f);
            int b0 = (int) (f0 / sr * fftSize), b1 = juce::jmax (b0 + 1, (int) (f1 / sr * fftSize));
            b0 = juce::jlimit (1, fftSize / 2 - 1, b0); b1 = juce::jlimit (b0 + 1, fftSize / 2, b1);
            float m = 0;
            for (int k = b0; k < b1; ++k) m = juce::jmax (m, fftData[(size_t) k]);
            const float db = gainToDb (m * norm) + 4.5f * std::log2 (juce::jmax (f0, 20.0f) / 1000.0f) * 0.5f; // gentle tilt, like most analysers
            auto& d = disp[(size_t) x];
            d = db > d ? db : juce::jmax (db, d - 1.6f);
        }
    };
    analyse (preHist, preDisp);
    analyse (postHist, postDisp);
}

void FxDisplay::timerCallback()
{
    if (visual == Visual::Tuner)
    {
        if (auto* tb = dynamic_cast<TunerBlock*> (&block))
            tunerReading = tb->getReading();
        const float target = tunerReading.valid ? juce::jlimit (-50.0f, 50.0f, tunerReading.cents) : 0.0f;
        needle += (target - needle) * 0.35f;
        if (tunerReading.valid) strobePhase += tunerReading.cents * 0.012f;
        if (centsHistory.size() < 120) centsHistory.resize (120, -1000.0f);
        std::move (centsHistory.begin() + 1, centsHistory.end(), centsHistory.begin());
        centsHistory.back() = tunerReading.valid ? target : -1000.0f;
    }
    if (visual == Visual::Spectrum || visual == Visual::Response)
        pullSpectrum();
    if (visual == Visual::Gate)
        if (auto* gb = dynamic_cast<GateBlock*> (&block))
        {
            if (gateOpen.size() != envIn.size())
            {
                gateOpen.assign (envIn.size(), 0);
                std::fill (envGain.begin(), envGain.end(), gb->get (GateBlock::pRange));
            }
            std::move (envIn.begin() + 1, envIn.end(), envIn.begin());
            std::move (envGain.begin() + 1, envGain.end(), envGain.begin());
            std::move (gateOpen.begin() + 1, gateOpen.end(), gateOpen.begin());
            envIn.back() = gb->takeKeyPeakDb();
            envGain.back() = gb->takeGainDb();
            gateOpen.back() = gb->isOpen() ? 1 : 0;
        }
    if (visual == Visual::Envelope || visual == Visual::Dynamics)
    {
        std::move (envIn.begin() + 1, envIn.end(), envIn.begin());
        std::move (envGain.begin() + 1, envGain.end(), envGain.begin());
        envIn.back() = block.meterIn.load();
        envGain.back() = block.meterGr.load();
    }
    repaint();
}

//==============================================================================
void FxDisplay::drawGrid (juce::Graphics& g, bool freqAxis, float dbRange)
{
    const auto p = plot();
    g.setColour (juce::Colour (0xff1c1d22));
    if (freqAxis)
    {
        g.setFont (mono (9.5f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xForHz (f);
            g.setColour (juce::Colour (0xff22242a));
            g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
            g.setColour (dim);
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                        (int) x - 16, (int) p.getBottom() + 1, 32, 12, juce::Justification::centred);
        }
        for (float db = -dbRange; db <= dbRange; db += dbRange / 3.0f)
        {
            g.setColour (db == 0.0f ? juce::Colour (0xff2e3038) : juce::Colour (0xff1e2025));
            g.drawHorizontalLine ((int) yForDb (db, dbRange), p.getX(), p.getRight());
        }
    }
}

void FxDisplay::drawSpectrum (juce::Graphics& g)
{
    if (preDisp.empty()) return;
    const auto p = plot();
    auto toY = [&p] (float db) { return p.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) * p.getHeight(); };

    juce::Path pre, post;
    pre.startNewSubPath (p.getX(), p.getBottom());
    for (size_t x = 0; x < preDisp.size(); ++x) pre.lineTo (p.getX() + (float) x, toY (preDisp[x]));
    pre.lineTo (p.getRight(), p.getBottom());
    pre.closeSubPath();
    g.setColour (juce::Colour (0xff3a3d46).withAlpha (0.55f));
    g.fillPath (pre);

    for (size_t x = 0; x < postDisp.size(); ++x)
    {
        const float y = toY (postDisp[x]);
        if (x == 0) post.startNewSubPath (p.getX(), y); else post.lineTo (p.getX() + (float) x, y);
    }
    g.setColour (colour.withAlpha (0.5f));
    g.strokePath (post, juce::PathStrokeType (1.2f));
}

void FxDisplay::drawResponse (juce::Graphics& g)
{
    const float range = 18.0f;
    drawGrid (g, true, range);
    drawSpectrum (g);
    const auto p = plot();

    juce::Path curve, fill;
    for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
    {
        const float y = yForDb (block.responseDb (hzForX (x)), range);
        if (x == p.getX()) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, p.getCentreY()); }
        else curve.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (p.getRight(), p.getCentreY());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0, p.getY(), colour.withAlpha (0.03f), 0, p.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (const float live = block.liveFrequency(); live > 0.0f)
    {
        const float x = xForHz (live);
        g.setColour (colour.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        g.setFont (mono (10.0f));
        g.drawText (juce::String ((int) live) + " Hz", (int) x + 4, (int) p.getY(), 70, 14, juce::Justification::centredLeft);
    }

    // EQ handles
    const auto hs = eqHandles();
    for (int i = 0; i < (int) hs.size(); ++i)
    {
        const auto c = hs[(size_t) i].pos;
        const bool active = i == dragHandle || i == hoverHandle;
        g.setColour (bg);
        g.fillEllipse (c.x - 8, c.y - 8, 16, 16);
        g.setColour (active ? text : colour);
        g.drawEllipse (c.x - 7, c.y - 7, 14, 14, 2.0f);
        g.setFont (font (9.0f, true));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - 8, c.y - 8, 16, 16), juce::Justification::centred);
    }
}

void FxDisplay::drawDynamics (juce::Graphics& g)
{
    const bool low = block.getType() == NodeType::Gate || block.getType() == NodeType::Expander;
    const float lo = low ? -90.0f : -60.0f;
    auto area = plot();
    auto meter = area.removeFromRight (16.0f);
    area.removeFromRight (8.0f);
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    const auto sq = area.withSizeKeepingCentre (side, side);

    auto toX = [&] (float db) { return sq.getX() + (db - lo) / -lo * sq.getWidth(); };
    auto toY = [&] (float db) { return sq.getBottom() - (juce::jlimit (lo, 6.0f, db) - lo) / -lo * sq.getHeight(); };

    // GR history behind
    juce::Path hist;
    for (size_t i = 0; i < envGain.size(); ++i)
    {
        const float x = sq.getX() + sq.getWidth() * (float) i / (float) (envGain.size() - 1);
        const float y = sq.getY() + juce::jlimit (0.0f, 1.0f, -envGain[i] / 24.0f) * sq.getHeight() * 0.5f;
        if (i == 0) hist.startNewSubPath (x, sq.getY());
        hist.lineTo (x, y);
    }
    hist.lineTo (sq.getRight(), sq.getY());
    hist.closeSubPath();
    g.setColour (red.withAlpha (0.12f));
    g.fillPath (hist);

    g.setColour (juce::Colour (0xff22242a));
    for (float d = lo; d <= 0.0f; d += low ? 15.0f : 10.0f)
    {
        g.drawVerticalLine ((int) toX (d), sq.getY(), sq.getBottom());
        g.drawHorizontalLine ((int) toY (d), sq.getX(), sq.getRight());
    }
    g.setColour (border);
    g.drawRect (sq);
    g.setColour (dim);
    const float dash[] = { 3.0f, 4.0f };
    g.drawDashedLine ({ sq.getX(), sq.getBottom(), sq.getRight(), sq.getY() }, dash, 2, 1.0f);

    juce::Path curve;
    for (float d = lo; d <= 0.0f; d += 0.5f)
    {
        const float y = toY (block.transferDb (d));
        if (d == lo) curve.startNewSubPath (toX (d), y); else curve.lineTo (toX (d), y);
    }
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved));

    // the signal riding on the curve
    const float in = block.meterIn.load();
    if (in > lo)
    {
        const juce::Point<float> dot (toX (in), toY (block.transferDb (in)));
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (dot.x - 10, dot.y - 10, 20, 20);
        g.setColour (text);
        g.fillEllipse (dot.x - 4.5f, dot.y - 4.5f, 9, 9);
    }

    // gain reduction meter
    g.setColour (bg);
    g.fillRoundedRectangle (meter, 3.0f);
    const float gr = juce::jlimit (0.0f, 1.0f, -block.meterGr.load() / 24.0f);
    g.setColour (red);
    g.fillRoundedRectangle (meter.withHeight (meter.getHeight() * gr), 3.0f);
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("GR " + juce::String (block.meterGr.load(), 1), sq.toNearestInt().removeFromBottom (16).translated (0, 12),
                juce::Justification::centredRight);
}

void FxDisplay::drawShaper (juce::Graphics& g)
{
    auto area = plot();
    auto curveBox = area.removeFromLeft (juce::jmin (area.getHeight(), area.getWidth() * 0.45f));
    area.removeFromLeft (12.0f);

    g.setColour (juce::Colour (0xff22242a));
    g.drawHorizontalLine ((int) curveBox.getCentreY(), curveBox.getX(), curveBox.getRight());
    g.drawVerticalLine ((int) curveBox.getCentreX(), curveBox.getY(), curveBox.getBottom());
    g.setColour (border);
    g.drawRect (curveBox);

    juce::Path c;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 200.0f;
        const float y = juce::jlimit (-1.2f, 1.2f, block.shape (x));
        const float px = curveBox.getCentreX() + x * curveBox.getWidth() * 0.5f;
        const float py = curveBox.getCentreY() - y * curveBox.getHeight() * 0.42f;
        if (i == 0) c.startNewSubPath (px, py); else c.lineTo (px, py);
    }
    g.setColour (colour);
    g.strokePath (c, juce::PathStrokeType (2.2f));

    // a sine going in and what comes out
    juce::Path in, out;
    for (int i = 0; i <= 240; ++i)
    {
        const float t = (float) i / 240.0f;
        const float s = std::sin (t * juce::MathConstants<float>::twoPi * 2.0f) * 0.9f;
        const float px = area.getX() + t * area.getWidth();
        const float yi = area.getCentreY() - s * area.getHeight() * 0.42f;
        const float yo = area.getCentreY() - juce::jlimit (-1.2f, 1.2f, block.shape (s)) * area.getHeight() * 0.42f;
        if (i == 0) { in.startNewSubPath (px, yi); out.startNewSubPath (px, yo); }
        else { in.lineTo (px, yi); out.lineTo (px, yo); }
    }
    g.setColour (dim);
    g.strokePath (in, juce::PathStrokeType (1.2f));
    g.setColour (colour);
    g.strokePath (out, juce::PathStrokeType (2.0f));
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("in -> out", area.toNearestInt().removeFromBottom (14).translated (0, 12), juce::Justification::centredRight);
}

void FxDisplay::drawLfo (juce::Graphics& g)
{
    const auto p = plot();
    const float depth = juce::jlimit (0.05f, 1.0f, block.lfoDepth());
    g.setColour (juce::Colour (0xff22242a));
    g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());

    juce::Path c;
    const int steps = 300;
    for (int i = 0; i <= steps; ++i)
    {
        const double ph = 2.0 * i / steps;
        const float y = p.getCentreY() - block.lfoShape (ph) * depth * p.getHeight() * 0.42f;
        const float x = p.getX() + p.getWidth() * (float) i / steps;
        if (i == 0) c.startNewSubPath (x, y); else c.lineTo (x, y);
    }
    g.setColour (colour.withAlpha (0.25f));
    g.strokePath (c, juce::PathStrokeType (6.0f));
    g.setColour (colour);
    g.strokePath (c, juce::PathStrokeType (2.0f));

    const double ph = block.lfoPhase();
    for (int k = 0; k < 2; ++k)
    {
        const float x = p.getX() + p.getWidth() * (float) ((ph + k) / 2.0);
        const float y = p.getCentreY() - block.lfoShape (ph) * depth * p.getHeight() * 0.42f;
        g.setColour (colour.withAlpha (0.3f));
        g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        g.setColour (text);
        g.fillEllipse (x - 5, y - 5, 10, 10);
    }
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText (juce::String (block.lfoRate(), 2) + " Hz   depth " + juce::String (juce::roundToInt (depth * 100)) + "%",
                p.toNearestInt().removeFromBottom (14).translated (0, 12), juce::Justification::centredRight);
}

void FxDisplay::drawDelay (juce::Graphics& g)
{
    std::vector<DelayTap> taps;
    block.getTaps (taps);
    const auto p = plot();
    float maxT = 1.0f;
    for (auto& t : taps) maxT = juce::jmax (maxT, t.timeSec);
    maxT = std::ceil (maxT * 2.0f) / 2.0f;

    auto toX = [&] (float t) { return p.getX() + t / maxT * p.getWidth(); };
    const float mid = p.getCentreY();
    g.setColour (juce::Colour (0xff22242a));
    g.drawHorizontalLine ((int) mid, p.getX(), p.getRight());
    g.setFont (mono (9.5f));
    for (float t = 0.5f; t < maxT; t += 0.5f)
    {
        g.setColour (juce::Colour (0xff1e2025));
        g.drawVerticalLine ((int) toX (t), p.getY(), p.getBottom());
        g.setColour (dim);
        g.drawText (juce::String (t, 1) + "s", (int) toX (t) - 15, (int) p.getBottom() + 1, 30, 12, juce::Justification::centred);
    }

    // dry hit
    g.setColour (text);
    g.fillRoundedRectangle (toX (0) , p.getY() + 4, 4, p.getHeight() - 8, 2);

    for (auto& t : taps)
    {
        const float h = juce::jlimit (0.0f, 1.0f, (gainToDb (t.gain) + 60.0f) / 60.0f) * p.getHeight() * 0.48f;
        const float x = toX (t.timeSec);
        const auto c = t.side < 0 ? blue : (t.side > 0 ? teal : colour);
        g.setColour (c.withAlpha (0.25f));
        if (t.side <= 0) g.fillRect (x - 5, mid - h, 10.0f, h);
        if (t.side >= 0) g.fillRect (x - 5, mid, 10.0f, h);
        g.setColour (c);
        if (t.side <= 0) g.fillRoundedRectangle (x - 2, mid - h, 4, h, 2);
        if (t.side >= 0) g.fillRoundedRectangle (x - 2, mid, 4, h, 2);
    }
    g.setColour (muted);
    g.setFont (font (10.0f, true));
    g.drawText ("L", p.toNearestInt().removeFromTop (14), juce::Justification::topRight);
    g.drawText ("R", p.toNearestInt().removeFromBottom (14), juce::Justification::bottomRight);
}

void FxDisplay::drawReverb (juce::Graphics& g)
{
    float pre, rt60, damping, size;
    block.getDecay (pre, rt60, damping, size);
    const auto p = plot();
    const float total = pre * 0.001f + rt60 * 1.15f;
    auto toX = [&] (float t) { return p.getX() + t / total * p.getWidth(); };
    auto toY = [&] (float db) { return p.getY() + juce::jlimit (0.0f, 1.0f, -db / 70.0f) * p.getHeight(); };

    g.setFont (mono (9.5f));
    for (float t = 0.5f; t < total; t += total > 6 ? 2.0f : 0.5f)
    {
        g.setColour (juce::Colour (0xff1e2025));
        g.drawVerticalLine ((int) toX (t), p.getY(), p.getBottom());
        g.setColour (dim);
        g.drawText (juce::String (t, 1) + "s", (int) toX (t) - 15, (int) p.getBottom() + 1, 30, 12, juce::Justification::centred);
    }

    const float t0 = pre * 0.001f;
    auto tail = [&] (float rt, juce::Colour c, bool filled)
    {
        juce::Path path;
        path.startNewSubPath (toX (t0), p.getBottom());
        path.lineTo (toX (t0), toY (-3.0f));
        for (float t = t0; t <= total; t += total / 200.0f)
            path.lineTo (toX (t), toY (-3.0f - 60.0f * (t - t0) / rt));
        if (filled)
        {
            path.lineTo (p.getRight(), p.getBottom());
            path.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.35f), toX (t0), 0, c.withAlpha (0.02f), p.getRight(), 0, false));
            g.fillPath (path);
        }
        else
        {
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (1.8f));
        }
    };
    tail (rt60, colour, true);
    tail (rt60, colour, false);
    tail (rt60 * (1.0f - damping * 0.75f), teal.withAlpha (0.8f), false);   // highs die sooner with damping

    // early reflections
    juce::Random rnd (7);
    for (int i = 0; i < 14; ++i)
    {
        const float t = t0 + (0.004f + 0.08f * size) * (float) i / 14.0f + rnd.nextFloat() * 0.004f;
        const float h = (0.4f + 0.6f * rnd.nextFloat()) * (1.0f - (float) i / 18.0f);
        g.setColour (text.withAlpha (0.7f));
        g.fillRect (toX (t), toY (-3.0f - (1.0f - h) * 20.0f), 2.0f, p.getBottom() - toY (-3.0f - (1.0f - h) * 20.0f));
    }
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("RT60 " + juce::String (rt60, 1) + "s   highs " + juce::String (rt60 * (1.0f - damping * 0.75f), 1) + "s",
                p.toNearestInt().removeFromTop (14), juce::Justification::topRight);
}

void FxDisplay::drawEnvelope (juce::Graphics& g)
{
    const auto p = plot();
    auto toY = [&p] (float db) { return p.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 70.0f) * p.getHeight(); };
    juce::Path in, gain;
    for (size_t i = 0; i < envIn.size(); ++i)
    {
        const float x = p.getX() + p.getWidth() * (float) i / (float) (envIn.size() - 1);
        if (i == 0) { in.startNewSubPath (x, p.getBottom()); gain.startNewSubPath (x, p.getCentreY() - envGain[i] / 18.0f * p.getHeight() * 0.5f); }
        in.lineTo (x, toY (envIn[i]));
        gain.lineTo (x, p.getCentreY() - juce::jlimit (-18.0f, 18.0f, envGain[i]) / 18.0f * p.getHeight() * 0.5f);
    }
    in.lineTo (p.getRight(), p.getBottom());
    in.closeSubPath();
    g.setColour (juce::Colour (0xff3a3d46).withAlpha (0.6f));
    g.fillPath (in);
    g.setColour (juce::Colour (0xff22242a));
    g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());
    g.setColour (colour);
    g.strokePath (gain, juce::PathStrokeType (2.0f));
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("input (grey)  gain change (line)", p.toNearestInt().removeFromBottom (14).translated (0, 12), juce::Justification::centredRight);
}

float FxDisplay::gateY (float db) const
{
    const auto p = plot();
    return p.getY() + juce::jlimit (0.0f, 1.0f, -db / 90.0f) * p.getHeight();
}

void FxDisplay::drawGate (juce::Graphics& g)
{
    // a scrolling picture: the key level, the open / close thresholds, and the gate itself
    auto* gb = dynamic_cast<GateBlock*> (&block);
    if (gb == nullptr) return;
    auto p = plot();
    const auto lane = juce::Rectangle<float> (p.getX(), p.getBottom() + 3.0f, p.getWidth(), 6.0f);
    const float thr = gb->get (GateBlock::pThreshold), hyst = gb->get (GateBlock::pHysteresis);
    const size_t n = envIn.size();
    auto xAt = [&] (size_t i) { return p.getX() + p.getWidth() * (float) i / (float) (n - 1); };

    g.setColour (juce::Colour (0xff1c1d22));
    for (float d = -15.0f; d > -90.0f; d -= 15.0f)
        g.drawHorizontalLine ((int) gateY (d), p.getX(), p.getRight());
    g.setFont (mono (9.0f));
    g.setColour (dim);
    for (float d : { -15.0f, -45.0f, -75.0f })
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (p.getRight() - 28.0f, gateY (d) - 11.0f, 26.0f, 10.0f), juce::Justification::centredRight);

    // the key level, filled from the bottom
    juce::Path key;
    key.startNewSubPath (p.getX(), p.getBottom());
    for (size_t i = 0; i < n; ++i) key.lineTo (xAt (i), gateY (juce::jmax (-90.0f, envIn[i])));
    key.lineTo (p.getRight(), p.getBottom());
    key.closeSubPath();
    juce::ColourGradient grad (colour.withAlpha (0.35f), 0.0f, p.getY(), juce::Colour (0xff3a3d46).withAlpha (0.25f), 0.0f, p.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (key);
    g.setColour (colour.withAlpha (0.55f));
    juce::Path keyLine;
    for (size_t i = 0; i < n; ++i)
    {
        const float y = gateY (juce::jmax (-90.0f, envIn[i]));
        if (i == 0) keyLine.startNewSubPath (xAt (i), y); else keyLine.lineTo (xAt (i), y);
    }
    g.strokePath (keyLine, juce::PathStrokeType (1.0f));

    // the gain: 0 dB at the top, down to the range when closed (same scale as the level)
    juce::Path gainLine;
    for (size_t i = 0; i < n; ++i)
    {
        const float y = gateY (juce::jmax (-90.0f, envGain[i]));
        if (i == 0) gainLine.startNewSubPath (xAt (i), y); else gainLine.lineTo (xAt (i), y);
    }
    g.setColour (text.withAlpha (0.85f));
    g.strokePath (gainLine, juce::PathStrokeType (1.6f));

    // open (threshold) and close (threshold - hysteresis) lines; drag the threshold
    const float ty = gateY (thr), cy = gateY (thr - hyst);
    if (hyst > 0.1f)
    {
        g.setColour (accent.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (p.getX(), ty, p.getWidth(), cy - ty));
        const float dash[] = { 4.0f, 4.0f };
        g.setColour (accent.withAlpha (0.55f));
        g.drawDashedLine ({ p.getX(), cy, p.getRight(), cy }, dash, 2, 1.0f);
    }
    g.setColour (draggingThreshold ? accent.brighter (0.3f) : accent);
    g.drawLine (p.getX(), ty, p.getRight(), ty, draggingThreshold ? 2.5f : 1.6f);
    g.setFont (mono (10.0f));
    g.drawText ("OPEN " + juce::String (thr, 0), juce::Rectangle<float> (p.getX() + 4.0f, ty - 14.0f, 90.0f, 12.0f), juce::Justification::centredLeft);
    if (hyst > 0.1f)
    {
        g.setColour (accent.withAlpha (0.7f));
        g.drawText ("CLOSE " + juce::String (thr - hyst, 0), juce::Rectangle<float> (p.getX() + 4.0f, cy + 2.0f, 90.0f, 12.0f), juce::Justification::centredLeft);
    }

    // the state lane underneath: lit where the gate was open
    g.setColour (juce::Colour (0xff1c1d22));
    g.fillRoundedRectangle (lane, 2.0f);
    g.setColour (green.withAlpha (0.8f));
    for (size_t i = 0; i + 1 < n && i < gateOpen.size(); ++i)
        if (gateOpen[i]) g.fillRect (juce::Rectangle<float> (xAt (i), lane.getY(), xAt (i + 1) - xAt (i) + 0.5f, lane.getHeight()));

    // badges
    auto badges = p.toNearestInt().removeFromTop (16).withTrimmedRight (34);
    auto badge = [&] (const juce::String& t, juce::Colour c)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (font (9.5f, true), t) + 12;
        auto r = badges.removeFromRight (w).toFloat();
        badges.removeFromRight (4);
        g.setColour (c.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (c);
        g.setFont (font (9.5f, true));
        g.drawText (t, r, juce::Justification::centred);
    };
    const bool isOpen = gb->isOpen();
    badge (isOpen ? "OPEN" : "CLOSED", isOpen ? green : red);
    if (gb->getChoice (GateBlock::pKey) == 1) badge ("KEY: RIG IN", teal);
    if (gb->isDual()) badge ("DUAL", purple);
    if (gb->get (GateBlock::pListen) > 0.5f) badge ("LISTEN", accent);
    if (gb->getLatency() > 0) badge ("LOOKAHEAD", muted);
}

void FxDisplay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111215));
    g.setColour (border);
    g.drawRect (getLocalBounds());

    switch (visual)
    {
        case Visual::Response: drawResponse (g); break;
        case Visual::Dynamics: drawDynamics (g); break;
        case Visual::Shaper:   drawShaper (g); break;
        case Visual::Lfo:      drawLfo (g); break;
        case Visual::Delay:    drawDelay (g); break;
        case Visual::Reverb:   drawReverb (g); break;
        case Visual::Envelope: drawEnvelope (g); break;
        case Visual::Tuner:    drawTuner (g); break;
        case Visual::Gate:     drawGate (g); break;
        case Visual::Modulator:
        case Visual::Spectrum: drawGrid (g, true, 18.0f); drawSpectrum (g); break;
        case Visual::None:     break;
    }

    if (highlight.isNotEmpty())
    {
        g.setFont (font (15.0f, true));
        const auto r = juce::Rectangle<int> (12, 10, juce::GlyphArrangement::getStringWidthInt (font (15.0f, true), highlight) + 16, 24);
        g.setColour (bg.withAlpha (0.85f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (text);
        g.drawText (highlight, r, juce::Justification::centred);
    }
}

//==============================================================================
void FxDisplay::drawTuner (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
    const auto& r = tunerReading;
    const bool valid = r.valid;
    const float cents = needle;
    const bool inTune = valid && std::abs (r.cents) < 3.0f;
    const auto c = ! valid ? dim : (inTune ? teal : (std::abs (r.cents) < 15.0f ? accent : red));
    const bool isMuted = block.get (TunerBlock::pMute) > 0.5f && ! block.bypassed.load();

    // left: the note, big
    auto left = area.removeFromLeft (juce::jmin (area.getWidth() * 0.34f, 150.0f));
    g.setColour (c);
    g.setFont (font (juce::jmin (left.getHeight() * 0.55f, 76.0f), true));
    g.drawText (valid ? r.note : juce::String ("-"), left.withTrimmedBottom (left.getHeight() * 0.3f).toNearestInt(), juce::Justification::centred);
    g.setFont (mono (11.0f));
    g.setColour (isMuted ? accent : theme::muted);
    juce::String sub = block.bypassed.load() ? juce::String ("switch on to tune")
                     : valid ? juce::String (r.frequency, 1) + " Hz" : juce::String ("play a string");
    g.drawText (sub, left.removeFromBottom (left.getHeight() * 0.3f).toNearestInt(), juce::Justification::centredTop);
    if (isMuted)
    {
        g.setFont (font (10.0f, true));
        g.drawText ("OUTPUT MUTED", left.removeFromBottom (14).toNearestInt(), juce::Justification::centred);
    }
    area.removeFromLeft (10.0f);

    // right top: strobe band (scrolls left when flat, right when sharp, stands still in tune)
    auto strobe = area.removeFromTop (area.getHeight() * 0.28f);
    g.setColour (juce::Colour (0xff0c0d10));
    g.fillRoundedRectangle (strobe, 6.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (strobe, 6.0f);
        g.reduceClipRegion (clip);
        const float period = 26.0f;
        const float off = std::fmod (strobePhase * period, period);
        for (float x = strobe.getX() - period + off; x < strobe.getRight(); x += period)
        {
            g.setColour ((valid ? c : dim).withAlpha (valid ? 0.85f : 0.25f));
            g.fillRect (x, strobe.getY(), period * 0.5f, strobe.getHeight() * 0.5f);
            g.fillRect (x + period * 0.5f, strobe.getCentreY(), period * 0.5f, strobe.getHeight() * 0.5f);
        }
    }
    area.removeFromTop (10.0f);

    // right middle: cents scale with needle
    auto scale = area.removeFromTop (area.getHeight() * 0.5f);
    const float cx = scale.getCentreX(), half = scale.getWidth() * 0.5f - 12.0f;
    g.setFont (mono (9.5f));
    for (int i = -50; i <= 50; i += 10)
    {
        const float x = cx + (float) i / 50.0f * half;
        const bool major = i % 50 == 0 || i == 0;
        g.setColour (i == 0 ? teal : (major ? borderHi : juce::Colour (0xff2a2c33)));
        const float th = i == 0 ? scale.getHeight() * 0.62f : scale.getHeight() * (major ? 0.42f : 0.28f);
        g.fillRect (x - (i == 0 ? 1.5f : 0.75f), scale.getY() + 2.0f, i == 0 ? 3.0f : 1.5f, th);
        if (i % 25 == 0 || i == 0 || std::abs (i) == 50)
        {
            g.setColour (dim);
            g.drawText ((i > 0 ? "+" : "") + juce::String (i), (int) x - 16, (int) (scale.getBottom() - 13.0f), 32, 12, juce::Justification::centred);
        }
    }
    g.setColour (teal.withAlpha (0.10f));
    g.fillRect (cx - 3.0f / 50.0f * half, scale.getY(), 6.0f / 50.0f * half, scale.getHeight() - 14.0f);
    if (valid)
    {
        const float x = cx + cents / 50.0f * half;
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (x - 7.0f, scale.getY() - 2.0f, 14.0f, scale.getHeight() - 10.0f, 7.0f);
        g.setColour (c);
        g.fillRoundedRectangle (x - 2.5f, scale.getY(), 5.0f, scale.getHeight() - 14.0f, 2.5f);
        g.setFont (font (13.0f, true));
        g.drawText ((r.cents >= 0 ? "+" : "") + juce::String (r.cents, 1) + " c", scale.withTrimmedBottom (14).toNearestInt().removeFromRight (70),
                    juce::Justification::topRight);
    }
    area.removeFromTop (6.0f);

    // right bottom: tuning history (how steady the note is)
    if (area.getHeight() > 18.0f && ! centsHistory.empty())
    {
        g.setColour (juce::Colour (0xff1e2025));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        juce::Path hp;
        bool started = false;
        for (size_t i = 0; i < centsHistory.size(); ++i)
        {
            const float v = centsHistory[i];
            const float x = area.getX() + area.getWidth() * (float) i / (float) (centsHistory.size() - 1);
            if (v < -999.0f) { started = false; continue; }
            const float y = area.getCentreY() - juce::jlimit (-50.0f, 50.0f, v) / 50.0f * area.getHeight() * 0.5f;
            if (! started) { hp.startNewSubPath (x, y); started = true; } else hp.lineTo (x, y);
        }
        g.setColour (c.withAlpha (0.8f));
        g.strokePath (hp, juce::PathStrokeType (1.5f));
        g.setColour (dim);
        g.setFont (mono (9.5f));
        g.drawText ("A = " + juce::String (juce::roundToInt (block.get (TunerBlock::pReference))) + " Hz", area.toNearestInt(), juce::Justification::bottomRight);
    }
}

//==============================================================================
std::vector<FxDisplay::Handle> FxDisplay::eqHandles() const
{
    std::vector<Handle> hs;
    if (block.getType() != NodeType::Eq) return hs;
    const float range = 18.0f;
    auto add = [&] (int fp, int gp)
    {
        const float f = block.get (fp);
        const float db = gp >= 0 ? block.get (gp) : 0.0f;
        hs.push_back ({ fp, gp, { xForHz (f), yForDb (db, range) } });
    };
    add (EqBlock::pHpf, -1);
    add (EqBlock::pLowFreq, EqBlock::pLow);
    add (EqBlock::pMidFreq, EqBlock::pMid);
    add (EqBlock::pHighFreq, EqBlock::pHigh);
    add (EqBlock::pLpf, -1);
    return hs;
}

int FxDisplay::hitHandle (juce::Point<float> pt) const
{
    const auto hs = eqHandles();
    int best = -1;
    float bestD = 16.0f;
    for (int i = 0; i < (int) hs.size(); ++i)
        if (const float d = hs[(size_t) i].pos.getDistanceFrom (pt); d < bestD) { bestD = d; best = i; }
    return best;
}

void FxDisplay::mouseMove (const juce::MouseEvent& e)
{
    if (visual == Visual::Gate)
    {
        setMouseCursor (std::abs (e.position.y - gateY (block.getBase (GateBlock::pThreshold))) < 10.0f ? juce::MouseCursor::UpDownResizeCursor
                                                                                                        : juce::MouseCursor::NormalCursor);
        return;
    }
    const int h = hitHandle (e.position);
    if (h != hoverHandle) { hoverHandle = h; setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void FxDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragHandle = hitHandle (e.position);
    draggingThreshold = visual == Visual::Gate && std::abs (e.position.y - gateY (block.getBase (GateBlock::pThreshold))) < 10.0f;
    if (draggingThreshold) mouseDrag (e);
}

void FxDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingThreshold)
    {
        const auto p = plot();
        block.set (GateBlock::pThreshold, (float) juce::roundToInt (-90.0f * (e.position.y - p.getY()) / p.getHeight()));
        highlight = "Threshold " + block.formatValue (GateBlock::pThreshold);
        if (onEdited) onEdited();
        repaint();
        return;
    }
    if (dragHandle < 0) return;
    const auto h = eqHandles()[(size_t) dragHandle];
    block.set (h.freqParam, hzForX (e.position.x));
    if (h.gainParam >= 0) block.set (h.gainParam, dbForY (e.position.y, 18.0f));
    highlight = block.getSpecs()[(size_t) h.freqParam].name + " " + block.formatValue (h.freqParam)
              + (h.gainParam >= 0 ? "   " + block.formatValue (h.gainParam) : juce::String());
    if (onEdited) onEdited();
    repaint();
}

void FxDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragHandle >= 0 || draggingThreshold) { highlight.clear(); repaint(); }
    dragHandle = -1;
    draggingThreshold = false;
}

void FxDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitHandle (e.position);
    if (h < 0) return;
    const auto hd = eqHandles()[(size_t) h];
    if (hd.gainParam >= 0) block.set (hd.gainParam, 0.0f);
    else block.set (hd.freqParam, block.getSpecs()[(size_t) hd.freqParam].def);
    if (onEdited) onEdited();
}

void FxDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (block.getType() != NodeType::Eq || hitHandle (e.position) != 2) return;
    block.set (EqBlock::pMidQ, block.getBase (EqBlock::pMidQ) * (w.deltaY > 0 ? 1.12f : 0.89f));
    highlight = "Mid Q " + block.formatValue (EqBlock::pMidQ);
    if (onEdited) onEdited();
}

} // namespace rig
