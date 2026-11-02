#include "Randomiser.h"

namespace rig
{
/** Blocks the dice never touch: locked ones, and tuners (a tool, not a sound). */
static bool keeps (const Node& n) { return n.locked.load() || n.getType() == NodeType::Tuner; }

static std::vector<NodeType> builtInsFor (Category c)
{
    switch (c)
    {
        case Category::Dynamics:   return { NodeType::Gate, NodeType::Comp, NodeType::Limiter, NodeType::Expander, NodeType::Transient, NodeType::Swell };
        case Category::Drive:      return { NodeType::Overdrive, NodeType::Fuzz, NodeType::Boost, NodeType::Crusher };
        case Category::Eq:         return { NodeType::Eq };
        case Category::Filter:     return { NodeType::Wah };
        case Category::Modulation: return { NodeType::Chorus, NodeType::Flanger, NodeType::Phaser, NodeType::Tremolo, NodeType::Vibe, NodeType::Rotary };
        case Category::Delay:      return { NodeType::Delay };
        case Category::Reverb:     return { NodeType::Reverb };
        case Category::Pitch:      return { NodeType::Pitch };
        case Category::Utility:    return { NodeType::Gain };
        default:                   return {};
    }
}

/** Where a category sits in a classic signal chain (for placing locked blocks). */
static int chainRank (Category c)
{
    switch (c)
    {
        case Category::Dynamics:   return 1;
        case Category::Filter:     return 2;
        case Category::Pitch:      return 3;
        case Category::Drive:      return 3;
        case Category::Amp:        return 5;
        case Category::Routing:    return 5;
        case Category::Cab:        return 6;
        case Category::Eq:         return 7;
        case Category::Modulation: return 8;
        case Category::Other:      return 8;
        case Category::Delay:      return 9;
        case Category::Reverb:     return 10;
        case Category::Utility:    return 11;
    }
    return 8;
}

Randomiser::Randomiser (Engine& e) : engine (e) {}

Category Randomiser::categoryOf (const Node& n)
{
    if (auto* pb = dynamic_cast<const PluginBlock*> (&n))
        return categoriseDescription (pb->getDescription());
    return categoryForBuiltIn (n.getType());
}

void Randomiser::collectFiles()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (lastScan != 0 && now - lastScan < 10000 && lastExclusions == engine.exclusionsVersion) return;
    lastScan = now;
    lastExclusions = engine.exclusionsVersion;
    auto scan = [this] (const juce::FileSearchPath& paths, const juce::String& pattern, juce::Array<juce::File>& out)
    {
        out.clear();
        for (int p = 0; p < paths.getNumPaths() && out.size() < 4000; ++p)
            for (const auto& entry : juce::RangedDirectoryIterator (paths.getRawString (p), true, pattern, juce::File::findFiles))
            {
                if (engine.isRandomExcluded (entry.getFile())) continue;   // unticked in the library
                out.add (entry.getFile());
                if (out.size() >= 4000) break;
            }
    };
    scan (engine.getPaths ("modelPaths"), "*.nam", models);
    scan (engine.getPaths ("irPaths"), "*.wav;*.aif;*.aiff;*.flac", irs);
}

juce::File Randomiser::pick (const juce::Array<juce::File>& files)
{
    return files.isEmpty() ? juce::File() : files[rng.nextInt (files.size())];
}

std::unique_ptr<Node> Randomiser::makeBuiltIn (NodeType t)
{
    if (engine.isRandomExcluded (t)) return nullptr;   // unticked in the library: that slot stays empty
    auto n = engine.createNode (t);
    auto* b = dynamic_cast<Block*> (n.get());
    if (b == nullptr) return n;

    juce::String err;
    if (auto* nb = dynamic_cast<NamBlock*> (b))
    {
        collectFiles();
        // try a couple of files in case one is unreadable
        for (int attempt = 0; attempt < 3 && ! models.isEmpty(); ++attempt)
            if (nb->loadModel (pick (models), err)) break;
    }
    else if (auto* ir = dynamic_cast<IrBlock*> (b))
    {
        collectFiles();
        if (! irs.isEmpty()) ir->loadIr (pick (irs), err);
    }
    b->randomise (rng, 1.0f);
    b->syncEdits();
    return n;
}

std::unique_ptr<Node> Randomiser::makePlugin (const juce::PluginDescription& d)
{
    juce::String err;
    auto n = engine.createPlugin (d, err);
    if (auto* pb = dynamic_cast<PluginBlock*> (n.get()))
    {
        pb->randomise (rng, 1.0f);
        pb->autoGain = true;   // unknown plug-in levels: let gain compensation level-match it
        pb->requestRelearn();
    }
    return n;
}

std::unique_ptr<Node> Randomiser::makeRandom (Category cat, const Node* avoid)
{
    collectFiles();

    struct Candidate { NodeType type; const juce::PluginDescription* plugin; float weight; };
    std::vector<Candidate> cands;

    const auto avoidType = avoid != nullptr ? avoid->getType() : NodeType::Chain;
    juce::String avoidPlugin;
    if (auto* pb = dynamic_cast<const PluginBlock*> (avoid)) avoidPlugin = pb->getDescription().createIdentifierString();

    if (cat == Category::Amp)
        cands.push_back ({ NodeType::Nam, nullptr, models.isEmpty() ? 0.01f : 4.0f });   // a NAM amp can always change model
    else if (cat == Category::Cab)
        cands.push_back ({ NodeType::Ir, nullptr, irs.isEmpty() ? 0.01f : 4.0f });
    else
        for (auto t : builtInsFor (cat))
            if (! engine.isRandomExcluded (t))
                cands.push_back ({ t, nullptr, t == NodeType::Comp ? 2.5f : (t == NodeType::Gate || t == NodeType::Limiter ? 0.6f : 1.0f) });

    const auto types = engine.knownPlugins.getTypes();
    if (options.usePlugins)
        for (auto& d : types)
            if (! d.isInstrument && categoriseDescription (d) == cat && ! engine.isRandomExcluded (d))
                cands.push_back ({ NodeType::Plugin, &d, 1.0f });

    // prefer something different from what's there now (but a NAM/IR always gets a new file)
    if (cands.size() > 1 && avoid != nullptr)
        cands.erase (std::remove_if (cands.begin(), cands.end(), [&] (const Candidate& c)
        {
            if (c.plugin != nullptr) return c.plugin->createIdentifierString() == avoidPlugin;
            return c.type == avoidType && c.type != NodeType::Nam && c.type != NodeType::Ir && builtInsFor (cat).size() > 1;
        }), cands.end());

    for (int attempt = 0; attempt < 4 && ! cands.empty(); ++attempt)
    {
        float total = 0;
        for (auto& c : cands) total += c.weight;
        float r = rng.nextFloat() * total;
        size_t i = 0;
        for (; i < cands.size() - 1; ++i) { r -= cands[i].weight; if (r <= 0) break; }

        auto n = cands[i].plugin != nullptr ? makePlugin (*cands[i].plugin) : makeBuiltIn (cands[i].type);
        if (n != nullptr) return n;
        cands.erase (cands.begin() + (long) i);   // plug-in failed to load: try another
    }
    return nullptr;
}

std::unique_ptr<Node> Randomiser::makeAmp()
{
    if (auto n = makeRandom (Category::Amp)) return n;
    return engine.createNode (NodeType::Nam);
}

std::unique_ptr<Node> Randomiser::makeCab()
{
    if (auto n = makeRandom (Category::Cab)) return n;
    return engine.createNode (NodeType::Ir);
}

//==============================================================================
void Randomiser::randomiseRig()
{
    Engine::UndoGroup undo (engine);
    collectFiles();

    auto chain = std::make_unique<Chain>();
    auto add = [] (Chain& c, std::unique_ptr<Node> n) { if (n != nullptr) c.nodes.push_back (std::move (n)); };
    auto chance = [this] (float p) { return rng.nextFloat() < p; };
    auto oneOf = [this] (std::initializer_list<NodeType> ts) { return *(ts.begin() + rng.nextInt ((int) ts.size())); };

    // --- front end: gate, wah, dynamics, drive, pitch
    if (chance (0.7f))  add (*chain, makeBuiltIn (NodeType::Gate));
    if (chance (0.12f)) add (*chain, makeRandom (Category::Filter));
    if (chance (0.4f))  add (*chain, options.usePlugins && chance (0.3f) ? makeRandom (Category::Dynamics)
                                                                         : makeBuiltIn (chance (0.7f) ? NodeType::Comp : oneOf ({ NodeType::Transient, NodeType::Swell, NodeType::Expander })));
    if (chance (0.6f))  add (*chain, makeRandom (Category::Drive));
    if (chance (0.1f))  add (*chain, makeRandom (Category::Pitch));

    // --- amps & cabs: one, or two/three in a split
    if (options.allowSplits && chance (0.3f))
    {
        auto s = std::make_unique<Split>();
        const int n = chance (0.15f) ? 3 : 2;
        for (int i = 0; i < n; ++i)
        {
            auto b = std::make_unique<Split::Branch>();
            add (*b->chain, makeAmp());
            add (*b->chain, makeCab());
            if (chance (0.25f))
                add (*b->chain, makeRandom (chance (0.5f) ? Category::Modulation : Category::Delay));
            s->branches.push_back (std::move (b));
        }
        const float m = rng.nextFloat();
        s->setMode (m < 0.4f ? SplitMode::Split : (m < 0.8f && n == 2 ? SplitMode::Stereo : SplitMode::Parallel));
        randomiseSplit (*s, 1.0f);
        add (*chain, std::move (s));
    }
    else
    {
        add (*chain, makeAmp());
        add (*chain, makeCab());
    }

    // --- post: EQ, modulation, delay, reverb
    if (chance (0.35f)) add (*chain, makeRandom (Category::Eq));
    if (chance (0.55f)) add (*chain, makeRandom (Category::Modulation));
    if (chance (0.12f)) add (*chain, makeRandom (Category::Modulation));
    if (chance (0.6f))  add (*chain, makeRandom (Category::Delay));
    if (chance (0.7f))  add (*chain, makeRandom (Category::Reverb));

    // --- keep locked blocks: they take the place of a generated block of the same kind,
    //     or slot in where that kind of effect usually goes
    std::vector<std::unique_ptr<Node>> graveyard;
    engine.installRoot (std::move (chain), [&graveyard] (Chain& oldRoot, Chain& newRoot)
    {
        std::vector<std::unique_ptr<Node>> keep;
        std::function<void (Chain&)> take = [&] (Chain& c)
        {
            for (size_t i = 0; i < c.nodes.size();)
            {
                if (keeps (*c.nodes[i]))
                {
                    keep.push_back (std::move (c.nodes[i]));
                    c.nodes.erase (c.nodes.begin() + (long) i);
                    continue;
                }
                if (auto* sp = dynamic_cast<Split*> (c.nodes[i].get()))
                    for (auto& b : sp->branches) take (*b->chain);
                ++i;
            }
        };
        take (oldRoot);

        std::vector<Node*> placed;
        for (auto& k : keep)
        {
            const auto cat = categoryOf (*k);
            bool done = false;
            std::function<bool (Chain&)> swapIn = [&] (Chain& c)
            {
                for (auto& slot : c.nodes)
                {
                    if (std::find (placed.begin(), placed.end(), slot.get()) == placed.end() && categoryOf (*slot) == cat)
                    {
                        graveyard.push_back (std::move (slot));
                        slot = std::move (k);
                        placed.push_back (slot.get());
                        return true;
                    }
                    if (auto* sp = dynamic_cast<Split*> (slot.get()))
                        for (auto& b : sp->branches)
                            if (swapIn (*b->chain)) return true;
                }
                return false;
            };
            done = swapIn (newRoot);
            if (! done)
            {
                const int rank = chainRank (cat);
                size_t at = 0;
                while (at < newRoot.nodes.size() && chainRank (categoryOf (*newRoot.nodes[at])) <= rank) ++at;
                placed.push_back (k.get());
                newRoot.nodes.insert (newRoot.nodes.begin() + (long) at, std::move (k));
            }
        }
    });
    graveyard.clear();   // generated blocks that a locked block replaced (never went live)

    engine.presetName = "Random Rig";
    engine.presetFile = juce::File();
    engine.markDirty();
}

void Randomiser::reshuffleBlocks()
{
    Engine::UndoGroup undo (engine);
    juce::StringArray ids;
    visitNodes (engine.getRoot(), [&ids] (Node& n)
    {
        if (dynamic_cast<Block*> (&n) != nullptr && ! keeps (n)) ids.add (n.id);
    });
    for (auto& id : ids)
        randomiseType (id);
}

void Randomiser::randomiseAllSettings (float amount)
{
    Engine::UndoGroup undo (engine);
    visitNodes (engine.getRoot(), [this, amount] (Node& n)
    {
        if (keeps (n) || &n == &engine.getRoot()) return;
        if (auto* b = dynamic_cast<Block*> (&n)) b->randomise (rng, amount);
        else if (auto* s = dynamic_cast<Split*> (&n)) randomiseSplit (*s, amount);
    });
    engine.storeScene();
    engine.markDirty();
}

void Randomiser::randomiseSettings (Node& n, float amount)
{
    engine.checkpoint();
    if (auto* b = dynamic_cast<Block*> (&n)) b->randomise (rng, amount);
    else if (auto* s = dynamic_cast<Split*> (&n)) randomiseSplit (*s, amount);
    engine.storeScene();
    engine.markDirty();
}

void Randomiser::randomiseSplit (Split& s, float amount)
{
    const int n = (int) s.branches.size();
    if (n == 0) return;
    if (s.getMode() == SplitMode::Split)
    {
        std::vector<float> w ((size_t) n);
        float total = 0;
        for (int i = 0; i < n; ++i)
        {
            const float cur = s.branches[(size_t) i]->level.load();
            w[(size_t) i] = juce::jmax (0.05f, amount >= 0.999f ? 0.3f + rng.nextFloat() : cur + (rng.nextFloat() - 0.5f) * 0.4f * amount);
            total += w[(size_t) i];
        }
        for (int i = 0; i < n; ++i) s.branches[(size_t) i]->level = w[(size_t) i] / total;
    }
    else if (s.getMode() == SplitMode::Parallel)
    {
        for (auto& b : s.branches)
            b->level = juce::jlimit (0.2f, 1.0f, amount >= 0.999f ? 0.4f + 0.6f * rng.nextFloat() : b->level.load() + (rng.nextFloat() - 0.5f) * 0.4f * amount);
    }
    s.touched();
}

Node* Randomiser::randomiseType (const juce::String& id)
{
    auto* n = engine.findNode (id);
    if (n == nullptr || dynamic_cast<Block*> (n) == nullptr || n->getType() == NodeType::Tuner) return nullptr;
    auto fresh = makeRandom (categoryOf (*n), n);
    if (fresh == nullptr) return nullptr;
    return engine.replace (id, std::move (fresh));
}

} // namespace rig
