#pragma once

#include "Nodes.h"
#include "Dsp.h"

namespace rig
{
/** One speaker cabinet as the Cabinet block models it: where the box resonates, how the cone
    breaks up, and where the top end gives out. */
struct CabSpec
{
    juce::String name, speakers, blurb;
    bool bass = false;
    int cones = 1;            // drawn on the panel: 1, 2, 4 or 8
    float inches = 12.0f;
    bool openBack = false;
    float resHz = 100.0f, resQ = 1.2f;       // the box/speaker resonance (a resonant high-pass)
    float bodyHz = 180.0f, bodyDb = 0.0f;    // low-mid thump
    float scoopHz = 450.0f, scoopDb = 0.0f;  // the "boxy" notch
    float peak1Hz = 2400.0f, peak1Db = 4.0f; // cone break-up peaks
    float peak2Hz = 3600.0f, peak2Db = 2.0f;
    float topHz = 5200.0f;                   // where the cone stops (a steep low-pass)
    float tweeterDb = -100.0f;               // bass cabs with a horn: how much gets past the cone's roll-off
};

struct MicSpec
{
    juce::String name, blurb;
    float lowHz = 0.0f, lowDb = 0.0f;        // shelf: proximity warmth, or a dynamic's thin low end
    float peakHz = 5000.0f, peakDb = 0.0f, peakQ = 1.0f;   // the presence peak
    float airHz = 9000.0f, airDb = 0.0f;     // shelf: a ribbon's dark top, a condenser's open one
    float proximity = 1.0f;                  // how much bass it gains up close (ribbons most, omni none)
};

/** A cabinet and a microphone with no impulse response needed: pick the cab, pick the mic, and
    drag the mic across the cone (bright at the dust cap, dark at the edge) and back from the
    grille. Ten guitar cabs and five bass cabs. It is a model built from filters, so it has no
    latency and costs almost nothing; a captured IR of a particular cab will still have more of
    that cab's fine detail. */
class CabBlock : public Block
{
public:
    CabBlock();
    juce::String getName() const override { return "Cabinet"; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Cab; }
    float responseDb (double hz) const override;
    Category getCategory() const override { return Category::Cab; }
    std::vector<int> panelOrder() const override { return { pCab, pMic, pPosition, pDistance, pRoom, pLowCut, pHighCut, pLevel }; }
    juce::String formatValue (int i, float v) const override;
    using Block::formatValue;

    enum Params { pCab = 0, pMic, pPosition, pDistance, pRoom, pLowCut, pHighCut, pLevel };

    static const std::vector<CabSpec>& cabs();
    static const std::vector<MicSpec>& mics();
    const CabSpec& cab() const;
    const MicSpec& mic() const;
    static int firstBassCab();

    /** Like a cab IR: switches itself out after an "Amp + Cab" capture. */
    std::atomic<bool> skipAfterFullRig { true };
    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    static constexpr int kMaxStages = 20;
    struct Filters
    {
        Biquad f[kMaxStages];
        int n = 0;
        float gain = 1.0f;
        double magnitudeDb (double hz, double sr) const noexcept;
    };
    void design (Filters&, double sr) const;

    Filters filters;
    ChangeWatch watch;
    // the room: a handful of early reflections per side
    static constexpr int kTaps = 5;
    dsp::DelayBuffer roomDelay[2];
    dsp::OnePole roomLp[2];
    juce::SmoothedValue<float> level, room;
};

} // namespace rig
