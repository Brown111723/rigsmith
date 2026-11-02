#pragma once

#include "Gear.h"
#include "Tuner.h"

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
    Visual getVisual() const override { return Visual::Dynamics; }
    float transferDb (float inDb) const override;

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
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    enum { pHpf, pLowFreq, pLow, pMidFreq, pMid, pMidQ, pHighFreq, pHigh, pLpf };

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    Biquad hp, low, mid, high, lp;
    ChangeWatch watch, watch2;
    void design (Biquad& h, Biquad& lo, Biquad& m, Biquad& hi, Biquad& l, double sr) const;
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
//==============================================================================
/** Cabinet impulse response loader with lo/hi cut and mix. */
class IrBlock : public Block
{
public:
    IrBlock();
    juce::String getName() const override;
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;

    bool loadIr (const juce::File& f, juce::String& error);
    juce::File getFile() const { return file; }
    int getIrLength() const;

    Gear getGear() const noexcept { return (Gear) gearKind.load(); }
    Category getCategory() const override { return gearCategory (getGear(), true); }
    ToneInfo getToneInfo() const { return tone; }
    /** On by default: when the amp model before this IR already includes a cab
        ("Amp + Cab"), the IR switches itself out so the cab isn't doubled. */
    std::atomic<bool> skipAfterFullRig { true };

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    juce::dsp::Convolution conv;
    juce::File file;
    std::atomic<int> gearKind { (int) Gear::Cab };
    ToneInfo tone;
    std::atomic<bool> hasIr { false };
    juce::AudioBuffer<float> dryBuf;
    Biquad hp, lp;
    ChangeWatch watch;
    juce::SmoothedValue<float> level;
};

//==============================================================================
/** A tuner you can put anywhere in the rig (e.g. after a capo-style pitch block, or on one
    path of a split). Passes audio through, optionally muted, and shows note, cents and a strobe. */
class TunerBlock : public Block
{
public:
    TunerBlock();
    juce::String getName() const override { return "Tuner"; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Tuner; }
    enum { pMute, pReference };

    /** Latest reading (message thread; re-analyses at most every 40 ms). */
    const Tuner::Reading& getReading();

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    Tuner tuner;
    Tuner::Reading reading;
    juce::uint32 lastAnalysis = 0;
    std::atomic<juce::uint32> lastAudio { 0 };
    juce::SmoothedValue<float> muteGain;
    float scratch[512] {};
};

} // namespace rig
