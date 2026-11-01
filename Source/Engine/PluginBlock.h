#pragma once

#include "Nodes.h"

namespace rig
{
/** Hosts any VST3 effect. If the plug-in is missing on this machine its saved state
    is kept so the preset round-trips unchanged. */
class PluginBlock : public Block
{
public:
    PluginBlock (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::PluginDescription& desc);
    ~PluginBlock() override;

    juce::String getName() const override;
    juce::String getDetail() const override;
    int getLatency() const noexcept override;

    juce::AudioPluginInstance* getInstance() const noexcept { return instance.get(); }
    const juce::PluginDescription& getDescription() const noexcept { return description; }
    bool isMissing() const noexcept { return instance == nullptr; }

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

    static juce::PluginDescription descriptionFromState (const juce::ValueTree&);

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    juce::MemoryBlock missingState;
    juce::AudioBuffer<float> work, dryBuf;
    juce::MidiBuffer midi;
    int inChans = 2, outChans = 2;
    juce::SmoothedValue<float> level;
};

} // namespace rig
