#include "Nodes.h"

namespace rig
{
static const std::pair<NodeType, const char*> typeNames[] = {
    { NodeType::Chain, "chain" }, { NodeType::Split, "split" }, { NodeType::Nam, "nam" }, { NodeType::Ir, "ir" },
    { NodeType::Gate, "gate" }, { NodeType::Eq, "eq" }, { NodeType::Gain, "gain" }, { NodeType::Comp, "comp" },
    { NodeType::Plugin, "plugin" }, { NodeType::Limiter, "limiter" }, { NodeType::Expander, "expander" },
    { NodeType::Transient, "transient" }, { NodeType::Swell, "swell" }, { NodeType::Overdrive, "overdrive" },
    { NodeType::Fuzz, "fuzz" }, { NodeType::Boost, "boost" }, { NodeType::Crusher, "crusher" }, { NodeType::Wah, "wah" },
    { NodeType::Chorus, "chorus" }, { NodeType::Flanger, "flanger" }, { NodeType::Phaser, "phaser" },
    { NodeType::Tremolo, "tremolo" }, { NodeType::Vibe, "vibe" }, { NodeType::Rotary, "rotary" },
    { NodeType::Delay, "delay" }, { NodeType::Reverb, "reverb" }, { NodeType::Pitch, "pitch" },
    { NodeType::Tuner, "tuner" }
};

const std::vector<std::pair<juce::String, std::vector<NodeType>>>& builtInGroups()
{
    static const std::vector<std::pair<juce::String, std::vector<NodeType>>> groups {
        { "Dynamics",       { NodeType::Gate, NodeType::Comp, NodeType::Limiter, NodeType::Expander, NodeType::Transient, NodeType::Swell } },
        { "Drive",          { NodeType::Overdrive, NodeType::Fuzz, NodeType::Boost, NodeType::Crusher } },
        { "EQ & Filter",    { NodeType::Eq, NodeType::Wah } },
        { "Modulation",     { NodeType::Chorus, NodeType::Flanger, NodeType::Phaser, NodeType::Tremolo, NodeType::Vibe, NodeType::Rotary } },
        { "Delay & Reverb", { NodeType::Delay, NodeType::Reverb } },
        { "Pitch",          { NodeType::Pitch } },
        { "Utility",        { NodeType::Tuner, NodeType::Gain } } };
    return groups;
}

juce::String builtInMenuName (NodeType t)
{
    switch (t)
    {
        case NodeType::Gate:      return "Noise Gate";
        case NodeType::Comp:      return "Compressor";
        case NodeType::Limiter:   return "Limiter";
        case NodeType::Expander:  return "Expander";
        case NodeType::Transient: return "Transient Shaper";
        case NodeType::Swell:     return "Auto Swell (violin swells)";
        case NodeType::Overdrive: return "Overdrive";
        case NodeType::Fuzz:      return "Fuzz";
        case NodeType::Boost:     return "Clean Boost";
        case NodeType::Crusher:   return "Bit Crusher";
        case NodeType::Eq:        return "Studio EQ";
        case NodeType::Wah:       return "Wah / Auto-Wah";
        case NodeType::Chorus:    return "Chorus";
        case NodeType::Flanger:   return "Flanger";
        case NodeType::Phaser:    return "Phaser";
        case NodeType::Tremolo:   return "Tremolo";
        case NodeType::Vibe:      return "Uni-Vibe";
        case NodeType::Rotary:    return "Rotary Speaker";
        case NodeType::Delay:     return "Delay (digital / tape / ping-pong / dual)";
        case NodeType::Reverb:    return "Reverb (room / hall / plate / spring / shimmer)";
        case NodeType::Pitch:     return "Pitch / Octave / Doubler";
        case NodeType::Gain:      return "Volume / Pan";
        case NodeType::Tuner:     return "Tuner (strobe + needle)";
        default:                  return "Block";
    }
}

juce::String builtInShortName (NodeType t)
{
    const auto full = builtInMenuName (t);
    return full.upToFirstOccurrenceOf (" (", false, false);
}

juce::String builtInBlurb (NodeType t)
{
    switch (t)
    {
        case NodeType::Gate:      return "Cuts hiss and hum between notes";
        case NodeType::Comp:      return "Evens out your picking, adds sustain";
        case NodeType::Limiter:   return "Hard ceiling on peaks";
        case NodeType::Expander:  return "Gentle downward expansion";
        case NodeType::Transient: return "More or less pick attack";
        case NodeType::Swell:     return "Violin-style volume swells";
        case NodeType::Overdrive: return "Mid-hump tube-style drive";
        case NodeType::Fuzz:      return "Thick fuzz, optional octave";
        case NodeType::Boost:     return "Clean level and tone boost";
        case NodeType::Crusher:   return "Lo-fi bits and sample rate";
        case NodeType::Eq:        return "Draggable parametric bands";
        case NodeType::Wah:       return "Pedal, envelope or LFO wah";
        case NodeType::Chorus:    return "Classic shimmering chorus";
        case NodeType::Flanger:   return "Jet sweeps with feedback";
        case NodeType::Phaser:    return "Swirling phase stages";
        case NodeType::Tremolo:   return "Volume or harmonic tremolo";
        case NodeType::Vibe:      return "Throbbing Uni-Vibe swirl";
        case NodeType::Rotary:    return "Leslie horn and drum";
        case NodeType::Delay:     return "Digital, tape, ping-pong, dual";
        case NodeType::Reverb:    return "Room, hall, plate, spring, shimmer";
        case NodeType::Pitch:     return "Octaves, pitch shift, doubler";
        case NodeType::Gain:      return "Volume and pan";
        case NodeType::Tuner:     return "Strobe tuner - never in dice";
        default:                  return {};
    }
}

juce::String nodeTypeToString (NodeType t)
{
    for (auto& [type, name] : typeNames)
        if (type == t) return name;
    return "gain";
}

NodeType nodeTypeFromString (const juce::String& s)
{
    for (auto& [type, name] : typeNames)
        if (s == name) return type;
    return NodeType::Gain;
}

juce::String categoryName (Category c)
{
    switch (c)
    {
        case Category::Dynamics:   return "Dynamics";
        case Category::Drive:      return "Drive";
        case Category::Amp:        return "Amp";
        case Category::Cab:        return "Cab / IR";
        case Category::Eq:         return "EQ";
        case Category::Filter:     return "Filter / Wah";
        case Category::Modulation: return "Modulation";
        case Category::Delay:      return "Delay";
        case Category::Reverb:     return "Reverb";
        case Category::Pitch:      return "Pitch";
        case Category::Utility:    return "Utility";
        case Category::Routing:    return "Routing";
        case Category::Other:      return "Other";
    }
    return "Other";
}

juce::Colour categoryColour (Category c)
{
    switch (c)
    {
        case Category::Dynamics:   return juce::Colour (0xfff0d264);  // yellow
        case Category::Drive:      return juce::Colour (0xffe86a5c);  // red
        case Category::Amp:        return juce::Colour (0xfff2a33a);  // amber
        case Category::Cab:        return juce::Colour (0xff6fd6c6);  // teal
        case Category::Eq:         return juce::Colour (0xff3dbe7a);  // green
        case Category::Filter:     return juce::Colour (0xff9fd85a);  // lime
        case Category::Modulation: return juce::Colour (0xff8ea7ff);  // blue
        case Category::Delay:      return juce::Colour (0xff5ac8fa);  // sky
        case Category::Reverb:     return juce::Colour (0xffc9a6ff);  // purple
        case Category::Pitch:      return juce::Colour (0xffff8fc7);  // pink
        case Category::Utility:    return juce::Colour (0xffb8bcc6);  // grey
        case Category::Routing:    return juce::Colour (0xfff2a33a);
        case Category::Other:      return juce::Colour (0xff8a8f99);
    }
    return juce::Colour (0xff8a8f99);
}

Category categoryForBuiltIn (NodeType t)
{
    switch (t)
    {
        case NodeType::Gate: case NodeType::Comp: case NodeType::Limiter: case NodeType::Expander:
        case NodeType::Transient: case NodeType::Swell:                         return Category::Dynamics;
        case NodeType::Overdrive: case NodeType::Fuzz: case NodeType::Boost:
        case NodeType::Crusher:                                                  return Category::Drive;
        case NodeType::Nam:                                                      return Category::Amp;
        case NodeType::Ir:                                                       return Category::Cab;
        case NodeType::Eq:                                                       return Category::Eq;
        case NodeType::Wah:                                                      return Category::Filter;
        case NodeType::Chorus: case NodeType::Flanger: case NodeType::Phaser: case NodeType::Tremolo:
        case NodeType::Vibe: case NodeType::Rotary:                              return Category::Modulation;
        case NodeType::Delay:                                                    return Category::Delay;
        case NodeType::Reverb:                                                   return Category::Reverb;
        case NodeType::Pitch:                                                    return Category::Pitch;
        case NodeType::Gain: case NodeType::Tuner:                               return Category::Utility;
        case NodeType::Split: case NodeType::Chain:                              return Category::Routing;
        case NodeType::Plugin:                                                   return Category::Other;
    }
    return Category::Other;
}

Category categoriseDescription (const juce::PluginDescription& d)
{
    const auto cat = d.category.toLowerCase();
    const auto name = (d.name + " " + d.descriptiveName).toLowerCase();

    auto has = [] (const juce::String& s, std::initializer_list<const char*> words)
    {
        for (auto* w : words) if (s.contains (w)) return true;
        return false;
    };

    // the plug-in's name is usually more specific than its declared category
    if (has (name, { "cab", "impulse", " ir", "ir ", "speaker", "cabinet" }))            return Category::Cab;
    if (has (name, { "amp", "head", "nam", "neural", "marshall", "mesa", "5150", "plexi", "fender", "vox" })) return Category::Amp;
    if (has (name, { "reverb", "verb", "hall", "plate", "room", "spring", "shimmer", "space" })) return Category::Reverb;
    if (has (name, { "delay", "echo", "tape", "ping" }))                                  return Category::Delay;
    if (has (name, { "chorus", "flang", "phase", "phasr", "trem", "vibe", "rotary", "leslie", "ensemble", "mod" })) return Category::Modulation;
    if (has (name, { "wah", "filter", "envelope", "formant", "talk" }))                   return Category::Filter;
    if (has (name, { "pitch", "octa", "harmon", "whammy", "shift", "tune" }))             return Category::Pitch;
    if (has (name, { "drive", "dist", "fuzz", "boost", "screamer", " ts", "klon", "crush", "satur", "tube", "muff", "rat" })) return Category::Drive;
    if (has (name, { "comp", "limit", "gate", "expand", "transient", "dyna", "squeez", "swell" })) return Category::Dynamics;
    if (has (name, { "eq", "equal", "tone", "graphic", "parametric" }))                   return Category::Eq;

    if (has (cat, { "reverb" }))                         return Category::Reverb;
    if (has (cat, { "delay" }))                          return Category::Delay;
    if (has (cat, { "modulation" }))                     return Category::Modulation;
    if (has (cat, { "distortion" }))                     return Category::Drive;
    if (has (cat, { "dynamics" }))                       return Category::Dynamics;
    if (has (cat, { "eq" }))                             return Category::Eq;
    if (has (cat, { "filter" }))                         return Category::Filter;
    if (has (cat, { "pitch" }))                          return Category::Pitch;
    if (has (cat, { "tools", "analyzer", "spatial", "surround", "mastering", "restoration" })) return Category::Utility;
    return Category::Other;
}

//==============================================================================
void AnalyzerTap::push (bool isPost, const juce::AudioBuffer<float>& b, int n) noexcept
{
    auto& s = isPost ? post : pre;
    const int count = juce::jmin (n, s.fifo.getFreeSpace());
    if (count <= 0) return;
    auto* l = b.getReadPointer (0);
    auto* r = b.getReadPointer (1);
    int k = 0;
    s.fifo.write (count).forEach ([&] (int idx) { s.data[(size_t) idx] = 0.5f * (l[k] + r[k]); ++k; });
}

int AnalyzerTap::read (bool isPost, float* dest, int max)
{
    auto& s = isPost ? post : pre;
    const int ready = s.fifo.getNumReady();
    if (ready == 0) return 0;
    // keep only the newest `max` samples
    if (ready > max) s.fifo.read (ready - max).forEach ([] (int) {});
    const int count = juce::jmin (ready, max);
    int k = 0;
    s.fifo.read (count).forEach ([&] (int idx) { dest[k++] = s.data[(size_t) idx]; });
    return count;
}

juce::String splitModeName (SplitMode m)
{
    switch (m)
    {
        case SplitMode::Split:     return "Split %";
        case SplitMode::Parallel:  return "Parallel";
        case SplitMode::Crossover: return "Crossover";
        case SplitMode::Stereo:    return "Stereo L/R";
    }
    return {};
}

void visitNodes (Node& root, const std::function<void (Node&)>& fn)
{
    fn (root);
    if (auto* c = dynamic_cast<Chain*> (&root))
        for (auto& n : c->nodes)
            visitNodes (*n, fn);
    else if (auto* s = dynamic_cast<Split*> (&root))
        for (auto& b : s->branches)
            visitNodes (*b->chain, fn);
}

//==============================================================================
void Node::prepare (double sr, int block)
{
    sampleRate = sr;
    maxBlock = juce::jmax (1, block);
    dry.setSize (2, maxBlock, false, true, true);
    wetGain.reset (sr, 0.015);
    wetGain.setCurrentAndTargetValue (bypassed.load() ? 0.0f : 1.0f);
    compGain.reset (sr, 0.25);
    compGain.setCurrentAndTargetValue (autoGain.load() ? dbToGain (autoGainDb.load()) : 1.0f);
    prepareImpl (sr, maxBlock);
}

void Node::process (juce::AudioBuffer<float>& buffer, int n) noexcept
{
    wetGain.setTargetValue (bypassed.load (std::memory_order_relaxed) ? 0.0f : 1.0f);
    auto* t = tap.load (std::memory_order_relaxed);
    if (t != nullptr) t->push (false, buffer, n);

    if (! wetGain.isSmoothing() && wetGain.getCurrentValue() < 0.5f)
    {
        if (t != nullptr) t->push (true, buffer, n);
        return;  // fully bypassed: untouched
    }

    const bool ag = autoGain.load (std::memory_order_relaxed);
    const bool needDry = ag || wetGain.isSmoothing();
    if (needDry)
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom (ch, 0, buffer, ch, 0, n);

    processImpl (buffer, n);

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    if (ag)
    {
        // Normalise-style gain compensation: measure while playing, then hold.
        const auto ec = editCount.load (std::memory_order_relaxed);
        if (relearn.exchange (false) || ec != learnedEdit.load (std::memory_order_relaxed))
        {
            learnedEdit.store (ec);
            learningNow = true;
            accIn = accOut = 0;
            accSamples = 0;
            autoGainProgress.store (0.0f);
        }

        if (learningNow)
        {
            double ein = 0, eout = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = dry.getReadPointer (ch);
                auto* o = buffer.getReadPointer (ch);
                for (int i = 0; i < n; ++i) { ein += (double) d[i] * d[i]; eout += (double) o[i] * o[i]; }
            }
            // only count blocks where something is being played (input above about -54 dBFS)
            if (ein / (2.0 * n) > 4.0e-6)
            {
                accIn += ein;
                accOut += eout;
                accSamples += n;
            }
            const int needed = (int) (sampleRate * 1.5);
            if (accSamples > (int) (sampleRate * 0.12) && accOut > 1.0e-12)
            {
                const float g = juce::jlimit (0.0316f, 31.6f, (float) std::sqrt (accIn / accOut));   // +-30 dB
                autoGainDb.store (gainToDb (g));
            }
            autoGainProgress.store (juce::jmin (1.0f, (float) accSamples / (float) needed));
            if (accSamples >= needed) learningNow = false;
        }

        compGain.setTargetValue (dbToGain (autoGainDb.load (std::memory_order_relaxed)));
        if (compGain.isSmoothing() || compGain.getCurrentValue() != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                const float g = compGain.getNextValue();
                l[i] *= g; r[i] *= g;
            }
    }
    else if (compGain.getCurrentValue() != 1.0f)
    {
        compGain.setCurrentAndTargetValue (1.0f);
    }

    if (wetGain.isSmoothing())
    {
        auto* dl = dry.getReadPointer (0);
        auto* dr = dry.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = wetGain.getNextValue();
            l[i] = dl[i] + (l[i] - dl[i]) * g;
            r[i] = dr[i] + (r[i] - dr[i]) * g;
        }
    }

    if (t != nullptr) t->push (true, buffer, n);
}

juce::ValueTree Node::toState() const
{
    juce::ValueTree v ("Node");
    v.setProperty ("type", nodeTypeToString (type), nullptr);
    v.setProperty ("id", id, nullptr);
    v.setProperty ("bypass", bypassed.load(), nullptr);
    if (autoGain.load())
    {
        v.setProperty ("autoGain", true, nullptr);
        v.setProperty ("autoGainDb", autoGainDb.load(), nullptr);
    }
    if (locked.load()) v.setProperty ("locked", true, nullptr);
    return v;
}

void Node::fromState (const juce::ValueTree& v)
{
    if (v.hasProperty ("id"))
        id = v["id"].toString();
    bypassed = (bool) v.getProperty ("bypass", false);
    autoGain = (bool) v.getProperty ("autoGain", false);
    autoGainDb = (float) v.getProperty ("autoGainDb", 0.0f);
    relearn = false;
    if (autoGain.load() && ! v.hasProperty ("autoGainDb")) requestRelearn();   // v0.2 presets: measure once
    locked = (bool) v.getProperty ("locked", false);
}

//==============================================================================
int Chain::getLatency() const noexcept
{
    int total = 0;
    for (auto& n : nodes)
        total += n->getLatency();
    return total;
}

juce::ValueTree Chain::toState() const
{
    auto v = Node::toState();
    for (auto& n : nodes)
        v.appendChild (n->toState(), nullptr);
    return v;
}

void Chain::prepareImpl (double sr, int block)
{
    for (auto& n : nodes)
        n->prepare (sr, block);
}

void Chain::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    for (auto& node : nodes)
        node->process (b, n);
}

//==============================================================================
static float skewFor (const ParamSpec& s) noexcept
{
    if (! s.hasCentre || s.centre <= s.min || s.centre >= s.max) return 1.0f;
    return (float) (std::log (0.5) / std::log ((s.centre - s.min) / (s.max - s.min)));
}

float ParamSpec::toProportion (float v) const noexcept
{
    if (max <= min) return 0.0f;
    const float p = juce::jlimit (0.0f, 1.0f, (v - min) / (max - min));
    const float k = skewFor (*this);
    return k == 1.0f || p <= 0.0f ? p : std::pow (p, k);
}

float ParamSpec::fromProportion (float p) const noexcept
{
    p = juce::jlimit (0.0f, 1.0f, p);
    const float k = skewFor (*this);
    if (k != 1.0f && p > 0.0f) p = std::exp (std::log (p) / k);
    return min + (max - min) * p;
}

void Block::randomise (juce::Random& rng, float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    const bool full = amount >= 0.999f;
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto& s = specs[i];
        if (s.noRand) continue;
        if (! s.hasRand && s.isDb() && (s.id == "level" || s.id == "output" || s.id == "makeup" || s.id == "ceiling"))
            continue;   // never randomise levels: random rigs should stay at a sane volume

        if (s.toggle || s.isChoice())
        {
            if (full || rng.nextFloat() < amount * 0.5f)
                set ((int) i, s.toggle ? (rng.nextBool() ? 1.0f : 0.0f) : (float) rng.nextInt (s.choices.size()));
            continue;
        }

        const float lo = s.toProportion (s.hasRand ? s.randMin : s.min + (s.max - s.min) * 0.1f);
        const float hi = s.toProportion (s.hasRand ? s.randMax : s.max - (s.max - s.min) * 0.1f);
        float p;
        if (full) p = lo + (hi - lo) * rng.nextFloat();
        else
        {
            const float cur = s.toProportion (get ((int) i));
            p = juce::jlimit (juce::jmin (lo, cur), juce::jmax (hi, cur), cur + (rng.nextFloat() * 2.0f - 1.0f) * 0.35f * amount);
        }
        float v = s.fromProportion (p);
        if (s.decimals == 0 && s.unit != "Hz" && s.unit != "ms" && s.unit != "%") v = (float) juce::roundToInt (v);
        set ((int) i, v);
    }
}

//==============================================================================
Block::Block (NodeType t, std::vector<ParamSpec> s)
    : Node (t), specs (std::move (s)), values (new std::atomic<float>[specs.size()])
{
    for (size_t i = 0; i < specs.size(); ++i)
        values[i].store (specs[i].def);
}

int Block::indexOf (const juce::String& pid) const
{
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].id == pid)
            return (int) i;
    return -1;
}

juce::String Block::formatValue (int i) const { return formatValue (i, get (i)); }

juce::String Block::formatValue (int i, float v) const
{
    const auto& s = specs[(size_t) i];
    if (s.toggle)
        return v > 0.5f ? "On" : "Off";
    if (s.isChoice())
        return s.choices[juce::jlimit (0, s.choices.size() - 1, juce::roundToInt (v))];
    if (s.unit == "Hz" && v >= 1000.0f)
        return juce::String (v / 1000.0f, 2) + " kHz";
    if (s.unit == "ms" && v >= 1000.0f)
        return juce::String (v / 1000.0f, 2) + " s";
    // every dB readout is shown to one decimal place
    const int dp = s.isDb() ? 1 : s.decimals;
    juce::String txt = dp == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, dp);   // String (v, 0) would print every digit
    if (s.isDb() && v > 0.05f)
        txt = "+" + txt;
    if (s.isDb() && v <= s.min && s.min <= -60.0f)
        return "-inf dB";
    return s.unit.isEmpty() ? txt : (s.unit == "%" || s.unit == ":1" ? txt + s.unit : txt + " " + s.unit);
}

void Block::updateMeters (float inPeak, float outPeak, float grDb) noexcept
{
    meterIn.store (gainToDb (inPeak));
    meterOut.store (gainToDb (outPeak));
    meterGr.store (grDb);
}

juce::ValueTree Block::toState() const
{
    auto v = Node::toState();
    for (size_t i = 0; i < specs.size(); ++i)
        v.setProperty ("p_" + specs[i].id, get ((int) i), nullptr);
    return v;
}

void Block::fromState (const juce::ValueTree& v)
{
    Node::fromState (v);
    for (size_t i = 0; i < specs.size(); ++i)
        set ((int) i, (float) v.getProperty ("p_" + specs[i].id, specs[i].def));
}

//==============================================================================
Split::Branch::Branch() : chain (std::make_unique<Chain>()) {}

void Split::Branch::prepare (double sr, int block)
{
    buffer.setSize (2, block, false, true, true);
    chain->prepare (sr, block);
    gainL.reset (sr, 0.03);
    gainR.reset (sr, 0.03);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
    delay.prepare (spec);
    delay.setMaximumDelayInSamples ((int) (sr * 0.5));
    delay.reset();
    xA.prepare (spec);
    xB.prepare (spec);
    compensation = 0;
}

Split::Split() : Node (NodeType::Split) {}

juce::String Split::getDetail() const
{
    return splitModeName (getMode()) + " - " + juce::String ((int) branches.size()) + " paths";
}

int Split::getLatency() const noexcept
{
    int m = 0;
    for (auto& b : branches)
        m = juce::jmax (m, b->chain->getLatency());
    return m;
}

std::unique_ptr<Split::Branch> Split::makeBranch() const
{
    auto b = std::make_unique<Branch>();
    b->prepare (sampleRate, maxBlock);
    return b;
}

void Split::setMode (SplitMode m)
{
    mode = (int) m;
    touched();
    const int n = (int) branches.size();
    if (n == 0)
        return;

    if (m == SplitMode::Split)
    {
        float total = 0;
        for (auto& b : branches) total += b->level.load();
        for (auto& b : branches)
            b->level = total > 0.001f ? b->level.load() / total : 1.0f / (float) n;
    }
    else if (m == SplitMode::Crossover || m == SplitMode::Parallel)
    {
        for (auto& b : branches) b->level = 1.0f;
    }
    else if (m == SplitMode::Stereo)
    {
        for (int i = 0; i < n; ++i)
        {
            branches[(size_t) i]->level = 1.0f;
            branches[(size_t) i]->pan = i == 0 ? -1.0f : (i == 1 ? 1.0f : 0.0f);
        }
    }
}

void Split::setLevel (int index, float v, bool linked)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    touched();
    const int n = (int) branches.size();
    if (! juce::isPositiveAndBelow (index, n))
        return;

    if (! linked || getMode() != SplitMode::Split || n < 2)
    {
        branches[(size_t) index]->level = v;
        return;
    }

    float othersTotal = 0;
    for (int i = 0; i < n; ++i)
        if (i != index) othersTotal += branches[(size_t) i]->level.load();

    const float rest = 1.0f - v;
    for (int i = 0; i < n; ++i)
    {
        if (i == index) continue;
        const float cur = branches[(size_t) i]->level.load();
        branches[(size_t) i]->level = othersTotal > 1.0e-4f ? cur / othersTotal * rest : rest / (float) (n - 1);
    }
    branches[(size_t) index]->level = v;
}

juce::ValueTree Split::toState() const
{
    auto v = Node::toState();
    v.setProperty ("mode", mode.load(), nullptr);
    v.setProperty ("xoverLow", xoverLow.load(), nullptr);
    v.setProperty ("xoverHigh", xoverHigh.load(), nullptr);
    v.setProperty ("autoGainSum", autoGainSum.load(), nullptr);
    for (auto& b : branches)
    {
        juce::ValueTree bt ("Branch");
        bt.setProperty ("level", b->level.load(), nullptr);
        bt.setProperty ("pan", b->pan.load(), nullptr);
        bt.setProperty ("mute", b->mute.load(), nullptr);
        bt.setProperty ("phase", b->phase.load(), nullptr);
        bt.appendChild (b->chain->toState(), nullptr);
        v.appendChild (bt, nullptr);
    }
    return v;
}

void Split::fromState (const juce::ValueTree& v)
{
    Node::fromState (v);
    if (! v.hasProperty ("autoGainSum")) autoGain = false;   // v0.1 stored the sum compensation as "autoGain"
    mode = (int) v.getProperty ("mode", 0);
    xoverLow = (float) v.getProperty ("xoverLow", 250.0f);
    xoverHigh = (float) v.getProperty ("xoverHigh", 2500.0f);
    autoGainSum = (bool) v.getProperty ("autoGainSum", v.getProperty ("autoGain", true));
}

void Split::prepareImpl (double sr, int block)
{
    input.setSize (2, block, false, true, true);
    mix.setSize (2, block, false, true, true);
    for (auto& b : branches)
        b->prepare (sr, block);
}

void Split::processImpl (juce::AudioBuffer<float>& buffer, int n) noexcept
{
    const int nb = (int) branches.size();
    if (nb == 0)
        return;

    for (int ch = 0; ch < 2; ++ch)
        input.copyFrom (ch, 0, buffer, ch, 0, n);
    mix.clear (0, n);

    const auto m = getMode();
    const bool crossover = m == SplitMode::Crossover && nb >= 2;
    const float fLow = xoverLow.load();
    const float fHigh = juce::jmax (fLow * 1.5f, xoverHigh.load());

    float comp = 1.0f;
    if (m == SplitMode::Parallel && autoGainSum.load())
    {
        float total = 0;
        for (auto& b : branches)
            if (! b->mute.load()) total += b->level.load();
        if (total > 1.0f) comp = 1.0f / total;
    }

    int maxLatency = 0;
    for (auto& b : branches)
        maxLatency = juce::jmax (maxLatency, b->chain->getLatency());

    using LRType = juce::dsp::LinkwitzRileyFilterType;

    for (int bi = 0; bi < nb; ++bi)
    {
        auto& b = *branches[(size_t) bi];
        for (int ch = 0; ch < 2; ++ch)
            b.buffer.copyFrom (ch, 0, input, ch, 0, n);

        // --- frequency split (Linkwitz-Riley, allpass-compensated for 3 bands)
        if (crossover && bi < 3)
        {
            LRType ta = LRType::lowpass, tb = LRType::allpass;
            bool useB = nb >= 3;
            if (bi == 0)      { ta = LRType::lowpass;  tb = LRType::allpass; }
            else if (bi == 1) { ta = LRType::highpass; tb = LRType::lowpass; }
            else              { ta = LRType::highpass; tb = LRType::highpass; }

            if (b.xA.getType() != ta) b.xA.setType (ta);
            if (b.xA.getCutoffFrequency() != fLow) b.xA.setCutoffFrequency (fLow);
            if (useB)
            {
                if (b.xB.getType() != tb) b.xB.setType (tb);
                if (b.xB.getCutoffFrequency() != fHigh) b.xB.setCutoffFrequency (fHigh);
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.buffer.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    float x = b.xA.processSample (ch, d[i]);
                    if (useB) x = b.xB.processSample (ch, x);
                    d[i] = x;
                }
            }
        }

        b.chain->process (b.buffer, n);

        // --- align paths with different plug-in latency so blends don't comb-filter
        const int comp_ = maxLatency - b.chain->getLatency();
        if (comp_ != b.compensation)
        {
            b.compensation = comp_;
            b.delay.setDelay ((float) comp_);
        }
        if (b.compensation > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.buffer.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    b.delay.pushSample (ch, d[i]);
                    d[i] = b.delay.popSample (ch);
                }
            }
        }

        // --- level / pan / phase / mute
        const float g = b.mute.load() ? 0.0f : b.level.load() * comp * (b.phase.load() ? -1.0f : 1.0f);
        const float p = b.pan.load();
        b.gainL.setTargetValue (g * juce::jmin (1.0f, 1.0f - p));
        b.gainR.setTargetValue (g * juce::jmin (1.0f, 1.0f + p));

        auto* sl = b.buffer.getReadPointer (0);
        auto* sr = b.buffer.getReadPointer (1);
        auto* ml = mix.getWritePointer (0);
        auto* mr = mix.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            ml[i] += sl[i] * b.gainL.getNextValue();
            mr[i] += sr[i] * b.gainR.getNextValue();
        }
    }

    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom (ch, 0, mix, ch, 0, n);
}

} // namespace rig
