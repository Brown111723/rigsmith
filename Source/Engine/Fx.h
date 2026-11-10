#pragma once

#include "Nodes.h"
#include "Dsp.h"

namespace rig
{
//==============================================================================
// Dynamics
//==============================================================================
class CompBlock : public Block
{
public:
    CompBlock();
    juce::String getName() const override { return "Compressor"; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Dynamics; }
    float autoMakeupDb() const;
    float transferDb (float inDb) const override;
    enum { pThr, pRatio, pAtt, pRel, pKnee, pMakeup, pAuto, pMix };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    float staticGr (float inDb) const noexcept;
    float grState = 0;
};

class LimiterBlock : public Block
{
public:
    LimiterBlock();
    juce::String getName() const override { return "Limiter"; }
    juce::String getDetail() const override { return "Ceiling " + formatValue (pCeil); }
    Visual getVisual() const override { return Visual::Dynamics; }
    float transferDb (float inDb) const override;
    enum { pDrive, pCeil, pRel };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    float gain = 1;
};

class ExpanderBlock : public Block
{
public:
    ExpanderBlock();
    juce::String getName() const override { return "Expander"; }
    juce::String getDetail() const override { return formatValue (pRatio) + " below " + formatValue (pThr); }
    Visual getVisual() const override { return Visual::Dynamics; }
    float transferDb (float inDb) const override;
    enum { pThr, pRatio, pRange, pAtt, pRel };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::Follower det;
    float grState = 0;
};

class TransientBlock : public Block
{
public:
    TransientBlock();
    juce::String getName() const override { return "Transient Shaper"; }
    juce::String getDetail() const override { return "Atk " + formatValue (pAttack) + "  Sus " + formatValue (pSustain); }
    Visual getVisual() const override { return Visual::Envelope; }
    enum { pAttack, pSustain, pOut };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::Follower fast, slow, tail;
    juce::SmoothedValue<float> g;
};

class SwellBlock : public Block
{
public:
    SwellBlock();
    juce::String getName() const override { return "Auto Swell"; }
    juce::String getDetail() const override { return "Rise " + formatValue (pRise); }
    Visual getVisual() const override { return Visual::Envelope; }
    enum { pThr, pRise, pRearm };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::Follower det;
    float ramp = 1, quietSamples = 0;
    bool armed = false;
};

//==============================================================================
// Drive
//==============================================================================
class DriveBase : public Block
{
public:
    using Block::Block;
    Visual getVisual() const override { return Visual::Shaper; }
    int getLatency() const noexcept override;
protected:
    void prepareOversampling (double sr, int block);
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
};

class FuzzBlock : public DriveBase
{
public:
    FuzzBlock();
    juce::String getName() const override { return "Fuzz"; }
    juce::String getDetail() const override { return get (pOctave) > 0.5f ? "Octave fuzz" : "Fuzz " + formatValue (pFuzz); }
    float shape (float x) const override;
    enum { pFuzz, pTone, pLevel, pGate, pOctave };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    Biquad tone;
    dsp::OnePole dcBlock[2];
    dsp::Follower gateDet;
    float gateGain = 0;
    ChangeWatch watch;
};

class BoostBlock : public Block
{
public:
    BoostBlock();
    juce::String getName() const override { return "Clean Boost"; }
    juce::String getDetail() const override { return formatValue (pGain); }
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    enum { pGain, pTight, pBright };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    Biquad hp, shelf;
    ChangeWatch watch;
    juce::SmoothedValue<float> g;
};

class CrusherBlock : public Block
{
public:
    CrusherBlock();
    juce::String getName() const override { return "Bit Crusher"; }
    juce::String getDetail() const override { return formatValue (pBits) + " bit  /" + formatValue (pDown); }
    Visual getVisual() const override { return Visual::Shaper; }
    float shape (float x) const override;
    enum { pBits, pDown, pTone, pMix };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    float hold[2] {}, counter = 0;
    Biquad lp;
    ChangeWatch watch;
};

//==============================================================================
// Filter
//==============================================================================
class WahBlock : public Block
{
public:
    WahBlock();
    juce::String getName() const override { return "Wah"; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    float liveFrequency() const override { return liveHz.load(); }
    std::vector<int> panelOrder() const override;
    void onChoiceChanged (int param) override;
    enum { pMode, pPos, pSens, pRate, pLow, pHigh, pQ, pMix, pModel, pBoost, pBass, pTreble, pBias, pRange, pAuto };
    enum Model { CryBaby = 0, Vox847, MultiWah535Q, BadHorsie, XoticXW1 };

    struct WahModel
    {
        const char* name;
        const char* blurb;
        float heel, toe, q;     // its stock sweep
        float leak;             // the inductor's low-pass leak (body under the sweep)
        float drive;            // the transistor stage's grit
        float taper;            // pot law: > 1 = audio taper (most of the sweep near the toe)
        float gainDb;
    };
    static const std::array<WahModel, 5>& models();
    /** Bad Horsie-style: off while the pedal rests heel-down, on as soon as it moves. */
    bool isEngaged() const noexcept { return engaged.load(); }

protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    void sweep (float& lo, float& hi) const noexcept;
    dsp::Svf svf[2];
    dsp::Follower det;
    dsp::Lfo lfo;
    dsp::OnePole posSmooth;
    Biquad lowShelf, highShelf;
    ChangeWatch watch;
    float engage = 1.0f;
    int restingSamples = 0;
    std::atomic<bool> engaged { true };
    std::atomic<float> liveHz { 700.0f };
};

//==============================================================================
// Modulation
//==============================================================================
class ChorusBlock : public Block
{
public:
    ChorusBlock();
    juce::String getName() const override { return "Chorus"; }
    juce::String getDetail() const override { return formatValue (pRate) + "  " + formatValue (pDepth); }
    Visual getVisual() const override { return Visual::Lfo; }
    float lfoShape (double ph) const override { return dsp::wave (ph, 0); }
    float lfoRate() const override { return get (pRate); }
    float lfoDepth() const override { return get (pDepth) / 100.0f; }
    enum { pRate, pDepth, pDelay, pWidth, pMix };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::DelayBuffer buf[2];
    dsp::Lfo lfo;
};

class FlangerBlock : public Block
{
public:
    FlangerBlock();
    juce::String getName() const override { return "Flanger"; }
    juce::String getDetail() const override { return formatValue (pRate) + "  fb " + formatValue (pFeedback); }
    Visual getVisual() const override { return Visual::Lfo; }
    float lfoShape (double ph) const override { return dsp::wave (ph, 1); }
    float lfoRate() const override { return get (pRate); }
    float lfoDepth() const override { return get (pDepth) / 100.0f; }
    enum { pRate, pDepth, pManual, pFeedback, pMix };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::DelayBuffer buf[2];
    dsp::Lfo lfo;
    float fb[2] {};
};

class PhaserBlock : public Block
{
public:
    PhaserBlock();
    juce::String getName() const override { return "Phaser"; }
    juce::String getDetail() const override { return getSpecs()[pStages].choices[getChoice (pStages)] + " stage"; }
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    float liveFrequency() const override { return liveHz.load(); }
    enum { pRate, pDepth, pCentre, pFeedback, pStages, pMix };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    int numStages() const noexcept;
    dsp::AllPass1 ap[2][12];
    float fb[2] {};
    dsp::Lfo lfo;
    std::atomic<float> liveHz { 700.0f };
};

class TremoloBlock : public Block
{
public:
    TremoloBlock();
    juce::String getName() const override { return "Tremolo"; }
    juce::String getDetail() const override { return getSpecs()[pShape].choices[getChoice (pShape)] + "  " + formatValue (pRate); }
    Visual getVisual() const override { return Visual::Lfo; }
    float lfoShape (double ph) const override;
    float lfoRate() const override { return get (pRate); }
    float lfoDepth() const override { return get (pDepth) / 100.0f; }
    enum { pRate, pDepth, pShape, pStereo };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::Lfo lfo;
    juce::dsp::LinkwitzRileyFilter<float> lowF, highF;
};

class VibeBlock : public Block
{
public:
    VibeBlock();
    juce::String getName() const override { return "Uni-Vibe"; }
    juce::String getDetail() const override { return getSpecs()[pMode].choices[getChoice (pMode)] + "  " + formatValue (pRate); }
    Visual getVisual() const override { return Visual::Lfo; }
    float lfoShape (double ph) const override;
    float lfoRate() const override { return get (pRate); }
    float lfoDepth() const override { return get (pIntensity) / 100.0f; }
    enum { pRate, pIntensity, pMode, pLevel };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::AllPass1 ap[2][4];
    dsp::Lfo lfo;
};

class RotaryBlock : public Block
{
public:
    RotaryBlock();
    juce::String getName() const override { return "Rotary Speaker"; }
    juce::String getDetail() const override { return getSpecs()[pSpeed].choices[getChoice (pSpeed)]; }
    Visual getVisual() const override { return Visual::Lfo; }
    float lfoShape (double ph) const override { return dsp::wave (ph, 0); }
    float lfoRate() const override { return hornRate.load(); }
    float lfoDepth() const override { return 0.8f; }
    enum { pSpeed, pRamp, pBalance, pWidth, pDrive };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::DelayBuffer hornBuf;
    dsp::OnePole xover;
    double hornPh = 0, drumPh = 0;
    float hornHz = 0.8f, drumHz = 0.7f;
    std::atomic<float> hornRate { 0.8f };
};

//==============================================================================
// Time
//==============================================================================
class DelayBlock : public Block
{
public:
    DelayBlock();
    juce::String getName() const override { return "Delay"; }
    juce::String getDetail() const override { return getSpecs()[pMode].choices[getChoice (pMode)] + "  " + formatValue (pTime); }
    Visual getVisual() const override { return Visual::Delay; }
    void getTaps (std::vector<DelayTap>&) const override;
    enum { pMode, pTime, pFeedback, pMix, pLowCut, pHighCut, pMod, pDuck, pSpread };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::DelayBuffer buf[2];
    Biquad hp[2], lp[2];
    ChangeWatch watch;
    dsp::OnePole timeSmooth;
    dsp::Follower duckDet;
    dsp::Lfo wow, flutter;
};

class ReverbBlock : public Block
{
public:
    ReverbBlock();
    juce::String getName() const override { return getSpecs()[pType].choices[getChoice (pType)] + " Reverb"; }
    juce::String getDetail() const override { return formatValue (pDecay) + "  mix " + formatValue (pMix); }
    Visual getVisual() const override { return Visual::Reverb; }
    void getDecay (float& predelayMs, float& rt60, float& damping, float& size) const override;
    enum { pType, pDecay, pPredelay, pSize, pDamping, pLowCut, pMod, pShimmer, pMix };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    static constexpr int N = 8;
    dsp::DelayBuffer pre, lines[N];
    dsp::Diffuser diff[6];
    dsp::OnePole damp[N], lowCut;
    dsp::PitchShifter shimmer;
    dsp::Lfo mod[N];
    float lengths[N] {}, gains[N] {}, last[N] {}, shimmerFb = 0;
    ChangeWatch watch;
    void updateLines();
};

class PitchBlock : public Block
{
public:
    PitchBlock();
    juce::String getName() const override { return getSpecs()[pMode].choices[getChoice (pMode)]; }
    juce::String getDetail() const override;
    enum { pMode, pSemis, pDetune, pMix, pTone };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    dsp::PitchShifter voice[2];
    dsp::DelayBuffer dbl[2];
    dsp::OnePole toneLp[2];
    int lastMode = -1;
};

} // namespace rig
