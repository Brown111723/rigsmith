#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/PluginScanner.h"

// The standalone app offers up to 8 inputs and 8 outputs of the audio interface (the main pair is the
// rig; split paths can take any other input and send to Out 3/4, 5/6, 7/8). In a DAW: a stereo
// input, an optional second stereo input (Inputs 3+4) and three optional extra stereo outputs.
juce::AudioProcessor::BusesProperties RigsmithProcessor::makeBuses()
{
    if (juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone)
        return BusesProperties()
                   .withInput ("Input", juce::AudioChannelSet::discreteChannels (rig::RigIo::maxChannels), true)
                   .withOutput ("Output", juce::AudioChannelSet::discreteChannels (rig::RigIo::maxChannels), true);
    return BusesProperties()
               .withInput ("Input", juce::AudioChannelSet::stereo(), true)
               .withInput ("Inputs 3+4", juce::AudioChannelSet::stereo(), false)
               .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
               .withOutput ("Out 3/4", juce::AudioChannelSet::stereo(), false)
               .withOutput ("Out 5/6", juce::AudioChannelSet::stereo(), false)
               .withOutput ("Out 7/8", juce::AudioChannelSet::stereo(), false);
}

RigsmithProcessor::RigsmithProcessor()
    : AudioProcessor (makeBuses())
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
    if (isStandalone() || juce::PluginHostType::getPluginLoadedAs() == wrapperType_Standalone)
    {
        // the standalone app: as many of the interface's channels as are switched on
        return out.size() >= 1 && out.size() <= rig::RigIo::maxChannels && in.size() <= rig::RigIo::maxChannels;
    }
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    // the extra buses: off, or mono / stereo
    for (int b = 1; b < layouts.inputBuses.size(); ++b)
        if (layouts.inputBuses[b].size() > 2) return false;
    for (int b = 1; b < layouts.outputBuses.size(); ++b)
        if (layouts.outputBuses[b].size() > 2) return false;
    return true;
}

void RigsmithProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rig::trace ("prepareToPlay " + juce::String (sampleRate) + " " + juce::String (samplesPerBlock));
    engine.prepare (sampleRate, samplesPerBlock);
    inputCopy.setSize (rig::RigIo::maxChannels, samplesPerBlock, false, true, false);
    spareOut.setSize (1, samplesPerBlock, false, true, false);
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

    // every input channel as it arrived (outputs share the buffer, so copy them before anything writes)
    const int n = buffer.getNumSamples();
    // (never trust the layout over the buffer: a host, or the demo player, may hand over fewer channels)
    const int bufChans = buffer.getNumChannels();
    const int numIns = juce::jmin (ins, rig::RigIo::maxChannels, bufChans);
    if (inputCopy.getNumChannels() < rig::RigIo::maxChannels || inputCopy.getNumSamples() < n)
        inputCopy.setSize (rig::RigIo::maxChannels, n, false, false, true);   // only if the host broke its promise
    std::array<const float*, rig::RigIo::maxChannels> inPtrs {};
    for (int k = 0; k < numIns; ++k)
    {
        inputCopy.copyFrom (k, 0, buffer, k, 0, n);
        inPtrs[(size_t) k] = inputCopy.getReadPointer (k);
    }

    // the main output pair, and the extra outputs (in the standalone, the interface's channels 3-8;
    // in a DAW, the Out 3/4, 5/6 and 7/8 buses when they're switched on)
    const int mainOuts = juce::jmin (getMainBusNumOutputChannels(), bufChans);
    std::array<float*, rig::RigIo::maxChannels> outPtrs {};
    int numOuts = 0;
    if (isStandalone())
    {
        for (int ch = 2; ch < juce::jmin (mainOuts, rig::RigIo::maxChannels); ++ch) outPtrs[(size_t) numOuts++] = buffer.getWritePointer (ch);
    }
    else
    {
        for (int bus = 1; bus < getBusCount (false) && numOuts + 1 < rig::RigIo::maxChannels; ++bus)
        {
            auto* b = getBus (false, bus);
            if (b == nullptr || ! b->isEnabled() || b->getNumberOfChannels() == 0) { numOuts += 2; continue; }   // keeps Out 5/6 on its own bus
            const int first = getChannelIndexInProcessBlockBuffer (false, bus, 0);
            const int count = b->getNumberOfChannels();
            if (first < 0 || first + (count > 1 ? 1 : 0) >= bufChans) { numOuts += 2; continue; }
            outPtrs[(size_t) numOuts] = buffer.getWritePointer (first);
            outPtrs[(size_t) numOuts + 1] = buffer.getWritePointer (count > 1 ? first + 1 : first);
            numOuts += 2;
        }
    }
    for (int k = 0; k < numOuts; ++k)
        if (outPtrs[(size_t) k] != nullptr) juce::FloatVectorOperations::clear (outPtrs[(size_t) k], n);
    // a switched-off DAW bus: its paths write to a scratch that goes nowhere
    for (int k = 0; k < numOuts; ++k)
        if (outPtrs[(size_t) k] == nullptr)
        {
            if (spareOut.getNumSamples() < n) spareOut.setSize (1, n, false, false, true);
            spareOut.clear();
            outPtrs[(size_t) k] = spareOut.getWritePointer (0);
        }
    rig::Engine::IoChannels io { inPtrs.data(), numIns, outPtrs.data(), numOuts };

    if (mainOuts >= 2 && buffer.getNumChannels() >= 2)
    {
        // the main pair: the rig's input is the first two input channels (a mono interface: the first)
        if (juce::jmin (getMainBusNumInputChannels(), bufChans) < 2) buffer.clear (1, 0, n);
        float* mainPtrs[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        juce::AudioBuffer<float> mainPair (mainPtrs, 2, n);
        engine.process (mainPair, midi, &io);
    }
    else
    {
        // mono-out host layout: run the stereo engine on a scratch copy, sum to mono
        juce::AudioBuffer<float> tmp (2, n);
        tmp.copyFrom (0, 0, buffer, 0, 0, n);
        tmp.clear (1, 0, n);
        engine.process (tmp, midi, &io);
        buffer.copyFrom (0, 0, tmp, 0, 0, n);
        buffer.addFrom (0, 0, tmp, 1, 0, n);
        buffer.applyGain (0, 0, n, 0.5f);
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
