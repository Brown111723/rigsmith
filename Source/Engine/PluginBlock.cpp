#include "PluginBlock.h"

namespace rig
{
PluginBlock::PluginBlock (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& desc)
    : Block (NodeType::Plugin, {
        P ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%").fixed(),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB") }),
      instance (std::move (inst)),
      description (desc)
{
    if (instance != nullptr) instance->addListener (this);
}

PluginBlock::~PluginBlock()
{
    if (instance != nullptr)
    {
        // The UI closes plug-in windows before a block is deleted (Engine::Listener::nodeWillBeDeleted).
        jassert (instance->getActiveEditor() == nullptr);
        instance->removeListener (this);
        instance->releaseResources();
    }
}

juce::String PluginBlock::getName() const { return description.name; }

Category PluginBlock::getCategory() const
{
    return autoCategorise ? categoriseDescription (description) : Category::Other;
}

bool PluginBlock::isPinned (int index) const
{
    return std::find (pinned.begin(), pinned.end(), index) != pinned.end();
}

void PluginBlock::setPinned (int index, bool shouldPin)
{
    pinned.erase (std::remove (pinned.begin(), pinned.end(), index), pinned.end());
    if (shouldPin) pinned.push_back (index);
}

bool PluginBlock::isUnsafeToRandomise (const juce::String& paramName)
{
    const auto n = " " + paramName.toLowerCase() + " ";
    for (auto* w : { "bypass", "mute", "solo", "midi", "output", "out ", "volume", "master", "power", " on ", "enable",
                     "active", "oversampl", "quality", "zoom", " ui", "gui", "lock", "link", "preset", "program", "channel",
                     "panic", "reset", "latency", "bank", "trim", "level", "dry", "learn", "sidechain", "side chain" })
        if (n.contains (w)) return true;
    return false;
}

void PluginBlock::randomise (juce::Random& rng, float amount)
{
    if (instance == nullptr) return;
    amount = juce::jlimit (0.0f, 1.0f, amount);
    const bool full = amount >= 0.999f;
    for (auto* p : instance->getParameters())
    {
        if (! p->isAutomatable() || p->isMetaParameter() || isUnsafeToRandomise (p->getName (64)))
            continue;
        const int steps = p->getNumSteps();
        const bool discrete = p->isDiscrete() || p->isBoolean() || (steps > 1 && steps <= 12);
        float v;
        if (discrete)
        {
            if (! full && rng.nextFloat() > amount * 0.5f) continue;
            const int n = juce::jmax (2, steps > 1 && steps <= 64 ? steps : 2);
            v = (float) rng.nextInt (n) / (float) (n - 1);
        }
        else
        {
            // stay in the neighbourhood of the designer's default: wide enough to surprise, not to explode
            const float centre = full ? p->getDefaultValue() : p->getValue();
            const float spread = full ? 0.4f : 0.15f * amount;
            v = juce::jlimit (0.0f, 1.0f, centre + (rng.nextFloat() * 2.0f - 1.0f) * spread);
        }
        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    }
    touched();
}

void PluginBlock::resetToDefaults()
{
    if (instance == nullptr) return;
    for (auto* p : instance->getParameters())
        if (p->isAutomatable())
            p->setValueNotifyingHost (p->getDefaultValue());
    touched();
}

juce::String PluginBlock::getDetail() const
{
    return isMissing() ? juce::String ("Missing on this computer") : description.manufacturerName;
}

int PluginBlock::getLatency() const noexcept { return instance != nullptr ? instance->getLatencySamples() : 0; }

juce::ValueTree PluginBlock::toState() const
{
    auto v = Block::toState();
    if (auto xml = description.createXml())
        v.setProperty ("desc", xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

    juce::MemoryBlock mb;
    if (instance != nullptr) instance->getStateInformation (mb);
    else mb = missingState;
    v.setProperty ("state", mb.toBase64Encoding(), nullptr);
    juce::StringArray pins;
    for (int i : pinned) pins.add (juce::String (i));
    v.setProperty ("pinned", pins.joinIntoString (","), nullptr);
    return v;
}

juce::PluginDescription PluginBlock::descriptionFromState (const juce::ValueTree& v)
{
    juce::PluginDescription d;
    if (auto xml = juce::parseXML (v.getProperty ("desc").toString()))
        d.loadFromXml (*xml);
    return d;
}

void PluginBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    pinned.clear();
    for (auto& s : juce::StringArray::fromTokens (v.getProperty ("pinned").toString(), ",", {}))
        if (s.isNotEmpty()) pinned.push_back (s.getIntValue());
    juce::MemoryBlock mb;
    mb.fromBase64Encoding (v.getProperty ("state").toString());
    if (instance != nullptr)
    {
        if (mb.getSize() > 0)
            instance->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    else
    {
        missingState = mb;
    }
}

void PluginBlock::prepareImpl (double sr, int block)
{
    dryBuf.setSize (2, block, false, true, true);
    level.reset (sr, 0.02);

    if (instance == nullptr)
        return;

    instance->releaseResources();
    instance->enableAllBuses();

    using Set = juce::AudioChannelSet;
    auto tryLayout = [this] (Set in, Set out)
    {
        juce::AudioProcessor::BusesLayout l;
        if (instance->getBusCount (true) > 0) l.inputBuses.add (in);
        for (int i = 1; i < instance->getBusCount (true); ++i) l.inputBuses.add (Set::disabled());
        l.outputBuses.add (out);
        for (int i = 1; i < instance->getBusCount (false); ++i) l.outputBuses.add (Set::disabled());
        return instance->setBusesLayout (l);
    };

    if (! tryLayout (Set::stereo(), Set::stereo()))
        if (! tryLayout (Set::mono(), Set::stereo()))
            tryLayout (Set::mono(), Set::mono());

    instance->setRateAndBufferSizeDetails (sr, block);
    instance->prepareToPlay (sr, block);

    inChans = instance->getTotalNumInputChannels();
    outChans = instance->getTotalNumOutputChannels();
    work.setSize (juce::jmax (2, inChans, outChans), block, false, true, true);
}

void PluginBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (instance == nullptr)
        return;

    for (int ch = 0; ch < 2; ++ch)
        dryBuf.copyFrom (ch, 0, b, ch, 0, n);

    work.clear (0, n);
    if (inChans == 1)
    {
        work.copyFrom (0, 0, b, 0, 0, n);
        work.addFrom (0, 0, b, 1, 0, n);
        work.applyGain (0, 0, n, 0.5f);
    }
    else
    {
        work.copyFrom (0, 0, b, 0, 0, n);
        work.copyFrom (1, 0, b, 1, 0, n);
    }

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), n);
    midi.clear();
    instance->processBlock (view, midi);

    if (outChans >= 2)
    {
        b.copyFrom (0, 0, work, 0, 0, n);
        b.copyFrom (1, 0, work, 1, 0, n);
    }
    else if (outChans == 1)
    {
        b.copyFrom (0, 0, work, 0, 0, n);
        b.copyFrom (1, 0, work, 0, 0, n);
    }

    const float wet = get (0) * 0.01f;
    level.setTargetValue (dbToGain (get (1)));
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    auto* dl = dryBuf.getReadPointer (0);
    auto* dr = dryBuf.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = level.getNextValue();
        l[i] = (dl[i] + (l[i] - dl[i]) * wet) * g;
        r[i] = (dr[i] + (r[i] - dr[i]) * wet) * g;
    }
}

} // namespace rig
