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
struct IrBlock::Bank
{
    std::vector<std::unique_ptr<juce::dsp::Convolution>> convs;
};

IrBlock::IrBlock()
    : Block (NodeType::Ir, {
        P ("lowcut", "Low Cut", 20.0f, 400.0f, 75.0f, "Hz").mid (90.0f).rand (40.0f, 140.0f),
        P ("highcut", "High Cut", 2000.0f, 20000.0f, 8000.0f, "Hz").mid (7000.0f).rand (5000.0f, 13000.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").fixed(),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB"),
        P ("capture", "Capture", 0.0f, 1.0f, 0.0f).dec (2).fixed() }),
      loader (std::make_unique<juce::dsp::ConvolutionMessageQueue>()),
      bank (std::make_unique<Bank>())
{
}

IrBlock::~IrBlock()
{
    // the convolvers use the loader: let them go first
    const juce::SpinLock::ScopedLockType sl (bankLock);
    bank.reset();
}

juce::File IrBlock::getFile() const
{
    return files.isEmpty() ? juce::File() : files[getNearestCapture()];
}

juce::StringArray IrBlock::getCaptureNames() const
{
    juce::StringArray names;
    for (auto& f : files) names.add (f.getFileNameWithoutExtension());
    return names;
}

float IrBlock::getCapturePosition() const noexcept
{
    const int n = files.size();
    return n > 1 ? get (pCapture) * (float) (n - 1) : 0.0f;
}

void IrBlock::setCapturePosition (float pos) noexcept
{
    const int n = files.size();
    set (pCapture, n > 1 ? juce::jlimit (0.0f, 1.0f, pos / (float) (n - 1)) : 0.0f);
}

int IrBlock::getNearestCapture() const noexcept
{
    return juce::jlimit (0, juce::jmax (0, files.size() - 1), juce::roundToInt (getCapturePosition()));
}

juce::String IrBlock::getName() const
{
    const auto file = getFile();
    if (! file.existsAsFile()) return "Cab IR";
    const auto tone = getToneInfo();
    return tone.valid && tone.title.isNotEmpty() ? tone.title : file.getFileNameWithoutExtension();
}

juce::String IrBlock::getDetail() const
{
    if (autoSkipped.load()) return "Off: the amp includes a cab";
    if (! getFile().existsAsFile()) return "Double-click to load .wav";
    const auto tone = getToneInfo();
    if (tone.valid && tone.creator.isNotEmpty()) return "by " + tone.creator;
    return "Lo " + formatValue (0) + " - Hi " + formatValue (1);
}

juce::String IrBlock::formatValue (int i, float v) const
{
    if (i == pCapture)
    {
        const int n = files.size();
        if (n == 0) return "-";
        const auto names = getCaptureNames();
        const float pos = n > 1 ? v * (float) (n - 1) : 0.0f;
        const int a = juce::jlimit (0, n - 1, (int) std::floor (pos));
        const float f = pos - (float) a;
        if (f < 0.02f || a == n - 1) return names[a];
        if (f > 0.98f) return names[a + 1];
        return names[a] + " > " + names[a + 1] + " " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
    }
    return Block::formatValue (i, v);
}

void IrBlock::randomise (juce::Random& rng, float amount)
{
    Block::randomise (rng, amount);
    const int n = files.size();
    if (n > 1 && amount >= 0.999f)
    {
        float pos = (float) rng.nextInt (n);
        if (rng.nextFloat() < 0.3f) pos = juce::jmin ((float) (n - 1), pos + rng.nextFloat());
        setCapturePosition (pos);
    }
}

float IrBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Biquad h, l;
    h.setHighPass (sr, get (0));
    l.setLowPass (sr, get (1));
    return (float) (h.magnitudeDb (hz, sr) + l.magnitudeDb (hz, sr) + get (3));
}

int IrBlock::getIrLength() const
{
    const juce::SpinLock::ScopedLockType sl (bankLock);
    const int i = getNearestCapture();
    return juce::isPositiveAndBelow (i, (int) bank->convs.size()) ? bank->convs[(size_t) i]->getCurrentIRSize() : 0;
}

bool IrBlock::loadIr (const juce::File& f, juce::String& error)
{
    return loadCaptures (juce::Array<juce::File> { f }, 0.0f, error);
}

bool IrBlock::loadCaptures (const juce::Array<juce::File>& wanted, float pos, juce::String& error)
{
    juce::Array<juce::File> list;
    for (auto& f : wanted)
        if (f.existsAsFile() && ! list.contains (f)) list.add (f);
    if (list.isEmpty())
    {
        error = wanted.isEmpty() ? juce::String ("No IRs to load") : "IR file not found: " + wanted[0].getFullPathName();
        return false;
    }

    // IRs already loaded keep their convolver; new ones load in the background and fade in
    auto fresh = std::make_unique<Bank>();
    std::vector<std::unique_ptr<juce::dsp::Convolution>> made ((size_t) list.size());
    std::vector<int> reuse ((size_t) list.size(), -1);
    for (int i = 0; i < list.size(); ++i)
    {
        reuse[(size_t) i] = files.indexOf (list[i]);
        if (reuse[(size_t) i] >= 0) continue;
        auto c = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::NonUniform { 0 }, *loader);
        c->prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
        c->loadImpulseResponse (list[i], juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                                juce::dsp::Convolution::Normalise::yes);
        made[(size_t) i] = std::move (c);
    }
    {
        const juce::SpinLock::ScopedLockType sl (bankLock);
        for (int i = 0; i < list.size(); ++i)
            fresh->convs.push_back (reuse[(size_t) i] >= 0 ? std::move (bank->convs[(size_t) reuse[(size_t) i]]) : std::move (made[(size_t) i]));
        std::swap (bank, fresh);
    }
    fresh.reset();   // convolvers no longer used, freed off the audio thread

    files = list;
    setCapturePosition (pos);
    gearKind = (int) classifyFile (files[0]);
    touched();
    return true;
}

juce::ValueTree IrBlock::toState() const
{
    auto v = Block::toState();
    v.setProperty ("file", getFile().getFullPathName(), nullptr);
    v.setProperty ("skipAfterFullRig", skipAfterFullRig.load(), nullptr);
    if (files.size() > 1)
    {
        juce::ValueTree list ("Captures");
        for (auto& f : files)
        {
            juce::ValueTree c ("Capture");
            c.setProperty ("file", f.getFullPathName(), nullptr);
            list.appendChild (c, nullptr);
        }
        v.appendChild (list, nullptr);
    }
    return v;
}

void IrBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    skipAfterFullRig = (bool) v.getProperty ("skipAfterFullRig", true);
    juce::Array<juce::File> list;
    for (auto c : v.getChildWithName ("Captures"))
        if (auto path = c.getProperty ("file").toString(); path.isNotEmpty()) list.add (juce::File (path));
    const juce::String path = v.getProperty ("file").toString();
    if (list.isEmpty() && path.isNotEmpty()) list.add (juce::File (path));
    if (list.isEmpty()) return;
    const float pos = list.size() > 1 ? (float) v.getProperty ("p_capture", 0.0f) * (float) (list.size() - 1) : 0.0f;
    juce::String err;
    loadCaptures (list, pos, err);
}

void IrBlock::prepareImpl (double sr, int block)
{
    {
        const juce::SpinLock::ScopedLockType sl (bankLock);
        for (auto& c : bank->convs) c->prepare ({ sr, (juce::uint32) block, 2 });
    }
    dryBuf.setSize (2, block, false, true, true);
    scratch.setSize (2, block, false, true, true);
    positions.assign ((size_t) block, 0.0f);
    hp.reset(); lp.reset();
    watch.invalidate();
    level.reset (sr, 0.02);
    position.reset (sr, 0.06);
    position.setCurrentAndTargetValue (get (pCapture));
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

    {
        const juce::SpinLock::ScopedTryLockType tl (bankLock);
        if (tl.isLocked() && ! bank->convs.empty())
        {
            // the IRs either side of the Capture position, blended sample by sample
            const int count = (int) bank->convs.size();
            const float span = (float) juce::jmax (0, count - 1);
            position.setTargetValue (get (pCapture));
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = 0; i < n; ++i)
            {
                const float p = position.getNextValue() * span;
                positions[(size_t) i] = p;
                lo = juce::jmin (lo, p);
                hi = juce::jmax (hi, p);
            }
            const int first = juce::jlimit (0, count - 1, (int) std::floor (lo));
            const int last = juce::jlimit (0, count - 1, (int) std::ceil (hi));

            if (first == last)
            {
                juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), 2, (size_t) n);
                bank->convs[(size_t) first]->process (juce::dsp::ProcessContextReplacing<float> (block));
            }
            else
            {
                b.clear (0, n);
                for (int k = first; k <= last; ++k)
                {
                    for (int ch = 0; ch < 2; ++ch) scratch.copyFrom (ch, 0, dryBuf, ch, 0, n);
                    juce::dsp::AudioBlock<float> block (scratch.getArrayOfWritePointers(), 2, (size_t) n);
                    bank->convs[(size_t) k]->process (juce::dsp::ProcessContextReplacing<float> (block));
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* out = b.getWritePointer (ch);
                        auto* y = scratch.getReadPointer (ch);
                        for (int i = 0; i < n; ++i)
                        {
                            const float w = 1.0f - std::abs (positions[(size_t) i] - (float) k);
                            if (w > 0.0f) out[i] += y[i] * w;
                        }
                    }
                }
            }
        }
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
