#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/PluginScanner.h"

RigsmithProcessor::RigsmithProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    rig::trace ("processor: start");
    sceneParam = new juce::AudioParameterChoice (juce::ParameterID { "scene", 1 }, "Scene",
                                                 juce::StringArray { "A", "B", "C", "D" }, 0);
    addParameter (sceneParam);
    sceneParam->addListener (this);

    rig::trace ("processor: engine built");
    engine.setupPluginScanning (isStandalone());
    if (isStandalone()) engine.beginStartup();
    rig::trace ("processor: scanning set up");
    engine.onLatencyChanged = [this] (int samples) { setLatencySamples (samples); };
    engine.onSceneChanged = [this] (int scene)
    {
        if (sceneParam->getIndex() != scene)
        {
            sceneParam->beginChangeGesture();
            *sceneParam = scene;
            sceneParam->endChangeGesture();
        }
    };
}

RigsmithProcessor::~RigsmithProcessor()
{
    if (isStandalone()) engine.finishStartup();   // a normal quit is a finished start too
    alive->store (false);
    sceneParam->removeListener (this);
}

void RigsmithProcessor::parameterValueChanged (int, float)
{
    engine.pendingHostScene = sceneParam->getIndex();
}

bool RigsmithProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void RigsmithProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rig::trace ("prepareToPlay " + juce::String (sampleRate) + " " + juce::String (samplesPerBlock));
    engine.prepare (sampleRate, samplesPerBlock);
    rig::trace ("prepareToPlay done");
    loadMeasurer.reset (sampleRate, samplesPerBlock);
    setLatencySamples (engine.getLatency());
}

void RigsmithProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());
    static std::atomic<int> traced { 0 };
    if (traced.load() < 3) { ++traced; rig::trace ("processBlock " + juce::String (buffer.getNumSamples())); }

    const int ins = getTotalNumInputChannels();
    for (int ch = ins; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    {
        // the host's tempo and position, for synced LFOs and the sequencer
        double bpm = 0.0, ppq = -1.0;
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
            {
                bpm = pos->getBpm().orFallback (0.0);
                if (pos->getIsPlaying()) ppq = pos->getPpqPosition().orFallback (-1.0);
            }
        engine.setTransport (bpm, ppq);
    }

    if (buffer.getNumChannels() >= 2)
    {
        engine.process (buffer, midi);
    }
    else
    {
        // mono-out host layout: run the stereo engine on a scratch copy, sum to mono
        juce::AudioBuffer<float> tmp (2, buffer.getNumSamples());
        tmp.copyFrom (0, 0, buffer, 0, 0, buffer.getNumSamples());
        tmp.clear (1, 0, buffer.getNumSamples());
        engine.process (tmp, midi);
        buffer.copyFrom (0, 0, tmp, 0, 0, buffer.getNumSamples());
        buffer.addFrom (0, 0, tmp, 1, 0, buffer.getNumSamples());
        buffer.applyGain (0, 0, buffer.getNumSamples(), 0.5f);
    }
    midi.clear();
}

juce::AudioProcessorEditor* RigsmithProcessor::createEditor()
{
    rig::trace ("createEditor");
    return new RigsmithEditor (*this);
}

void RigsmithProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = engine.getState (true).createXml())
        copyXmlToBinary (*xml, dest);
}

void RigsmithProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (isStandalone() && engine.isSafeStart())
    {
        // the last start hung or crashed: keep the session to one side and start with a fresh rig
        const auto keep = engine.ini.getFile().getSiblingFile ("Rigsmith - session that didn't open.rigsession");
        keep.replaceWithData (data, (size_t) size);   // Menu > Session state > Load a saved state... reads it
        rig::trace ("skipped restoring the last session (saved as " + keep.getFileName() + ")");
        return;
    }
    rig::trace ("restoring session");
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        engine.setState (tree, true);
        rig::trace ("session restored");
    }
    else
    {
        // plug-in instances must be created on the message thread
        auto flag = alive;
        juce::MessageManager::callAsync ([this, flag, tree]
        {
            if (flag->load()) engine.setState (tree, true);
        });
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RigsmithProcessor();
}
