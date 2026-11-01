#include "NamBlock.h"

#include <filesystem>
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
// AudioDSPTools' resampler comes from iPlug2 and expects a couple of its constants.
namespace iplug { static constexpr double PI = 3.14159265358979323846; }
#ifndef DEFAULT_BLOCK_SIZE
 #define DEFAULT_BLOCK_SIZE 64
#endif
#include "ResamplingContainer/ResamplingContainer.h"

namespace rig
{
struct NamBlock::Model
{
    using Resampler = ::dsp::ResamplingContainer<float, 1, 12>;

    std::unique_ptr<nam::DSP> dsp;
    std::unique_ptr<Resampler> resampler;
    std::function<void (float**, float**, int)> fn;
    std::vector<std::vector<float>> outChans;
    std::vector<float*> inPtrs, outPtrs;
    int nIn = 1, nOut = 1;
    bool resample = false;
    double rate = 48000.0;
    float normGain = 1.0f;
    bool hasLoudness = false;
    float loudness = 0.0f;

    void run (float* in, float* out0, int n) noexcept
    {
        for (auto& p : inPtrs) p = in;
        outPtrs[0] = out0;
        for (size_t c = 1; c < outPtrs.size(); ++c) outPtrs[c] = outChans[c].data();
        dsp->process (inPtrs.data(), outPtrs.data(), n);
    }
};

static std::filesystem::path toPath (const juce::File& f)
{
   #if JUCE_WINDOWS
    return std::filesystem::path (f.getFullPathName().toWideCharPointer());
   #else
    return std::filesystem::path (f.getFullPathName().toStdString());
   #endif
}

static juce::String jsonString (const nlohmann::json& j, const char* key)
{
    if (j.is_object() && j.contains (key) && j[key].is_string())
        return juce::String::fromUTF8 (j[key].get<std::string>().c_str());
    return {};
}

//==============================================================================
NamBlock::NamBlock()
    : Block (NodeType::Nam, {
        P ("input", "Input", -24.0f, 24.0f, 0.0f, "dB").rand (-3.0f, 6.0f),
        P ("output", "Output", -40.0f, 24.0f, 0.0f, "dB"),
        P ("bass", "Bass", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        P ("mid", "Middle", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        P ("treble", "Treble", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        P ("presence", "Presence", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        Toggle ("normalize", "Normalize", true).fixed() })
{
}

void NamBlock::designToneStack (Biquad& b, Biquad& m, Biquad& t, Biquad& p, double sr) const
{
    b.setLowShelf (sr, 150.0, (get (pBass) - 5.0f) * 2.0f);
    m.setPeak (sr, 425.0, 0.7, (get (pMid) - 5.0f) * 2.0f);
    t.setHighShelf (sr, 1800.0, (get (pTreble) - 5.0f) * 2.0f);
    p.setHighShelf (sr, 4500.0, (get (pPresence) - 5.0f) * 1.6f);
}

float NamBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Biquad b, m, t, p;
    designToneStack (b, m, t, p, sr);
    return (float) (b.magnitudeDb (hz, sr) + m.magnitudeDb (hz, sr) + t.magnitudeDb (hz, sr) + p.magnitudeDb (hz, sr));
}

NamBlock::~NamBlock() = default;

juce::String NamBlock::getName() const
{
    if (! info.loaded) return "NAM Amp";
    return info.name.isNotEmpty() ? info.name : info.file.getFileNameWithoutExtension();
}

juce::String NamBlock::getDetail() const
{
    if (! info.loaded) return "Double-click to load .nam";
    juce::String s = info.gear.isNotEmpty() ? info.gear : info.architecture;
    if (info.resampling) s << " - resampled";
    return s;
}

NamBlock::Info NamBlock::getInfo() const { return info; }

void NamBlock::prepareModel (Model& m, double sr, int block)
{
    m.nIn = juce::jmax (1, m.dsp->NumInputChannels());
    m.nOut = juce::jmax (1, m.dsp->NumOutputChannels());

    double modelRate = m.dsp->GetExpectedSampleRate();
    if (modelRate <= 0) modelRate = 48000.0;   // older .nam files omit the rate; they were all trained at 48 kHz
    m.rate = modelRate;
    m.resample = ! m.dsp->SupportsArbitrarySampleRate() && std::abs (modelRate - sr) > 1.0;

    int innerBlock = block;
    if (m.resample)
    {
        m.resampler = std::make_unique<Model::Resampler> (modelRate);
        m.resampler->Reset (sr, block);
        innerBlock = (int) std::ceil ((double) block * modelRate / sr) + 4;
        m.dsp->Reset (modelRate, innerBlock);
    }
    else
    {
        m.resampler.reset();
        m.dsp->Reset (sr, block);
    }

    m.outChans.assign ((size_t) m.nOut, std::vector<float> ((size_t) juce::jmax (innerBlock, block) + 8, 0.0f));
    m.inPtrs.assign ((size_t) m.nIn, nullptr);
    m.outPtrs.assign ((size_t) m.nOut, nullptr);

    Model* mp = &m;
    m.fn = [mp] (float** in, float** out, int k) { mp->run (in[0], out[0], k); };

    m.hasLoudness = m.dsp->HasLoudness();
    m.loudness = m.hasLoudness ? (float) m.dsp->GetLoudness() : 0.0f;
    m.normGain = m.hasLoudness ? dbToGain (-18.0f - m.loudness) : 1.0f;
}

bool NamBlock::loadModel (const juce::File& f, juce::String& error)
{
    if (! f.existsAsFile())
    {
        error = "Model not found: " + f.getFullPathName();
        return false;
    }

    try
    {
        nam::dspData cfg;
        auto m = std::make_unique<Model>();
        m->dsp = nam::get_dsp (toPath (f), cfg);
        if (m->dsp == nullptr)
        {
            error = "Could not read " + f.getFileName();
            return false;
        }
        prepareModel (*m, sampleRate, maxBlock);

        Info ni;
        ni.loaded = true;
        ni.file = f;
        ni.name = jsonString (cfg.metadata, "name");
        ni.architecture = juce::String (cfg.architecture);
        const auto make = jsonString (cfg.metadata, "gear_make"), modelName = jsonString (cfg.metadata, "gear_model");
        ni.gear = (make + " " + modelName).trim();
        ni.toneType = jsonString (cfg.metadata, "tone_type");
        ni.modelRate = m->rate;
        ni.resampling = m->resample;
        ni.hasLoudness = m->hasLoudness;
        ni.loudnessDb = m->loudness;
        const int newLatency = m->resample ? m->resampler->GetLatency() : 0;

        {
            const juce::SpinLock::ScopedLockType sl (modelLock);
            std::swap (model, m);
        }
        latency = newLatency;
        info = ni;
        modelFile = f;
        touched();
        return true;  // old model (now in m) is freed here, off the audio thread
    }
    catch (const std::exception& e)
    {
        error = f.getFileName() + ": " + juce::String (e.what());
    }
    catch (...)
    {
        error = "Unknown error loading " + f.getFileName();
    }
    return false;
}

void NamBlock::clearModel()
{
    std::unique_ptr<Model> old;
    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (model, old);
    }
    info = {};
    modelFile = juce::File();
    latency = 0;
}

juce::ValueTree NamBlock::toState() const
{
    auto v = Block::toState();
    v.setProperty ("file", modelFile.getFullPathName(), nullptr);
    return v;
}

void NamBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    const juce::String path = v.getProperty ("file").toString();
    if (path.isNotEmpty())
    {
        juce::String err;
        if (! loadModel (juce::File (path), err))
        {
            DBG (err);
        }
    }
}

void NamBlock::prepareImpl (double sr, int block)
{
    mono.assign ((size_t) block, 0.0f);
    for (auto* b : { &bass, &mid, &treble, &presence }) b->reset();
    watch.invalidate();
    inGain.reset (sr, 0.02);
    outGain.reset (sr, 0.02);

    const juce::SpinLock::ScopedLockType sl (modelLock);
    if (model != nullptr)
    {
        prepareModel (*model, sr, block);
        latency = model->resample ? model->resampler->GetLatency() : 0;
        info.resampling = model->resample;
    }
}

void NamBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);

    inGain.setTargetValue (dbToGain (get (pInput)));
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (l[i] + r[i]) * inGain.getNextValue();

    bool processed = false;
    float norm = 1.0f;
    {
        const juce::SpinLock::ScopedTryLockType tl (modelLock);
        if (tl.isLocked() && model != nullptr)
        {
            auto& m = *model;
            if (m.resample)
            {
                float* ip = mono.data();
                float* op = l;
                m.resampler->ProcessBlock (&ip, &op, n, m.fn);
                juce::FloatVectorOperations::copy (r, l, n);
            }
            else
            {
                m.run (mono.data(), l, n);
                if (m.nOut > 1) juce::FloatVectorOperations::copy (r, m.outChans[1].data(), n);
                else            juce::FloatVectorOperations::copy (r, l, n);
            }
            if (get (pNormalize) > 0.5f) norm = m.normGain;
            processed = true;
        }
    }

    if (! processed)
    {
        juce::FloatVectorOperations::copy (l, mono.data(), n);
        juce::FloatVectorOperations::copy (r, mono.data(), n);
    }

    // Tone stack (post amp, like the NAM plug-in)
    if (watch.changed ({ get (pBass), get (pMid), get (pTreble), get (pPresence) }))
    {
        designToneStack (bass, mid, treble, presence, sampleRate);
    }

    outGain.setTargetValue (dbToGain (get (pOutput)) * norm);
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        float xl = l[i], xr = r[i];
        xl = presence.process (treble.process (mid.process (bass.process (xl, 0), 0), 0), 0);
        xr = presence.process (treble.process (mid.process (bass.process (xr, 1), 1), 1), 1);
        l[i] = xl * g;
        r[i] = xr * g;
    }
}

} // namespace rig
