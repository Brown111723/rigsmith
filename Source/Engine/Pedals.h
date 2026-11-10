#pragma once

#include "Fx.h"

namespace rig
{
/** How a pedal's clipping stage behaves. */
enum class ClipKind
{
    OpAmpSoft,    // diodes in an op-amp's feedback loop: the clean signal passes, the gain is clipped (Tube Screamer, SD-1, Bluesbreaker)
    HardSilicon,  // silicon diodes to ground after the gain (DS-1, RAT, MT-2)
    HardGermanium,// germanium diodes to ground: lower, softer knee (Distortion+, Klon)
    HardLed,      // LEDs to ground: much more headroom, louder (Turbo RAT)
    Mosfet,       // MOSFET-style: asymmetric and open (OCD)
    Muff          // two cascaded clipping stages with a coupling filter between (Big Muff)
};

/** How its tone control works. */
enum class ToneKind
{
    None,          // OD-1, Distortion+: no tone knob
    LowPassSweep,  // a low-pass that opens up (Tube Screamer, OCD, Bluesbreakers)
    Tilt,          // trade lows against highs around a pivot
    ShelfTreble,   // active treble shelf (Klon, Soul Food)
    MuffBlend,     // low-pass and high-pass blended: the mid scoop (Big Muff, DS-1)
    RatFilter,     // a low-pass that closes as the knob goes up (RAT)
    ThreeBand,     // bass / mid / treble (+ presence) (MT-2, Shredmaster, BE-OD)
    ColorMix,      // HM-2's two huge resonant boosts
    Timmy          // bass cut before the gain, treble cut after
};

/** The circuit of a modelled pedal (plain numbers: safe to copy on the audio thread). */
struct PedalCircuit
{
    float inHp = 30.0f;           // input coupling (Hz)
    float clipHp = 0.0f;          // high-pass in the clipping path (TS: 720 Hz - the mid hump)
    float dryThru = 0.0f;         // un-clipped signal around the clipper (op-amp feedback designs)
    float gainMinDb = 0, gainMaxDb = 40;
    ClipKind clip = ClipKind::OpAmpSoft;
    float threshold = 0.5f, asym = 1.0f;
    float bandwidth = 8000.0f;    // fixed post-clip low-pass (op-amp / caps)
    ToneKind tone = ToneKind::LowPassSweep;
    float toneA = 700.0f, toneB = 6000.0f;   // tone stack frequencies (meaning depends on the kind)
    float lowShelfDb = 0.0f;      // fixed voicing: a low shelf at 100 Hz...
    float midPeakDb = 0.0f, midPeakHz = 800.0f;   // ...and a mid peak
    float cleanDrop = 0.0f;       // Klon-style: a clean path that fades as the gain comes up (0..1)
    int eqStyle = 0;              // ThreeBand: 0 parametric mid (MT-2), 1 contour cut (Shredmaster), 2 bass/treble/presence (BE-OD)
    float makeupDb = 0.0f;        // so models sit at similar levels
    float lowKeepHz = 0.0f;       // bass pedals: everything below this also passes clean, so the bottom stays solid
    float lowKeep = 0.0f;         // ...at this gain
};

/** One modelled pedal: what its knobs are called, and its circuit. */
struct PedalModel
{
    juce::String name, maker, year, blurb;
    std::vector<int> knobs;                      // the block's parameters this pedal has (beyond Level and Clean Blend)
    std::map<int, juce::String> labels;          // what the pedal calls them
    juce::String voiceName;                      // its switch, if any ("Mode", "Version", "Clipping")
    juce::StringArray voices;
    PedalCircuit c;
    std::vector<std::pair<int, float>> stock;    // knob settings the pedal starts with (a bass drive's blend, say)
};

/** A drive pedal with several famous circuits inside, picked with the Model selector:
    the Overdrive block (Tube Screamer, Klon, SD-1, OCD, Timmy, Soul Food, Morning Glory, OD-1,
    King of Tone, Lightspeed) and the Distortion block (DS-1, RAT, Tube Screamer, Big Muff, MT-2,
    HM-2, Shredmaster, OCD, Distortion+, BE-OD). Each model has its own knobs and switch, with
    the names the pedal uses, plus a Clean Blend. Runs 4x oversampled. */
class PedalBlock : public DriveBase
{
public:
    explicit PedalBlock (NodeType);

    juce::String getName() const override;
    juce::String getDetail() const override;
    float shape (float x) const override;
    Visual getVisual() const override { return Visual::Shaper; }
    std::vector<int> panelOrder() const override;
    juce::String getParamName (int i) const override;
    juce::StringArray getChoiceNames (int i) const override;
    juce::String formatValue (int i, float v) const override;
    using Block::formatValue;

    enum Params { pDrive = 0, pTone, pLevel, pBlend, pModel, pBass, pMid, pTreble, pMidFreq, pVoice, pTight, pPresence };

    static const std::vector<PedalModel>& overdriveModels();
    static const std::vector<PedalModel>& distortionModels();
    static const std::vector<PedalModel>& bassModels();
    static const std::vector<PedalModel>& modelsFor (NodeType t)
    {
        return t == NodeType::Distortion ? distortionModels() : t == NodeType::BassDrive ? bassModels() : overdriveModels();
    }
    const std::vector<PedalModel>& models() const { return modelsFor (getType()); }
    void onChoiceChanged (int param) override;
    const PedalModel& model() const;
    /** The circuit as the switch sets it (a TS9 is a brighter TS808, Turbo RAT has LEDs...). */
    PedalCircuit circuit() const noexcept;

protected:
    void prepareImpl (double, int) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    static float clipSample (float v, const PedalCircuit&) noexcept;
    void design (const PedalCircuit&) noexcept;
    double osRate = 96000.0;
    Biquad inHp, clipHp, bw, bw2, eqLow, eqMid, eqHigh, eqPres, preLow, preHigh, fixLow, fixMid;
    bool useClipHp = false, useBw2 = false, usePre = false, useFixLow = false, useFixMid = false;
    dsp::OnePole toneLp[2], toneLp2[2], muffHp[2], dcBlock[2], lowLp[2];
    float toneMix = 0.5f, gainLin = 1.0f, makeup = 1.0f;
    PedalCircuit cur;
    ChangeWatch watch;
    juce::AudioBuffer<float> dry;
};

class OverdriveBlock : public PedalBlock { public: OverdriveBlock() : PedalBlock (NodeType::Overdrive) {} };
class DistortionBlock : public PedalBlock { public: DistortionBlock() : PedalBlock (NodeType::Distortion) {} };
/** Bass preamps and drives: the same engine, voiced for bass - the lows stay clean and solid under
    the grit, and every model starts with some clean blend. */
class BassDriveBlock : public PedalBlock { public: BassDriveBlock() : PedalBlock (NodeType::BassDrive) {} };

} // namespace rig
