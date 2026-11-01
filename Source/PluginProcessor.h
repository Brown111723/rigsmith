#pragma once

#include "Engine/Engine.h"

class RigsmithProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorParameter::Listener
{
public:
    RigsmithProcessor();
    ~RigsmithProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Rigsmith"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return engine.presetName; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }
    bool isStandalone() const noexcept { return wrapperType == wrapperType_Standalone; }

    rig::Engine engine;

private:
    juce::AudioProcessorEditor* createEditor() override;
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}

    juce::AudioParameterChoice* sceneParam = nullptr;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigsmithProcessor)
};
