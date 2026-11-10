#include "Fx.h"

namespace rig
{
using namespace dsp;

/** Wet/dry law: dry stays at unity up to 50% mix, wet reaches unity at 50%. */
static inline void mixGains (float m, float& dryG, float& wetG) noexcept
{
    m = juce::jlimit (0.0f, 1.0f, m);
    dryG = m <= 0.5f ? 1.0f : 2.0f * (1.0f - m);
    wetG = m <= 0.5f ? 2.0f * m : 1.0f;
}

static inline float peak2 (const float* l, const float* r, int n) noexcept
{
    float p = 0;
    for (int i = 0; i < n; ++i) p = juce::jmax (p, std::abs (l[i]), std::abs (r[i]));
    return p;
}

//==============================================================================
// Compressor
//==============================================================================
CompBlock::CompBlock()
    : Block (NodeType::Comp, {
        P ("threshold", "Threshold", -60.0f, 0.0f, -24.0f, "dB").rand (-36.0f, -12.0f),
        P ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1").mid (4.0f).rand (2.0f, 8.0f),
        P ("attack", "Attack", 0.1f, 100.0f, 8.0f, "ms").mid (10.0f).dec (1).rand (1.0f, 30.0f),
        P ("release", "Release", 10.0f, 1000.0f, 150.0f, "ms").mid (150.0f).rand (60.0f, 400.0f),
        P ("knee", "Knee", 0.0f, 24.0f, 6.0f, "dB").rand (0.0f, 12.0f),
        P ("makeup", "Makeup", -12.0f, 24.0f, 0.0f, "dB").fixed(),
        Toggle ("auto", "Auto makeup", true).fixed(),
        P ("mix", "Blend", 0.0f, 100.0f, 100.0f, "%").rand (50.0f, 100.0f) })
{
}

juce::String CompBlock::getDetail() const { return formatValue (pRatio) + " @ " + formatValue (pThr); }

float CompBlock::staticGr (float in) const noexcept
{
    const float thr = get (pThr), r = get (pRatio), knee = get (pKnee);
    const float over = in - thr, slope = 1.0f / r - 1.0f;
    if (2.0f * over < -knee) return 0.0f;
    if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
        return slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
    return slope * over;
}

float CompBlock::autoMakeupDb() const
{
    if (get (pAuto) <= 0.5f) return 0.0f;
    // What the compressor takes off a guitar played normally (strums peaking around -12 dBFS spend most
    // of their time well under that), so switching it in doesn't jump in level. Rigs saved before 0.13
    // keep the old rule (half the reduction at full scale), which ran several dB hot.
    return calibrated.load (std::memory_order_relaxed) ? -0.55f * staticGr (-15.0f) : -0.5f * staticGr (0.0f);
}

float CompBlock::transferDb (float in) const
{
    return in + staticGr (in) + get (pMakeup) + autoMakeupDb();
}

void CompBlock::prepareImpl (double, int) { grState = 0; }

void CompBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float sr = (float) sampleRate;
    const float att = std::exp (-1.0f / (get (pAtt) * 0.001f * sr));
    const float rel = std::exp (-1.0f / (get (pRel) * 0.001f * sr));
    const float makeupDb = get (pMakeup) + autoMakeupDb();
    float dryG, wetG;
    mixGains (get (pMix) * 0.01f * 0.5f + (get (pMix) >= 100.0f ? 0.5f : 0.0f), dryG, wetG);
    const float wet = get (pMix) * 0.01f;

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float minGr = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        const float target = staticGr (gainToDb (x));
        grState = target + (target < grState ? att : rel) * (grState - target);
        minGr = juce::jmin (minGr, grState);
        const float g = dbToGain (grState + makeupDb);
        l[i] = l[i] * (1.0f - wet) + l[i] * g * wet;
        r[i] = r[i] * (1.0f - wet) + r[i] * g * wet;
    }
    juce::ignoreUnused (dryG, wetG);
    updateMeters (inPk, peak2 (l, r, n), minGr);
}

//==============================================================================
// Limiter
//==============================================================================
LimiterBlock::LimiterBlock()
    : Block (NodeType::Limiter, {
        P ("drive", "Input", 0.0f, 18.0f, 0.0f, "dB").rand (0.0f, 6.0f),
        P ("ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, "dB").fixed(),
        P ("release", "Release", 5.0f, 500.0f, 60.0f, "ms").mid (60.0f).rand (20.0f, 200.0f) })
{
}

float LimiterBlock::transferDb (float in) const { return juce::jmin (in + get (pDrive), get (pCeil)); }

void LimiterBlock::prepareImpl (double, int) { gain = 1; }

void LimiterBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float drive = dbToGain (get (pDrive)), ceil = dbToGain (get (pCeil));
    const float rel = std::exp (-1.0f / (get (pRel) * 0.001f * (float) sampleRate));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n) * drive;
    float minG = 1;
    for (int i = 0; i < n; ++i)
    {
        const float xl = l[i] * drive, xr = r[i] * drive;
        const float pk = juce::jmax (std::abs (xl), std::abs (xr));
        const float target = pk > ceil ? ceil / pk : 1.0f;
        gain = target < gain ? target : target + rel * (gain - target);
        minG = juce::jmin (minG, gain);
        l[i] = juce::jlimit (-ceil, ceil, xl * gain);
        r[i] = juce::jlimit (-ceil, ceil, xr * gain);
    }
    updateMeters (inPk, peak2 (l, r, n), gainToDb (minG));
}

//==============================================================================
// Expander
//==============================================================================
ExpanderBlock::ExpanderBlock()
    : Block (NodeType::Expander, {
        P ("threshold", "Threshold", -80.0f, 0.0f, -45.0f, "dB").rand (-60.0f, -35.0f),
        P ("ratio", "Ratio", 1.0f, 10.0f, 2.0f, ":1").mid (3.0f).rand (1.5f, 4.0f),
        P ("range", "Range", -80.0f, 0.0f, -40.0f, "dB").rand (-40.0f, -10.0f),
        P ("attack", "Attack", 0.1f, 50.0f, 2.0f, "ms").mid (5.0f).dec (1).rand (0.5f, 10.0f),
        P ("release", "Release", 10.0f, 1000.0f, 120.0f, "ms").mid (120.0f).rand (50.0f, 300.0f) })
{
}

float ExpanderBlock::transferDb (float in) const
{
    const float thr = get (pThr);
    if (in >= thr) return in;
    return in + juce::jmax (get (pRange), (in - thr) * (get (pRatio) - 1.0f));
}

void ExpanderBlock::prepareImpl (double sr, int) { det.set (sr, 0.3, 25.0); grState = 0; }

void ExpanderBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float sr = (float) sampleRate;
    const float att = std::exp (-1.0f / (get (pAtt) * 0.001f * sr));
    const float rel = std::exp (-1.0f / (get (pRel) * 0.001f * sr));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float minGr = 0;
    for (int i = 0; i < n; ++i)
    {
        const float e = det.process (juce::jmax (std::abs (l[i]), std::abs (r[i])));
        const float inDb = gainToDb (e);
        const float target = transferDb (inDb) - inDb;
        grState = target + (target > grState ? att : rel) * (grState - target);
        minGr = juce::jmin (minGr, grState);
        const float g = dbToGain (grState);
        l[i] *= g; r[i] *= g;
    }
    updateMeters (inPk, peak2 (l, r, n), minGr);
}

//==============================================================================
// Transient shaper
//==============================================================================
TransientBlock::TransientBlock()
    : Block (NodeType::Transient, {
        P ("attack", "Attack", -100.0f, 100.0f, 0.0f, "%").rand (-60.0f, 80.0f),
        P ("sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%").rand (-60.0f, 60.0f),
        P ("output", "Output", -24.0f, 12.0f, 0.0f, "dB") })
{
}

void TransientBlock::prepareImpl (double sr, int)
{
    fast.set (sr, 0.3, 60.0);
    slow.set (sr, 25.0, 60.0);
    tail.set (sr, 0.3, 450.0);
    g.reset (sr, 0.003);
}

void TransientBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float atk = get (pAttack) * 0.01f, sus = get (pSustain) * 0.01f, out = get (pOut);
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float lastDb = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        const float f = gainToDb (fast.process (x)), s = gainToDb (slow.process (x)), t = gainToDb (tail.process (x));
        const float gDb = juce::jlimit (-18.0f, 18.0f, atk * juce::jmax (0.0f, f - s) * 1.5f + sus * juce::jmax (0.0f, t - f) * 1.2f);
        g.setTargetValue (dbToGain (gDb + out));
        const float gg = g.getNextValue();
        l[i] *= gg; r[i] *= gg;
        lastDb = gDb;
    }
    updateMeters (inPk, peak2 (l, r, n), lastDb);
}

//==============================================================================
// Auto swell
//==============================================================================
SwellBlock::SwellBlock()
    : Block (NodeType::Swell, {
        P ("threshold", "Threshold", -70.0f, -10.0f, -45.0f, "dB").rand (-55.0f, -30.0f),
        P ("rise", "Rise", 20.0f, 3000.0f, 600.0f, "ms").mid (500.0f).rand (150.0f, 1500.0f),
        P ("rearm", "Re-arm", 20.0f, 1000.0f, 200.0f, "ms").mid (200.0f).rand (100.0f, 400.0f) })
{
}

void SwellBlock::prepareImpl (double sr, int) { det.set (sr, 1.0, 40.0); ramp = 1; armed = false; quietSamples = 0; }

void SwellBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float sr = (float) sampleRate;
    const float thr = get (pThr);
    const float riseInc = 1.0f / (get (pRise) * 0.001f * sr);
    const float fallInc = 1.0f / (0.02f * sr);
    const float rearmSamples = get (pRearm) * 0.001f * sr;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float gNow = 1;
    for (int i = 0; i < n; ++i)
    {
        const float eDb = gainToDb (det.process (juce::jmax (std::abs (l[i]), std::abs (r[i]))));
        if (eDb < thr - 6.0f)
        {
            if ((quietSamples += 1.0f) > rearmSamples) armed = true;
        }
        else quietSamples = 0;

        if (armed && eDb > thr) armed = false;          // a new note: start swelling
        ramp = armed ? juce::jmax (0.0f, ramp - fallInc) : juce::jmin (1.0f, ramp + riseInc);

        const float s = std::sin (ramp * juce::MathConstants<float>::halfPi);
        gNow = s * s;
        l[i] *= gNow; r[i] *= gNow;
    }
    updateMeters (inPk, peak2 (l, r, n), gainToDb (gNow));
}

//==============================================================================
// Drive base: 2x oversampling against aliasing
//==============================================================================
void DriveBase::prepareOversampling (double, int block)
{
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing ((size_t) block);
    os->reset();
}

int DriveBase::getLatency() const noexcept
{
    return os != nullptr ? juce::roundToInt (os->getLatencyInSamples()) : 0;
}

//==============================================================================
// Fuzz
//==============================================================================
static inline float fuzzShape (float x) noexcept
{
    const float y = x > 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.25f * x)) * 0.85f;
    return juce::jlimit (-1.0f, 1.0f, y);
}

FuzzBlock::FuzzBlock()
    : DriveBase (NodeType::Fuzz, {
        P ("fuzz", "Fuzz", 0.0f, 10.0f, 7.0f).rand (4.0f, 10.0f),
        P ("tone", "Tone", 0.0f, 10.0f, 5.0f).rand (2.0f, 8.0f),
        P ("level", "Level", -24.0f, 12.0f, -6.0f, "dB"),
        P ("gate", "Gate", -90.0f, -20.0f, -70.0f, "dB").rand (-80.0f, -55.0f),
        Toggle ("octave", "Octave up", false) })
{
}

float FuzzBlock::shape (float x) const
{
    const float g = dbToGain (14.0f + get (pFuzz) * 4.0f) * 0.25f;
    if (get (pOctave) > 0.5f) x = std::abs (x) * 1.6f - 0.3f;
    return fuzzShape (x * g);
}

void FuzzBlock::prepareImpl (double sr, int block)
{
    prepareOversampling (sr, block);
    tone.reset();
    for (auto& d : dcBlock) { d.setCutoff (sr, 25.0); d.reset(); }
    gateDet.set (sr, 1.0, 80.0);
    gateGain = 0;
    watch.invalidate();
}

void FuzzBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pTone) }))
        tone.setLowPass (sampleRate, 500.0 * std::pow (10.0, get (pTone) / 10.0), 0.6);
    const float g = dbToGain (14.0f + get (pFuzz) * 4.0f) * 0.25f;
    const bool octave = get (pOctave) > 0.5f;
    const float level = dbToGain (get (pLevel));
    const float gateThr = dbToGain (get (pGate));
    const float gateCoef = std::exp (-1.0f / (0.01f * (float) sampleRate));

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    float gates[4096];
    const int gn = juce::jmin (n, 4096);
    for (int i = 0; i < gn; ++i)
    {
        const float e = gateDet.process (0.5f * (l[i] + r[i]));
        gateGain = (e > gateThr ? 1.0f : 0.0f) + gateCoef * (gateGain - (e > gateThr ? 1.0f : 0.0f));
        gates[i] = gateGain;
    }

    float* ptrs[2] = { l, r };
    juce::dsp::AudioBlock<float> blk (ptrs, 2, (size_t) n);
    auto up = os->processSamplesUp (blk);
    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto* d = up.getChannelPointer (ch);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            float x = d[i];
            if (octave) x = std::abs (x) * 1.6f;
            d[i] = fuzzShape (x * g);
        }
    }
    os->processSamplesDown (blk);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            d[i] = tone.process (dcBlock[ch].hp (d[i]), ch) * level * (i < gn ? gates[i] : gateGain);
    }
}

//==============================================================================
// Clean boost
//==============================================================================
BoostBlock::BoostBlock()
    : Block (NodeType::Boost, {
        P ("gain", "Boost", 0.0f, 24.0f, 6.0f, "dB").rand (2.0f, 10.0f),
        P ("tight", "Tight", 20.0f, 400.0f, 60.0f, "Hz").mid (90.0f).rand (40.0f, 200.0f),
        P ("bright", "Bright", -6.0f, 6.0f, 0.0f, "dB").rand (-3.0f, 4.0f) })
{
}

float BoostBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Biquad h, s;
    h.setHighPass (sr, get (pTight), 0.6);
    s.setHighShelf (sr, 3000.0, get (pBright));
    return (float) (get (pGain) + h.magnitudeDb (hz, sr) + s.magnitudeDb (hz, sr));
}

void BoostBlock::prepareImpl (double sr, int) { hp.reset(); shelf.reset(); watch.invalidate(); g.reset (sr, 0.02); }

void BoostBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pTight), get (pBright) }))
    {
        hp.setHighPass (sampleRate, get (pTight), 0.6);
        shelf.setHighShelf (sampleRate, 3000.0, get (pBright));
    }
    g.setTargetValue (dbToGain (get (pGain)));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float gg = g.getNextValue();
        l[i] = shelf.process (hp.process (l[i], 0), 0) * gg;
        r[i] = shelf.process (hp.process (r[i], 1), 1) * gg;
    }
}

//==============================================================================
// Bit crusher
//==============================================================================
CrusherBlock::CrusherBlock()
    : Block (NodeType::Crusher, {
        P ("bits", "Bits", 2.0f, 16.0f, 8.0f).dec (0).rand (5.0f, 12.0f),
        P ("down", "Downsample", 1.0f, 32.0f, 4.0f, "x").dec (0).rand (1.0f, 8.0f),
        P ("tone", "Tone", 1000.0f, 20000.0f, 12000.0f, "Hz").mid (5000.0f).rand (3000.0f, 16000.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").rand (30.0f, 100.0f) })
{
}

float CrusherBlock::shape (float x) const
{
    const float q = std::pow (2.0f, get (pBits) - 1.0f);
    return std::round (x * q) / q;
}

void CrusherBlock::prepareImpl (double, int) { hold[0] = hold[1] = 0; counter = 0; lp.reset(); watch.invalidate(); }

void CrusherBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pTone) })) lp.setLowPass (sampleRate, get (pTone));
    const float q = std::pow (2.0f, get (pBits) - 1.0f);
    const float down = get (pDown);
    float dg, wg;
    mixGains (get (pMix) * 0.01f, dg, wg);
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        if ((counter += 1.0f) >= down)
        {
            counter -= down;
            hold[0] = std::round (l[i] * q) / q;
            hold[1] = std::round (r[i] * q) / q;
        }
        l[i] = l[i] * dg + lp.process (hold[0], 0) * wg;
        r[i] = r[i] * dg + lp.process (hold[1], 1) * wg;
    }
}

//==============================================================================
// Wah
//==============================================================================
const std::array<WahBlock::WahModel, 5>& WahBlock::models()
{
    static const std::array<WahModel, 5> m { {
        { "Cry Baby GCB95", "Dunlop, 1966: the best-selling pedal of all time. Aggressive, mid-forward sweep: classic rock and funk",
          350.0f, 2200.0f, 6.5f, 0.12f, 0.35f, 1.6f, 2.0f },
        { "Vox V847A", "Vox, 1967 design: smoother, more vocal and subtle, with more low end kept (Clapton in Cream)",
          450.0f, 1800.0f, 4.2f, 0.22f, 0.2f, 1.2f, 1.0f },
        { "Cry Baby 535Q Multi-Wah", "Dunlop: the Swiss Army knife. Six sweep ranges, adjustable Q and a variable boost",
          300.0f, 2000.0f, 6.0f, 0.12f, 0.3f, 1.6f, 1.0f },
        { "Morley Bad Horsie", "Steve Vai's optical wah, 1996: a wide, smooth sweep. Switchless: it turns on when you move it, off when you rest it heel-down",
          330.0f, 2700.0f, 7.5f, 0.1f, 0.25f, 1.0f, 2.0f },
        { "Xotic XW-1", "The boutique benchmark, 2015: bias, treble, bass and Q on the outside, from vintage Italian Vox to modern funk",
          380.0f, 2300.0f, 5.5f, 0.15f, 0.3f, 1.4f, 1.0f },
    } };
    return m;
}

static juce::StringArray wahModelNames()
{
    juce::StringArray s;
    for (auto& m : WahBlock::models()) s.add (m.name);
    return s;
}

WahBlock::WahBlock()
    : Block (NodeType::Wah, {
        Choice ("mode", "Mode", { "Pedal", "Envelope", "LFO" }, 0),
        P ("position", "Pedal", 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 80.0f),
        P ("sens", "Sensitivity", 0.0f, 100.0f, 60.0f, "%").rand (30.0f, 90.0f),
        P ("rate", "LFO Rate", 0.1f, 10.0f, 1.5f, "Hz").mid (1.5f).dec (2).rand (0.3f, 4.0f),
        P ("low", "Heel", 200.0f, 800.0f, 350.0f, "Hz").mid (400.0f).rand (300.0f, 600.0f),
        P ("high", "Toe", 1000.0f, 4000.0f, 2200.0f, "Hz").mid (2000.0f).rand (1500.0f, 3000.0f),
        P ("q", "Q", 1.0f, 15.0f, 6.5f).mid (5.0f).rand (3.0f, 10.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").rand (70.0f, 100.0f),
        Choice ("model", "Model", wahModelNames(), 0),
        P ("boost", "Boost", 0.0f, 16.0f, 0.0f, "dB").rand (0.0f, 6.0f),
        P ("bass", "Bass", -10.0f, 10.0f, 0.0f, "dB").rand (-4.0f, 4.0f),
        P ("treble", "Treble", -10.0f, 10.0f, 0.0f, "dB").rand (-4.0f, 4.0f),
        P ("bias", "Bias", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        Choice ("range", "Range", { "1", "2", "3", "4", "5", "6" }, 2),
        Toggle ("auto", "Auto-engage", false).fixed() })
{
}

juce::String WahBlock::getDetail() const
{
    const auto& m = models()[(size_t) juce::jlimit (0, 4, getChoice (pModel))];
    return juce::String (m.name) + "  " + getSpecs()[pMode].choices[getChoice (pMode)];
}

std::vector<int> WahBlock::panelOrder() const
{
    const int mode = getChoice (pMode), model = getChoice (pModel);
    std::vector<int> o { pModel, pMode };
    if (model == MultiWah535Q) o.push_back (pRange);
    o.push_back (mode == 0 ? pPos : mode == 1 ? pSens : pRate);
    switch (model)
    {
        case MultiWah535Q: o.insert (o.end(), { pQ, pBoost }); break;
        case XoticXW1:     o.insert (o.end(), { pQ, pBias, pBass, pTreble, pLow, pHigh }); break;
        default:           o.insert (o.end(), { pLow, pHigh, pQ }); break;
    }
    o.push_back (pMix);
    if (model == BadHorsie && mode == 0) o.push_back (pAuto);
    return o;
}

void WahBlock::onChoiceChanged (int param)
{
    if (param != pModel) return;
    // a new model: its own stock sweep
    const auto& m = models()[(size_t) juce::jlimit (0, 4, getChoice (pModel))];
    set (pLow, m.heel);
    set (pHigh, m.toe);
    set (pQ, m.q);
    set (pAuto, getChoice (pModel) == BadHorsie ? 1.0f : 0.0f);
    if (getChoice (pModel) != MultiWah535Q) set (pBoost, 0.0f);
}

void WahBlock::sweep (float& lo, float& hi) const noexcept
{
    if (getChoice (pModel) == MultiWah535Q)
    {
        // the six-position range selector: each step moves the whole sweep up
        const int r = juce::jlimit (0, 5, getChoice (pRange));
        lo = 220.0f * std::pow (1.15f, (float) r);
        hi = 1300.0f * std::pow (1.18f, (float) r);
        return;
    }
    lo = get (pLow);
    hi = juce::jmax (get (pHigh), lo * 1.2f);
    if (getChoice (pModel) == XoticXW1)
    {
        const float shift = 0.9f + get (pBias) * 0.02f;   // bias nudges the sweep and the grit
        lo *= shift; hi *= shift;
    }
}

float WahBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Biquad bp;
    bp.setBandPass (sr, liveHz.load(), get (pQ));
    const float m = get (pMix) * 0.01f;
    const float wet = (float) std::pow (10.0, bp.magnitudeDb (hz, sr) / 20.0) * 2.0f;
    const auto& md = models()[(size_t) juce::jlimit (0, 4, getChoice (pModel))];
    float db = gainToDb ((1.0f - m) + m * (wet + md.leak * (hz < liveHz.load() ? 1.0f : 0.2f)));
    if (getChoice (pModel) == XoticXW1)
    {
        Biquad ls, hs;
        ls.setLowShelf (sr, 200.0, get (pBass));
        hs.setHighShelf (sr, 3000.0, get (pTreble));
        db += (float) (ls.magnitudeDb (hz, sr) + hs.magnitudeDb (hz, sr));
    }
    return db + md.gainDb + (getChoice (pModel) == MultiWah535Q ? get (pBoost) : 0.0f);
}

void WahBlock::prepareImpl (double sr, int)
{
    for (auto& s : svf) s.reset();
    det.set (sr, 3.0, 120.0);
    posSmooth.setTime (sr, 12.0);
    lfo.phase = 0;
    lowShelf.reset(); highShelf.reset();
    watch.invalidate();
    engage = 1.0f;
    restingSamples = 0;
}

void WahBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const int mode = getChoice (pMode);
    const int modelIx = juce::jlimit (0, 4, getChoice (pModel));
    const auto& md = models()[(size_t) modelIx];
    float lo, hi;
    sweep (lo, hi);
    const float q = get (pQ);
    const float sens = get (pSens) * 0.01f, m = get (pMix) * 0.01f;
    const bool xw = modelIx == XoticXW1;
    if (watch.changed ({ get (pBass), get (pTreble), (float) modelIx }))
    {
        lowShelf.setLowShelf (sampleRate, 200.0, xw ? get (pBass) : 0.0f);
        highShelf.setHighShelf (sampleRate, 3000.0, xw ? get (pTreble) : 0.0f);
    }
    const float drive = xw ? 0.1f + get (pBias) * 0.06f : md.drive;
    const float dg = 1.0f + drive * 4.0f;
    const float outGain = dbToGain (md.gainDb + (modelIx == MultiWah535Q ? get (pBoost) : 0.0f));
    const bool autoEngage = get (pAuto) > 0.5f && mode == 0;
    const float engStep = 1.0f / (0.02f * (float) sampleRate);
    lfo.setRate (sampleRate, get (pRate));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    float f = lo;
    for (int i = 0; i < n; ++i)
    {
        float pos = get (pPos) * 0.01f;
        if (mode == 1)      pos = juce::jlimit (0.0f, 1.0f, det.process (0.5f * (l[i] + r[i])) * sens * 14.0f);
        else if (mode == 2) pos = 0.5f + 0.5f * wave (lfo.tick(), 0);
        pos = posSmooth.lp (pos);

        // switchless: resting heel-down for a moment turns it off, moving it turns it on
        if (autoEngage)
        {
            restingSamples = pos < 0.03f ? restingSamples + 1 : 0;
            const float target = restingSamples > (int) (0.35 * sampleRate) ? 0.0f : 1.0f;
            engage += juce::jlimit (-engStep, engStep, target - engage);
        }
        else engage = 1.0f;

        f = lo * std::pow (hi / lo, std::pow (pos, 1.0f / md.taper));
        if ((i & 7) == 0) { svf[0].set (sampleRate, f, q); svf[1].set (sampleRate, f, q); }
        float lp_, bp, hp;
        svf[0].process (l[i], lp_, bp, hp);
        float yl = bp * svf[0].k * 2.0f + lp_ * md.leak;
        svf[1].process (r[i], lp_, bp, hp);
        float yr = bp * svf[1].k * 2.0f + lp_ * md.leak;
        // the transistor stage
        yl = dsp::fastTanh (yl * dg) / dg * outGain;
        yr = dsp::fastTanh (yr * dg) / dg * outGain;
        if (xw) { yl = highShelf.process (lowShelf.process (yl, 0), 0); yr = highShelf.process (lowShelf.process (yr, 1), 1); }
        const float wet = m * engage;
        l[i] = l[i] * (1.0f - wet) + yl * wet;
        r[i] = r[i] * (1.0f - wet) + yr * wet;
    }
    engaged.store (engage > 0.5f);
    liveHz.store (f);
    livePhase.store (lfo.phase);
}

//==============================================================================
// Chorus
//==============================================================================
ChorusBlock::ChorusBlock()
    : Block (NodeType::Chorus, {
        P ("rate", "Rate", 0.05f, 5.0f, 0.8f, "Hz").mid (0.8f).dec (2).rand (0.2f, 2.5f),
        P ("depth", "Depth", 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 80.0f),
        P ("delay", "Delay", 5.0f, 30.0f, 12.0f, "ms").mid (12.0f).dec (1).rand (7.0f, 20.0f),
        P ("width", "Width", 0.0f, 100.0f, 100.0f, "%").rand (50.0f, 100.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%").rand (25.0f, 60.0f) })
{
}

void ChorusBlock::prepareImpl (double sr, int)
{
    for (auto& d : buf) d.prepare ((int) (sr * 0.05));
    lfo.phase = 0;
}

void ChorusBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float sr = (float) sampleRate;
    lfo.setRate (sampleRate, get (pRate));
    const float base = get (pDelay) * 0.001f * sr, depth = get (pDepth) * 0.01f * 0.003f * sr;
    const float off = get (pWidth) * 0.01f * 0.25f;
    float dg, wg;
    mixGains (get (pMix) * 0.01f, dg, wg);
    if (calibrated.load (std::memory_order_relaxed)) { const float norm = 1.0f / std::sqrt (dg * dg + wg * wg); dg *= norm; wg *= norm; }   // at level whatever the mix
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.tick();
        buf[0].push (l[i]);
        buf[1].push (r[i]);
        const float wl = buf[0].read (base + depth * wave (ph, 0));
        const float wr = buf[1].read (base + depth * wave (ph + off, 0));
        l[i] = l[i] * dg + wl * wg;
        r[i] = r[i] * dg + wr * wg;
    }
    livePhase.store (lfo.phase);
}

//==============================================================================
// Flanger
//==============================================================================
FlangerBlock::FlangerBlock()
    : Block (NodeType::Flanger, {
        P ("rate", "Rate", 0.02f, 5.0f, 0.25f, "Hz").mid (0.5f).dec (2).rand (0.05f, 1.0f),
        P ("depth", "Depth", 0.0f, 100.0f, 70.0f, "%").rand (30.0f, 90.0f),
        P ("manual", "Manual", 0.3f, 8.0f, 1.5f, "ms").mid (2.0f).dec (2).rand (0.5f, 4.0f),
        P ("feedback", "Feedback", -95.0f, 95.0f, 50.0f, "%").rand (-80.0f, 80.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%").rand (30.0f, 60.0f) })
{
}

void FlangerBlock::prepareImpl (double sr, int)
{
    for (auto& d : buf) d.prepare ((int) (sr * 0.03));
    fb[0] = fb[1] = 0;
    lfo.phase = 0;
}

void FlangerBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float sr = (float) sampleRate;
    lfo.setRate (sampleRate, get (pRate));
    const float man = get (pManual) * 0.001f * sr, depth = get (pDepth) * 0.01f * 0.9f;
    const float feed = get (pFeedback) * 0.01f;
    float dg, wg;
    mixGains (get (pMix) * 0.01f, dg, wg);
    if (calibrated.load (std::memory_order_relaxed)) { const float norm = 1.0f / std::sqrt (dg * dg + wg * wg); dg *= norm; wg *= norm; }   // at level whatever the mix
    float* ch[2] = { b.getWritePointer (0), b.getWritePointer (1) };
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.tick();
        for (int c = 0; c < 2; ++c)
        {
            const float d = juce::jmax (1.0f, man * (1.0f + depth * wave (ph + c * 0.25, 1)));
            buf[c].push (fastTanh (ch[c][i] + fb[c] * feed));
            const float w = buf[c].read (d);
            fb[c] = w;
            ch[c][i] = ch[c][i] * dg + w * wg;
        }
    }
    livePhase.store (lfo.phase);
}

//==============================================================================
// Phaser
//==============================================================================
PhaserBlock::PhaserBlock()
    : Block (NodeType::Phaser, {
        P ("rate", "Rate", 0.05f, 8.0f, 0.5f, "Hz").mid (0.6f).dec (2).rand (0.1f, 2.0f),
        P ("depth", "Depth", 0.0f, 100.0f, 80.0f, "%").rand (40.0f, 100.0f),
        P ("centre", "Centre", 150.0f, 2500.0f, 700.0f, "Hz").mid (700.0f).rand (300.0f, 1500.0f),
        P ("feedback", "Feedback", 0.0f, 90.0f, 40.0f, "%").rand (10.0f, 70.0f),
        Choice ("stages", "Stages", { "4", "6", "8", "12" }, 1),
        P ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%").rand (40.0f, 70.0f) })
{
}

int PhaserBlock::numStages() const noexcept
{
    static const int s[] = { 4, 6, 8, 12 };
    return s[juce::jlimit (0, 3, getChoice (pStages))];
}

float PhaserBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    const double a = AllPass1::coef (sr, liveHz.load());
    const auto z1 = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / sr);
    auto h = (a + z1) / (1.0 + a * z1);
    std::complex<double> total (1.0, 0.0);
    for (int i = 0; i < numStages(); ++i) total *= h;
    const double fbk = get (pFeedback) * 0.01;
    total = total / (1.0 - fbk * total);
    const double m = juce::jlimit (0.0, 1.0, (double) get (pMix) * 0.01) * 0.5;
    const auto out = (1.0 - m) + m * total * (1.0 - fbk);
    return (float) (20.0 * std::log10 (juce::jmax (1.0e-4, std::abs (out))));
}

void PhaserBlock::prepareImpl (double, int)
{
    for (auto& side : ap) for (auto& a : side) a.z = 0;
    fb[0] = fb[1] = 0;
    lfo.phase = 0;
}

void PhaserBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    lfo.setRate (sampleRate, get (pRate));
    const float centre = get (pCentre), depth = get (pDepth) * 0.01f * 2.0f;
    const float feed = get (pFeedback) * 0.01f, m = get (pMix) * 0.01f * 0.5f;
    const int stages = numStages();
    float* ch[2] = { b.getWritePointer (0), b.getWritePointer (1) };
    float fl = centre;
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.tick();
        for (int c = 0; c < 2; ++c)
        {
            const float f = centre * std::pow (2.0f, depth * wave (ph + c * 0.2, 0));
            if (c == 0) fl = f;
            if ((i & 3) == 0)
            {
                const float a = AllPass1::coef (sampleRate, f);
                for (int s = 0; s < stages; ++s) ap[c][s].a = a;
            }
            float x = ch[c][i] + fb[c] * feed;
            for (int s = 0; s < stages; ++s) x = ap[c][s].process (x);
            fb[c] = x;
            ch[c][i] = ch[c][i] * (1.0f - m) + x * m;
        }
    }
    liveHz.store (fl);
    livePhase.store (lfo.phase);
}

//==============================================================================
// Tremolo
//==============================================================================
TremoloBlock::TremoloBlock()
    : Block (NodeType::Tremolo, {
        P ("rate", "Rate", 0.5f, 15.0f, 5.0f, "Hz").mid (5.0f).dec (2).rand (2.0f, 9.0f),
        P ("depth", "Depth", 0.0f, 100.0f, 60.0f, "%").rand (30.0f, 90.0f),
        Choice ("shape", "Shape", { "Sine", "Triangle", "Square", "Harmonic" }, 0),
        Toggle ("stereo", "Stereo pan", false) })
{
}

float TremoloBlock::lfoShape (double ph) const
{
    static const int map[] = { 0, 1, 2, 0 };
    return wave (ph, map[juce::jlimit (0, 3, getChoice (pShape))]);
}

void TremoloBlock::prepareImpl (double sr, int block)
{
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
    lowF.prepare (spec); highF.prepare (spec);
    lowF.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    highF.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lowF.setCutoffFrequency (800.0f); highF.setCutoffFrequency (800.0f);
    lfo.phase = 0;
}

void TremoloBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    lfo.setRate (sampleRate, get (pRate));
    const float depth = get (pDepth) * 0.01f;
    const int shape = getChoice (pShape);
    const bool stereo = get (pStereo) > 0.5f;
    // a tremolo only ever turns down, so it sounds quieter when it's switched in: make up the average
    // (rigs saved before 0.13 keep the level they had)
    const float makeup = calibrated.load (std::memory_order_relaxed)
                           ? juce::jmin (1.6f, 1.0f / std::sqrt ((1.0f - depth * 0.5f) * (1.0f - depth * 0.5f) + depth * depth * 0.125f)) : 1.0f;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.tick();
        const float w = lfoShape (ph);
        const float wr = stereo ? lfoShape (ph + 0.5) : w;
        l[i] *= makeup; r[i] *= makeup;
        if (shape == 3)
        {
            // harmonic tremolo: lows and highs pulse in opposition
            const float gLo = 1.0f - depth * (0.5f - 0.5f * w), gHi = 1.0f - depth * (0.5f + 0.5f * w);
            const float gLoR = 1.0f - depth * (0.5f - 0.5f * wr), gHiR = 1.0f - depth * (0.5f + 0.5f * wr);
            l[i] = lowF.processSample (0, l[i]) * gLo + highF.processSample (0, l[i]) * gHi;
            r[i] = lowF.processSample (1, r[i]) * gLoR + highF.processSample (1, r[i]) * gHiR;
        }
        else
        {
            l[i] *= 1.0f - depth * (0.5f - 0.5f * w);
            r[i] *= 1.0f - depth * (0.5f - 0.5f * wr);
        }
    }
    livePhase.store (lfo.phase);
}

//==============================================================================
// Uni-Vibe
//==============================================================================
static inline float lamp (double ph) noexcept
{
    return 2.0f * std::pow (0.5f + 0.5f * std::sin (twoPi * (float) ph), 1.8f) - 1.0f;
}

VibeBlock::VibeBlock()
    : Block (NodeType::Vibe, {
        P ("rate", "Speed", 0.2f, 10.0f, 2.0f, "Hz").mid (2.0f).dec (2).rand (0.5f, 6.0f),
        P ("intensity", "Intensity", 0.0f, 100.0f, 70.0f, "%").rand (30.0f, 90.0f),
        Choice ("mode", "Mode", { "Chorus", "Vibrato" }, 0),
        P ("level", "Level", -12.0f, 12.0f, 0.0f, "dB").fixed() })
{
}

float VibeBlock::lfoShape (double ph) const { return lamp (ph); }

void VibeBlock::prepareImpl (double, int)
{
    for (auto& side : ap) for (auto& a : side) a.z = 0;
    lfo.phase = 0;
}

void VibeBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    static const float base[] = { 90.0f, 240.0f, 640.0f, 1600.0f };
    lfo.setRate (sampleRate, get (pRate));
    const float inten = get (pIntensity) * 0.01f * 2.5f;
    const bool vibrato = getChoice (pMode) == 1;
    const float level = dbToGain (get (pLevel));
    float* ch[2] = { b.getWritePointer (0), b.getWritePointer (1) };
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.tick();
        const float mod = std::pow (2.0f, inten * lamp (ph));
        if ((i & 3) == 0)
            for (int s = 0; s < 4; ++s)
                ap[0][s].a = ap[1][s].a = AllPass1::coef (sampleRate, base[s] * mod);
        for (int c = 0; c < 2; ++c)
        {
            float x = ch[c][i];
            for (int s = 0; s < 4; ++s) x = ap[c][s].process (x);
            ch[c][i] = (vibrato ? x : 0.5f * (ch[c][i] + x)) * level;
        }
    }
    livePhase.store (lfo.phase);
}

//==============================================================================
// Rotary speaker
//==============================================================================
RotaryBlock::RotaryBlock()
    : Block (NodeType::Rotary, {
        Choice ("speed", "Speed", { "Slow", "Fast", "Brake" }, 0),
        P ("ramp", "Ramp", 0.2f, 5.0f, 1.2f, "s").mid (1.2f).dec (2),
        P ("balance", "Horn / Drum", 0.0f, 100.0f, 60.0f, "%").rand (30.0f, 80.0f),
        P ("width", "Width", 0.0f, 100.0f, 80.0f, "%").rand (40.0f, 100.0f),
        P ("drive", "Drive", 0.0f, 100.0f, 20.0f, "%").rand (0.0f, 50.0f) })
{
}

void RotaryBlock::prepareImpl (double sr, int)
{
    hornBuf.prepare ((int) (sr * 0.01));
    xover.setCutoff (sr, 800.0);
    xover.reset();
}

void RotaryBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const int speed = getChoice (pSpeed);
    const float hornTarget = speed == 0 ? 0.83f : (speed == 1 ? 6.7f : 0.0f);
    const float drumTarget = speed == 0 ? 0.67f : (speed == 1 ? 5.8f : 0.0f);
    const float sr = (float) sampleRate;
    const float rampHorn = std::exp (-1.0f / (get (pRamp) * sr * 0.3f));
    const float rampDrum = std::exp (-1.0f / (get (pRamp) * sr * 0.5f));
    const float bal = get (pBalance) * 0.01f, width = get (pWidth) * 0.01f;
    const float drive = 1.0f + get (pDrive) * 0.04f;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        hornHz = hornTarget + rampHorn * (hornHz - hornTarget);
        drumHz = drumTarget + rampDrum * (drumHz - drumTarget);
        hornPh += hornHz / sr; hornPh -= std::floor (hornPh);
        drumPh += drumHz / sr; drumPh -= std::floor (drumPh);

        const float m = fastTanh (0.5f * (l[i] + r[i]) * drive) / drive;
        const float low = xover.lp (m), high = m - low;

        const float hs = std::sin (twoPi * (float) hornPh), ds = std::sin (twoPi * (float) drumPh);
        hornBuf.push (high);
        const float horn = hornBuf.read ((1.2f + 0.9f * hs) * 0.001f * sr) * (1.0f - 0.35f * (0.5f - 0.5f * hs));
        const float drum = low * (1.0f - 0.3f * (0.5f + 0.5f * ds));

        const float hl = horn * (0.5f + 0.5f * hs * width), hr = horn * (0.5f - 0.5f * hs * width);
        const float dl = drum * (0.5f + 0.5f * ds * width), dr = drum * (0.5f - 0.5f * ds * width);
        l[i] = (hl * bal + dl * (1.0f - bal)) * 2.0f;
        r[i] = (hr * bal + dr * (1.0f - bal)) * 2.0f;
    }
    hornRate.store (hornHz);
    livePhase.store (hornPh);
}

//==============================================================================
// Delay
//==============================================================================
DelayBlock::DelayBlock()
    : Block (NodeType::Delay, {
        Choice ("mode", "Mode", { "Digital", "Tape", "Ping-Pong", "Dual" }, 0),
        P ("time", "Time", 20.0f, 2000.0f, 400.0f, "ms").mid (400.0f).rand (120.0f, 800.0f),
        P ("feedback", "Feedback", 0.0f, 100.0f, 35.0f, "%").rand (10.0f, 55.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 30.0f, "%").rand (15.0f, 40.0f),
        P ("lowcut", "Low Cut", 20.0f, 1000.0f, 120.0f, "Hz").mid (150.0f).rand (60.0f, 300.0f),
        P ("highcut", "High Cut", 1000.0f, 20000.0f, 7000.0f, "Hz").mid (5000.0f).rand (2500.0f, 9000.0f),
        P ("mod", "Modulation", 0.0f, 100.0f, 15.0f, "%").rand (0.0f, 40.0f),
        P ("duck", "Ducking", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 50.0f),
        P ("spread", "Spread", 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 100.0f) })
{
}

void DelayBlock::getTaps (std::vector<DelayTap>& taps) const
{
    taps.clear();
    const int mode = getChoice (pMode);
    const float t = get (pTime) * 0.001f, fb = juce::jmin (0.98f, get (pFeedback) * 0.01f);
    const float mix = juce::jmin (1.0f, get (pMix) * 0.02f);
    const float ratio = 1.0f - get (pSpread) * 0.005f;
    float g = mix;
    for (int k = 1; k <= 40 && g > 0.002f; ++k, g *= fb)
    {
        if (mode == 2)      taps.push_back ({ k * t, g, (k % 2) ? -get (pSpread) * 0.01f : get (pSpread) * 0.01f });
        else if (mode == 3) { taps.push_back ({ k * t, g, -1 }); taps.push_back ({ k * t * ratio, g, 1 }); }
        else                taps.push_back ({ k * t, g * (mode == 1 ? std::pow (0.9f, (float) k) : 1.0f), 0 });
        if (fb <= 0.0f) break;
    }
}

void DelayBlock::prepareImpl (double sr, int)
{
    for (auto& d : buf) d.prepare ((int) (sr * 2.2));
    for (auto* f : { &hp[0], &lp[0] }) f->reset();
    watch.invalidate();
    timeSmooth.setTime (sr, 120.0);
    timeSmooth.reset ((float) (get (pTime) * 0.001 * sr));
    duckDet.set (sr, 5.0, 250.0);
    wow.setRate (sr, 0.55); flutter.setRate (sr, 6.3);
}

std::vector<int> DelayBlock::panelOrder() const
{
    // Spread only means something with two different sides: Ping-Pong (how wide) and Dual (how far apart)
    const int mode = getChoice (pMode);
    std::vector<int> o { pMode, pTime, pFeedback, pMix, pLowCut, pHighCut, pMod, pDuck };
    if (mode >= 2) o.push_back (pSpread);
    return modelPanel (o);
}

void DelayBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pLowCut), get (pHighCut) }))
    {
        hp[0].setHighPass (sampleRate, get (pLowCut), 0.6);
        lp[0].setLowPass (sampleRate, get (pHighCut), 0.6);
    }
    const int mode = getChoice (pMode);
    const float sr = (float) sampleRate;
    const float target = get (pTime) * 0.001f * sr;
    const float fb = juce::jmin (0.98f, get (pFeedback) * 0.01f);
    float dg, wg;
    mixGains (get (pMix) * 0.01f, dg, wg);
    const float modAmt = get (pMod) * 0.01f;
    const float duck = get (pDuck) * 0.01f;
    const float spread = get (pSpread) * 0.01f;
    const float ratio = 1.0f - spread * 0.5f;
    const bool tape = mode == 1;
    // ping-pong: Spread is the width, from both sides together (0) to hard left / right (100)
    const float wide = 0.5f + 0.5f * spread, narrow = 1.0f - wide;

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float t = timeSmooth.lp (target);
        const float w = (float) wave (wow.tick(), 0), fl = (float) wave (flutter.tick(), 0);
        const float modS = tape ? modAmt * (0.004f * sr * w + 0.0004f * sr * fl) : modAmt * 0.0003f * sr * w;
        const float tl = t + modS, tr = (mode == 3 ? t * ratio : t) + modS;

        // what goes into the line is filtered, so the tone controls shape every repeat (the first too)
        const float rl = buf[0].read (tl), rr = buf[1].read (tr);
        const float dryL = l[i], dryR = r[i];
        auto voice = [&] (float x, int ch)
        {
            x = lp[0].process (hp[0].process (x, ch), ch);
            return tape ? fastTanh (x * 1.3f) / 1.3f : x;
        };
        float outL = rl, outR = rr;
        if (mode == 2)
        {
            // ping-pong: the input starts on the left, each repeat crosses to the other side
            buf[0].push (voice (0.5f * (dryL + dryR) + fb * rr, 0));
            buf[1].push (fb * rl);
            outL = wide * rl + narrow * rr;
            outR = wide * rr + narrow * rl;
        }
        else
        {
            buf[0].push (voice (dryL + fb * rl, 0));
            buf[1].push (voice (dryR + fb * rr, 1));
        }

        const float env = duckDet.process (0.5f * (dryL + dryR));
        const float duckG = 1.0f - duck * juce::jmin (1.0f, env * 6.0f);
        l[i] = dryL * dg + outL * wg * duckG;
        r[i] = dryR * dg + outR * wg * duckG;
    }
}

//==============================================================================
// Reverb: 8-line feedback delay network
//==============================================================================
ReverbBlock::ReverbBlock()
    : Block (NodeType::Reverb, {
        Choice ("type", "Type", { "Room", "Hall", "Plate", "Spring", "Shimmer" }, 1),
        P ("decay", "Decay", 0.2f, 20.0f, 2.5f, "s").mid (2.5f).dec (2).rand (0.6f, 6.0f),
        P ("predelay", "Pre-delay", 0.0f, 250.0f, 20.0f, "ms").mid (40.0f).rand (0.0f, 60.0f),
        P ("size", "Size", 0.0f, 100.0f, 60.0f, "%").rand (30.0f, 90.0f),
        P ("damping", "Damping", 0.0f, 100.0f, 45.0f, "%").rand (20.0f, 70.0f),
        P ("lowcut", "Low Cut", 20.0f, 1000.0f, 150.0f, "Hz").mid (150.0f).rand (80.0f, 300.0f),
        P ("mod", "Modulation", 0.0f, 100.0f, 30.0f, "%").rand (10.0f, 50.0f),
        P ("shimmer", "Shimmer", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 40.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 25.0f, "%").rand (12.0f, 40.0f) })
{
}

void ReverbBlock::getDecay (float& pre_, float& rt60, float& damping, float& size) const
{
    pre_ = get (pPredelay);
    rt60 = get (pDecay);
    damping = get (pDamping) * 0.01f;
    size = get (pSize) * 0.01f;
}

void ReverbBlock::updateLines()
{
    static const float baseMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.9f, 67.3f, 73.1f };
    static const float typeScale[] = { 0.45f, 1.5f, 0.9f, 0.6f, 1.8f };
    const int type = juce::jlimit (0, 4, getChoice (pType));
    const float scale = typeScale[type] * (0.5f + get (pSize) * 0.01f);
    const float sr = (float) sampleRate;
    const float rt = get (pDecay);
    const float dampHz = 16000.0f * std::pow (0.1f, get (pDamping) * 0.01f);
    for (int i = 0; i < N; ++i)
    {
        lengths[i] = baseMs[i] * scale * 0.001f * sr;
        gains[i] = std::pow (10.0f, -3.0f * lengths[i] / (rt * sr));
        damp[i].setCutoff (sampleRate, dampHz * (type == 2 ? 1.6f : 1.0f));
    }
    static const float diffMs[6] = { 4.77f, 3.59f, 12.73f, 9.3f, 1.1f, 1.7f };
    for (int d = 0; d < 6; ++d)
    {
        diff[d].length = juce::jmax (1, (int) (diffMs[d] * (type == 0 ? 0.6f : 1.0f) * 0.001f * sr));
        diff[d].g = type == 2 ? 0.72f : (type == 3 ? 0.78f : 0.62f);
    }
    lowCut.setCutoff (sampleRate, get (pLowCut));
}

void ReverbBlock::prepareImpl (double sr, int)
{
    pre.prepare ((int) (sr * 0.3));
    for (auto& l : lines) l.prepare ((int) (sr * 0.35));
    for (auto& d : diff) d.prepare ((int) (sr * 0.03));
    shimmer.prepare (sr, 60.0f);
    shimmer.ratio = 2.0f;
    for (int i = 0; i < N; ++i) { mod[i].setRate (sr, 0.3 + 0.17 * i); mod[i].phase = i / (double) N; damp[i].reset(); last[i] = 0; }
    lowCut.reset();
    shimmerFb = 0;
    watch.invalidate();
    updateLines();
}

void ReverbBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pType), get (pDecay), get (pSize), get (pDamping), get (pLowCut) }))
        updateLines();

    const int type = getChoice (pType);
    const float sr = (float) sampleRate;
    const int preSamples = (int) (get (pPredelay) * 0.001f * sr);
    const float modDepth = get (pMod) * 0.01f * 0.0012f * sr;
    const float shim = (get (pShimmer) * 0.01f) + (type == 4 ? 0.35f : 0.0f);
    const int numDiff = (type == 2 || type == 3) ? 6 : 4;
    float dg, wg;
    mixGains (get (pMix) * 0.01f, dg, wg);

    auto* L = b.getWritePointer (0);
    auto* R = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        pre.push (0.5f * (L[i] + R[i]));
        float x = lowCut.hp (pre.readInt (preSamples));
        for (int d = 0; d < numDiff; ++d) x = diff[d].process (x);
        x += shimmerFb * juce::jmin (0.6f, shim);

        float o[N], sum = 0;
        for (int k = 0; k < N; ++k)
        {
            const float m = k < 4 ? modDepth * wave (mod[k].tick(), 0) : 0.0f;
            o[k] = damp[k].lp (lines[k].read (lengths[k] + m)) * gains[k];
            sum += o[k];
        }
        const float h = sum * (2.0f / N);
        for (int k = 0; k < N; ++k)
            lines[k].push ((k & 1 ? -x : x) * 0.5f + (o[k] - h));

        const float wl = (o[0] - o[2] + o[4] - o[6] + 0.5f * (o[1] + o[5])) * 0.45f;
        const float wr = (o[1] - o[3] + o[5] - o[7] + 0.5f * (o[2] + o[6])) * 0.45f;
        shimmerFb = shim > 0.0f ? shimmer.process (0.5f * (wl + wr)) : 0.0f;

        L[i] = L[i] * dg + wl * wg;
        R[i] = R[i] * dg + wr * wg;
    }
}

//==============================================================================
// Pitch / octave / doubler
//==============================================================================
PitchBlock::PitchBlock()
    : Block (NodeType::Pitch, {
        Choice ("mode", "Mode", { "Octave Down", "Octave Up", "Pitch Shift", "Doubler" }, 0),
        P ("semis", "Semitones", -12.0f, 12.0f, 7.0f, "st").dec (0),
        P ("detune", "Detune", 0.0f, 50.0f, 12.0f, "ct").dec (0).rand (5.0f, 25.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%").rand (25.0f, 60.0f),
        P ("tone", "Tone", 1000.0f, 20000.0f, 9000.0f, "Hz").mid (5000.0f).rand (4000.0f, 12000.0f) })
{
}

juce::String PitchBlock::getDetail() const
{
    switch (getChoice (pMode))
    {
        case 2:  return formatValue (pSemis);
        case 3:  return "+/- " + formatValue (pDetune);
        default: return "Mix " + formatValue (pMix);
    }
}

std::vector<int> PitchBlock::panelOrder() const
{
    const int mode = getChoice (pMode);
    std::vector<int> o { pMode };
    if (mode == 2) o.push_back (pSemis);
    if (mode == 3) o.push_back (pDetune);
    o.push_back (pMix); o.push_back (pTone);
    return modelPanel (o);
}

void PitchBlock::prepareImpl (double sr, int)
{
    for (auto& v : voice) v.prepare (sr, 40.0f);
    for (auto& d : dbl) d.prepare ((int) (sr * 0.05));
    lastMode = -1;
}

void PitchBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const int mode = getChoice (pMode);
    const float sr = (float) sampleRate;
    if (mode != lastMode)
    {
        const float ms = mode == 0 ? 70.0f : (mode == 1 ? 30.0f : (mode == 2 ? 45.0f : 35.0f));
        voice[0].setWindow (sampleRate, ms);
        voice[1].setWindow (sampleRate, ms);
        lastMode = mode;
    }
    for (auto& t : toneLp) t.setCutoff (sampleRate, get (pTone));
    switch (mode)
    {
        case 0:  voice[0].ratio = 0.5f; break;
        case 1:  voice[0].ratio = 2.0f; break;
        case 2:  voice[0].ratio = std::pow (2.0f, get (pSemis) / 12.0f); break;
        default:
            voice[0].ratio = std::pow (2.0f, get (pDetune) / 1200.0f);
            voice[1].ratio = std::pow (2.0f, -get (pDetune) / 1200.0f);
            break;
    }
    float dg, wg;
    mixGains (get (pMix) * 0.01f, dg, wg);
    auto* L = b.getWritePointer (0);
    auto* R = b.getWritePointer (1);
    const float d1 = 0.011f * sr, d2 = 0.017f * sr;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        float wl, wr;
        if (mode == 3)
        {
            dbl[0].push (voice[0].process (m));
            dbl[1].push (voice[1].process (m));
            wl = dbl[0].read (d1); wr = dbl[1].read (d2);
        }
        else
        {
            wl = wr = voice[0].process (m);
        }
        L[i] = L[i] * dg + toneLp[0].lp (wl) * wg;
        R[i] = R[i] * dg + toneLp[1].lp (wr) * wg;
    }
}

} // namespace rig
