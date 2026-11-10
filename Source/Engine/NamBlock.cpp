#include "NamBlock.h"

#include <filesystem>
#include <thread>
#include "NAM/dsp.h"
#include "NAM/activations.h"
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
    std::vector<float> out;          // this capture's output for the current block
    int nIn = 1, nOut = 1;
    bool resample = false;
    double rate = 48000.0;
    float normGain = 1.0f;
    bool hasLoudness = false;
    float loudness = 0.0f;
    std::array<float, numGainPoints> comp {};   // output trim (dB) at Gain -24..+24 dB

    void run (float* in, float* out0, int n) noexcept
    {
        for (auto& p : inPtrs) p = in;
        outPtrs[0] = out0;
        for (size_t c = 1; c < outPtrs.size(); ++c) outPtrs[c] = outChans[c].data();
        dsp->process (inPtrs.data(), outPtrs.data(), n);
    }

    /** One block through this capture (resampling if it needs to). */
    void render (float* in, int n) noexcept
    {
        if (resample)
        {
            float* ip = in;
            float* op = out.data();
            resampler->ProcessBlock (&ip, &op, n, fn);
        }
        else
            run (in, out.data(), n);
    }

    float compAt (float gainDb) const noexcept
    {
        const float x = juce::jlimit (0.0f, (float) (numGainPoints - 1), (gainDb + 24.0f) / 6.0f);
        const int i = juce::jmin ((int) x, numGainPoints - 2);
        const float f = x - (float) i;
        return comp[(size_t) i] + (comp[(size_t) i + 1] - comp[(size_t) i]) * f;
    }
};

struct NamBlock::Bank
{
    std::vector<std::unique_ptr<Model>> models;
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
        P ("input", "Input", -24.0f, 24.0f, 0.0f, "dB").fixed(),
        P ("output", "Output", -40.0f, 24.0f, 0.0f, "dB"),
        P ("bass", "Bass", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        P ("mid", "Middle", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        P ("treble", "Treble", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        P ("presence", "Presence", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.5f),
        Toggle ("normalize", "Normalize", true).fixed(),
        P ("gain", "Gain", 0.0f, 10.0f, 5.0f).rand (3.5f, 7.5f),
        P ("capture", "Capture", 0.0f, 1.0f, 0.0f).dec (2).fixed() }),
      bank (std::make_unique<Bank>())
{
    // the same fast tanh / sigmoid the Neural Amp Modeler plug-in itself runs with: models sound
    // as they do there, for a good deal less CPU than the exact functions
    static const bool fast = [] { nam::activations::Activation::enable_fast_tanh(); return true; }();
    juce::ignoreUnused (fast);
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

//==============================================================================
float NamBlock::getCapturePosition() const noexcept
{
    const int n = captureFiles.size();
    return n > 1 ? get (pCapture) * (float) (n - 1) : 0.0f;
}

void NamBlock::setCapturePosition (float pos) noexcept
{
    const int n = captureFiles.size();
    set (pCapture, n > 1 ? juce::jlimit (0.0f, 1.0f, pos / (float) (n - 1)) : 0.0f);
}

int NamBlock::getNearestCapture() const noexcept
{
    return juce::jlimit (0, juce::jmax (0, captureFiles.size() - 1), juce::roundToInt (getCapturePosition()));
}

juce::File NamBlock::getFile() const
{
    return captureFiles.isEmpty() ? juce::File() : captureFiles[getNearestCapture()];
}

NamBlock::Info NamBlock::getInfo (int i) const
{
    return juce::isPositiveAndBelow (i, (int) infos.size()) ? infos[(size_t) i] : Info();
}

NamBlock::Info NamBlock::getInfo() const { return getInfo (getNearestCapture()); }

juce::String NamBlock::getCaptureName() const
{
    const auto i = getInfo();
    if (! i.loaded) return {};
    return i.name.isNotEmpty() ? i.name : i.file.getFileNameWithoutExtension();
}

juce::StringArray NamBlock::getCaptureNames() const
{
    juce::StringArray names;
    for (auto& i : infos) names.add (i.name.isNotEmpty() ? i.name : i.file.getFileNameWithoutExtension());
    return names;
}

float NamBlock::getGainCompensationDb() const
{
    const int i = getNearestCapture();
    if (! juce::isPositiveAndBelow (i, (int) compCurves.size())) return 0.0f;
    const float x = juce::jlimit (0.0f, 8.0f, (getGainDb() + 24.0f) / 6.0f);
    const int k = juce::jmin ((int) x, 7);
    const auto& c = compCurves[(size_t) i];
    return c[(size_t) k] + (c[(size_t) k + 1] - c[(size_t) k]) * (x - (float) k);
}

juce::String NamBlock::getName() const
{
    const auto info = getInfo();
    if (! info.loaded) return "NAM Amp";
    // the gear itself ("Darkglass Alpha Omega Ultra"); the capture's own name is the detail
    if (info.gear.isNotEmpty()) return info.gear;
    const auto tone = getToneInfo();
    if (tone.valid && tone.title.isNotEmpty()) return tone.title;
    return info.name.isNotEmpty() ? info.name : info.file.getFileNameWithoutExtension();
}

juce::String NamBlock::getDetail() const
{
    const auto info = getInfo();
    if (! info.loaded) return "Double-click to load .nam";
    // the capture's own name, or (TONE3000 downloads without one) who made it
    const auto tone = getToneInfo();
    juce::String s = info.name.isNotEmpty() || ! tone.valid ? getCaptureName() : juce::String();
    if (s.isEmpty() || s == getName())
        s = tone.valid && tone.creator.isNotEmpty() ? "by " + tone.creator
          : info.modeledBy.isNotEmpty() ? "by " + info.modeledBy : info.architecture;
    if (info.resampling) s << " - resampled";
    return s;
}

juce::String NamBlock::formatValue (int i, float v) const
{
    if (i == pCapture)
    {
        const int n = captureFiles.size();
        if (n == 0) return "-";
        const float pos = n > 1 ? v * (float) (n - 1) : 0.0f;
        const auto names = getCaptureNames();
        const int a = juce::jlimit (0, n - 1, (int) std::floor (pos));
        const float f = pos - (float) a;
        if (f < 0.02f || a == n - 1) return names[a];
        if (f > 0.98f) return names[a + 1];
        return names[a] + " > " + names[a + 1] + " " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
    }
    if (i == pGain) return juce::String (v, 1);
    return Block::formatValue (i, v);
}

//==============================================================================
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
    m.out.assign ((size_t) block + 8, 0.0f);
    m.inPtrs.assign ((size_t) m.nIn, nullptr);
    m.outPtrs.assign ((size_t) m.nOut, nullptr);

    Model* mp = &m;
    m.fn = [mp] (float** in, float** out, int k) { mp->run (in[0], out[0], k); };

    m.hasLoudness = m.dsp->HasLoudness();
    m.loudness = m.hasLoudness ? (float) m.dsp->GetLoudness() : 0.0f;
    m.normGain = m.hasLoudness ? dbToGain (-18.0f - m.loudness) : 1.0f;
}

/** A short guitar-like strum (an E5 chord, plucked and decaying, peaking around -10 dBFS):
    what we play through each capture to learn how its loudness follows the drive. */
static std::vector<float> makeStrum (double sr)
{
    const int n = (int) (sr * 0.11);
    std::vector<float> x ((size_t) n, 0.0f);
    const double freqs[] = { 82.41, 123.47, 164.81, 246.94 };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double env = (1.0 - std::exp (-t / 0.002)) * std::exp (-t / 0.22);
        double v = 0;
        for (auto f : freqs)
            for (int h = 1; h <= 6; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * f * h * t + h * 0.7) / (h * h * 0.6 + 0.4);
        x[(size_t) i] = (float) (v * env);
    }
    float peak = 0;
    for (auto v : x) peak = juce::jmax (peak, std::abs (v));
    for (auto& v : x) v *= 0.32f / juce::jmax (1.0e-6f, peak);
    return x;
}

void NamBlock::measureGainCurve (Model& m)
{
    // run at the model's own rate, straight through (no resampler), then reset
    const int chunk = 512;
    m.dsp->Reset (m.rate, chunk);
    const auto strum = makeStrum (m.rate);
    std::vector<float> in ((size_t) chunk), out ((size_t) chunk + 8);
    std::vector<std::vector<float>> extra ((size_t) m.nOut, std::vector<float> ((size_t) chunk + 8));
    std::vector<float*> ip ((size_t) m.nIn), op ((size_t) m.nOut);
    const int skip = (int) (m.rate * 0.02);

    std::array<float, numGainPoints> level {};
    for (int g = 0; g < numGainPoints; ++g)
    {
        const float gain = dbToGain (-24.0f + 6.0f * (float) g);
        double sum = 0;
        int count = 0;
        for (int pos = 0; pos < (int) strum.size(); pos += chunk)
        {
            const int k = juce::jmin (chunk, (int) strum.size() - pos);
            for (int i = 0; i < k; ++i) in[(size_t) i] = strum[(size_t) (pos + i)] * gain;
            for (auto& p : ip) p = in.data();
            op[0] = out.data();
            for (size_t c = 1; c < op.size(); ++c) op[c] = extra[c].data();
            m.dsp->process (ip.data(), op.data(), k);
            for (int i = 0; i < k; ++i)
                if (pos + i >= skip && std::isfinite (out[(size_t) i])) { sum += (double) out[(size_t) i] * out[(size_t) i]; ++count; }
        }
        level[(size_t) g] = (float) (10.0 * std::log10 (sum / juce::jmax (1, count) + 1.0e-12));
    }

    // a real amp gets a little louder as the gain goes up, but mostly dirtier, and turned down
    // it gets cleaner without vanishing: the level moves at most 0.3 dB per dB of drive.
    // The trim only ever pulls towards Gain 5's level, so a capture that barely changes stays put.
    const float at0 = level[4];
    for (int g = 0; g < numGainPoints; ++g)
    {
        const float drive = -24.0f + 6.0f * (float) g;
        float c = at0 + 0.3f * drive - level[(size_t) g];
        if (drive > 0.0f) c = juce::jmin (0.0f, c);   // only ever pulled back towards Gain 5's level
        if (drive < 0.0f) c = juce::jmax (0.0f, c);
        m.comp[(size_t) g] = juce::jlimit (-24.0f, 18.0f, c);
    }
    m.comp[4] = 0.0f;
}

bool NamBlock::loadOne (const juce::File& f, std::unique_ptr<Model>& out, Info& ni, juce::String& error)
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
        measureGainCurve (*m);
        prepareModel (*m, sampleRate, maxBlock);   // fresh state after the measurement

        ni = {};
        ni.loaded = true;
        ni.file = f;
        ni.name = jsonString (cfg.metadata, "name");
        ni.architecture = juce::String (cfg.architecture);
        CaptureInfo ci;
        ci.gearMake = jsonString (cfg.metadata, "gear_make");
        ci.gearModel = jsonString (cfg.metadata, "gear_model");
        ni.gear = ci.gearTitle();   // "Marshall JCM800", not "Marshall JCM800 Marshall JCM800"
        ni.modeledBy = jsonString (cfg.metadata, "modeled_by");
        ni.toneType = jsonString (cfg.metadata, "tone_type");
        ni.gearType = jsonString (cfg.metadata, "gear_type");
        ni.modelRate = m->rate;
        ni.resampling = m->resample;
        ni.hasLoudness = m->hasLoudness;
        ni.loudnessDb = m->loudness;
        out = std::move (m);
        return true;
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

bool NamBlock::loadModel (const juce::File& f, juce::String& error)
{
    return loadCaptures (juce::Array<juce::File> { f }, 0.0f, error);
}

bool NamBlock::loadCaptures (const juce::Array<juce::File>& files, float pos, juce::String& error)
{
    // captures already loaded are kept (moved into the new set), only new ones are read
    const int n = files.size();
    std::vector<int> reuse ((size_t) n, -1);
    std::vector<bool> taken ((size_t) captureFiles.size(), false);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < captureFiles.size(); ++j)
            if (! taken[(size_t) j] && captureFiles[j] == files[i]) { reuse[(size_t) i] = j; taken[(size_t) j] = true; break; }

    std::vector<std::unique_ptr<Model>> fresh ((size_t) n);
    std::vector<Info> newInfos ((size_t) n);
    std::vector<char> ok ((size_t) n, 0);
    std::vector<juce::String> errors ((size_t) n);
    std::vector<int> todo;
    for (int i = 0; i < n; ++i)
    {
        if (reuse[(size_t) i] >= 0) { newInfos[(size_t) i] = infos[(size_t) reuse[(size_t) i]]; ok[(size_t) i] = 1; }
        else todo.push_back (i);
    }

    // read and measure the new captures side by side (a 16-capture pack loads in about the time of two)
    std::atomic<int> next { 0 };
    auto work = [&]
    {
        for (;;)
        {
            const int t = next.fetch_add (1);
            if (t >= (int) todo.size()) return;
            const int i = todo[(size_t) t];
            ok[(size_t) i] = loadOne (files[i], fresh[(size_t) i], newInfos[(size_t) i], errors[(size_t) i]) ? 1 : 0;
        }
    };
    const int threads = juce::jlimit (1, 8, juce::jmin ((int) todo.size(), (int) std::thread::hardware_concurrency() - 1));
    std::vector<std::thread> pool;
    for (int k = 1; k < threads; ++k) pool.emplace_back (work);
    work();
    for (auto& t : pool) t.join();

    juce::String firstError;
    for (auto& e : errors) if (firstError.isEmpty() && e.isNotEmpty()) firstError = e;

    int loaded = 0;
    for (char b : ok) loaded += b != 0 ? 1 : 0;
    if (loaded == 0)
    {
        error = firstError.isNotEmpty() ? firstError : juce::String ("No captures to load");
        return false;
    }

    auto newBank = std::make_unique<Bank>();
    juce::Array<juce::File> newFiles;
    std::vector<Info> keptInfos;
    std::vector<std::array<float, numGainPoints>> curves;
    int newLatency = 0;
    {
        // the audio thread skips one block while the models change hands
        const juce::SpinLock::ScopedLockType sl (modelLock);
        for (int i = 0; i < n; ++i)
        {
            if (! ok[(size_t) i]) continue;
            auto m = reuse[(size_t) i] >= 0 ? std::move (bank->models[(size_t) reuse[(size_t) i]]) : std::move (fresh[(size_t) i]);
            if (newBank->models.empty()) newLatency = m->resample ? m->resampler->GetLatency() : 0;
            curves.push_back (m->comp);
            newBank->models.push_back (std::move (m));
            newFiles.add (files[i]);
            keptInfos.push_back (newInfos[(size_t) i]);
        }
        std::swap (bank, newBank);
    }
    // old models (in newBank now) are freed here, off the audio thread
    newBank.reset();

    captureFiles = newFiles;
    infos = keptInfos;
    compCurves = curves;
    latency = newLatency;
    setCapturePosition (pos);
    gearKind = (int) classifyFile (captureFiles[0], infos[0].gearType);
    touched();
    return true;
}

void NamBlock::clearModel()
{
    std::unique_ptr<Bank> old = std::make_unique<Bank>();
    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (bank, old);
    }
    captureFiles.clear();
    infos.clear();
    compCurves.clear();
    latency = 0;
}

void NamBlock::randomise (juce::Random& rng, float amount)
{
    Block::randomise (rng, amount);
    // a sweep: usually one of its captures, sometimes a blend of two neighbours
    const int n = captureFiles.size();
    if (n > 1 && amount >= 0.999f)
    {
        float pos = (float) rng.nextInt (n);
        if (rng.nextFloat() < 0.3f) pos = juce::jmin ((float) (n - 1), pos + rng.nextFloat());
        setCapturePosition (pos);
    }
}

//==============================================================================
juce::ValueTree NamBlock::toState() const
{
    auto v = Block::toState();
    v.setProperty ("file", getFile().getFullPathName(), nullptr);
    if (captureFiles.size() > 1)
    {
        juce::ValueTree list ("Captures");
        for (auto& f : captureFiles)
        {
            juce::ValueTree c ("Capture");
            c.setProperty ("file", f.getFullPathName(), nullptr);
            list.appendChild (c, nullptr);
        }
        v.appendChild (list, nullptr);
    }
    return v;
}

void NamBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    juce::Array<juce::File> files;
    for (auto c : v.getChildWithName ("Captures"))
        if (auto path = c.getProperty ("file").toString(); path.isNotEmpty()) files.add (juce::File (path));
    const juce::String single = v.getProperty ("file").toString();
    if (files.isEmpty() && single.isNotEmpty()) files.add (juce::File (single));
    if (files.isEmpty()) return;

    const float pos = files.size() > 1 ? (float) v.getProperty ("p_capture", 0.0f) * (float) (files.size() - 1) : 0.0f;
    juce::String err;
    if (! loadCaptures (files, pos, err))
    {
        DBG (err);
    }
}

void NamBlock::prepareImpl (double sr, int block)
{
    mono.assign ((size_t) block, 0.0f);
    positions.assign ((size_t) block, 0.0f);
    for (auto* b : { &bass, &mid, &treble, &presence }) b->reset();
    watch.invalidate();
    inGain.reset (sr, 0.02);
    outGain.reset (sr, 0.02);
    position.reset (sr, 0.06);
    gainDbSmoothed.reset (sr, 0.05);
    position.setCurrentAndTargetValue (get (pCapture));
    gainDbSmoothed.setCurrentAndTargetValue (getGainDb());

    const juce::SpinLock::ScopedLockType sl (modelLock);
    for (size_t i = 0; i < bank->models.size(); ++i)
    {
        auto& m = *bank->models[i];
        prepareModel (m, sr, block);
        if (i == 0) latency = m.resample ? m.resampler->GetLatency() : 0;
        if (i < infos.size()) infos[i].resampling = m.resample;
    }
}

void NamBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);

    // drive: the input trim plus the Gain knob
    const float g0 = gainDbSmoothed.getCurrentValue();
    gainDbSmoothed.setTargetValue (getGainDb());
    gainDbSmoothed.skip (n);
    const float g1 = gainDbSmoothed.getCurrentValue();
    inGain.setTargetValue (dbToGain (get (pInput)));
    for (int i = 0; i < n; ++i)
    {
        const float drive = dbToGain (g0 + (g1 - g0) * (float) i / (float) n);
        // captures are trained on signals within +-1; far beyond that a model's fast maths can overflow
        const float x = 0.5f * (l[i] + r[i]) * inGain.getNextValue() * drive;
        mono[(size_t) i] = x > -8.0f && x < 8.0f ? x : (x >= 8.0f ? 8.0f : (x <= -8.0f ? -8.0f : 0.0f));
    }

    bool processed = false;
    {
        const juce::SpinLock::ScopedTryLockType tl (modelLock);
        if (tl.isLocked() && ! bank->models.empty())
        {
            auto& models = bank->models;
            const int count = (int) models.size();
            const float span = (float) juce::jmax (0, count - 1);

            // where the sweep is, sample by sample
            position.setTargetValue (get (pCapture));
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = 0; i < n; ++i)
            {
                const float p = position.getNextValue() * span;
                positions[(size_t) i] = p;
                lo = juce::jmin (lo, p);
                hi = juce::jmax (hi, p);
            }
            const int first = juce::jlimit (0, count - 1, (int) std::floor (lo));
            const int last = juce::jlimit (0, count - 1, (int) std::ceil (hi));
            const bool normalize = get (pNormalize) > 0.5f;

            juce::FloatVectorOperations::clear (l, n);
            for (int k = first; k <= last; ++k)
            {
                auto& m = *models[(size_t) k];
                m.render (mono.data(), n);
                const float norm = normalize ? m.normGain : 1.0f;
                const float c0 = dbToGain (m.compAt (g0)) * norm, c1 = dbToGain (m.compAt (g1)) * norm;
                const float* y = m.out.data();
                for (int i = 0; i < n; ++i)
                {
                    const float w = 1.0f - std::abs (positions[(size_t) i] - (float) k);
                    if (w > 0.0f)
                        l[i] += y[i] * w * (c0 + (c1 - c0) * (float) i / (float) n);
                }
            }
            juce::FloatVectorOperations::copy (r, l, n);
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
        designToneStack (bass, mid, treble, presence, sampleRate);

    outGain.setTargetValue (dbToGain (get (pOutput)));
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
