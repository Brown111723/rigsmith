#pragma once

#include "Nodes.h"

namespace nam { class DSP; }

namespace rig
{
/** Neural Amp Modeler block: loads a .nam file, resamples transparently when the
    model rate differs from the host, and adds a tone stack + loudness normalisation. */
class NamBlock : public Block
{
public:
    NamBlock();
    ~NamBlock() override;

    juce::String getName() const override;
    juce::String getDetail() const override;
    int getLatency() const noexcept override { return latency.load(); }

    bool loadModel (const juce::File& f, juce::String& error);
    void clearModel();

    struct Info
    {
        bool loaded = false;
        juce::String name, architecture, gear, toneType;
        double modelRate = 0;
        bool resampling = false, hasLoudness = false;
        float loudnessDb = 0;
        juce::File file;
    };
    Info getInfo() const;

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

    enum Params { pInput = 0, pOutput, pBass, pMid, pTreble, pPresence, pNormalize };

    Visual getVisual() const override { return Visual::Response; }
    float responseDb (double hz) const override;

    /** Other .nam files next to the loaded one (for quick browsing). */
    juce::File getFile() const { return modelFile; }

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    struct Model;
    void prepareModel (Model&, double sr, int block);
    void designToneStack (Biquad&, Biquad&, Biquad&, Biquad&, double sr) const;

    std::unique_ptr<Model> model;
    juce::SpinLock modelLock;
    juce::File modelFile;
    Info info;
    std::atomic<int> latency { 0 };

    std::vector<float> mono;
    Biquad bass, mid, treble, presence;
    ChangeWatch watch;
    juce::SmoothedValue<float> inGain, outGain;
};

} // namespace rig
