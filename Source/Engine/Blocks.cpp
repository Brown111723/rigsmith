#include "Blocks.h"

namespace rig
{
//==============================================================================
GateBlock::GateBlock()
    : Block (NodeType::Gate, {
        { "threshold", "Threshold", -90.0f, 0.0f, -58.0f, "dB" },
        { "release",   "Release",   5.0f, 500.0f, 90.0f, "ms", false, 80.0f, 0 },
        { "hold",      "Hold",      0.0f, 250.0f, 20.0f, "ms", false, 40.0f, 0 },
        { "range",     "Range",     -90.0f, 0.0f, -80.0f, "dB", false, -1.0f, 0 } })
{
}

juce::String GateBlock::getDetail() const { return formatValue (0); }

void GateBlock::prepareImpl (double, int) { env = 0; gain = 0; open = false; holdCounter = 0; }

void GateBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float thr = dbToGain (get (0));
    const float closeThr = thr * 0.5f;  // 6 dB hysteresis
    const float sr = (float) sampleRate;
    const float envRel = std::exp (-1.0f / (0.05f * sr));
    const float openCoef = std::exp (-1.0f / (0.0008f * sr));
    const float closeCoef = std::exp (-1.0f / (get (1) * 0.001f * sr));
    const int holdSamples = (int) (get (2) * 0.001f * sr);
    const float floorGain = dbToGain (get (3));

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    float minGain = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float x = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        env = x > env ? x : env * envRel + x * (1.0f - envRel);

        if (env > thr) { open = true; holdCounter = holdSamples; }
        else if (env < closeThr) { if (holdCounter > 0) --holdCounter; else open = false; }

        const float target = open ? 1.0f : floorGain;
        const float coef = target > gain ? openCoef : closeCoef;
        gain = target + (gain - target) * coef;

        l[i] *= gain;
        r[i] *= gain;
        minGain = juce::jmin (minGain, gain);
    }
    reductionDb.store (gainToDb (minGain));
}

//==============================================================================
EqBlock::EqBlock()
    : Block (NodeType::Eq, {
        { "hpf",     "Low Cut",   20.0f, 400.0f, 20.0f, "Hz", false, 90.0f, 0 },
        { "low",     "Bass",      -12.0f, 12.0f, 0.0f, "dB" },
        { "midfreq", "Mid Freq",  200.0f, 5000.0f, 800.0f, "Hz", false, 1000.0f, 0 },
        { "mid",     "Mid",       -12.0f, 12.0f, 0.0f, "dB" },
        { "midq",    "Mid Q",     0.3f, 4.0f, 0.9f, "", false, 1.0f, 2 },
        { "high",    "Treble",    -12.0f, 12.0f, 0.0f, "dB" },
        { "lpf",     "High Cut",  2000.0f, 20000.0f, 20000.0f, "Hz", false, 7000.0f, 0 } })
{
}

juce::String EqBlock::getDetail() const
{
    return "B " + juce::String (get (1), 1) + "  M " + juce::String (get (3), 1) + "  T " + juce::String (get (5), 1);
}

void EqBlock::prepareImpl (double, int)
{
    for (auto* f : { &hp, &low, &mid, &high, &lp }) f->reset();
    watch.invalidate();
}

void EqBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const double sr = sampleRate;
    if (watch.changed ({ get (0), get (1), get (2), get (3), get (4), get (5), get (6) }))
    {
        hp.setHighPass (sr, get (0));
        low.setLowShelf (sr, 110.0, get (1));
        mid.setPeak (sr, get (2), get (4), get (3));
        high.setHighShelf (sr, 3500.0, get (5));
        lp.setLowPass (sr, get (6));
    }
    const bool useHp = get (0) > 21.0f, useLp = get (6) < 19900.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            if (useHp) x = hp.process (x, ch);
            x = low.process (x, ch);
            x = mid.process (x, ch);
            x = high.process (x, ch);
            if (useLp) x = lp.process (x, ch);
            d[i] = x;
        }
    }
}

//==============================================================================
GainBlock::GainBlock()
    : Block (NodeType::Gain, {
        { "gain", "Gain", -40.0f, 24.0f, 0.0f, "dB" },
        { "pan",  "Pan",  -1.0f, 1.0f, 0.0f, "", false, -1.0f, 2 } })
{
}

juce::String GainBlock::getDetail() const { return formatValue (0); }

void GainBlock::prepareImpl (double sr, int)
{
    gl.reset (sr, 0.02);
    gr.reset (sr, 0.02);
}

void GainBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float g = dbToGain (get (0)), p = get (1);
    gl.setTargetValue (g * juce::jmin (1.0f, 1.0f - p));
    gr.setTargetValue (g * juce::jmin (1.0f, 1.0f + p));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        l[i] *= gl.getNextValue();
        r[i] *= gr.getNextValue();
    }
}

//==============================================================================
CompBlock::CompBlock()
    : Block (NodeType::Comp, {
        { "threshold", "Threshold", -60.0f, 0.0f, -24.0f, "dB" },
        { "ratio",     "Ratio",     1.0f, 20.0f, 4.0f, ":1", false, 4.0f, 1 },
        { "attack",    "Attack",    0.1f, 100.0f, 8.0f, "ms", false, 10.0f, 1 },
        { "release",   "Release",   10.0f, 1000.0f, 150.0f, "ms", false, 150.0f, 0 },
        { "makeup",    "Makeup",    0.0f, 24.0f, 6.0f, "dB" },
        { "mix",       "Blend",     0.0f, 100.0f, 100.0f, "%", false, -1.0f, 0 } })
{
}

juce::String CompBlock::getDetail() const { return formatValue (1) + " @ " + formatValue (0); }

void CompBlock::prepareImpl (double, int block)
{
    env = 0;
    dryBuf.setSize (2, block, false, true, true);
}

void CompBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const float sr = (float) sampleRate;
    const float thr = get (0), ratio = get (1);
    const float att = std::exp (-1.0f / (get (2) * 0.001f * sr));
    const float rel = std::exp (-1.0f / (get (3) * 0.001f * sr));
    const float makeup = dbToGain (get (4));
    const float wet = get (5) * 0.01f;

    for (int ch = 0; ch < 2; ++ch)
        dryBuf.copyFrom (ch, 0, b, ch, 0, n);

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    auto* dl = dryBuf.getReadPointer (0);
    auto* dr = dryBuf.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float x = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        env = x > env ? att * env + (1 - att) * x : rel * env + (1 - rel) * x;
        const float over = gainToDb (env) - thr;
        const float g = over > 0 ? dbToGain (-over * (1.0f - 1.0f / ratio)) * makeup : makeup;
        l[i] = dl[i] + (l[i] * g - dl[i]) * wet;
        r[i] = dr[i] + (r[i] * g - dr[i]) * wet;
    }
}

//==============================================================================
IrBlock::IrBlock()
    : Block (NodeType::Ir, {
        { "lowcut",  "Low Cut",  20.0f, 400.0f, 75.0f, "Hz", false, 90.0f, 0 },
        { "highcut", "High Cut", 2000.0f, 20000.0f, 8000.0f, "Hz", false, 7000.0f, 0 },
        { "mix",     "Mix",      0.0f, 100.0f, 100.0f, "%", false, -1.0f, 0 },
        { "level",   "Level",    -24.0f, 12.0f, 0.0f, "dB" } }),
      conv (juce::dsp::Convolution::NonUniform { 0 })
{
}

juce::String IrBlock::getName() const
{
    return file.existsAsFile() ? file.getFileNameWithoutExtension() : juce::String ("Cab IR");
}

juce::String IrBlock::getDetail() const
{
    return file.existsAsFile() ? "Lo " + formatValue (0) + " - Hi " + formatValue (1) : juce::String ("Double-click to load .wav");
}

int IrBlock::getIrLength() const { return conv.getCurrentIRSize(); }

bool IrBlock::loadIr (const juce::File& f, juce::String& error)
{
    if (! f.existsAsFile())
    {
        error = "IR file not found: " + f.getFullPathName();
        return false;
    }
    // Convolution loads on a background thread and swaps in click-free.
    conv.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                              juce::dsp::Convolution::Normalise::yes);
    file = f;
    hasIr = true;
    return true;
}

juce::ValueTree IrBlock::toState() const
{
    auto v = Block::toState();
    v.setProperty ("file", file.getFullPathName(), nullptr);
    return v;
}

void IrBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    const juce::String path = v.getProperty ("file").toString();
    if (path.isNotEmpty())
    {
        juce::String err;
        loadIr (juce::File (path), err);
    }
}

void IrBlock::prepareImpl (double sr, int block)
{
    conv.prepare ({ sr, (juce::uint32) block, 2 });
    dryBuf.setSize (2, block, false, true, true);
    hp.reset(); lp.reset();
    watch.invalidate();
    level.reset (sr, 0.02);
}

void IrBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (0), get (1) }))
    {
        hp.setHighPass (sampleRate, get (0));
        lp.setLowPass (sampleRate, get (1));
    }

    const float wet = get (2) * 0.01f;
    level.setTargetValue (dbToGain (get (3)));

    for (int ch = 0; ch < 2; ++ch)
        dryBuf.copyFrom (ch, 0, b, ch, 0, n);

    if (hasIr.load())
    {
        juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    auto* dl = dryBuf.getReadPointer (0);
    auto* dr = dryBuf.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float wl = lp.process (hp.process (l[i], 0), 0);
        const float wr = lp.process (hp.process (r[i], 1), 1);
        const float g = level.getNextValue();
        l[i] = (dl[i] + (wl - dl[i]) * wet) * g;
        r[i] = (dr[i] + (wr - dr[i]) * wet) * g;
    }
}

} // namespace rig
