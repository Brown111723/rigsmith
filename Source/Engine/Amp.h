#pragma once

#include "Fx.h"

namespace rig
{
/** The parts of a passive Fender / Marshall / Vox ("FMV") tone stack: the three pots and the
    slope resistor, and the three caps. */
struct StackParts
{
    double R1 = 250e3, R2 = 1e6, R3 = 25e3, R4 = 56e3;   // treble pot, bass pot, mid pot, slope
    double C1 = 250e-12, C2 = 20e-9, C3 = 20e-9;
};

/** The passive tone stack itself, after Yeh & Smith's closed-form analysis of the '59 Bassman's:
    a third-order filter whose every pole and zero moves with all three knobs, as the real one's do
    (turning the bass up changes the mids...). Runs in double precision. */
struct ToneStack
{
    void design (const StackParts&, double sampleRate, double treble, double mid, double bass) noexcept;
    /** The analogue response, for a level reference and for drawing. */
    static double magnitude (const StackParts&, double hz, double treble, double mid, double bass) noexcept;
    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b[0] * x + z[0];
        z[0] = b[1] * x - a[1] * y + z[1];
        z[1] = b[2] * x - a[2] * y + z[2];
        z[2] = b[3] * x - a[3] * y;
        return (float) y;
    }
    void reset() noexcept { z[0] = z[1] = z[2] = 0.0; }
    double b[4] { 1, 0, 0, 0 }, a[4] { 1, 0, 0, 0 }, z[3] {};
};

enum class StackKind { Fmv, Active };

/** An amp's circuit, in the terms the model below needs (plain numbers: safe to copy on the
    audio thread). Levels are normalised: a preamp stage clips at about 1. */
struct AmpCircuit
{
    // preamp
    int stages = 3;                  // triode gain stages, the first before the gain pot
    float inputHp = 20.0f;           // input coupling (Hz); the Tight knob raises it
    float firstDb = 6.0f;            // gain of the first stage
    float gainMinDb = -20.0f, gainMaxDb = 20.0f;   // the gain (volume) pot, before the later stages
    float laterDb = 12.0f;           // gain of each later stage
    float stageHp = 30.0f;           // coupling between later stages (high-gain amps cut lows here)
    float stageLp = 10000.0f;        // Miller capacitance roll-off in the later stages
    float cathodeDb = 0.0f;          // first stages' partial cathode bypass: less gain below shelfHz (negative)
    float shelfHz = 300.0f;
    float asym = 1.3f;               // negative swing headroom relative to positive (even harmonics)
    float blocking = 0.3f;           // grid-current bias shift when driven hard (compression, sputter)
    float brightDb = 0.0f;           // bright cap across the gain pot: treble boost at low gain
    bool solidState = false;         // transistor / op-amp: clean, then clips hard

    // tone stack
    StackKind stack = StackKind::Fmv;
    StackParts parts;
    bool stackEarly = false;         // straight after the first stage (Fender, Vox) or after the preamp (Marshall)
    float fixedMid = -1.0f;          // amps without a Middle knob: where the stack's mid sits (0..1)

    // fixed voicing after the preamp (a 5150's mid push, a Rectifier's scoop)
    float voiceLowDb = 0.0f, voiceMidDb = 0.0f, voiceMidHz = 700.0f, voiceHighDb = 0.0f;

    // power amp
    float paMinDb = -12.0f, paMaxDb = 12.0f;   // drive into the power tubes across the Master knob
    float feedback = 0.4f;           // negative feedback: tighter, cleaner; Presence and Depth work through it
    float presenceHz = 3500.0f, depthHz = 90.0f;
    float classA = 0.0f;             // class A (AC30, Deluxe-ish): softer, more even harmonics
    float sag = 0.4f;                // power supply sag at Sag 10
    float otLow = 45.0f, otHigh = 11000.0f;    // output transformer bandwidth
    float loadDb = 2.0f;             // the speaker's impedance bump the power amp follows (less with more feedback)
};

/** A switch on the amp: a bright cap, a channel voicing, a Rectifier's Raw / Vintage / Modern. */
struct AmpVoice
{
    juce::String name;
    float gainAddDb = 0, brightAddDb = 0, tightMul = 1, scoopDb = 0, lowAddDb = 0, highAddDb = 0, feedbackMul = 1;
};

struct AmpModel
{
    juce::String name, maker, year, blurb;
    juce::String section;                         // "Clean", "Crunch", "High gain", "Bass"
    bool bass = false;
    std::vector<int> knobs;                       // the block parameters this amp has (beyond Level)
    std::map<int, juce::String> labels;           // what the amp calls them
    juce::String voiceName;
    std::vector<AmpVoice> voices;
    AmpCircuit c;
    std::vector<std::pair<int, float>> stock;     // where its knobs start
};

/** A built-in tube amp: preamp stages that clip, bias-shift and compress as triodes do, the real
    passive tone stack of each amp (or an active EQ), and a push-pull power amp with negative
    feedback, Presence and Depth, sag and its output transformer. 26 famous amps from sparkling
    cleans to modern high gain, plus four bass amps, and Studio with every control.
    Mono (amps are) and 4x oversampled. Put a Cabinet after it. */
class AmpBlock : public DriveBase
{
public:
    AmpBlock();

    juce::String getName() const override;
    juce::String getDetail() const override;
    float shape (float x) const override;
    std::vector<int> panelOrder() const override;
    juce::String getParamName (int i) const override;
    juce::StringArray getChoiceNames (int i) const override;
    std::vector<std::pair<int, juce::String>> getChoiceSections (int i) const override;
    juce::String formatValue (int i, float v) const override;
    using Block::formatValue;
    void onChoiceChanged (int param) override;
    int getLatency() const noexcept override;

    enum Params { pModel = 0, pGain, pBass, pMid, pTreble, pPresence, pDepth, pMaster, pLevel, pVoice, pBright, pTight, pSag };

    static const std::vector<AmpModel>& models();
    static int firstBassModel();
    const AmpModel& model() const;
    bool isBassModel() const { return model().bass; }
    /** The circuit as the switch and Studio's knobs set it. */
    AmpCircuit circuit() const noexcept;

protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    void design (const AmpCircuit&) noexcept;

    struct Stage
    {
        dsp::OnePole shelf, hp, lp;
        float gain = 1, cathode = 1, charge = 0;
    };
    std::unique_ptr<juce::dsp::Oversampling<float>> mono4x;
    juce::AudioBuffer<float> monoBuf;
    double osRate = 192000.0;
    AmpCircuit cur;
    Stage st[5];
    dsp::OnePole inHp, brightHp, presHp, depthLp, otHp, otLp, dcOut;
    ToneStack stack;
    Biquad activeLow, activeMid, activeHigh, voiceLow, voiceMid, voiceHigh, load;
    bool useVoiceLow = false, useVoiceMid = false, useVoiceHigh = false, useLoad = false;
    float gainPot = 1, brightAmt = 0, stackNorm = 1, paDrive = 1, beta = 0, presAmt = 0, depthAmt = 0, makeup = 1, sagAmt = 0;
    float blockRel = 0.999f, sagEnv = 0, sagAtt = 0, sagRel = 0, lastOut = 0;
    ChangeWatch watch;
};

/** The level trim for an amp model and switch position (Calibration.cpp). */
float ampTrimDb (int model, int voice);

} // namespace rig
