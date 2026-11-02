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
    return n.getCategory();   // a NAM pedal capture is a drive, a space IR a reverb
}

void Randomiser::Pool::finish()
{
    std::map<juce::String, int> at;
    groups.clear();
    for (auto& f : files)
    {
        const auto key = captureGroupKey (f);
        auto it = at.find (key);
        if (it == at.end()) { it = at.emplace (key, (int) groups.size()).first; groups.emplace_back(); }
        groups[(size_t) it->second].add (f);
    }
}

void Randomiser::collectFiles()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (lastScan != 0 && now - lastScan < 10000 && lastExclusions == engine.exclusionsVersion) return;
    lastScan = now;
    lastExclusions = engine.exclusionsVersion;

    // the library's scan (grouped by gear), looked at again if it's older than half a minute
    auto& lib = engine.captures;
    models.clear(); pedalModels.clear(); irs.clear(); spaceIrs.clear();
    for (auto& g : lib.get (false, 30000).groups)
        for (auto& f : g->files)
        {
            if (engine.isRandomExcluded (f)) continue;   // switched off in the library
            switch (g->gear)
            {
                // amps for the amp slot, pedal captures for drives
                case Gear::Pedal:                             pedalModels.add (f); break;
                case Gear::Outboard: case Gear::Experimental: break;   // not a sound to roll by chance
                default:                                      models.add (f); break;
            }
        }
    for (auto& g : lib.get (true, 30000).groups)
        for (auto& f : g->files)
        {
            if (engine.isRandomExcluded (f)) continue;
            switch (g->gear)
            {
                // cabinets for the cab slot, rooms and halls for the reverb slot
                case Gear::Space:                             spaceIrs.add (f); break;
                case Gear::Pedal: case Gear::Experimental:    break;
                default:                                      irs.add (f); break;
            }
        }
    for (auto* p : { &models, &pedalModels, &irs, &spaceIrs }) p->finish();
}

juce::File Randomiser::pick (const Pool& pool)
{
    // every piece of gear gets the same chance, however many captures it has; then any one of them
    if (pool.groups.empty()) return {};
    const auto& g = pool.groups[(size_t) rng.nextInt ((int) pool.groups.size())];
    return g[rng.nextInt (g.size())];
}

std::unique_ptr<Node> Randomiser::makeBuiltIn (NodeType t, int variant)
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
        const auto& pool = variant == 1 ? pedalModels : models;
        for (int attempt = 0; attempt < 3 && ! pool.isEmpty(); ++attempt)
            if (nb->loadModel (pick (pool), err)) break;
    }
    else if (auto* ir = dynamic_cast<IrBlock*> (b))
    {
        collectFiles();
        const auto& pool = variant == 1 ? spaceIrs : irs;
        if (! pool.isEmpty()) ir->loadIr (pick (pool), err);
        if (variant == 1) ir->set (2, 35.0f);   // a room / hall IR as a send-style blend
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

    struct Candidate { NodeType type; const juce::PluginDescription* plugin; float weight; int variant = 0; };
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

    // NAM pedal captures can be the drive; space IRs can be the reverb
    if (cat == Category::Drive && ! pedalModels.isEmpty())  cands.push_back ({ NodeType::Nam, nullptr, 1.5f, 1 });
    if (cat == Category::Reverb && ! spaceIrs.isEmpty())    cands.push_back ({ NodeType::Ir, nullptr, 0.6f, 1 });

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
            return c.variant == 0 && c.type == avoidType && c.type != NodeType::Nam && c.type != NodeType::Ir && builtInsFor (cat).size() > 1;
        }), cands.end());

    for (int attempt = 0; attempt < 4 && ! cands.empty(); ++attempt)
    {
        float total = 0;
        for (auto& c : cands) total += c.weight;
        float r = rng.nextFloat() * total;
        size_t i = 0;
        for (; i < cands.size() - 1; ++i) { r -= cands[i].weight; if (r <= 0) break; }

        auto n = cands[i].plugin != nullptr ? makePlugin (*cands[i].plugin) : makeBuiltIn (cands[i].type, cands[i].variant);
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
    // an "Amp + Cab" capture already has its cabinet: no IR after it
    auto addAmpAndCab = [&] (Chain& c)
    {
        auto amp = makeAmp();
        const bool hasCab = [&] { auto* nb = dynamic_cast<NamBlock*> (amp.get()); return nb != nullptr && gearIncludesCab (nb->getGear()); }();
        add (c, std::move (amp));
        if (! hasCab) add (c, makeCab());
    };

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
            addAmpAndCab (*b->chain);
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
        addAmpAndCab (*chain);

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
