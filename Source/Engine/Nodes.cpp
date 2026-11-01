#include "Nodes.h"

namespace rig
{
juce::String nodeTypeToString (NodeType t)
{
    switch (t)
    {
        case NodeType::Chain:  return "chain";
        case NodeType::Split:  return "split";
        case NodeType::Nam:    return "nam";
        case NodeType::Ir:     return "ir";
        case NodeType::Gate:   return "gate";
        case NodeType::Eq:     return "eq";
        case NodeType::Gain:   return "gain";
        case NodeType::Comp:   return "comp";
        case NodeType::Plugin: return "plugin";
    }
    return "gain";
}

NodeType nodeTypeFromString (const juce::String& s)
{
    for (auto t : { NodeType::Chain, NodeType::Split, NodeType::Nam, NodeType::Ir, NodeType::Gate,
                    NodeType::Eq, NodeType::Gain, NodeType::Comp, NodeType::Plugin })
        if (nodeTypeToString (t) == s)
            return t;
    return NodeType::Gain;
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
    prepareImpl (sr, maxBlock);
}

void Node::process (juce::AudioBuffer<float>& buffer, int n) noexcept
{
    wetGain.setTargetValue (bypassed.load (std::memory_order_relaxed) ? 0.0f : 1.0f);

    if (! wetGain.isSmoothing())
    {
        if (wetGain.getCurrentValue() < 0.5f)
            return;  // fully bypassed: untouched
        processImpl (buffer, n);
        return;
    }

    // click-free bypass crossfade
    for (int ch = 0; ch < 2; ++ch)
        dry.copyFrom (ch, 0, buffer, ch, 0, n);

    processImpl (buffer, n);

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    auto* dl = dry.getReadPointer (0);
    auto* dr = dry.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = wetGain.getNextValue();
        l[i] = dl[i] + (l[i] - dl[i]) * g;
        r[i] = dr[i] + (r[i] - dr[i]) * g;
    }
}

juce::ValueTree Node::toState() const
{
    juce::ValueTree v ("Node");
    v.setProperty ("type", nodeTypeToString (type), nullptr);
    v.setProperty ("id", id, nullptr);
    v.setProperty ("bypass", bypassed.load(), nullptr);
    return v;
}

void Node::fromState (const juce::ValueTree& v)
{
    if (v.hasProperty ("id"))
        id = v["id"].toString();
    bypassed = (bool) v.getProperty ("bypass", false);
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

juce::String Block::formatValue (int i) const
{
    const auto& s = specs[(size_t) i];
    const float v = get (i);
    if (s.toggle)
        return v > 0.5f ? "On" : "Off";
    if (s.unit == "Hz" && v >= 1000.0f)
        return juce::String (v / 1000.0f, 2) + " kHz";
    juce::String txt = juce::String (v, s.decimals);
    if (s.unit == "dB" && v > 0.0f)
        txt = "+" + txt;
    return s.unit.isEmpty() ? txt : txt + " " + s.unit;
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
    v.setProperty ("autoGain", autoGain.load(), nullptr);
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
    mode = (int) v.getProperty ("mode", 0);
    xoverLow = (float) v.getProperty ("xoverLow", 250.0f);
    xoverHigh = (float) v.getProperty ("xoverHigh", 2500.0f);
    autoGain = (bool) v.getProperty ("autoGain", true);
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
    if (m == SplitMode::Parallel && autoGain.load())
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
