#include "Modulation.h"

namespace rig
{
namespace
{
    const juce::StringArray lfoSyncNames { "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/16 T", "1/4 D", "1/8 D" };
    const double lfoSyncBeats[] { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    const juce::StringArray seqSyncNames { "Free", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" };
    const double seqSyncBeats[] { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };

    juce::Colour colourFor (ModKind k, int n)
    {
        switch (k)
        {
            case ModKind::Lfo:   return n == 1 ? juce::Colour (0xff6fd6c6) : n == 2 ? juce::Colour (0xff5fb4f0) : juce::Colour (0xff8ea7ff);
            case ModKind::Env:   return n == 1 ? juce::Colour (0xfff2a33a) : n == 2 ? juce::Colour (0xfff07f4f) : juce::Colour (0xffe86a8c);
            case ModKind::Seq:   return juce::Colour (0xffc9a6ff);
            case ModKind::Level: return juce::Colour (0xff3dbe7a);
            case ModKind::Pitch: return juce::Colour (0xffe8d25c);
        }
        return juce::Colours::grey;
    }

    float msCoef (float ms, double sr, int n)
    {
        // one-pole coefficient for a tick of n samples
        return ms <= 0.01f ? 0.0f : (float) std::exp (-(double) n / (0.001 * ms * sr));
    }
}

//==============================================================================
Modulator::Modulator (ModKind k, int n, std::vector<ParamSpec> specs)
    : Block (NodeType::Modulator, std::move (specs)), kind (k), number (n), colour (colourFor (k, n))
{
    for (int i = 0; i < historySize; ++i) hist[i].store (0.0f);
}

juce::String Modulator::getName() const
{
    switch (kind)
    {
        case ModKind::Lfo:   return "LFO " + juce::String (number);
        case ModKind::Env:   return "ENV " + juce::String (number);
        case ModKind::Seq:   return "SEQ";
        case ModKind::Level: return "LEVEL";
        case ModKind::Pitch: return "PITCH";
    }
    return "MOD";
}

juce::String Modulator::describe() const
{
    switch (kind)
    {
        case ModKind::Lfo:   return "Low-frequency oscillator: a steady wobble, free or in time";
        case ModKind::Env:   return "Envelope: rises and falls every time you pick a note";
        case ModKind::Seq:   return "Step sequencer: up to 16 steps, in time";
        case ModKind::Level: return "Input level: follows how hard you play";
        case ModKind::Pitch: return "Input pitch: follows the note you play, low to high";
    }
    return {};
}

juce::String Modulator::getDetail() const { return describe(); }

void Modulator::publish (float v, const ModContext& c) noexcept
{
    out.store (v, std::memory_order_relaxed);
    histAcc += c.numSamples;
    const double every = c.sampleRate * 0.01;
    if (histAcc >= every)
    {
        histAcc = std::fmod (histAcc, every);
        const int w = histWrite.load (std::memory_order_relaxed);
        hist[w].store (v, std::memory_order_relaxed);
        histWrite.store ((w + 1) % historySize, std::memory_order_release);
    }
}

void Modulator::readHistory (std::vector<float>& dest) const
{
    dest.resize ((size_t) historySize);
    const int w = histWrite.load (std::memory_order_acquire);
    for (int i = 0; i < historySize; ++i)
        dest[(size_t) i] = hist[(w + i) % historySize].load (std::memory_order_relaxed);
}

//==============================================================================
LfoMod::LfoMod (int n)
    : Modulator (ModKind::Lfo, n, {
        Choice ("shape", "Shape", { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Drift" }, n == 1 ? 0 : n == 2 ? 1 : 4),
        Choice ("sync", "Sync", lfoSyncNames, n == 3 ? 6 : 0),
        P ("rate", "Rate", 0.02f, 20.0f, n == 1 ? 1.0f : n == 2 ? 0.25f : 4.0f, "Hz").mid (1.5f).dec (2).rand (0.1f, 6.0f),
        P ("phase", "Phase", 0.0f, 360.0f, 0.0f, "deg").dec (0),
        P ("depth", "Depth", 0.0f, 100.0f, 100.0f, "%"),
        Choice ("polarity", "Polarity", { "Bipolar", "Unipolar" }),
        Choice ("retrig", "Restart", { "Free run", "On pick" }),
        P ("smooth", "Smooth", 0.0f, 100.0f, 0.0f, "%") })
{
    id = "mod:" + juce::String (n - 1);
}

double LfoMod::syncBeats (int c) { return juce::isPositiveAndBelow (c, (int) std::size (lfoSyncBeats)) ? lfoSyncBeats[c] : 0.0; }

juce::String LfoMod::formatValue (int i, float v) const
{
    if (i == pRate && getChoice (pSync) > 0) return lfoSyncNames[getChoice (pSync)];
    return Block::formatValue (i, v);
}

float LfoMod::shapeAt (double p) const noexcept
{
    p -= std::floor (p);
    switch (getChoice (pShape))
    {
        case 0: return (float) std::sin (juce::MathConstants<double>::twoPi * p);
        case 1: return (float) (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        case 2: return (float) (2.0 * p - 1.0);
        case 3: return (float) (1.0 - 2.0 * p);
        case 4: return p < 0.5 ? 1.0f : -1.0f;
        default: break;
    }
    return 0.0f;
}

float LfoMod::lfoShape (double p) const
{
    const int s = getChoice (pShape);
    if (s == 5) { const int k = (int) std::floor (p * 4.0); static const float steps[] { 0.6f, -0.4f, 0.9f, -0.8f }; return steps[k & 3]; }
    if (s == 6) return 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * p) * (float) std::cos (juce::MathConstants<double>::pi * p * 1.7);
    return shapeAt (p + get (pPhase) / 360.0f);
}

void LfoMod::tick (const ModContext& c) noexcept
{
    const double beats = syncBeats (getChoice (pSync));
    const double hz = beats > 0.0 ? c.bpm / 60.0 / beats : (double) get (pRate);
    const double dt = c.numSamples / c.sampleRate;

    const double before = phase;
    const bool restarted = c.pick && getChoice (pRetrig) == 1;
    if (restarted) phase = 0.0;
    if (beats > 0.0 && c.ppq >= 0.0 && getChoice (pRetrig) == 0)
        phase = std::fmod (c.ppq / beats, 1.0);   // locked to the host's bar
    else
        phase += hz * dt;
    phase -= std::floor (phase);
    const bool wrapped = restarted || phase < before;
    const double p = phase + get (pPhase) / 360.0;

    float v;
    const int shape = getChoice (pShape);
    if (shape == 5 || shape == 6)
    {
        if (wrapped || lastCycle < 0)
        {
            prevHeld = held;
            held = rng.nextFloat() * 2.0f - 1.0f;
        }
        lastCycle = 0;
        if (shape == 5) v = held;
        else
        {
            const float t = (float) (p - std::floor (p));
            const float s = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
            v = prevHeld + (held - prevHeld) * s;
        }
    }
    else v = shapeAt (p);

    const float smoothMs = get (pSmooth) * 0.01f * 60.0f;
    const float k = msCoef (smoothMs, c.sampleRate, c.numSamples);
    smoothed = v + (smoothed - v) * k;
    v = smoothed;

    if (! isBipolar()) v = 0.5f * (v + 1.0f);
    livePos.store ((float) phase);
    publish (v * get (pDepth) * 0.01f, c);
}

//==============================================================================
EnvMod::EnvMod (int n)
    : Modulator (ModKind::Env, n, {
        P ("threshold", "Threshold", -70.0f, 0.0f, -42.0f, "dB").fixed(),
        P ("attack", "Attack", 0.0f, 2000.0f, n == 2 ? 300.0f : 5.0f, "ms").mid (60.0f),
        P ("decay", "Decay", 5.0f, 5000.0f, n == 3 ? 1500.0f : 300.0f, "ms").mid (400.0f),
        P ("sustain", "Sustain", 0.0f, 100.0f, n == 2 ? 100.0f : 20.0f, "%"),
        P ("release", "Release", 5.0f, 5000.0f, 250.0f, "ms").mid (400.0f),
        P ("depth", "Depth", 0.0f, 100.0f, 100.0f, "%"),
        Toggle ("invert", "Invert", false) })
{
    id = "mod:" + juce::String (2 + n);
}

void EnvMod::tick (const ModContext& c) noexcept
{
    const float thr = get (pThreshold);
    const bool above = c.levelDb > thr;
    const bool below = c.levelDb < thr - 4.0f;
    const double dt = c.numSamples / c.sampleRate;

    if ((c.pick && above) || (above && ! gate)) { stage = Attack; gate = true; }
    else if (gate && below) { gate = false; if (stage != Idle) stage = Release; }

    const float sus = get (pSustain) * 0.01f;
    switch (stage)
    {
        case Attack:
        {
            const float a = get (pAttack) * 0.001f;
            level = a <= 0.0005f ? 1.0f : level + (float) (dt / a);
            if (level >= 1.0f) { level = 1.0f; stage = Decay; }
            break;
        }
        case Decay:
        {
            const float k = msCoef (get (pDecay) * 0.25f, c.sampleRate, c.numSamples);
            level = sus + (level - sus) * k;
            if (std::abs (level - sus) < 0.002f) { level = sus; stage = Sustain; }
            break;
        }
        case Sustain: level = sus; break;
        case Release:
        {
            const float k = msCoef (get (pRelease) * 0.25f, c.sampleRate, c.numSamples);
            level *= k;
            if (level < 0.001f) { level = 0.0f; stage = Idle; }
            break;
        }
        default: level = 0.0f; break;
    }
    liveStage.store (stage);
    livePos.store (level);
    const float v = get (pInvert) > 0.5f ? 1.0f - level : level;
    publish (v * get (pDepth) * 0.01f, c);
}

//==============================================================================
static std::vector<ParamSpec> seqSpecs()
{
    std::vector<ParamSpec> s {
        P ("steps", "Steps", 1.0f, 16.0f, 8.0f).dec (0).fixed(),
        Choice ("sync", "Sync", seqSyncNames, 4),
        P ("rate", "Rate", 0.1f, 30.0f, 4.0f, "Hz").mid (4.0f).dec (2),
        P ("glide", "Glide", 0.0f, 100.0f, 0.0f, "%"),
        P ("depth", "Depth", 0.0f, 100.0f, 100.0f, "%"),
        Choice ("polarity", "Polarity", { "Unipolar", "Bipolar" }),
        Choice ("retrig", "Restart", { "Free run", "On pick" }) };
    static const float pattern[] { 1.0f, 0.25f, 0.6f, 0.15f, 0.85f, 0.35f, 0.7f, 0.0f, 1.0f, 0.5f, 0.75f, 0.25f, 0.9f, 0.4f, 0.65f, 0.1f };
    for (int i = 0; i < SeqMod::maxSteps; ++i)
        s.push_back (P ("s" + juce::String (i + 1), "Step " + juce::String (i + 1), 0.0f, 1.0f, pattern[i]).dec (2));
    return s;
}

SeqMod::SeqMod (int n) : Modulator (ModKind::Seq, n, seqSpecs())
{
    id = "mod:6";
}

double SeqMod::stepBeats (int c) { return juce::isPositiveAndBelow (c, (int) std::size (seqSyncBeats)) ? seqSyncBeats[c] : 0.0; }

juce::String SeqMod::formatValue (int i, float v) const
{
    if (i == pRate && getChoice (pSync) > 0) return seqSyncNames[getChoice (pSync)];
    if (i >= pStep1) return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    return Block::formatValue (i, v);
}

void SeqMod::tick (const ModContext& c) noexcept
{
    const double beats = stepBeats (getChoice (pSync));
    const double stepsPerSec = beats > 0.0 ? c.bpm / 60.0 / beats : (double) get (pRate);
    const int steps = numSteps();
    if (c.pick && getChoice (pRetrig) == 1) pos = 0.0;
    if (beats > 0.0 && c.ppq >= 0.0 && getChoice (pRetrig) == 0) pos = c.ppq / beats;
    else pos += stepsPerSec * c.numSamples / c.sampleRate;
    if (pos >= 4096.0 * steps) pos = std::fmod (pos, (double) steps);

    const int step = (int) std::floor (pos) % steps;
    float v = get (pStep1 + step);
    if (isBipolar()) v = v * 2.0f - 1.0f;
    const float glideMs = get (pGlide) * 0.01f * (float) (1000.0 / juce::jmax (0.1, stepsPerSec)) * 0.5f;
    smoothed = v + (smoothed - v) * msCoef (glideMs, c.sampleRate, c.numSamples);
    liveStage.store (step);
    livePos.store ((float) (pos - std::floor (pos)));
    publish (smoothed * get (pDepth) * 0.01f, c);
}

//==============================================================================
LevelMod::LevelMod (int n)
    : Modulator (ModKind::Level, n, {
        P ("attack", "Attack", 0.1f, 200.0f, 5.0f, "ms").mid (10.0f).dec (1),
        P ("release", "Release", 5.0f, 2000.0f, 150.0f, "ms").mid (150.0f),
        P ("floor", "Floor", -70.0f, -10.0f, -48.0f, "dB").fixed(),
        P ("ceiling", "Ceiling", -40.0f, 0.0f, -8.0f, "dB").fixed(),
        P ("depth", "Depth", 0.0f, 100.0f, 100.0f, "%"),
        Toggle ("invert", "Invert", false) })
{
    id = "mod:7";
}

void LevelMod::tick (const ModContext& c) noexcept
{
    const float att = msCoef (get (pAttack), c.sampleRate, 1);
    const float rel = msCoef (get (pRelease), c.sampleRate, 1);
    for (int i = 0; i < c.numSamples; ++i)
    {
        const float x = std::abs (c.input[i]);
        env = x + (env - x) * (x > env ? att : rel);
    }
    const float db = gainToDb (env);
    liveDb.store (db);
    const float lo = get (pFloor), hi = juce::jmax (lo + 1.0f, get (pCeiling));
    float v = juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo));
    if (get (pInvert) > 0.5f) v = 1.0f - v;
    livePos.store (v);
    publish (v * get (pDepth) * 0.01f, c);
}

//==============================================================================
PitchMod::PitchMod (int n)
    : Modulator (ModKind::Pitch, n, {
        P ("low", "Low Note", 40.0f, 1000.0f, 82.4f, "Hz").mid (160.0f).fixed(),
        P ("high", "High Note", 100.0f, 2000.0f, 1318.5f, "Hz").mid (600.0f).fixed(),
        P ("glide", "Glide", 0.0f, 1000.0f, 40.0f, "ms").mid (80.0f),
        P ("depth", "Depth", 0.0f, 100.0f, 100.0f, "%"),
        Choice ("nonote", "Between notes", { "Hold", "Fall to 0" }) })
{
    id = "mod:8";
}

void PitchMod::tick (const ModContext& c) noexcept
{
    const float lo = get (pLow), hi = juce::jmax (lo * 1.05f, get (pHigh));
    if (c.pitchHz > 20.0f)
        target = juce::jlimit (0.0f, 1.0f, std::log (c.pitchHz / lo) / std::log (hi / lo));
    else if (getChoice (pNoNote) == 1 && c.levelDb < -55.0f)
        target = 0.0f;
    liveHz.store (c.pitchHz);
    smoothed = target + (smoothed - target) * msCoef (get (pGlide), c.sampleRate, c.numSamples);
    livePos.store (smoothed);
    publish (smoothed * get (pDepth) * 0.01f, c);
}

//==============================================================================
ModMatrix::ModMatrix()
{
    for (int i = 1; i <= 3; ++i) sources.push_back (std::make_unique<LfoMod> (i));
    for (int i = 1; i <= 3; ++i) sources.push_back (std::make_unique<EnvMod> (i));
    sources.push_back (std::make_unique<SeqMod> (1));
    sources.push_back (std::make_unique<LevelMod> (1));
    sources.push_back (std::make_unique<PitchMod> (1));
}

int ModMatrix::indexOf (const Modulator* m) const noexcept
{
    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i].get() == m) return (int) i;
    return -1;
}

Modulator* ModMatrix::sourceFromTarget (const juce::String& t) const noexcept
{
    if (! t.startsWith ("mod:")) return nullptr;
    const int i = t.substring (4).getIntValue();
    return juce::isPositiveAndBelow (i, numSources) ? sources[(size_t) i].get() : nullptr;
}

void ModMatrix::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    tuner.prepare (sr);
    for (auto& s : sources) { s->prepare (sr, maxBlock); s->restart(); }
    fastEnv = slowEnv = 0.0f;
}

void ModMatrix::routesChanged() noexcept
{
    for (size_t i = 0; i < routes.size() && i < (size_t) maxRoutes; ++i)
        liveAmounts[i].store (routes[i].amount);
    hasRoutes.store (! routes.empty());
}

void ModMatrix::setAmount (int i, float a) noexcept
{
    if (! juce::isPositiveAndBelow (i, (int) routes.size()) || i >= maxRoutes) return;
    a = juce::jlimit (-1.0f, 1.0f, a);
    routes[(size_t) i].amount = a;
    liveAmounts[(size_t) i].store (a);
}

static Block* findBlock (Node& n, const juce::String& id) noexcept
{
    if (n.id == id) return dynamic_cast<Block*> (&n);
    if (auto* c = dynamic_cast<Chain*> (&n))
    {
        for (auto& child : c->nodes)
            if (auto* b = findBlock (*child, id)) return b;
    }
    else if (auto* s = dynamic_cast<Split*> (&n))
    {
        for (auto& br : s->branches)
            if (auto* b = findBlock (*br->chain, id)) return b;
    }
    return nullptr;
}

void ModMatrix::resolve (Node& root) noexcept
{
    numSlots = numResolved = 0;
    for (size_t ri = 0; ri < routes.size(); ++ri)
    {
        auto& r = routes[ri];
        if (numResolved >= maxRoutes || ! juce::isPositiveAndBelow (r.source, numSources)) continue;
        Block* b = r.target.startsWith ("mod:") ? sourceFromTarget (r.target) : findBlock (root, r.target);
        if (b == nullptr || ! juce::isPositiveAndBelow (r.param, b->numParams())) continue;
        int slot = -1;
        for (int s = 0; s < numSlots; ++s)
            if (slots[(size_t) s].block == b && slots[(size_t) s].param == r.param) { slot = s; break; }
        if (slot < 0) { slot = numSlots++; slots[(size_t) slot] = { b, r.param, 0.0f }; }
        resolvedIndex[(size_t) numResolved] = (int) ri;
        resolvedSources[(size_t) numResolved] = sources[(size_t) r.source].get();
        routeSlot[(size_t) numResolved] = slot;
        ++numResolved;
    }
}

void ModMatrix::process (const float* in, int n, double ppq) noexcept
{
    // picking: a fast envelope jumping well above a slow one
    bool pick = false;
    const float fa = std::exp (-1.0f / (0.0005f * (float) sampleRate)), fr = std::exp (-1.0f / (0.02f * (float) sampleRate));
    const float sa = std::exp (-1.0f / (0.04f * (float) sampleRate)), sr = std::exp (-1.0f / (0.25f * (float) sampleRate));
    for (int i = 0; i < n; ++i)
    {
        const float x = std::abs (in[i]);
        fastEnv = x + (fastEnv - x) * (x > fastEnv ? fa : fr);
        slowEnv = x + (slowEnv - x) * (x > slowEnv ? sa : sr);
        if (refractory > 0) --refractory;
        else if (fastEnv > 0.003f && fastEnv > slowEnv * 1.9f)
        {
            pick = true;
            refractory = (int) (0.07 * sampleRate);
        }
    }
    if (pick) pickedFlash.store (true);

    // the pitch tracker only listens when something uses it
    if (watching.load (std::memory_order_relaxed)) tuner.push (in, n);
    else
        for (int r = 0; r < numResolved; ++r)
            if (resolvedSources[(size_t) r]->getKind() == ModKind::Pitch) { tuner.push (in, n); break; }

    ModContext c;
    c.sampleRate = sampleRate;
    c.numSamples = n;
    c.input = in;
    const double host = hostBpm.load (std::memory_order_relaxed);
    c.bpm = host > 0.0 ? host : (double) bpm.load (std::memory_order_relaxed);
    c.ppq = ppq;
    c.pick = pick;
    c.levelDb = gainToDb (fastEnv);
    c.pitchHz = pitchHz.load (std::memory_order_relaxed);
    for (auto& s : sources) s->tick (c);

    for (int s = 0; s < numSlots; ++s) slots[(size_t) s].sum = 0.0f;
    for (int r = 0; r < numResolved; ++r)
        slots[(size_t) routeSlot[(size_t) r]].sum += liveAmounts[(size_t) resolvedIndex[(size_t) r]].load (std::memory_order_relaxed)
                                                     * resolvedSources[(size_t) r]->value();
    for (int s = 0; s < numSlots; ++s)
        slots[(size_t) s].block->modulate (slots[(size_t) s].param, slots[(size_t) s].sum);
}

void ModMatrix::analysePitch()
{
    const auto r = tuner.analyse();
    pitchHz.store (r.valid ? r.frequency : 0.0f);
}

std::vector<const ModRoute*> ModMatrix::routesTo (const juce::String& target, int param) const
{
    std::vector<const ModRoute*> out;
    for (auto& r : routes)
        if (r.target == target && r.param == param) out.push_back (&r);
    return out;
}

juce::Range<float> ModMatrix::reach (const juce::String& target, int param) const
{
    float lo = 0, hi = 0;
    for (auto* r : routesTo (target, param))
    {
        const bool bi = source (r->source).isBipolar();
        const float a = r->amount;
        if (bi) { lo -= std::abs (a); hi += std::abs (a); }
        else if (a >= 0) hi += a;
        else lo += a;
    }
    return { lo, hi };
}

juce::ValueTree ModMatrix::toState() const
{
    juce::ValueTree v ("Modulation");
    v.setProperty ("bpm", bpm.load(), nullptr);
    for (int i = 0; i < numSources; ++i)
    {
        auto s = sources[(size_t) i]->toState();
        s.setProperty ("index", i, nullptr);
        v.appendChild (s, nullptr);
    }
    for (auto& r : routes)
    {
        juce::ValueTree rt ("Route");
        rt.setProperty ("source", r.source, nullptr);
        rt.setProperty ("target", r.target, nullptr);
        rt.setProperty ("param", r.param, nullptr);
        rt.setProperty ("amount", r.amount, nullptr);
        v.appendChild (rt, nullptr);
    }
    return v;
}

void ModMatrix::resetToDefaults()
{
    for (auto& s : sources)
        for (int p = 0; p < s->numParams(); ++p) s->set (p, s->getSpecs()[(size_t) p].def);
    bpm = 120.0f;
}

void ModMatrix::fromState (const juce::ValueTree& v)
{
    routes.clear();
    for (auto& s : sources)
        for (int p = 0; p < s->numParams(); ++p) s->unmodulate (p);
    resetToDefaults();
    struct Done { ModMatrix& m; ~Done() { m.routesChanged(); } } done { *this };
    if (! v.hasType ("Modulation")) return;
    bpm = (float) v.getProperty ("bpm", 120.0f);
    for (auto c : v)
    {
        if (c.hasType ("Route"))
        {
            ModRoute r;
            r.source = (int) c["source"];
            r.target = c["target"].toString();
            r.param = (int) c["param"];
            r.amount = juce::jlimit (-1.0f, 1.0f, (float) c.getProperty ("amount", 0.5f));
            if (juce::isPositiveAndBelow (r.source, numSources) && r.target.isNotEmpty() && (int) routes.size() < maxRoutes)
                routes.push_back (r);
        }
        else if (c.hasProperty ("index"))
        {
            const int i = (int) c["index"];
            if (juce::isPositiveAndBelow (i, numSources))
            {
                auto copy = c.createCopy();
                copy.setProperty ("id", sources[(size_t) i]->id, nullptr);
                sources[(size_t) i]->fromState (copy);
            }
        }
    }
}

} // namespace rig
