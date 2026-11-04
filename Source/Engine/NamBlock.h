#pragma once

#include "Captures.h"

namespace nam { class DSP; }

namespace rig
{
/** Neural Amp Modeler block. Holds one capture, or several captures of the same gear that the
    Capture control sweeps through: between two captures both models run and their outputs are
    crossfaded, each level-matched, so the sound morphs from one to the next.

    Gain drives the model like an amp's gain knob. Each capture's response is measured when it
    loads (a short synthetic strum at several drive levels), and the output is trimmed by that
    curve, so turning Gain up changes the drive far more than the volume.
    Input and Output are level trims (the panel shows them as meters with handles). */
class NamBlock : public Block,
                 public Sweepable
{
public:
    NamBlock();
    ~NamBlock() override;

    juce::String getName() const override;
    juce::String getDetail() const override;
    int getLatency() const noexcept override { return latency.load(); }

    /** One capture. */
    bool loadModel (const juce::File& f, juce::String& error);
    /** Several captures to sweep through, in this order. position: 0..n-1 (fractions blend). */
    bool loadCaptures (const juce::Array<juce::File>& files, float position, juce::String& error) override;
    void clearModel();

    struct Info
    {
        bool loaded = false;
        juce::String name, architecture, gear, toneType, gearType, modeledBy;
        double modelRate = 0;
        bool resampling = false, hasLoudness = false;
        float loudnessDb = 0;
        juce::File file;
    };
    /** The capture nearest the Capture position. */
    Info getInfo() const;
    Info getInfo (int capture) const;
    /** This capture's own name ("Omega - Omega-BITE G3"), or the file name. */
    juce::String getCaptureName() const;

    //--- the sweep --------------------------------------------------------------
    int getNumCaptures() const noexcept override { return captureFiles.size(); }
    juce::Array<juce::File> getCaptureFiles() const override { return captureFiles; }
    juce::StringArray getCaptureNames() const override;
    juce::String getCaptureToneType (int i) const override { return getInfo (i).toneType; }
    /** 0..n-1; between two whole numbers the two captures are blended. */
    float getCapturePosition() const noexcept override;
    void setCapturePosition (float position) noexcept override;
    int getNearestCapture() const noexcept override;
    int getCaptureParam() const noexcept override { return pCapture; }
    bool takesFile (const juce::File& f) const override { return isNamFile (f); }
    Gear getSweepGear() const override { return getGear(); }

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;
    void randomise (juce::Random&, float amount) override;
    juce::String formatValue (int i, float v) const override;
    using Block::formatValue;
    std::vector<int> panelOrder() const override { return { pGain, pBass, pMid, pTreble, pPresence, pNormalize }; }

    enum Params { pInput = 0, pOutput, pBass, pMid, pTreble, pPresence, pNormalize, pGain, pCapture };

    /** Gain knob (0..10, 5 = as captured) in dB of drive. */
    float getGainDb() const noexcept { return (get (pGain) - 5.0f) * 4.8f; }
    /** How much the output is trimmed at the current Gain, for the nearest capture (dB). */
    float getGainCompensationDb() const;
    static constexpr int numGainPoints = 9;   // -24 .. +24 dB in 6 dB steps

    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;

    /** The capture nearest the Capture position. */
    juce::File getFile() const;

    /** What the loaded model is (amp head, amp + cab, pedal...). */
    Gear getGear() const noexcept { return (Gear) gearKind.load(); }
    Category getCategory() const override { return gearCategory (getGear(), false); }
    ToneInfo getToneInfo() const { return ToneInfo::forFile (getFile()); }

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    struct Model;
    struct Bank;
    void prepareModel (Model&, double sr, int block);
    void measureGainCurve (Model&);
    bool loadOne (const juce::File&, std::unique_ptr<Model>&, Info&, juce::String& error);
    void designToneStack (Biquad&, Biquad&, Biquad&, Biquad&, double sr) const;

    std::unique_ptr<Bank> bank;          // audio thread: only under modelLock
    juce::SpinLock modelLock;
    juce::Array<juce::File> captureFiles; // message thread
    std::vector<Info> infos;
    std::vector<std::array<float, 9>> compCurves;   // per capture, message-thread copy
    std::atomic<int> latency { 0 };
    std::atomic<int> gearKind { (int) Gear::Unknown };

    std::vector<float> mono, positions;
    Biquad bass, mid, treble, presence;
    ChangeWatch watch;
    juce::SmoothedValue<float> inGain, outGain, position, gainDbSmoothed;
};

} // namespace rig
