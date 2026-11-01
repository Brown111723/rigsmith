#include "Blocks.h"

namespace rig
{
//==============================================================================
GateBlock::GateBlock()
    : Block (NodeType::Gate, {
        P ("threshold", "Threshold", -90.0f, 0.0f, -58.0f, "dB").rand (-72.0f, -48.0f),
        P ("release", "Release", 5.0f, 500.0f, 90.0f, "ms").mid (80.0f).rand (30.0f, 200.0f),
        P ("hold", "Hold", 0.0f, 250.0f, 20.0f, "ms").mid (40.0f).rand (5.0f, 60.0f),
        P ("range", "Range", -90.0f, 0.0f, -80.0f, "dB").rand (-90.0f, -55.0f) })
{
}

float GateBlock::transferDb (float in) const
{
    return in >= get (0) ? in : in + get (3);
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
    float inPk = 0;
    for (int i = 0; i < n; ++i) inPk = juce::jmax (inPk, std::abs (l[i]), std::abs (r[i]));

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
    updateMeters (inPk, inPk * minGain, gainToDb (minGain));
}

//==============================================================================
EqBlock::EqBlock()
    : Block (NodeType::Eq, {
        P ("hpf", "Low Cut", 20.0f, 400.0f, 20.0f, "Hz").mid (80.0f).rand (20.0f, 140.0f),
        P ("lowfreq", "Bass Freq", 40.0f, 500.0f, 110.0f, "Hz").mid (120.0f).rand (70.0f, 250.0f),
        P ("low", "Bass", -15.0f, 15.0f, 0.0f, "dB").rand (-6.0f, 6.0f),
        P ("midfreq", "Mid Freq", 150.0f, 6000.0f, 800.0f, "Hz").mid (900.0f).rand (300.0f, 3000.0f),
        P ("mid", "Mid", -15.0f, 15.0f, 0.0f, "dB").rand (-6.0f, 6.0f),
        P ("midq", "Mid Q", 0.3f, 6.0f, 0.9f).mid (1.0f).dec (2).rand (0.5f, 2.0f),
        P ("highfreq", "Treble Freq", 1000.0f, 12000.0f, 3500.0f, "Hz").mid (3500.0f).rand (2000.0f, 8000.0f),
        P ("high", "Treble", -15.0f, 15.0f, 0.0f, "dB").rand (-6.0f, 5.0f),
        P ("lpf", "High Cut", 2000.0f, 20000.0f, 20000.0f, "Hz").mid (7000.0f).rand (5000.0f, 20000.0f) })
{
}

juce::String EqBlock::getDetail() const
{
    return "B " + juce::String (get (pLow), 1) + "  M " + juce::String (get (pMid), 1) + "  T " + juce::String (get (pHigh), 1);
}

void EqBlock::design (Biquad& h, Biquad& lo, Biquad& m, Biquad& hi, Biquad& l, double sr) const
{
    h.setHighPass (sr, get (pHpf));
    lo.setLowShelf (sr, get (pLowFreq), get (pLow));
    m.setPeak (sr, get (pMidFreq), get (pMidQ), get (pMid));
    hi.setHighShelf (sr, get (pHighFreq), get (pHigh));
    l.setLowPass (sr, get (pLpf));
}

float EqBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Biquad h, lo, m, hi, l;
    design (h, lo, m, hi, l, sr);
    double db = lo.magnitudeDb (hz, sr) + m.magnitudeDb (hz, sr) + hi.magnitudeDb (hz, sr);
    if (get (pHpf) > 21.0f) db += h.magnitudeDb (hz, sr);
    if (get (pLpf) < 19900.0f) db += l.magnitudeDb (hz, sr);
    return (float) db;
}

void EqBlock::prepareImpl (double, int)
{
    for (auto* f : { &hp, &low, &mid, &high, &lp }) f->reset();
    watch.invalidate();
    watch2.invalidate();
}

void EqBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (0), get (1), get (2), get (3), get (4), get (5), get (6), get (7) }) | watch2.changed ({ get (8) }))
        design (hp, low, mid, high, lp, sampleRate);
    const bool useHp = get (pHpf) > 21.0f, useLp = get (pLpf) < 19900.0f;

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
        P ("gain", "Gain", -40.0f, 24.0f, 0.0f, "dB").fixed(),
        P ("pan", "Pan", -1.0f, 1.0f, 0.0f).dec (2).fixed() })
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
IrBlock::IrBlock()
    : Block (NodeType::Ir, {
        P ("lowcut", "Low Cut", 20.0f, 400.0f, 75.0f, "Hz").mid (90.0f).rand (40.0f, 140.0f),
        P ("highcut", "High Cut", 2000.0f, 20000.0f, 8000.0f, "Hz").mid (7000.0f).rand (5000.0f, 13000.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").fixed(),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB") }),
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

float IrBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Biquad h, l;
    h.setHighPass (sr, get (0));
    l.setLowPass (sr, get (1));
    return (float) (h.magnitudeDb (hz, sr) + l.magnitudeDb (hz, sr) + get (3));
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
    touched();
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

//==============================================================================
TunerBlock::TunerBlock()
    : Block (NodeType::Tuner, {
        Toggle ("mute", "Mute output", false).fixed(),
        P ("reference", "Reference A", 430.0f, 450.0f, 440.0f, "Hz").fixed() })
{
}

juce::String TunerBlock::getDetail() const
{
    if (! reading.valid) return "A = " + formatValue (pReference);
    return reading.note + juce::String (reading.octave) + "  " + (reading.cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (reading.cents)) + " c";
}

const Tuner::Reading& TunerBlock::getReading()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (bypassed.load() || now - lastAudio.load() > 400)
    {
        reading = {};
        return reading;
    }
    if (now - lastAnalysis >= 40)
    {
        lastAnalysis = now;
        tuner.referenceA = get (pReference);
        reading = tuner.analyse();
    }
    return reading;
}

void TunerBlock::prepareImpl (double sr, int)
{
    tuner.prepare (sr);
    muteGain.reset (sr, 0.01);
    muteGain.setCurrentAndTargetValue (get (pMute) > 0.5f ? 0.0f : 1.0f);
}

void TunerBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    float pk = 0;
    for (int off = 0; off < n; off += 512)
    {
        const int len = juce::jmin (512, n - off);
        for (int i = 0; i < len; ++i)
        {
            scratch[i] = 0.5f * (l[off + i] + r[off + i]);
            pk = juce::jmax (pk, std::abs (scratch[i]));
        }
        tuner.push (scratch, len);
    }
    lastAudio.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    muteGain.setTargetValue (get (pMute) > 0.5f ? 0.0f : 1.0f);
    if (muteGain.isSmoothing() || muteGain.getCurrentValue() < 0.999f)
        for (int i = 0; i < n; ++i)
        {
            const float g = muteGain.getNextValue();
            l[i] *= g; r[i] *= g;
        }
    updateMeters (pk, pk * muteGain.getCurrentValue(), 0.0f);
}

} // namespace rig
