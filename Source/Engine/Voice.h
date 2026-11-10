#pragma once

#include "Fx.h"

namespace rig
{
/** Finds the pitch of a single voice or note (YIN, on a decimated copy): runs on the audio thread,
    cheaply, and says how sure it is. */
class PitchTracker
{
public:
    void prepare (double sampleRate);
    /** Feed one sample; returns true when a new estimate is ready. */
    bool push (float x) noexcept;
    float hz = 0.0f;           // last estimate (0 = no pitch)
    float confidence = 0.0f;   // 0..1
private:
    static constexpr int W = 512, maxTau = 200, hop = 96;
    std::vector<float> ring, frame, diff;
    int write = 0, decim = 4, count = 0, sinceHop = 0;
    float acc = 0, lp = 0, lpA = 0;
    double rate = 12000.0;
    void analyse() noexcept;
};

/** Musical scales for pitch correction and harmony. */
juce::StringArray keyNames();
juce::StringArray scaleNames();
/** The nearest note of a scale to a (fractional) MIDI note. */
float nearestInScale (float midi, int key, int scale) noexcept;
/** A note of the scale a number of scale steps from another (diatonic harmony). */
float scaleStep (float midiInScale, int key, int scale, int steps) noexcept;

//==============================================================================
/** Makes an electric guitar sound like an acoustic one: the resonances of an acoustic body (the
    air in it, the top), the scooped mids and the sparkle a magnetic pickup doesn't have. Body
    shapes from a dreadnought to a nylon-string and a resonator, and a 12-string's octave strings. */
class AcousticBodyBlock : public Block
{
public:
    AcousticBodyBlock();
    juce::String getName() const override { return "Acoustic Sim"; }
    juce::String getDetail() const override { return getSpecs()[pBody].choices[getChoice (pBody)]; }
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    enum { pBody, pResonance, pTop, pWidth, pMix, pLevel };
    struct Shape { const char* name; float f[5], g[5], q[5]; float scoopHz, scoopDb, topDb, lpHz; bool octave; };
    static const Shape& shape (int i);
    static int numShapes();
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    void design() noexcept;
    Biquad res[2][5], scoop, top, pickup, lowCut, lp;
    dsp::OnePole exciteHp[2];
    dsp::PitchShifter octave;
    dsp::DelayBuffer octDelay;
    float makeup = 1, resGain = 1, sparkle = 0;
    ChangeWatch watch;
};

/** A preamp / DI for an acoustic guitar's own pickup (piezo or mic): tone, a notch for feedback,
    a body image that puts back the wood a piezo misses, a one-knob compressor and polarity. */
class AcousticPreBlock : public Block
{
public:
    AcousticPreBlock();
    juce::String getName() const override { return "Acoustic Preamp"; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    enum { pGain, pLowCut, pBass, pMid, pMidFreq, pTreble, pPresence, pNotch, pNotchDepth, pBodyImage, pComp, pPhase, pLevel };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    void design() noexcept;
    Biquad lowCut, bass, mid, treble, presence, notch, body[3];
    dsp::Follower comp;
    float compGain = 1;
    ChangeWatch watch;
};

/** A vocal channel strip: mic preamp, low cut, tube warmth, a one-knob compressor, a de-esser,
    three musical EQ bands (body, presence, air) and a gate - after the console and outboard
    channels every vocal is cut through. */
class VoiceStripBlock : public Block
{
public:
    VoiceStripBlock();
    juce::String getName() const override { return "Vocal Strip"; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    enum { pGain, pLowCut, pWarmth, pComp, pDeEss, pBody, pPresence, pAir, pGate, pLevel };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    void design() noexcept;
    Biquad lowCut, body, presence, air, essBand;
    dsp::Follower compDet, essDet, gateDet;
    float compG = 1, essG = 1, gateG = 1;
    ChangeWatch watch;
};

/** Takes the "s" off a vocal: compresses only the sibilant band when it jumps out. */
class DeEsserBlock : public Block
{
public:
    DeEsserBlock();
    juce::String getName() const override { return "De-Esser"; }
    juce::String getDetail() const override { return formatValue (pFreq) + "  " + formatValue (pThreshold); }
    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;
    enum { pThreshold, pFreq, pRange, pMode, pListen };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    Biquad split[2], detect;
    dsp::Follower det;
    float g = 1;
    ChangeWatch watch;
};

/** Pitch correction: pulls a voice (or a single-note line) to the nearest note of a key and
    scale. Slow for a natural touch-up, instant for the robotic effect. */
class PitchCorrectBlock : public Block
{
public:
    PitchCorrectBlock();
    juce::String getName() const override { return "Pitch Correct"; }
    juce::String getDetail() const override;
    std::vector<int> panelOrder() const override;
    enum { pKey, pScale, pSpeed, pHumanize, pTranspose, pMix };
    /** For the panel: the note heard and how far it's being moved (cents). */
    std::atomic<float> heardMidi { 0.0f }, correction { 0.0f };
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    PitchTracker tracker;
    dsp::PitchShifter shifter;
    float cents = 0, heldTarget = 0;
    int heldFor = 0;
};

/** Harmony in key: one or two voices a third, fourth, fifth, sixth or octave from what you sing or
    play, following the scale so the intervals stay in key. */
class HarmonizerBlock : public Block
{
public:
    HarmonizerBlock();
    juce::String getName() const override { return "Harmonizer"; }
    juce::String getDetail() const override;
    enum { pKey, pScale, pVoice1, pVoice2, pLevel1, pLevel2, pSpread, pGlide };
    static juce::StringArray intervalNames();
protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
private:
    PitchTracker tracker;
    dsp::PitchShifter voice[2];
    dsp::DelayBuffer align;
    float ratio[2] { 1, 1 }, target[2] { 1, 1 };
};

} // namespace rig
