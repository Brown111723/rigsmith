#include "Voice.h"

namespace rig
{
namespace
{
inline float peak2 (const float* l, const float* r, int n) noexcept
{
    float p = 0;
    for (int i = 0; i < n; ++i) p = juce::jmax (p, std::abs (l[i]), std::abs (r[i]));
    return p;
}

inline float softSat (float x, float amount) noexcept
{
    // a tube-ish stage: mostly clean, a little second harmonic, rounding at the top
    if (amount <= 0.0f) return x;
    const float d = 1.0f + amount * 3.0f;
    const float y = dsp::fastTanh (x * d + amount * 0.15f * x * x) / d;
    return y;
}

double magOf (const Biquad& b, double hz, double sr) { return b.magnitudeDb (hz, sr); }
}

//==============================================================================
// Pitch tracking (YIN)
//==============================================================================
void PitchTracker::prepare (double sr)
{
    decim = juce::jmax (1, (int) std::round (sr / 12000.0));
    rate = sr / decim;
    ring.assign ((size_t) (W + maxTau) * 2, 0.0f);
    frame.assign ((size_t) (W + maxTau), 0.0f);
    diff.assign ((size_t) maxTau + 1, 0.0f);
    write = 0; count = 0; sinceHop = 0; acc = 0; lp = 0;
    lpA = (float) std::exp (-juce::MathConstants<double>::twoPi * 1500.0 / sr);
    hz = 0; confidence = 0;
}

bool PitchTracker::push (float x) noexcept
{
    lp = x + lpA * (lp - x);
    acc += lp;
    if (++count < decim) return false;
    const float v = acc / (float) decim;
    acc = 0; count = 0;
    const int n = (int) ring.size();
    ring[(size_t) write] = v;
    write = (write + 1) % n;
    if (++sinceHop < hop) return false;
    sinceHop = 0;
    analyse();
    return true;
}

void PitchTracker::analyse() noexcept
{
    const int n = (int) ring.size(), len = W + maxTau;
    for (int i = 0; i < len; ++i) frame[(size_t) i] = ring[(size_t) ((write - len + i + n) % n)];
    float energy = 0;
    for (int i = 0; i < W; ++i) energy += frame[(size_t) i] * frame[(size_t) i];
    if (energy / W < 1.0e-7f) { hz = 0; confidence = 0; return; }

    const int minTau = juce::jmax (2, (int) (rate / 1100.0));
    const int topTau = juce::jmin (maxTau, (int) (rate / 60.0));
    float running = 0;
    diff[0] = 1.0f;
    for (int tau = 1; tau <= topTau; ++tau)
    {
        float d = 0;
        for (int j = 0; j < W; ++j) { const float e = frame[(size_t) j] - frame[(size_t) (j + tau)]; d += e * e; }
        running += d;
        diff[(size_t) tau] = running > 0 ? d * (float) tau / running : 1.0f;
    }
    int best = -1;
    for (int tau = minTau; tau < topTau; ++tau)
        if (diff[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < topTau && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0) { hz = 0; confidence = 0; return; }
    // parabolic refinement
    const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) juce::jmin (topTau, best + 1)];
    const float den = a - 2.0f * b + c;
    const float t = (float) best + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
    hz = (float) (rate / t);
    confidence = juce::jlimit (0.0f, 1.0f, 1.0f - b);
}

//==============================================================================
// Scales
//==============================================================================
juce::StringArray keyNames() { return { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; }
juce::StringArray scaleNames() { return { "Chromatic", "Major", "Minor", "Major pentatonic", "Minor pentatonic", "Blues", "Harmonic minor", "Dorian", "Mixolydian" }; }

namespace
{
const std::vector<int>& scaleSteps (int scale)
{
    static const std::vector<int> s[] {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        { 0, 2, 4, 5, 7, 9, 11 },
        { 0, 2, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 7, 9 },
        { 0, 3, 5, 7, 10 },
        { 0, 3, 5, 6, 7, 10 },
        { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 2, 3, 5, 7, 9, 10 },
        { 0, 2, 4, 5, 7, 9, 10 },
    };
    return s[juce::jlimit (0, 8, scale)];
}

/** Every note of the scale as (octave * size + degree) <-> midi. */
int degreeOf (int midi, int key, int scale)
{
    const auto& st = scaleSteps (scale);
    const int rel = midi - key;
    const int oct = (int) std::floor (rel / 12.0);
    const int pc = rel - oct * 12;
    int deg = 0;
    for (int k = 0; k < (int) st.size(); ++k) if (st[(size_t) k] <= pc) deg = k;
    return oct * (int) st.size() + deg;
}

int midiOfDegree (int degree, int key, int scale)
{
    const auto& st = scaleSteps (scale);
    const int size = (int) st.size();
    const int oct = (int) std::floor ((double) degree / size);
    const int deg = degree - oct * size;
    return key + oct * 12 + st[(size_t) deg];
}
}

float nearestInScale (float midi, int key, int scale) noexcept
{
    const auto& st = scaleSteps (scale);
    float best = midi, bestD = 1.0e9f;
    const int base = (int) std::floor (midi) - 12;
    for (int m = base; m <= base + 24; ++m)
    {
        const int pc = ((m - key) % 12 + 12) % 12;
        if (std::find (st.begin(), st.end(), pc) == st.end()) continue;
        const float d = std::abs ((float) m - midi);
        if (d < bestD) { bestD = d; best = (float) m; }
    }
    return best;
}

float scaleStep (float midiInScale, int key, int scale, int steps) noexcept
{
    const int m = juce::roundToInt (midiInScale);
    if (scale == 0)
    {
        // chromatic: fixed intervals (a major third, a fourth...)
        static const int semis[] { 0, 2, 4, 5, 7, 9, 11, 12 };
        const int a = std::abs (steps);
        const int s = a >= 7 ? 12 * (a / 7) + semis[a % 7] : semis[a];
        return (float) (m + (steps < 0 ? -s : s));
    }
    return (float) midiOfDegree (degreeOf (m, key, scale) + steps, key, scale);
}

//==============================================================================
// Acoustic Sim
//==============================================================================
namespace
{
const AcousticBodyBlock::Shape shapes[] {
    //  name               resonances (Hz)                       gains (dB)                 Q                       scoop      top  lp     octave
    { "Dreadnought",    { 98, 190, 390, 1200, 2600 },  { 8, 6, 3, 2, 3 },   { 4, 5, 6, 4, 3 },   650, -6, 7, 16000, false },
    { "Jumbo",          { 85, 165, 320, 1000, 2400 },  { 8, 6, 4, 2, 3 },   { 4, 5, 6, 4, 3 },   600, -5, 6, 15000, false },
    { "Small body (OM)",{ 120, 230, 450, 1500, 3000 }, { 5, 6, 4, 3, 3 },   { 4, 5, 6, 4, 3 },   800, -4, 6, 17000, false },
    { "Parlour",        { 140, 260, 520, 1700, 3200 }, { 4, 5, 5, 3, 2 },   { 3, 5, 6, 4, 3 },   900, -3, 5, 14000, false },
    { "Classical (nylon)", { 95, 200, 400, 900, 2000 },{ 6, 6, 4, 2, 1 },   { 4, 5, 5, 4, 3 },   700, -3, 2, 7000,  false },
    { "12-string",      { 98, 190, 390, 1200, 2600 },  { 7, 5, 3, 2, 3 },   { 4, 5, 6, 4, 3 },   650, -6, 8, 17000, true  },
    { "Resonator",      { 110, 600, 1100, 2200, 3500 },{ 3, 4, 6, 5, 4 },   { 3, 2, 3, 3, 3 },   0,    0, 2, 9000,  false },
};
}

const AcousticBodyBlock::Shape& AcousticBodyBlock::shape (int i) { return shapes[juce::jlimit (0, (int) std::size (shapes) - 1, i)]; }
int AcousticBodyBlock::numShapes() { return (int) std::size (shapes); }

static juce::StringArray shapeNames()
{
    juce::StringArray s;
    for (int i = 0; i < AcousticBodyBlock::numShapes(); ++i) s.add (AcousticBodyBlock::shape (i).name);
    return s;
}

AcousticBodyBlock::AcousticBodyBlock()
    : Block (NodeType::AcousticBody, {
        Choice ("body", "Body", shapeNames(), 0),
        P ("resonance", "Body Resonance", 0.0f, 100.0f, 60.0f, "%").rand (30.0f, 85.0f),
        P ("top", "Top Sparkle", 0.0f, 10.0f, 5.0f).rand (3.0f, 8.0f),
        P ("width", "Width", 0.0f, 100.0f, 40.0f, "%").rand (0.0f, 80.0f),
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").rand (70.0f, 100.0f),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB") })
{
}

void AcousticBodyBlock::prepareImpl (double sr, int)
{
    for (auto& side : res) for (auto& b : side) b.reset();
    for (auto* b : { &scoop, &top, &pickup, &lowCut, &lp }) b->reset();
    for (auto& e : exciteHp) { e.reset(); e.setCutoff (sr, 4500.0); }
    octave.prepare (sr, 40.0f);
    octave.ratio = 2.0f;
    octDelay.prepare ((int) (sr * 0.03));
    watch.invalidate();
}

void AcousticBodyBlock::design() noexcept
{
    const auto& s = shape (getChoice (pBody));
    const double sr = sampleRate;
    const float amount = get (pResonance) / 60.0f;
    const float spread = get (pWidth) * 0.0004f;   // up to +-4 % between the sides: a wider, roomier body
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < 5; ++k)
            res[side][k].setPeak (sr, s.f[k] * (1.0f + (side == 0 ? -spread : spread) * (float) (k + 1)), s.q[k], s.g[k] * amount);
    if (s.scoopHz > 0) scoop.setPeak (sr, s.scoopHz, 0.8, s.scoopDb); else scoop.setIdentity();
    top.setHighShelf (sr, 5500.0, s.topDb * get (pTop) / 5.0f);
    pickup.setPeak (sr, 2600.0, 1.2, -4.0);          // a magnetic pickup's resonant hump, taken down
    lowCut.setHighPass (sr, 70.0, 0.7);
    lp.setLowPass (sr, juce::jmin ((double) s.lpHz, sr * 0.45), 0.7);
    sparkle = get (pTop) * 0.05f;

    // level: the body's boost over a guitar's spectrum, made up so the knobs change the tone, not the volume
    double sum = 0, wsum = 0;
    for (double f = 80.0; f < 6000.0; f *= 1.12)
    {
        const double db = responseDb (f);
        const double w = 1.0 / std::sqrt (f);
        sum += std::pow (10.0, db / 10.0) * w; wsum += w;
    }
    makeup = (float) (1.0 / std::sqrt (sum / wsum));
}

float AcousticBodyBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    double db = magOf (scoop, hz, sr) + magOf (top, hz, sr) + magOf (pickup, hz, sr) + magOf (lowCut, hz, sr) + magOf (lp, hz, sr);
    for (int k = 0; k < 5; ++k) db += magOf (res[0][k], hz, sr);
    return (float) db;
}

void AcousticBodyBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ (float) getChoice (pBody), get (pResonance), get (pTop), get (pWidth) })) design();
    const auto& s = shape (getChoice (pBody));
    float dg, wg;
    {
        const float m = get (pMix) * 0.01f;
        dg = 1.0f - m; wg = m;
    }
    const float level = dbToGain (get (pLevel));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    const float octD = (float) (sampleRate * 0.008);
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        float base = lowCut.process (pickup.process (m, 0), 0);
        if (s.octave)
        {
            // the octave strings: an octave up, a touch late and quieter, as the second string of each pair
            octDelay.push (octave.process (base));
            base += 0.35f * octDelay.read (octD);
        }
        float y[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float v = base;
            for (int k = 0; k < 5; ++k) v = res[ch][k].process (v, ch);
            v = scoop.process (v, ch);
            v = top.process (v, ch);
            // a little harmonic sparkle on the top strings, where a pickup is dull
            const float hi = exciteHp[ch].hp (v);
            v += sparkle * dsp::fastTanh (hi * 4.0f) * 0.25f;
            y[ch] = lp.process (v, ch) * makeup;
        }
        l[i] = (l[i] * dg + y[0] * wg) * level;
        r[i] = (r[i] * dg + y[1] * wg) * level;
    }
    updateMeters (inPk, peak2 (l, r, n), 0.0f);
}

//==============================================================================
// Acoustic Preamp
//==============================================================================
AcousticPreBlock::AcousticPreBlock()
    : Block (NodeType::AcousticPre, {
        P ("gain", "Gain", -12.0f, 18.0f, 0.0f, "dB"),
        P ("lowcut", "Low Cut", 20.0f, 300.0f, 70.0f, "Hz").mid (80.0f).rand (40.0f, 120.0f),
        P ("bass", "Bass", -12.0f, 12.0f, 0.0f, "dB").rand (-4.0f, 4.0f),
        P ("mid", "Mid", -12.0f, 12.0f, -2.0f, "dB").rand (-6.0f, 3.0f),
        P ("midfreq", "Mid Freq", 200.0f, 5000.0f, 800.0f, "Hz").mid (900.0f).rand (400.0f, 2000.0f),
        P ("treble", "Treble", -12.0f, 12.0f, 1.0f, "dB").rand (-3.0f, 5.0f),
        P ("presence", "Presence", -12.0f, 12.0f, 0.0f, "dB").rand (-3.0f, 4.0f),
        P ("notch", "Notch", 40.0f, 2000.0f, 120.0f, "Hz").mid (200.0f).rand (80.0f, 300.0f),
        P ("notchdepth", "Notch Depth", -30.0f, 0.0f, 0.0f, "dB").rand (-12.0f, 0.0f),
        P ("bodyimage", "Body Image", 0.0f, 100.0f, 30.0f, "%").rand (0.0f, 70.0f),
        P ("comp", "Comp", 0.0f, 10.0f, 2.0f).rand (0.0f, 6.0f),
        Toggle ("phase", "Phase", false),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB") })
{
}

juce::String AcousticPreBlock::getDetail() const
{
    juce::String d = formatValue (pGain);
    if (get (pNotchDepth) < -0.5f) d << "  notch " << formatValue (pNotch);
    return d;
}

void AcousticPreBlock::prepareImpl (double sr, int)
{
    for (auto* b : { &lowCut, &bass, &mid, &treble, &presence, &notch, &body[0], &body[1], &body[2] }) b->reset();
    comp.set (sr, 8.0, 150.0);
    compGain = 1;
    watch.invalidate();
}

void AcousticPreBlock::design() noexcept
{
    const double sr = sampleRate;
    lowCut.setHighPass (sr, get (pLowCut), 0.7);
    bass.setLowShelf (sr, 110.0, get (pBass));
    mid.setPeak (sr, get (pMidFreq), 0.9, get (pMid));
    treble.setHighShelf (sr, 5000.0, get (pTreble));
    presence.setPeak (sr, 3200.0, 0.9, get (pPresence));
    if (get (pNotchDepth) < -0.1f) notch.setPeak (sr, get (pNotch), 6.0, get (pNotchDepth)); else notch.setIdentity();
    // the body image: the resonances a piezo under the saddle never hears
    const float bi = get (pBodyImage) / 100.0f;
    body[0].setPeak (sr, 100.0, 3.0, 6.0 * bi);
    body[1].setPeak (sr, 210.0, 4.0, 4.0 * bi);
    body[2].setPeak (sr, 1300.0, 0.7, -5.0 * bi);   // and the piezo's quack, smoothed
}

float AcousticPreBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    double db = get (pGain) + get (pLevel);
    for (auto* b : { &lowCut, &bass, &mid, &treble, &presence, &notch, &body[0], &body[1], &body[2] }) db += magOf (*b, hz, sr);
    return (float) db;
}

void AcousticPreBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pLowCut), get (pBass), get (pMid), get (pMidFreq), get (pTreble), get (pPresence), get (pNotch), get (pNotchDepth), get (pBodyImage) }))
        design();
    const float gain = dbToGain (get (pGain)) * (get (pPhase) > 0.5f ? -1.0f : 1.0f);
    const float level = dbToGain (get (pLevel));
    const float c = get (pComp) / 10.0f;
    const float thrDb = -12.0f - 22.0f * c, ratio = 1.0f + 5.0f * c;
    const float makeupC = dbToGain (c * 6.0f);
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float minG = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        float y[2] = { l[i] * gain, r[i] * gain };
        for (int ch = 0; ch < 2; ++ch)
        {
            float v = lowCut.process (y[ch], ch);
            v = notch.process (v, ch);
            v = body[2].process (body[1].process (body[0].process (v, ch), ch), ch);
            v = presence.process (treble.process (mid.process (bass.process (v, ch), ch), ch), ch);
            y[ch] = v;
        }
        if (c > 0.0f)
        {
            const float env = comp.process (juce::jmax (std::abs (y[0]), std::abs (y[1])));
            const float eDb = gainToDb (env + 1.0e-9f);
            const float grDb = eDb > thrDb ? (eDb - thrDb) * (1.0f - 1.0f / ratio) : 0.0f;
            compGain = dbToGain (-grDb) * makeupC;
            minG = juce::jmin (minG, dbToGain (-grDb));
        }
        l[i] = y[0] * compGain * level;
        r[i] = y[1] * compGain * level;
    }
    updateMeters (inPk, peak2 (l, r, n), gainToDb (minG));
}

//==============================================================================
// Vocal Strip
//==============================================================================
VoiceStripBlock::VoiceStripBlock()
    : Block (NodeType::VoiceStrip, {
        P ("gain", "Mic Gain", -12.0f, 24.0f, 0.0f, "dB"),
        P ("lowcut", "Low Cut", 20.0f, 300.0f, 90.0f, "Hz").mid (90.0f).rand (60.0f, 150.0f),
        P ("warmth", "Warmth", 0.0f, 10.0f, 3.0f).rand (0.0f, 6.0f),
        P ("comp", "Comp", 0.0f, 10.0f, 4.0f).rand (2.0f, 7.0f),
        P ("deess", "De-Ess", 0.0f, 10.0f, 3.0f).rand (0.0f, 6.0f),
        P ("body", "Body", -12.0f, 12.0f, 0.0f, "dB").rand (-3.0f, 3.0f),
        P ("presence", "Presence", -12.0f, 12.0f, 2.0f, "dB").rand (-1.0f, 4.0f),
        P ("air", "Air", -12.0f, 12.0f, 3.0f, "dB").rand (0.0f, 5.0f),
        P ("gate", "Gate", -90.0f, -20.0f, -90.0f, "dB").rand (-90.0f, -60.0f),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB") })
{
}

juce::String VoiceStripBlock::getDetail() const
{
    return "comp " + formatValue (pComp) + "  air " + formatValue (pAir);
}

void VoiceStripBlock::prepareImpl (double sr, int)
{
    for (auto* b : { &lowCut, &body, &presence, &air, &essBand }) b->reset();
    compDet.set (sr, 10.0, 120.0);
    essDet.set (sr, 1.0, 60.0);
    gateDet.set (sr, 1.0, 150.0);
    compG = essG = gateG = 1;
    watch.invalidate();
}

void VoiceStripBlock::design() noexcept
{
    const double sr = sampleRate;
    lowCut.setHighPass (sr, get (pLowCut), 0.7);
    body.setLowShelf (sr, 200.0, get (pBody));
    presence.setPeak (sr, 4200.0, 0.8, get (pPresence));
    air.setHighShelf (sr, juce::jmin (11000.0, sr * 0.4), get (pAir));
    essBand.setHighPass (sr, 6000.0, 0.7);
}

float VoiceStripBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    double db = get (pGain) + get (pLevel);
    for (auto* b : { &lowCut, &body, &presence, &air }) db += magOf (*b, hz, sr);
    return (float) db;
}

void VoiceStripBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pLowCut), get (pBody), get (pPresence), get (pAir) })) design();
    const float gain = dbToGain (get (pGain)), level = dbToGain (get (pLevel));
    const float warmth = get (pWarmth) / 10.0f;
    const float c = get (pComp) / 10.0f;
    const float thrDb = -10.0f - 24.0f * c, ratio = 1.5f + 4.5f * c, makeupC = dbToGain (c * 4.0f);
    const float ess = get (pDeEss) / 10.0f;
    const float essThr = dbToGain (-24.0f - 12.0f * ess);
    const float gateThr = get (pGate) > -89.5f ? dbToGain (get (pGate)) : 0.0f;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float minG = 1.0f;
    const float att = std::exp (-1.0f / (0.002f * (float) sampleRate)), rel = std::exp (-1.0f / (0.08f * (float) sampleRate));
    for (int i = 0; i < n; ++i)
    {
        float y[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float v = lowCut.process ((ch == 0 ? l[i] : r[i]) * gain, ch);
            v = softSat (v, warmth * 0.6f);
            y[ch] = v;
        }
        const float m = juce::jmax (std::abs (y[0]), std::abs (y[1]));
        // gate first, so the compressor doesn't pull the room up between phrases
        if (gateThr > 0.0f)
        {
            const float e = gateDet.process (m);
            const float tg = e > gateThr ? 1.0f : 0.05f;
            gateG = tg > gateG ? tg + att * (gateG - tg) : tg + rel * (gateG - tg);
        }
        else gateG = 1.0f;
        if (c > 0.0f)
        {
            const float eDb = gainToDb (compDet.process (m) + 1.0e-9f);
            const float grDb = eDb > thrDb ? (eDb - thrDb) * (1.0f - 1.0f / ratio) : 0.0f;
            compG = dbToGain (-grDb);
            minG = juce::jmin (minG, compG);
        }
        // de-ess: only the band above 6 kHz comes down, and only when it jumps out
        float hiL = essBand.process (y[0], 0), hiR = essBand.process (y[1], 1);
        if (ess > 0.0f)
        {
            const float e = essDet.process (juce::jmax (std::abs (hiL), std::abs (hiR)));
            essG = e > essThr ? juce::jmax (0.15f, essThr / e) : 1.0f;
        }
        else essG = 1.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float hi = ch == 0 ? hiL : hiR;
            float v = y[ch] - hi * (1.0f - essG);
            v *= compG * makeupC * gateG;
            v = air.process (presence.process (body.process (v, ch), ch), ch);
            y[ch] = v * level;
        }
        l[i] = y[0]; r[i] = y[1];
    }
    updateMeters (inPk, peak2 (l, r, n), gainToDb (minG));
}

//==============================================================================
// De-Esser
//==============================================================================
DeEsserBlock::DeEsserBlock()
    : Block (NodeType::DeEsser, {
        P ("threshold", "Threshold", -60.0f, 0.0f, -30.0f, "dB").rand (-40.0f, -20.0f),
        P ("freq", "Frequency", 3000.0f, 12000.0f, 6500.0f, "Hz").mid (6500.0f).rand (5000.0f, 8000.0f),
        P ("range", "Range", -24.0f, 0.0f, -10.0f, "dB").rand (-12.0f, -4.0f),
        Choice ("mode", "Mode", { "Split band", "Wideband" }, 0),
        Toggle ("listen", "Listen", false).fixed() })
{
}

void DeEsserBlock::prepareImpl (double sr, int)
{
    for (auto& s : split) s.reset();
    detect.reset();
    det.set (sr, 0.5, 60.0);
    g = 1;
    watch.invalidate();
}

float DeEsserBlock::responseDb (double hz) const
{
    // the band it works on, at full reduction
    const double f = get (pFreq);
    const double x = hz / f;
    return (float) (get (pRange) * (x * x * x * x / (1.0 + x * x * x * x)));
}

void DeEsserBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ get (pFreq) }))
    {
        split[0].setLowPass (sampleRate, get (pFreq), 0.7);   // the band is what's left above it: x - lowpass(x)
        detect.setHighPass (sampleRate, get (pFreq) * 0.9, 0.9);
    }
    const float thr = dbToGain (get (pThreshold));
    const float floorG = dbToGain (get (pRange));
    const bool wide = getChoice (pMode) == 1, listen = get (pListen) > 0.5f;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float minG = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float hiL = l[i] - split[0].process (l[i], 0), hiR = r[i] - split[0].process (r[i], 1);
        const float side = detect.process (0.5f * (l[i] + r[i]), 0);
        const float e = det.process (std::abs (side));
        const float tg = e > thr ? juce::jmax (floorG, thr / e) : 1.0f;
        g = tg < g ? tg : tg + 0.999f * (g - tg);
        minG = juce::jmin (minG, g);
        if (listen) { l[i] = hiL; r[i] = hiR; continue; }
        if (wide) { l[i] *= g; r[i] *= g; }
        else { l[i] -= hiL * (1.0f - g); r[i] -= hiR * (1.0f - g); }
    }
    updateMeters (inPk, peak2 (l, r, n), gainToDb (minG));
}

//==============================================================================
// Pitch Correct
//==============================================================================
PitchCorrectBlock::PitchCorrectBlock()
    : Block (NodeType::PitchCorrect, {
        Choice ("key", "Key", keyNames(), 0),
        Choice ("scale", "Scale", scaleNames(), 0),
        P ("speed", "Speed", 0.0f, 400.0f, 40.0f, "ms").mid (60.0f).rand (10.0f, 150.0f),
        P ("humanize", "Humanize", 0.0f, 100.0f, 30.0f, "%").rand (0.0f, 60.0f),
        P ("transpose", "Transpose", -12.0f, 12.0f, 0.0f, "st").dec (0).fixed(),
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").fixed() })
{
}

std::vector<int> PitchCorrectBlock::panelOrder() const
{
    // a chromatic scale has every note in every key
    if (getChoice (pScale) == 0) return { pScale, pSpeed, pHumanize, pTranspose, pMix };
    return { pKey, pScale, pSpeed, pHumanize, pTranspose, pMix };
}

juce::String PitchCorrectBlock::getDetail() const
{
    const auto scale = getChoice (pScale) == 0 ? juce::String ("Chromatic") : keyNames()[getChoice (pKey)] + " " + scaleNames()[getChoice (pScale)].toLowerCase();
    return scale + "  " + formatValue (pSpeed);
}

void PitchCorrectBlock::prepareImpl (double sr, int)
{
    tracker.prepare (sr);
    shifter.prepare (sr, 30.0f);
    shifter.setWindow (sr, 30.0f);
    cents = 0; heldTarget = 0; heldFor = 0;
}

void PitchCorrectBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const int key = getChoice (pKey), scale = getChoice (pScale);
    const float speedMs = get (pSpeed);
    const float human = get (pHumanize) / 100.0f;
    const float transpose = get (pTranspose);
    const float mix = get (pMix) / 100.0f;
    const float sr = (float) sampleRate;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    float want = 0.0f;   // cents to move by
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        if (tracker.push (m))
        {
            if (tracker.hz > 0.0f && tracker.confidence > 0.6f)
            {
                const float midi = 69.0f + 12.0f * std::log2 (tracker.hz / 440.0f);
                float target = nearestInScale (midi, key, scale);
                // don't flick between two notes when singing between them: hold the last for a moment
                if (heldFor > 0 && std::abs (midi - heldTarget) < 0.55f) target = heldTarget;
                heldTarget = target; heldFor = 6;
                float dev = (target - midi) * 100.0f;
                // humanize: let small, natural wobble through (vibrato, scoops), still land the note
                const float keep = human * 35.0f;
                if (std::abs (dev) < keep) dev *= std::abs (dev) / juce::jmax (1.0f, keep);
                want = dev + transpose * 100.0f;
                heardMidi.store (midi, std::memory_order_relaxed);
                correction.store (want, std::memory_order_relaxed);
            }
            else
            {
                if (heldFor > 0) --heldFor;
                want = transpose * 100.0f;
                if (tracker.hz <= 0.0f) heardMidi.store (0.0f, std::memory_order_relaxed);
            }
        }
        else if (i == 0) want = correction.load (std::memory_order_relaxed);
        // retune speed: 0 = instantly there (the robot), slower = natural
        const float coeff = speedMs <= 0.5f ? 0.0f : std::exp (-1.0f / (speedMs * 0.001f * sr));
        cents = want + coeff * (cents - want);
        shifter.ratio = std::pow (2.0f, cents / 1200.0f);
        const float y = shifter.process (m);
        l[i] = l[i] * (1.0f - mix) + y * mix;
        r[i] = r[i] * (1.0f - mix) + y * mix;
    }
    updateMeters (inPk, peak2 (l, r, n), 0.0f);
}

//==============================================================================
// Harmonizer
//==============================================================================
juce::StringArray HarmonizerBlock::intervalNames()
{
    return { "Off", "3rd above", "4th above", "5th above", "6th above", "Octave above", "3rd below", "4th below", "5th below", "6th below", "Octave below" };
}

static int intervalSteps (int choice)
{
    static const int steps[] { 0, 2, 3, 4, 5, 7, -2, -3, -4, -5, -7 };
    return steps[juce::jlimit (0, 10, choice)];
}

HarmonizerBlock::HarmonizerBlock()
    : Block (NodeType::Harmonizer, {
        Choice ("key", "Key", keyNames(), 0),
        Choice ("scale", "Scale", scaleNames(), 1),
        Choice ("voice1", "Voice 1", intervalNames(), 1),
        Choice ("voice2", "Voice 2", intervalNames(), 0),
        P ("level1", "Voice 1 Level", 0.0f, 100.0f, 60.0f, "%").rand (40.0f, 80.0f),
        P ("level2", "Voice 2 Level", 0.0f, 100.0f, 50.0f, "%").rand (30.0f, 70.0f),
        P ("spread", "Spread", 0.0f, 100.0f, 60.0f, "%").rand (20.0f, 100.0f),
        P ("glide", "Glide", 0.0f, 200.0f, 25.0f, "ms").mid (40.0f).rand (5.0f, 80.0f) })
{
}

juce::String HarmonizerBlock::getDetail() const
{
    juce::String d = keyNames()[getChoice (pKey)] + " " + scaleNames()[getChoice (pScale)].toLowerCase();
    if (getChoice (pVoice1) > 0) d << "  " << intervalNames()[getChoice (pVoice1)];
    return d;
}

void HarmonizerBlock::prepareImpl (double sr, int)
{
    tracker.prepare (sr);
    for (auto& v : voice) { v.prepare (sr, 32.0f); v.setWindow (sr, 32.0f); v.ratio = 1.0f; }
    align.prepare ((int) (sr * 0.05));
    ratio[0] = ratio[1] = target[0] = target[1] = 1.0f;
}

void HarmonizerBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    const int key = getChoice (pKey), scale = getChoice (pScale);
    const int v1 = getChoice (pVoice1), v2 = getChoice (pVoice2);
    const float g1 = v1 > 0 ? get (pLevel1) / 100.0f : 0.0f, g2 = v2 > 0 ? get (pLevel2) / 100.0f : 0.0f;
    const float spread = get (pSpread) / 100.0f;
    const float glide = get (pGlide);
    const float coeff = glide <= 0.5f ? 0.0f : std::exp (-1.0f / (glide * 0.001f * (float) sampleRate));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        if (tracker.push (m) && tracker.hz > 0.0f && tracker.confidence > 0.6f)
        {
            const float midi = 69.0f + 12.0f * std::log2 (tracker.hz / 440.0f);
            const float base = nearestInScale (midi, key, scale);
            for (int k = 0; k < 2; ++k)
            {
                const int c = k == 0 ? v1 : v2;
                if (c == 0) continue;
                const float to = scaleStep (base, key, scale, intervalSteps (c));
                // the harmony is the interval from the note in key, applied to what's actually sung
                target[k] = std::pow (2.0f, (to - base) / 12.0f);
            }
        }
        float y[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k)
        {
            ratio[k] = target[k] + coeff * (ratio[k] - target[k]);
            voice[k].ratio = ratio[k];
            y[k] = voice[k].process (m) * (k == 0 ? g1 : g2);
        }
        // voice 1 leans left, voice 2 right
        const float p1 = 0.5f - 0.5f * spread, p2 = 0.5f + 0.5f * spread;
        l[i] += y[0] * (1.0f - p1) * 1.4f + y[1] * (1.0f - p2) * 1.4f;
        r[i] += y[0] * p1 * 1.4f + y[1] * p2 * 1.4f;
    }
    updateMeters (inPk, peak2 (l, r, n), 0.0f);
}

} // namespace rig
