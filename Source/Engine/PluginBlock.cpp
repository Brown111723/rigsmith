#include "PluginBlock.h"

namespace rig
{
PluginBlock::PluginBlock (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& desc)
    : Block (NodeType::Plugin, {
        { "mix",   "Mix",    0.0f, 100.0f, 100.0f, "%", false, -1.0f, 0 },
        { "level", "Level",  -24.0f, 12.0f, 0.0f, "dB" } }),
      instance (std::move (inst)),
      description (desc)
{
}

PluginBlock::~PluginBlock()
{
    if (instance != nullptr)
    {
        // The UI closes plug-in windows before a block is deleted (Engine::Listener::nodeWillBeDeleted).
        jassert (instance->getActiveEditor() == nullptr);
        instance->releaseResources();
    }
}

juce::String PluginBlock::getName() const { return description.name; }

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
