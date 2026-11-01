#include "Engine.h"

namespace rig
{
//==============================================================================
bool MidiMapping::isGlobal() const noexcept
{
    return target == Target::Scene || target == Target::PresetNext || target == Target::PresetPrev
        || target == Target::Tuner || target == Target::MasterVolume;
}

juce::ValueTree MidiMapping::toState() const
{
    juce::ValueTree v ("Map");
    v.setProperty ("cc", cc, nullptr);
    v.setProperty ("ch", channel, nullptr);
    v.setProperty ("target", (int) target, nullptr);
    v.setProperty ("node", nodeId, nullptr);
    v.setProperty ("index", index, nullptr);
    v.setProperty ("latching", latching, nullptr);
    return v;
}

MidiMapping MidiMapping::fromState (const juce::ValueTree& v)
{
    MidiMapping m;
    m.cc = v.getProperty ("cc", -1);
    m.channel = v.getProperty ("ch", 0);
    m.target = (Target) (int) v.getProperty ("target", 0);
    m.nodeId = v.getProperty ("node").toString();
    m.index = v.getProperty ("index", 0);
    m.latching = v.getProperty ("latching", false);
    return m;
}

//==============================================================================
Engine::Engine() : root (std::make_unique<Chain>())
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Rigsmith";
    o.filenameSuffix = ".settings";
    o.folderName = "Rigsmith";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);

    juce::addDefaultFormatsToManager (formatManager);

    if (auto* s = props.getUserSettings())
        if (auto xml = s->getXmlValue ("pluginList"))
            knownPlugins.recreateFromXml (*xml);
    PluginBlock::autoCategorise = getSetting ("autoCategorise", true);
    knownPlugins.addChangeListener (this);

    loadDefaultRig();
    for (auto& s : scenes) s = captureScene();
    startTimerHz (100);
}

Engine::~Engine()
{
    stopTimer();
    knownPlugins.removeChangeListener (this);
}

void Engine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
        if (auto* s = props.getUserSettings())
        {
            s->setValue ("pluginList", xml.get());
            s->saveIfNeeded();
        }
}

juce::File Engine::getFolder (const juce::String& key) const
{
    if (auto* s = const_cast<juce::ApplicationProperties&> (props).getUserSettings())
    {
        const juce::File f (s->getValue (key));
        if (f.isDirectory()) return f;
    }
    auto base = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rigsmith");
    const auto sub = key == "modelFolder" ? "Models" : key == "irFolder" ? "IRs" : key == "presetFolder" ? "Presets" : "";
    auto f = juce::String (sub).isEmpty() ? base : base.getChildFile (sub);
    f.createDirectory();
    return f;
}

juce::FileSearchPath Engine::getPaths (const juce::String& key) const
{
    juce::FileSearchPath p;
    if (auto* s = const_cast<juce::ApplicationProperties&> (props).getUserSettings())
        p = juce::FileSearchPath (s->getValue (key));
    p.removeNonExistentPaths();
    if (p.getNumPaths() == 0)
        p.add (getFolder (key == "modelPaths" ? "modelFolder" : "irFolder"));   // v0.1 single folder, or the default
    return p;
}

void Engine::setPaths (const juce::String& key, const juce::FileSearchPath& p)
{
    if (auto* s = props.getUserSettings())
    {
        s->setValue (key, p.toString());
        s->saveIfNeeded();
    }
}

bool Engine::getSetting (const juce::String& key, bool def) const
{
    if (auto* s = const_cast<juce::ApplicationProperties&> (props).getUserSettings())
        return s->getBoolValue (key, def);
    return def;
}

void Engine::setSetting (const juce::String& key, bool value)
{
    if (auto* s = props.getUserSettings())
    {
        s->setValue (key, value);
        s->saveIfNeeded();
    }
}

void Engine::setAnalyzedNode (Node* n)
{
    visitNodes (*root, [] (Node& x) { x.tap = nullptr; });
    analyzer.reset();
    if (n != nullptr) n->tap = &analyzer;
}

void Engine::setFolder (const juce::String& key, const juce::File& f)
{
    if (auto* s = props.getUserSettings())
    {
        s->setValue (key, f.getFullPathName());
        s->saveIfNeeded();
    }
}

juce::File Engine::getDeadMansPedalFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Rigsmith").getChildFile ("scan-in-progress.txt");
}

//==============================================================================
void Engine::prepare (double sr, int block)
{
    sampleRate = sr;
    blockSize = juce::jmax (16, block);
    {
        const juce::ScopedLock sl (lock);
        root->prepare (sampleRate, blockSize);
    }
    tuner.prepare (sr);
    limiter.prepare ({ sr, (juce::uint32) blockSize, 2 });
    limiter.setThreshold (-0.3f);
    limiter.setRelease (60.0f);
    inGain.reset (sr, 0.02);
    outGain.reset (sr, 0.02);
    reportedLatency = -1;
}

int Engine::getLatency() const noexcept { return root->getLatency(); }

void Engine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        int type = -1, num = 0, val = 0;
        if (m.isController())        { type = 0; num = m.getControllerNumber(); val = m.getControllerValue(); }
        else if (m.isProgramChange()) { type = 1; num = m.getProgramChangeNumber(); }
        if (type >= 0 && midiFifo.getFreeSpace() > 0)
        {
            const auto packed = (juce::uint32) ((type << 24) | ((m.getChannel() & 0xff) << 16) | ((num & 0x7f) << 8) | (val & 0x7f));
            midiFifo.write (1).forEach ([&] (int idx) { midiData[(size_t) idx] = packed; });
        }
    }

    const int total = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || total == 0)
        return;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    // --- input routing
    switch (inputMode.load())
    {
        case Input1:   juce::FloatVectorOperations::copy (R, L, total); break;
        case Input2:   juce::FloatVectorOperations::copy (L, R, total); break;
        case InputSum: juce::FloatVectorOperations::add (L, R, total); juce::FloatVectorOperations::copy (R, L, total); break;
        default: break;
    }

    inGain.setTargetValue (dbToGain (inputGainDb.load()));
    float peak = 0;
    for (int i = 0; i < total; ++i)
    {
        const float g = inGain.getNextValue();
        L[i] *= g; R[i] *= g;
        peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
    }
    inPeak.store (juce::jmax (inPeak.load(), peak));
    if (tunerActive.load())
        tuner.push (L, total);

    // --- the rig, in chunks no larger than prepared
    {
        const juce::ScopedTryLock tl (lock);
        for (int off = 0; off < total; off += blockSize)
        {
            const int len = juce::jmin (blockSize, total - off);
            float* ptrs[2] = { L + off, R + off };
            juce::AudioBuffer<float> view (ptrs, 2, len);
            if (tl.isLocked()) root->process (view, len);
            else view.clear();
        }
    }

    if (tunerMute.load() && tunerActive.load())
        buffer.clear (0, 0, total), buffer.clear (1, 0, total);

    outGain.setTargetValue (dbToGain (outputGainDb.load()));
    for (int i = 0; i < total; ++i)
    {
        const float g = outGain.getNextValue();
        L[i] *= g; R[i] *= g;
    }

    if (limiterOn.load())
    {
        for (int off = 0; off < total; off += blockSize)
        {
            const int len = juce::jmin (blockSize, total - off);
            float* ptrs[2] = { L + off, R + off };
            juce::dsp::AudioBlock<float> blk (ptrs, 2, (size_t) len);
            limiter.process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
    }

    peak = juce::jmax (buffer.getMagnitude (0, 0, total), buffer.getMagnitude (1, 0, total));
    outPeak.store (juce::jmax (outPeak.load(), peak));
}

//==============================================================================
void Engine::timerCallback()
{
    while (midiFifo.getNumReady() > 0)
    {
        juce::uint32 packed = 0;
        midiFifo.read (1).forEach ([&] (int idx) { packed = midiData[(size_t) idx]; });
        handleMidi ((int) (packed >> 24), (int) ((packed >> 16) & 0xff), (int) ((packed >> 8) & 0x7f), (int) (packed & 0x7f));
    }

    const int hs = pendingHostScene.exchange (-1);
    if (hs >= 0 && hs != activeScene)
        setScene (hs);

    if (++tick % 10 == 0)
    {
        const int lat = getLatency();
        if (lat != reportedLatency)
        {
            reportedLatency = lat;
            if (onLatencyChanged) onLatencyChanged (lat);
        }
    }
}

void Engine::handleMidi (int type, int channel, int number, int value)
{
    if (type == 1)
    {
        auto files = getPresetFiles();
        if (juce::isPositiveAndBelow (number, files.size()))
            loadPreset (files[number]);
        return;
    }

    if (learning)
    {
        learning = false;
        learnProto.cc = number;
        learnProto.channel = 0;
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& m)
        {
            return m.target == learnProto.target && m.nodeId == learnProto.nodeId && m.index == learnProto.index;
        }), mappings.end());
        mappings.push_back (learnProto);
        markDirty();
        message ("Mapped CC " + juce::String (number) + " to " + describe (learnProto));
        listeners.call ([] (Listener& l) { l.midiMapChanged(); });
        return;
    }

    const int ch = juce::jlimit (1, 16, channel);
    const int previous = lastCcValue[ch - 1][number];
    lastCcValue[ch - 1][number] = (juce::uint8) value;

    for (const auto& m : mappings)
    {
        if (m.cc != number || (m.channel != 0 && m.channel != ch))
            continue;

        const bool continuous = m.target == MidiMapping::Target::BranchLevel || m.target == MidiMapping::Target::Blend
                             || m.target == MidiMapping::Target::Param || m.target == MidiMapping::Target::MasterVolume;
        if (continuous || m.latching)
            applyMapping (m, value);
        else if (value >= 64 && previous < 64)   // rising edge = one press
            applyMapping (m, 127);
        else if (value >= 64 && previous >= 64 && value == 127)  // switches that only ever send 127
            applyMapping (m, 127);
    }
}

void Engine::applyMapping (const MidiMapping& m, int value)
{
    const float v = (float) value / 127.0f;
    using T = MidiMapping::Target;

    switch (m.target)
    {
        case T::Scene:      if (value >= 64) setScene (m.index); return;
        case T::PresetNext: if (value >= 64) stepPreset (1); return;
        case T::PresetPrev: if (value >= 64) stepPreset (-1); return;
        case T::Tuner:
            if (value >= 64 || m.latching)
            {
                tunerActive = m.latching ? value >= 64 : ! tunerActive.load();
                listeners.call ([] (Listener& l) { l.tunerToggled(); });
            }
            return;
        case T::MasterVolume: outputGainDb = juce::jmap (v, -40.0f, 6.0f); return;
        default: break;
    }

    auto* node = findNode (m.nodeId);
    if (node == nullptr) return;

    if (m.target == T::Bypass)
    {
        node->bypassed = m.latching ? value < 64 : ! node->bypassed.load();
    }
    else if (auto* s = dynamic_cast<Split*> (node))
    {
        if (m.target == T::BranchLevel)
            s->setLevel (m.index, v, true);
        else if (m.target == T::Blend && s->branches.size() >= 2)
        {
            s->branches[0]->level = 1.0f - v;
            s->branches[1]->level = v;
        }
    }
    else if (auto* pb = dynamic_cast<PluginBlock*> (node); pb != nullptr && m.target == T::Param && m.index >= 1000)
    {
        if (auto* inst = pb->getInstance())
        {
            auto params = inst->getParameters();
            if (juce::isPositiveAndBelow (m.index - 1000, params.size()))
                params[m.index - 1000]->setValueNotifyingHost (v);
        }
    }
    else if (auto* b = dynamic_cast<Block*> (node))
    {
        if (m.target == T::Param && juce::isPositiveAndBelow (m.index, b->numParams()))
        {
            const auto& sp = b->getSpecs()[(size_t) m.index];
            b->set (m.index, sp.min + (sp.max - sp.min) * v);
        }
    }

    storeScene();
    listeners.call ([] (Listener& l) { l.sceneChanged(); });
}

void Engine::startLearn (MidiMapping proto)
{
    learnProto = proto;
    learning = true;
    message ("MIDI learn: move a controller or press a footswitch for " + describe (proto));
}

void Engine::cancelLearn()
{
    learning = false;
    message ({});
}

void Engine::removeMapping (int index)
{
    if (juce::isPositiveAndBelow (index, (int) mappings.size()))
    {
        mappings.erase (mappings.begin() + index);
        markDirty();
        listeners.call ([] (Listener& l) { l.midiMapChanged(); });
    }
}

juce::String Engine::describe (const MidiMapping& m) const
{
    using T = MidiMapping::Target;
    auto nodeName = [this, &m]
    {
        auto* n = findNode (m.nodeId);
        return n != nullptr ? n->getName() : juce::String ("(missing block)");
    };
    switch (m.target)
    {
        case T::Scene:        return "Scene " + sceneNames[juce::jlimit (0, kNumScenes - 1, m.index)];
        case T::Bypass:       return "Bypass " + nodeName();
        case T::BranchLevel:  return nodeName() + " path " + juce::String::charToString ((juce::juce_wchar) ('A' + m.index)) + " level";
        case T::Blend:        return nodeName() + " A/B blend";
        case T::Param:
        {
            if (auto* pb = dynamic_cast<PluginBlock*> (findNode (m.nodeId)); pb != nullptr && m.index >= 1000 && pb->getInstance() != nullptr)
            {
                auto params = pb->getInstance()->getParameters();
                if (juce::isPositiveAndBelow (m.index - 1000, params.size()))
                    return pb->getName() + " " + params[m.index - 1000]->getName (32);
            }
            if (auto* b = dynamic_cast<Block*> (findNode (m.nodeId)))
                if (juce::isPositiveAndBelow (m.index, b->numParams()))
                    return b->getName() + " " + b->getSpecs()[(size_t) m.index].name;
            return nodeName();
        }
        case T::PresetNext:   return "Next preset";
        case T::PresetPrev:   return "Previous preset";
        case T::Tuner:        return "Tuner";
        case T::MasterVolume: return "Master volume";
    }
    return {};
}

//==============================================================================
Node* Engine::findNode (const juce::String& id) const
{
    Node* found = nullptr;
    if (id.isEmpty()) return nullptr;
    visitNodes (*root, [&] (Node& n) { if (found == nullptr && n.id == id) found = &n; });
    return found;
}

Engine::Location Engine::locate (const juce::String& id) const
{
    Location loc;
    visitNodes (*root, [&] (Node& n)
    {
        if (loc.chain != nullptr) return;
        if (auto* c = dynamic_cast<Chain*> (&n))
            for (int i = 0; i < (int) c->nodes.size(); ++i)
                if (c->nodes[(size_t) i]->id == id) { loc.chain = c; loc.index = i; return; }
    });
    return loc;
}

Split* Engine::findOwnerSplit (const Chain& chain, int* branchIndex) const
{
    Split* owner = nullptr;
    visitNodes (*root, [&] (Node& n)
    {
        if (owner != nullptr) return;
        if (auto* s = dynamic_cast<Split*> (&n))
            for (int i = 0; i < (int) s->branches.size(); ++i)
                if (s->branches[(size_t) i]->chain.get() == &chain)
                {
                    owner = s;
                    if (branchIndex != nullptr) *branchIndex = i;
                    return;
                }
    });
    return owner;
}

std::unique_ptr<Node> Engine::createNode (NodeType type) const
{
    switch (type)
    {
        case NodeType::Chain: return std::make_unique<Chain>();
        case NodeType::Split:
        {
            auto s = std::make_unique<Split>();
            for (int i = 0; i < 2; ++i)
            {
                s->branches.push_back (std::make_unique<Split::Branch>());
                s->branches.back()->level = 0.5f;
            }
            return s;
        }
        case NodeType::Nam:    return std::make_unique<NamBlock>();
        case NodeType::Ir:     return std::make_unique<IrBlock>();
        case NodeType::Gate:   return std::make_unique<GateBlock>();
        case NodeType::Eq:     return std::make_unique<EqBlock>();
        case NodeType::Gain:   return std::make_unique<GainBlock>();
        case NodeType::Comp:      return std::make_unique<CompBlock>();
        case NodeType::Limiter:   return std::make_unique<LimiterBlock>();
        case NodeType::Expander:  return std::make_unique<ExpanderBlock>();
        case NodeType::Transient: return std::make_unique<TransientBlock>();
        case NodeType::Swell:     return std::make_unique<SwellBlock>();
        case NodeType::Overdrive: return std::make_unique<OverdriveBlock>();
        case NodeType::Fuzz:      return std::make_unique<FuzzBlock>();
        case NodeType::Boost:     return std::make_unique<BoostBlock>();
        case NodeType::Crusher:   return std::make_unique<CrusherBlock>();
        case NodeType::Wah:       return std::make_unique<WahBlock>();
        case NodeType::Chorus:    return std::make_unique<ChorusBlock>();
        case NodeType::Flanger:   return std::make_unique<FlangerBlock>();
        case NodeType::Phaser:    return std::make_unique<PhaserBlock>();
        case NodeType::Tremolo:   return std::make_unique<TremoloBlock>();
        case NodeType::Vibe:      return std::make_unique<VibeBlock>();
        case NodeType::Rotary:    return std::make_unique<RotaryBlock>();
        case NodeType::Delay:     return std::make_unique<DelayBlock>();
        case NodeType::Reverb:    return std::make_unique<ReverbBlock>();
        case NodeType::Pitch:     return std::make_unique<PitchBlock>();
        case NodeType::Plugin: break;
    }
    return nullptr;
}

std::unique_ptr<Node> Engine::createPlugin (const juce::PluginDescription& desc, juce::String& error)
{
    auto inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, error);
    if (inst == nullptr)
        return nullptr;
    return std::make_unique<PluginBlock> (std::move (inst), desc);
}

void Engine::prepareNode (Node& n) { n.prepare (sampleRate, blockSize); }

void Engine::announceDeletion (Node& n)
{
    visitNodes (n, [this] (Node& x) { listeners.call ([&x] (Listener& l) { l.nodeWillBeDeleted (x); }); });
}

Node* Engine::insert (std::unique_ptr<Node> node, Chain& target, int index)
{
    if (node == nullptr) return nullptr;
    prepareNode (*node);
    auto* raw = node.get();
    {
        const juce::ScopedLock sl (lock);
        index = juce::jlimit (0, (int) target.nodes.size(), index < 0 ? (int) target.nodes.size() : index);
        target.nodes.insert (target.nodes.begin() + index, std::move (node));
    }
    storeScene();
    markDirty();
    notifyGraphChanged();
    return raw;
}

Node* Engine::replace (const juce::String& id, std::unique_ptr<Node> n, bool keepMappings)
{
    auto loc = locate (id);
    if (loc.chain == nullptr || n == nullptr) return nullptr;
    checkpoint();
    auto& slot = loc.chain->nodes[(size_t) loc.index];
    n->id = id;
    n->bypassed = slot->bypassed.load();
    n->locked = slot->locked.load();
    n->autoGain = slot->autoGain.load();
    if (n->autoGain.load())
    {
        n->autoGainDb = slot->autoGainDb.load();   // start from the old match, then measure the new block
        n->requestRelearn();
    }
    prepareNode (*n);
    auto* raw = n.get();
    std::unique_ptr<Node> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (slot);
        slot = std::move (n);
    }
    announceDeletion (*old);
    old.reset();
    // mappings that pointed at parameters of the old block no longer make sense
    if (! keepMappings)
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& m)
        {
            return m.nodeId == id && m.target == MidiMapping::Target::Param;
        }), mappings.end());
    markDirty();
    notifyGraphChanged();
    return raw;
}

std::unique_ptr<Node> Engine::extract (const juce::String& id, Location* where)
{
    auto loc = locate (id);
    if (loc.chain == nullptr) return nullptr;
    if (where != nullptr) *where = loc;
    const juce::ScopedLock sl (lock);
    auto n = std::move (loc.chain->nodes[(size_t) loc.index]);
    loc.chain->nodes.erase (loc.chain->nodes.begin() + loc.index);
    return n;
}

bool Engine::swap (const juce::String& a, const juce::String& b)
{
    if (a == b) return false;
    auto la = locate (a), lb = locate (b);
    if (la.chain == nullptr || lb.chain == nullptr) return false;
    if (! canDropInto (a, *lb.chain) || ! canDropInto (b, *la.chain)) return false;
    checkpoint();
    {
        const juce::ScopedLock sl (lock);
        std::swap (la.chain->nodes[(size_t) la.index], lb.chain->nodes[(size_t) lb.index]);
    }
    markDirty();
    notifyGraphChanged();
    return true;
}

void Engine::installRoot (std::unique_ptr<Chain> r, const std::function<void (Chain&, Chain&)>& splice)
{
    if (r == nullptr) return;
    r->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        if (splice) splice (*root, *r);
        std::swap (root, r);
    }
    announceDeletion (*r);
    r.reset();
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [this] (const MidiMapping& m)
    {
        return ! m.isGlobal() && findNode (m.nodeId) == nullptr;
    }), mappings.end());
    for (auto& s : scenes) s = captureScene();
    markDirty();
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); });
}

void Engine::remove (const juce::String& id)
{
    if (locate (id).chain == nullptr) return;
    checkpoint();
    auto n = extract (id);
    if (n == nullptr) return;
    announceDeletion (*n);
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& m) { return m.nodeId == id; }), mappings.end());
    n.reset();
    markDirty();
    notifyGraphChanged();
}

bool Engine::canDropInto (const juce::String& movingId, const Chain& target) const
{
    auto* moving = findNode (movingId);
    if (moving == nullptr) return false;
    bool inside = false;
    visitNodes (*moving, [&] (Node& n) { if (&n == &target) inside = true; });
    return ! inside;
}

bool Engine::move (const juce::String& id, Chain& target, int index)
{
    if (! canDropInto (id, target)) return false;
    auto loc = locate (id);
    if (loc.chain == nullptr) return false;
    {
        const juce::ScopedLock sl (lock);
        auto n = std::move (loc.chain->nodes[(size_t) loc.index]);
        loc.chain->nodes.erase (loc.chain->nodes.begin() + loc.index);
        if (loc.chain == &target && index > loc.index) --index;
        index = juce::jlimit (0, (int) target.nodes.size(), index);
        target.nodes.insert (target.nodes.begin() + index, std::move (n));
    }
    markDirty();
    notifyGraphChanged();
    return true;
}

static void stripIds (juce::ValueTree v)
{
    v.removeProperty ("id", nullptr);
    for (auto c : v) stripIds (c);
}

Node* Engine::duplicate (const juce::String& id)
{
    auto loc = locate (id);
    if (loc.chain == nullptr) return nullptr;
    auto state = loc.chain->nodes[(size_t) loc.index]->toState().createCopy();
    stripIds (state);
    return insert (buildNode (state), *loc.chain, loc.index + 1);
}

Split* Engine::addSplit (Chain& target, int index, int numBranches)
{
    auto s = std::make_unique<Split>();
    for (int i = 0; i < numBranches; ++i)
    {
        s->branches.push_back (std::make_unique<Split::Branch>());
        s->branches.back()->level = 1.0f / (float) numBranches;
    }
    return static_cast<Split*> (insert (std::move (s), target, index));
}

Split* Engine::wrapInSplit (const juce::String& id, int numBranches)
{
    Location loc;
    auto n = extract (id, &loc);
    if (n == nullptr) return nullptr;
    auto s = std::make_unique<Split>();
    for (int i = 0; i < numBranches; ++i)
    {
        s->branches.push_back (std::make_unique<Split::Branch>());
        s->branches.back()->level = 1.0f / (float) numBranches;
    }
    s->branches[0]->chain->nodes.push_back (std::move (n));
    return static_cast<Split*> (insert (std::move (s), *loc.chain, loc.index));
}

void Engine::addBranch (Split& split)
{
    if ((int) split.branches.size() >= kMaxBranches)
    {
        message ("A split can have up to " + juce::String (kMaxBranches) + " paths");
        return;
    }
    auto b = split.makeBranch();
    {
        const juce::ScopedLock sl (lock);
        b->level = split.getMode() == SplitMode::Split ? 0.0f : 1.0f;
        split.branches.push_back (std::move (b));
    }
    if (split.getMode() == SplitMode::Split)
        split.setLevel ((int) split.branches.size() - 1, 1.0f / (float) split.branches.size(), true);
    storeScene();
    markDirty();
    notifyGraphChanged();
}

void Engine::removeBranch (Split& split, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) split.branches.size()) || split.branches.size() <= 1)
        return;
    checkpoint();
    std::unique_ptr<Split::Branch> b;
    {
        const juce::ScopedLock sl (lock);
        b = std::move (split.branches[(size_t) index]);
        split.branches.erase (split.branches.begin() + index);
    }
    announceDeletion (*b->chain);
    b.reset();
    if (split.getMode() == SplitMode::Split)
        split.setMode (SplitMode::Split);  // renormalise to 100%
    storeScene();
    markDirty();
    notifyGraphChanged();
}

void Engine::unwrapSplit (Split& split)
{
    auto loc = locate (split.id);
    if (loc.chain == nullptr) return;
    checkpoint();
    std::unique_ptr<Node> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (loc.chain->nodes[(size_t) loc.index]);
        loc.chain->nodes.erase (loc.chain->nodes.begin() + loc.index);
        auto& s = static_cast<Split&> (*old);
        if (! s.branches.empty())
        {
            auto& keep = s.branches[0]->chain->nodes;
            for (int i = 0; i < (int) keep.size(); ++i)
                loc.chain->nodes.insert (loc.chain->nodes.begin() + loc.index + i, std::move (keep[(size_t) i]));
            keep.clear();
        }
    }
    announceDeletion (*old);
    old.reset();
    markDirty();
    notifyGraphChanged();
}

void Engine::notifyGraphChanged()
{
    listeners.call ([] (Listener& l) { l.graphChanged(); });
}

void Engine::message (const juce::String& text)
{
    listeners.call ([&text] (Listener& l) { l.showMessage (text); });
}

void Engine::markDirty()
{
    if (! dirty)
    {
        dirty = true;
        listeners.call ([] (Listener& l) { l.presetChanged(); });
    }
}

//==============================================================================
juce::ValueTree Engine::captureScene() const
{
    juce::ValueTree s ("Scene");
    visitNodes (*root, [&] (Node& n)
    {
        if (&n == root.get()) return;
        juce::ValueTree e ("N");
        e.setProperty ("id", n.id, nullptr);
        e.setProperty ("bypass", n.bypassed.load(), nullptr);
        if (auto* sp = dynamic_cast<Split*> (&n))
        {
            juce::StringArray lv, mu;
            for (auto& b : sp->branches)
            {
                lv.add (juce::String (b->level.load(), 4));
                mu.add (b->mute.load() ? "1" : "0");
            }
            e.setProperty ("levels", lv.joinIntoString (","), nullptr);
            e.setProperty ("mutes", mu.joinIntoString (","), nullptr);
        }
        s.appendChild (e, nullptr);
    });
    return s;
}

void Engine::recallScene (const juce::ValueTree& s)
{
    for (auto e : s)
    {
        auto* n = findNode (e["id"].toString());
        if (n == nullptr) continue;
        n->bypassed = (bool) e["bypass"];
        if (auto* sp = dynamic_cast<Split*> (n))
        {
            auto lv = juce::StringArray::fromTokens (e["levels"].toString(), ",", {});
            auto mu = juce::StringArray::fromTokens (e["mutes"].toString(), ",", {});
            for (int i = 0; i < (int) sp->branches.size(); ++i)
            {
                if (i < lv.size()) sp->branches[(size_t) i]->level = lv[i].getFloatValue();
                if (i < mu.size()) sp->branches[(size_t) i]->mute = mu[i] == "1";
            }
        }
    }
}

void Engine::storeScene()
{
    scenes[activeScene] = captureScene();
}

void Engine::copySceneTo (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumScenes)) return;
    storeScene();
    scenes[index] = captureScene();
    markDirty();
}

void Engine::setScene (int index)
{
    index = juce::jlimit (0, kNumScenes - 1, index);
    if (index == activeScene) return;
    storeScene();
    activeScene = index;
    if (scenes[index].isValid() && scenes[index].getNumChildren() > 0)
        recallScene (scenes[index]);
    else
        storeScene();
    if (onSceneChanged) onSceneChanged (index);
    listeners.call ([] (Listener& l) { l.sceneChanged(); });
}

//==============================================================================
std::unique_ptr<Node> Engine::createFromState (const juce::ValueTree& v) { return buildNode (v); }

static void syncAll (Node& n) { visitNodes (n, [] (Node& x) { x.syncEdits(); }); }

std::unique_ptr<Node> Engine::buildNode (const juce::ValueTree& v)
{
    const auto type = nodeTypeFromString (v["type"].toString());

    if (type == NodeType::Chain)
    {
        auto c = std::make_unique<Chain>();
        c->fromState (v);
        for (auto child : v)
            if (child.hasType ("Node"))
                if (auto n = buildNode (child))
                    c->nodes.push_back (std::move (n));
        syncAll (*c);
        return c;
    }

    if (type == NodeType::Split)
    {
        auto s = std::make_unique<Split>();
        s->fromState (v);
        for (auto bt : v)
        {
            if (! bt.hasType ("Branch")) continue;
            auto b = std::make_unique<Split::Branch>();
            b->level = (float) bt.getProperty ("level", 0.5f);
            b->pan = (float) bt.getProperty ("pan", 0.0f);
            b->mute = (bool) bt.getProperty ("mute", false);
            b->phase = (bool) bt.getProperty ("phase", false);
            auto ct = bt.getChildWithName ("Node");
            if (ct.isValid())
            {
                auto built = buildNode (ct);
                if (auto* chain = dynamic_cast<Chain*> (built.get()))
                {
                    built.release();
                    b->chain.reset (chain);
                }
            }
            s->branches.push_back (std::move (b));
        }
        syncAll (*s);
        return s;
    }

    if (type == NodeType::Plugin)
    {
        auto desc = PluginBlock::descriptionFromState (v);
        std::unique_ptr<juce::AudioPluginInstance> inst;
        juce::String err;

        const juce::PluginDescription* match = nullptr;
        auto types = knownPlugins.getTypes();
        for (auto& t : types)
            if (t.uniqueId == desc.uniqueId && t.pluginFormatName == desc.pluginFormatName && t.name == desc.name)
                { match = &t; break; }
        if (match == nullptr)
            for (auto& t : types)
                if (t.name == desc.name && t.manufacturerName == desc.manufacturerName)
                    { match = &t; break; }

        if (match != nullptr) inst = formatManager.createPluginInstance (*match, sampleRate, blockSize, err);
        if (inst == nullptr && desc.fileOrIdentifier.isNotEmpty())
            inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, err);
        if (inst == nullptr)
            message ("Missing plug-in: " + desc.name + " (" + desc.manufacturerName + ")");

        auto pb = std::make_unique<PluginBlock> (std::move (inst), match != nullptr ? *match : desc);
        pb->fromState (v);
        pb->syncEdits();
        return pb;
    }

    auto n = createNode (type);
    if (n != nullptr)
    {
        n->fromState (v);
        n->syncEdits();
    }
    return n;
}

juce::ValueTree Engine::getState (bool includeSession) const
{
    juce::ValueTree v ("Rigsmith");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("name", presetName, nullptr);
    v.setProperty ("scene", activeScene, nullptr);
    v.appendChild (root->toState(), nullptr);

    juce::ValueTree sc ("Scenes");
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto s = (i == activeScene ? captureScene() : scenes[i]).createCopy();
        if (! s.isValid()) s = juce::ValueTree ("Scene");
        s.setProperty ("name", sceneNames[i], nullptr);
        sc.appendChild (s, nullptr);
    }
    v.appendChild (sc, nullptr);

    juce::ValueTree mm ("Midi");
    for (auto& m : mappings)
        if (includeSession || ! m.isGlobal())
            mm.appendChild (m.toState(), nullptr);
    v.appendChild (mm, nullptr);

    if (includeSession)
    {
        juce::ValueTree s ("Session");
        s.setProperty ("inputMode", inputMode.load(), nullptr);
        s.setProperty ("inputGain", inputGainDb.load(), nullptr);
        s.setProperty ("outputGain", outputGainDb.load(), nullptr);
        s.setProperty ("limiter", limiterOn.load(), nullptr);
        s.setProperty ("presetFile", presetFile.getFullPathName(), nullptr);
        s.setProperty ("dirty", dirty, nullptr);
        v.appendChild (s, nullptr);
    }
    return v;
}

void Engine::setState (const juce::ValueTree& v, bool includeSession)
{
    if (! v.hasType ("Rigsmith"))
        return;

    auto rootState = v.getChildWithName ("Node");
    std::unique_ptr<Chain> newRoot;
    if (rootState.isValid())
    {
        auto built = buildNode (rootState);
        if (auto* c = dynamic_cast<Chain*> (built.get()))
        {
            built.release();
            newRoot.reset (c);
        }
    }
    if (newRoot == nullptr)
        newRoot = std::make_unique<Chain>();

    newRoot->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        std::swap (root, newRoot);
    }
    announceDeletion (*newRoot);
    newRoot.reset();

    presetName = v.getProperty ("name", "Untitled").toString();
    activeScene = juce::jlimit (0, kNumScenes - 1, (int) v.getProperty ("scene", 0));

    auto sc = v.getChildWithName ("Scenes");
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto s = sc.getChild (i);
        scenes[i] = s.isValid() ? s.createCopy() : juce::ValueTree ("Scene");
        sceneNames[i] = s.isValid() ? s.getProperty ("name", juce::String::charToString ((juce::juce_wchar) ('A' + i))).toString()
                                    : juce::String::charToString ((juce::juce_wchar) ('A' + i));
    }
    if (scenes[activeScene].getNumChildren() > 0)
        recallScene (scenes[activeScene]);

    // node mappings come from the preset; global ones (scenes, tuner...) survive unless this is a full session
    std::vector<MidiMapping> next;
    if (! includeSession)
        for (auto& m : mappings)
            if (m.isGlobal()) next.push_back (m);
    for (auto mt : v.getChildWithName ("Midi"))
    {
        auto m = MidiMapping::fromState (mt);
        if (includeSession || ! m.isGlobal()) next.push_back (m);
    }
    mappings = next;

    if (includeSession)
    {
        auto s = v.getChildWithName ("Session");
        if (s.isValid())
        {
            inputMode = (int) s.getProperty ("inputMode", 0);
            inputGainDb = (float) s.getProperty ("inputGain", 0.0f);
            outputGainDb = (float) s.getProperty ("outputGain", 0.0f);
            limiterOn = (bool) s.getProperty ("limiter", true);
            presetFile = juce::File (s.getProperty ("presetFile").toString());
            dirty = (bool) s.getProperty ("dirty", false);
        }
    }
    else
    {
        dirty = false;
    }

    if (onSceneChanged) onSceneChanged (activeScene);
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); l.midiMapChanged(); });
}

void Engine::loadDefaultRig()
{
    if (dirty) checkpoint();
    auto r = std::make_unique<Chain>();
    r->nodes.push_back (std::make_unique<GateBlock>());
    r->nodes.push_back (std::make_unique<NamBlock>());
    r->nodes.push_back (std::make_unique<IrBlock>());
    r->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        std::swap (root, r);
    }
    if (r != nullptr) announceDeletion (*r);
    r.reset();
    presetName = initName;
    presetFile = juce::File();
    activeScene = 0;
    for (auto& s : scenes) s = captureScene();
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [] (const MidiMapping& m) { return ! m.isGlobal(); }), mappings.end());
    dirty = false;
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); });
}

//==============================================================================
juce::File Engine::getPresetFolder() const
{
    return getFolder ("presetFolder");
}

juce::Array<juce::File> Engine::getPresetFiles() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, true, "*.rig");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    return files;
}

bool Engine::loadPreset (const juce::File& f)
{
    if (dirty) checkpoint();   // unsaved changes can be got back with undo
    auto xml = juce::parseXML (f);
    if (xml == nullptr)
    {
        message ("Couldn't read preset " + f.getFileName());
        return false;
    }
    setState (juce::ValueTree::fromXml (*xml), false);
    presetFile = f;
    presetName = f.getFileNameWithoutExtension();
    dirty = false;
    listeners.call ([] (Listener& l) { l.presetChanged(); });
    return true;
}

bool Engine::savePreset (const juce::File& f)
{
    presetName = f.getFileNameWithoutExtension();
    storeScene();
    auto state = getState (false);
    if (auto xml = state.createXml())
    {
        f.getParentDirectory().createDirectory();
        if (xml->writeTo (f))
        {
            presetFile = f;
            dirty = false;
            listeners.call ([] (Listener& l) { l.presetChanged(); });
            message ("Saved " + f.getFileName());
            return true;
        }
    }
    message ("Couldn't save " + f.getFullPathName());
    return false;
}

void Engine::stepPreset (int delta)
{
    // position 0 is Init, then the saved presets
    auto files = getPresetFiles();
    const int count = files.size() + 1;
    const int cur = files.indexOf (presetFile) + 1;   // 0 when on Init or an unsaved rig
    const int next = ((cur + delta) % count + count) % count;
    if (next == 0) loadDefaultRig();
    else loadPreset (files[next - 1]);
}

//==============================================================================
juce::ValueTree Engine::snapshot() const
{
    auto v = getState (false);
    v.setProperty ("undoFile", presetFile.getFullPathName(), nullptr);
    v.setProperty ("undoDirty", dirty, nullptr);
    return v;
}

void Engine::restore (const juce::ValueTree& v)
{
    setState (v, false);
    presetFile = juce::File (v.getProperty ("undoFile").toString());
    dirty = (bool) v.getProperty ("undoDirty", true);
    listeners.call ([] (Listener& l) { l.presetChanged(); });
}

void Engine::checkpoint()
{
    if (undoSuppress > 0) return;
    undoStack.push_back (snapshot());
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool Engine::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (snapshot());
    auto v = undoStack.back();
    undoStack.pop_back();
    restore (v);
    return true;
}

bool Engine::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (snapshot());
    auto v = redoStack.back();
    redoStack.pop_back();
    restore (v);
    return true;
}

//==============================================================================
juce::File Engine::getBlockPresetFolder (const Node& n) const
{
    juce::String key;
    if (auto* pb = dynamic_cast<const PluginBlock*> (&n)) key = "VST - " + pb->getDescription().name;
    else if (n.getType() == NodeType::Nam) key = "NAM Amp";
    else if (n.getType() == NodeType::Ir) key = "Cab IR";
    else key = n.getName();
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rigsmith")
        .getChildFile ("Block Presets").getChildFile (juce::File::createLegalFileName (key));
}

juce::Array<juce::File> Engine::getBlockPresets (const Node& n) const
{
    auto files = getBlockPresetFolder (n).findChildFiles (juce::File::findFiles, false, "*.rbp");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files;
}

bool Engine::saveBlockPreset (const Node& n, const juce::String& name)
{
    auto v = n.toState().createCopy();
    for (auto* prop : { "id", "bypass", "autoGain", "autoGainDb", "locked" }) v.removeProperty (prop, nullptr);
    v.setProperty ("presetName", name, nullptr);
    const auto f = getBlockPresetFolder (n).getChildFile (juce::File::createLegalFileName (name) + ".rbp");
    f.getParentDirectory().createDirectory();
    if (auto xml = v.createXml(); xml != nullptr && xml->writeTo (f))
    {
        message ("Saved block preset " + name);
        return true;
    }
    message ("Couldn't save " + f.getFullPathName());
    return false;
}

Node* Engine::loadBlockPreset (const juce::String& id, const juce::File& f)
{
    auto* current = findNode (id);
    auto xml = juce::parseXML (f);
    if (current == nullptr || xml == nullptr) return nullptr;
    auto v = juce::ValueTree::fromXml (*xml);
    if (nodeTypeFromString (v["type"].toString()) != current->getType()) { message ("That preset is for a different block"); return nullptr; }
    auto n = buildNode (v);
    if (n == nullptr) return nullptr;
    auto* r = replace (id, std::move (n), true);
    if (r != nullptr) message ("Loaded " + f.getFileNameWithoutExtension());
    return r;
}

Node* Engine::initBlock (const juce::String& id)
{
    auto* current = findNode (id);
    if (current == nullptr) return nullptr;
    if (auto* pb = dynamic_cast<PluginBlock*> (current))
    {
        checkpoint();
        pb->resetToDefaults();
        for (int i = 0; i < pb->numParams(); ++i) pb->set (i, pb->getSpecs()[(size_t) i].def);
        markDirty();
        return pb;
    }
    auto v = current->toState().createCopy();
    if (auto* b = dynamic_cast<Block*> (current))
        for (auto& sp : b->getSpecs())
            v.setProperty ("p_" + sp.id, sp.def, nullptr);
    auto n = buildNode (v);
    return n != nullptr ? replace (id, std::move (n), true) : nullptr;
}

} // namespace rig
