#pragma once

#include "Nodes.h"

namespace rig
{
//==============================================================================
class GateBlock : public Block
{
public:
    GateBlock();
    juce::String getName() const override { return "Noise Gate"; }
    juce::String getDetail() const override;
    float getReduction() const noexcept { return reductionDb.load(); }

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    float env = 0, gain = 0;
    int holdCounter = 0;
    bool open = false;
    std::atomic<float> reductionDb { 0.0f };
};

//==============================================================================
class EqBlock : public Block
{
public:
    EqBlock();
    juce::String getName() const override { return "Studio EQ"; }
    juce::String getDetail() const override;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    Biquad hp, low, mid, high, lp;
    ChangeWatch watch;
};

//==============================================================================
class GainBlock : public Block
{
public:
    GainBlock();
    juce::String getName() const override { return "Volume"; }
    juce::String getDetail() const override;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    juce::SmoothedValue<float> gl, gr;
};

//==============================================================================
class CompBlock : public Block
{
public:
    CompBlock();
    juce::String getName() const override { return "Compressor"; }
    juce::String getDetail() const override;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    float env = 0;
    juce::AudioBuffer<float> dryBuf;
};

//==============================================================================
/** Cabinet impulse response loader with lo/hi cut and mix. */
class IrBlock : public Block
{
public:
    IrBlock();
    juce::String getName() const override;
    juce::String getDetail() const override;

    bool loadIr (const juce::File& f, juce::String& error);
    juce::File getFile() const { return file; }
    int getIrLength() const;

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    juce::dsp::Convolution conv;
    juce::File file;
    std::atomic<bool> hasIr { false };
    juce::AudioBuffer<float> dryBuf;
    Biquad hp, lp;
    ChangeWatch watch;
    juce::SmoothedValue<float> level;
};

} // namespace rig
