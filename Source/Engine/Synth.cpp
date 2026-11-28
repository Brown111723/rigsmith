#include "Synth.h"
#include "Dsp.h"
#include "RigFonts.h"
#include <map>
#include <mutex>

namespace rig
{
//==============================================================================
namespace keys
{
void Env::set (double sr, float aMs, float dMs, float s, float rMs) noexcept
{
    attackStep = 1.0f / (float) juce::jmax (1.0, aMs * 0.001 * sr);
    auto coef = [sr] (float ms) { return (float) std::exp (-1.0 / (juce::jmax (1.0, (double) ms * 0.001 * sr) / 4.6)); };   // -40 dB over the time
    decayCoef = coef (dMs);
    releaseCoef = coef (rMs);
    sus = juce::jlimit (0.0f, 1.0f, s);
}

float Env::next() noexcept
{
    switch (stage)
    {
        case Attack:
            level += attackStep;
            if (level >= 1.0f) { level = 1.0f; stage = Decay; }
            break;
        case Decay:
            level = sus + (level - sus) * decayCoef;
            if (level - sus < 1.0e-4f) { level = sus; stage = Sustain; }
            break;
        case Sustain: level = sus; break;
        case Release:
            level *= releaseCoef;
            if (level < 1.0e-4f) { level = 0.0f; stage = Idle; }
            break;
        case Idle: level = 0.0f; break;
    }
    return level;
}

float Svf::process (float x, float g, float k, int mode) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float v3 = x - s2;
    const float v1 = a1 * s1 + g * a1 * v3;
    const float v2 = s2 + g * v1;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    switch (mode)
    {
        case 1:  return v1 * k;                  // band (unity peak)
        case 2:  return x - k * v1 - v2;         // high
        case 3:  return x - k * v1;              // notch
        default: return v2;                      // low
    }
}

const juce::StringArray& filterTypes()
{
    static const juce::StringArray t { "LP 12", "LP 24", "Band", "HP 12", "Notch", "Off" };
    return t;
}

juce::String noteName (int n)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    n = juce::jlimit (0, 127, n);
    return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
}
} // namespace keys

//==============================================================================
float Wavetable::read (double phase, float position, int mip) const noexcept
{
    const float fp = juce::jlimit (0.0f, 1.0f, position) * (float) (frames - 1);
    const int f0 = (int) fp, f1 = juce::jmin (f0 + 1, frames - 1);
    const float ff = fp - (float) f0;
    const double x = phase * size;
    int i0 = (int) x;
    const float fr = (float) (x - i0);
    i0 &= size - 1;
    const int i1 = (i0 + 1) & (size - 1);
    const float* m = mips[(size_t) juce::jlimit (0, numMips - 1, mip)].data();
    const float* a = m + (size_t) f0 * size;
    const float* b = m + (size_t) f1 * size;
    const float va = a[i0] + (a[i1] - a[i0]) * fr;
    const float vb = b[i0] + (b[i1] - b[i0]) * fr;
    return va + (vb - va) * ff;
}

int Wavetable::mipFor (double hz, double sr) noexcept
{
    const double maxH = sr * 0.45 / juce::jmax (1.0, hz);
    if (maxH >= 1024.0) return 0;
    return juce::jlimit (0, numMips - 1, (int) std::ceil (std::log2 (1024.0 / juce::jmax (1.0, maxH))));
}

std::shared_ptr<const Wavetable> Wavetable::fromFrames (const juce::String& name, const std::vector<std::vector<float>>& in)
{
    auto t = std::make_shared<Wavetable>();
    t->name = name;
    t->frames = juce::jmax (1, (int) in.size());
    t->mips.assign ((size_t) numMips, std::vector<float> ((size_t) t->frames * size, 0.0f));
    juce::dsp::FFT fft (11);   // 2048
    std::vector<float> spec ((size_t) size * 2), work ((size_t) size * 2);
    for (int f = 0; f < t->frames; ++f)
    {
        // the frame at 2048 samples
        const auto& src = in[(size_t) juce::jmin (f, (int) in.size() - 1)];
        std::fill (spec.begin(), spec.end(), 0.0f);
        const int len = juce::jmax (1, (int) src.size());
        for (int i = 0; i < size; ++i)
        {
            const double x = (double) i * len / size;
            const int a = (int) x % len, b = (a + 1) % len;
            const float fr = (float) (x - std::floor (x));
            spec[(size_t) i] = src.empty() ? 0.0f : src[(size_t) a] + (src[(size_t) b] - src[(size_t) a]) * fr;
        }
        fft.performRealOnlyForwardTransform (spec.data());
        spec[0] = spec[1] = 0.0f;   // no DC
        float scale = 0.0f;
        for (int m = 0; m < numMips; ++m)
        {
            const int H = 1024 >> m;
            work = spec;
            for (int h = H + 1; h <= size / 2; ++h)
            {
                work[(size_t) h * 2] = work[(size_t) h * 2 + 1] = 0.0f;
                if (h < size)
                {
                    const int mirror = size - h;
                    if (mirror > H) work[(size_t) mirror * 2] = work[(size_t) mirror * 2 + 1] = 0.0f;
                }
            }
            fft.performRealOnlyInverseTransform (work.data());
            if (m == 0)
            {
                float pk = 0.0f;
                for (int i = 0; i < size; ++i) pk = juce::jmax (pk, std::abs (work[(size_t) i]));
                scale = pk > 1.0e-9f ? 0.9f / pk : 0.0f;   // every frame at the same peak
            }
            auto* dst = t->mips[(size_t) m].data() + (size_t) f * size;
            for (int i = 0; i < size; ++i) dst[i] = work[(size_t) i] * scale;
        }
    }
    return t;
}

std::shared_ptr<const Wavetable> Wavetable::fromFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) { error = "Couldn't read " + file.getFileName(); return nullptr; }
    const int len = (int) juce::jmin ((juce::int64) size * 256, r->lengthInSamples);
    if (len < 16) { error = file.getFileName() + " is too short for a wavetable"; return nullptr; }
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, r->numChannels), len);
    r->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
    std::vector<std::vector<float>> frames;
    if (len % size == 0 || len >= size * 2)
        for (int at = 0; at + size <= len; at += size)
            frames.emplace_back (mono.begin() + at, mono.begin() + at + size);   // Serum's layout: 2048-sample frames
    else
        frames.push_back (mono);                                                    // one cycle
    return fromFrames (file.getFileNameWithoutExtension(), frames);
}

const std::vector<std::shared_ptr<const Wavetable>>& Wavetable::factory()
{
    static const std::vector<std::shared_ptr<const Wavetable>> tables = []
    {
        constexpr int F = 32, N = 2048;
        constexpr double tau = juce::MathConstants<double>::twoPi;
        std::vector<std::shared_ptr<const Wavetable>> out;
        auto make = [&] (const char* name, std::function<float (double x, float t)> fn)
        {
            std::vector<std::vector<float>> fr ((size_t) F, std::vector<float> ((size_t) N));
            for (int f = 0; f < F; ++f)
                for (int i = 0; i < N; ++i)
                    fr[(size_t) f][(size_t) i] = fn ((double) i / N, (float) f / (F - 1));
            out.push_back (fromFrames (name, fr));
        };
        auto additive = [&] (const char* name, std::function<float (int h, float t)> amp)
        {
            std::vector<std::vector<float>> fr ((size_t) F, std::vector<float> ((size_t) N, 0.0f));
            for (int f = 0; f < F; ++f)
                for (int h = 1; h <= 256; ++h)
                {
                    const float a = amp (h, (float) f / (F - 1));
                    if (std::abs (a) < 1.0e-5f) continue;
                    for (int i = 0; i < N; ++i) fr[(size_t) f][(size_t) i] += a * (float) std::sin (tau * h * i / N);
                }
            out.push_back (fromFrames (name, fr));
        };
        auto saw = [] (double x) { return (float) (2.0 * (x - std::floor (x)) - 1.0); };
        auto tri = [] (double x) { const double p = x - std::floor (x); return (float) (1.0 - 4.0 * std::abs (p - 0.5)); };
        auto sq = [] (double x) { return x - std::floor (x) < 0.5 ? 1.0f : -1.0f; };

        // sine > triangle > saw > square
        make ("Basic Shapes", [&] (double x, float t)
        {
            const float s[] { (float) std::sin (tau * x), tri (x + 0.25), saw (x + 0.5), sq (x) };
            const float p = t * 3.0f;
            const int a = juce::jmin (2, (int) p);
            return s[a] + (s[a + 1] - s[a]) * (p - (float) a);
        });
        make ("PWM", [&] (double x, float t)
        {
            const double w = 0.5 - 0.46 * t;
            return (x < w ? 1.0f : -1.0f) - (float) (2.0 * w - 1.0);
        });
        additive ("Harmonics", [] (int h, float t)
        {
            const int top = 1 + (int) (t * 47.0f);
            return h <= top ? 1.0f / (float) h : 0.0f;
        });
        // vowels a e i o u, as formants over a low fundamental
        additive ("Formant", [] (int h, float t)
        {
            static const float fmt[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 280, 2250, 2900 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const float p = t * 4.0f;
            const int a = juce::jmin (3, (int) p);
            const float fr = p - (float) a;
            const float hz = 110.0f * (float) h;
            float g = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float f = fmt[a][k] + (fmt[a + 1][k] - fmt[a][k]) * fr;
                const float bw = 80.0f + 40.0f * (float) k;
                g += std::exp (-0.5f * juce::square ((hz - f) / bw)) / (1.0f + (float) k * 0.6f);
            }
            return hz > 8000.0f ? 0.0f : g / std::sqrt ((float) h);
        });
        additive ("Organ", [] (int h, float t)
        {
            // drawbar registrations morphing: 8' alone, 8'+4', gospel 888, full
            static const float reg[4][7] { { 1, 0, 0, 0, 0, 0, 0 }, { 1, 0.8f, 0, 0.5f, 0, 0, 0 }, { 1, 0.9f, 0.8f, 0.6f, 0.3f, 0, 0.2f }, { 1, 1, 1, 1, 0.8f, 0.7f, 0.8f } };
            static const int harm[7] { 1, 2, 3, 4, 5, 6, 8 };
            const float p = t * 3.0f;
            const int a = juce::jmin (2, (int) p);
            for (int k = 0; k < 7; ++k)
                if (harm[k] == h) return reg[a][k] + (reg[a + 1][k] - reg[a][k]) * (p - (float) a);
            return 0.0f;
        });
        make ("FM Bell", [&] (double x, float t)
        {
            const double index = 0.2 + 7.0 * t;
            return (float) std::sin (tau * x + index * std::sin (tau * 3.0 * x));
        });
        make ("Sync", [&] (double x, float t)
        {
            const double ratio = 1.0 + 6.0 * t;
            return saw (x * ratio) * (float) (1.0 - x * 0.3);
        });
        make ("Digital", [&] (double x, float t)
        {
            const float levels = std::pow (2.0f, 6.0f - 5.0f * t);
            const float v = (float) std::sin (tau * x) * 0.6f + saw (x * 3.0) * 0.4f;
            return std::round (v * levels) / levels;
        });
        make ("Growl", [&] (double x, float t)
        {
            const double m = std::sin (tau * 2.0 * x) * (1.0 + 5.0 * t);
            return dsp::fastTanh ((float) (std::sin (tau * x + m) * (1.0 + 3.0 * t)));
        });
        {
            // random spectra, a little brighter each frame
            juce::Random rng (20260611);
            std::vector<std::vector<float>> fr ((size_t) F, std::vector<float> ((size_t) N, 0.0f));
            std::vector<float> phases (65);
            for (auto& p : phases) p = rng.nextFloat() * (float) tau;
            for (int f = 0; f < F; ++f)
                for (int h = 1; h <= 64; ++h)
                {
                    const float a = rng.nextFloat() * std::pow ((float) h, -1.6f + 1.2f * (float) f / (F - 1));
                    for (int i = 0; i < N; ++i) fr[(size_t) f][(size_t) i] += a * (float) std::sin (tau * h * i / N + phases[(size_t) h]);
                }
            out.push_back (fromFrames ("Spectral", fr));
        }
        return out;
    }();
    return tables;
}

juce::StringArray Wavetable::factoryNames()
{
    juce::StringArray n;
    for (auto& t : factory()) n.add (t->name);
    return n;
}

//==============================================================================
namespace keys
{
const std::vector<BankEntry>& banks()
{
    static const std::vector<BankEntry> all = []
    {
        std::vector<BankEntry> out;
        const char* p = RigFonts::KeysmithBanks_bin;
        const size_t n = (size_t) RigFonts::KeysmithBanks_binSize;
        if (n < 8 || std::memcmp (p, "KSB1", 4) != 0) return out;
        size_t at = 4;
        auto u8 = [&] { return (int) (unsigned char) p[at++]; };
        auto u16 = [&] { const int v = (int) (unsigned char) p[at] | ((int) (unsigned char) p[at + 1] << 8); at += 2; return v; };
        auto u32 = [&] { juce::uint32 v = 0; for (int k = 0; k < 4; ++k) v |= (juce::uint32) (unsigned char) p[at + (size_t) k] << (8 * k); at += 4; return (size_t) v; };
        auto str = [&] { const int len = u16(); juce::String s = juce::String::fromUTF8 (p + at, len); at += (size_t) len; return s; };
        const size_t count = u32();
        for (size_t i = 0; i < count && at < n; ++i)
        {
            BankEntry e;
            e.sample = u8() == 1;
            e.category = str();
            e.name = str();
            if (! e.sample)
            {
                e.frames = u16();
                e.frameLen = u16();
                e.data = p + at;
                e.size = (size_t) (e.frames * e.frameLen * 2);
            }
            else
            {
                e.root = u8();
                e.size = u32();
                e.data = p + at;
            }
            at += e.size;
            if (at > n) break;
            out.push_back (e);
        }
        return out;
    }();
    return all;
}

const BankEntry* findBank (const juce::String& ref)
{
    for (auto& e : banks()) if (e.ref().equalsIgnoreCase (ref)) return &e;
    return nullptr;
}

juce::StringArray bankCategories (bool samples)
{
    juce::StringArray c;
    for (auto& e : banks()) if (e.sample == samples) c.addIfNotAlreadyThere (e.category);
    return c;
}

static std::shared_ptr<const Wavetable> bankTable (const BankEntry& e)
{
    static std::mutex m;
    static std::map<juce::String, std::shared_ptr<const Wavetable>> cache;
    const std::lock_guard<std::mutex> g (m);
    if (auto it = cache.find (e.ref()); it != cache.end()) return it->second;
    std::vector<std::vector<float>> frames ((size_t) e.frames, std::vector<float> ((size_t) e.frameLen));
    const auto* s = reinterpret_cast<const unsigned char*> (e.data);
    for (int f = 0; f < e.frames; ++f)
        for (int i = 0; i < e.frameLen; ++i)
        {
            const size_t k = ((size_t) f * (size_t) e.frameLen + (size_t) i) * 2;
            frames[(size_t) f][(size_t) i] = (float) (juce::int16) (s[k] | (s[k + 1] << 8)) / 32767.0f;
        }
    auto t = Wavetable::fromFrames (e.name, frames);
    cache[e.ref()] = t;
    return t;
}
} // namespace keys

//==============================================================================
static ParamSpec timeParam (const char* id, const char* name, float def, float mx = 10000.0f)
{
    return P (id, name, 0.0f, mx, def, "ms").mid (mx >= 10000.0f ? 400.0f : 300.0f);
}

/** The filter, envelopes and voicing every note block has, appended to its own knobs. */
static void addCommon (std::vector<ParamSpec>& s)
{
    s.push_back (Choice ("ftype", "Filter", keys::filterTypes(), 1));
    s.push_back (P ("cutoff", "Cutoff", 20.0f, 20000.0f, 4000.0f, "Hz").mid (1200.0f).rand (500.0f, 9000.0f));
    s.push_back (P ("res", "Resonance", 0.0f, 100.0f, 15.0f, "%").rand (0.0f, 60.0f));
    s.push_back (P ("fdrive", "Drive", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 40.0f));
    s.push_back (P ("keytrack", "Key Track", 0.0f, 100.0f, 50.0f, "%"));
    s.push_back (P ("fenv", "Env Amount", -100.0f, 100.0f, 20.0f, "%").rand (-20.0f, 60.0f));
    s.push_back (timeParam ("attack", "Attack", 4.0f).rand (1.0f, 600.0f));
    s.push_back (timeParam ("decay", "Decay", 600.0f).rand (80.0f, 2500.0f));
    s.push_back (P ("sustain", "Sustain", 0.0f, 100.0f, 75.0f, "%").rand (20.0f, 100.0f));
    s.push_back (timeParam ("release", "Release", 350.0f).rand (60.0f, 2500.0f));
    s.push_back (P ("velocity", "Velocity", 0.0f, 100.0f, 60.0f, "%").fixed());
    s.push_back (timeParam ("matk", "Env 2 Attack", 10.0f).rand (1.0f, 2000.0f));
    s.push_back (timeParam ("mdec", "Env 2 Decay", 900.0f).rand (100.0f, 4000.0f));
    s.push_back (P ("msus", "Env 2 Sustain", 0.0f, 100.0f, 30.0f, "%"));
    s.push_back (timeParam ("mrel", "Env 2 Release", 500.0f).rand (60.0f, 2500.0f));
}

static void addVoicing (std::vector<ParamSpec>& s)
{
    s.push_back (P ("voices", "Voices", 1.0f, 16.0f, 12.0f).dec (0).fixed());
    s.push_back (Choice ("vmode", "Play", { "Poly", "Mono", "Legato" }, 0));
    s.push_back (P ("glide", "Glide", 0.0f, 2000.0f, 0.0f, "ms").mid (150.0f).rand (0.0f, 40.0f));
    s.push_back (P ("bend", "Bend Range", 0.0f, 24.0f, 2.0f, "st").dec (0).fixed());
    s.push_back (P ("level", "Level", -36.0f, 6.0f, -6.0f, "dB").fixed());
}

NoteBlock::NoteBlock (NodeType t, std::vector<ParamSpec> s) : Block (t, std::move (s))
{
    for (auto& v : voices) v.rng.setSeedRandomly();
}

void NoteBlock::prepareImpl (double sr, int block)
{
    juce::ignoreUnused (sr);
    mixL.assign ((size_t) juce::jmax (64, block), 0.0f);
    mixR.assign ((size_t) juce::jmax (64, block), 0.0f);
    vl.assign (64, 0.0f);
    vr.assign (64, 0.0f);
    for (auto& v : voices) { v.amp.kill(); v.mod.kill(); v.note = -1; for (auto& f : v.fl) f.reset(); for (auto& f : v.fl2) f.reset(); }
    monoStack.clear();
    monoStack.reserve (128);
}

void NoteBlock::noteOn (int note, float vel) noexcept
{
    const int mode = getChoice (pMode);
    const int limit = juce::jlimit (1, maxVoices, juce::roundToInt (get (pVoices)));
    const bool glide = get (pGlide) > 0.5f;
    ++clock;
    if (mode != 0)
    {
        // one voice: the newest note plays; legato keeps the envelopes going
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        if (monoStack.size() < 127) monoStack.push_back (note);
        auto& v = voices[0];
        const bool sliding = v.active() && v.held;
        v.note = note;
        v.target = note;
        v.velocity = sliding && mode == 2 ? v.velocity : vel;
        v.held = true;
        v.sustained = false;
        v.age = clock;
        if (! glide || ! v.active()) v.pitch = note;   // else it glides from where it is
        if (sliding && mode == 2) return;   // legato: just slide there
        const bool fresh = ! v.active();
        v.amp.start();
        v.mod.start();
        startVoice (v, fresh);
        return;
    }
    // poly: the same note again takes its voice; else a free one; else the oldest released; else the oldest
    Voice* pick = nullptr;
    double lastPitch = -1.0;
    juce::uint32 newest = 0;
    for (auto& v : voices) if (v.active() && v.age >= newest) { newest = v.age; lastPitch = v.pitch; }
    for (int i = 0; i < limit && pick == nullptr; ++i) if (voices[(size_t) i].note == note && voices[(size_t) i].active()) pick = &voices[(size_t) i];
    for (int i = 0; i < limit && pick == nullptr; ++i) if (! voices[(size_t) i].active()) pick = &voices[(size_t) i];
    if (pick == nullptr)
    {
        juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
        for (int pass = 0; pass < 2 && pick == nullptr; ++pass)
            for (int i = 0; i < limit; ++i)
                if ((pass == 1 || ! voices[(size_t) i].held) && voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; pick = &voices[(size_t) i]; }
    }
    if (pick == nullptr) return;
    const bool fresh = ! pick->active();
    pick->note = note;
    pick->target = note;
    pick->pitch = glide && lastPitch >= 0.0 ? lastPitch : (double) note;
    pick->velocity = vel;
    pick->held = true;
    pick->sustained = false;
    pick->age = clock;
    pick->amp.start();
    pick->mod.start();
    startVoice (*pick, fresh);
}

void NoteBlock::noteOff (int note) noexcept
{
    const int mode = getChoice (pMode);
    auto release = [this] (Voice& v)
    {
        v.held = false;
        if (pedal) { v.sustained = true; return; }
        v.amp.stop();
        v.mod.stop();
    };
    if (mode != 0)
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        auto& v = voices[0];
        if (v.note != note || ! v.held) return;
        if (! monoStack.empty())
        {
            // back to the note still held
            v.note = monoStack.back();
            v.target = v.note;
            if (get (pGlide) <= 0.5f) v.pitch = v.target;
            if (mode == 1) { v.amp.start(); v.mod.start(); startVoice (v, false); }
            return;
        }
        release (v);
        return;
    }
    for (auto& v : voices)
        if (v.note == note && v.held) release (v);
}

void NoteBlock::processImpl (juce::AudioBuffer<float>& buffer, int n) noexcept
{
    if ((int) mixL.size() < n) return;   // never more than prepared
    beginChunk();
    const double sr = sampleRate;
    for (auto& v : voices)
    {
        v.amp.set (sr, get (pAttack), get (pDecay), get (pSustain) * 0.01f, get (pRelease));
        v.mod.set (sr, get (pModA), get (pModD), get (pModS) * 0.01f, get (pModR));
    }
    std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + n, 0.0f);

    // the notes for this stretch, in time
    int pos = 0;
    if (const auto* ev = RigMidi::events; ev != nullptr)
    {
        const int off = RigIo::offset;
        for (auto it = ev->findNextSamplePosition (off); it != ev->cend(); ++it)
        {
            const auto meta = *it;
            const int t = meta.samplePosition - off;
            if (t >= n) break;
            if (t > pos) { render (mixL.data() + pos, mixR.data() + pos, t - pos); pos = t; }
            const auto m = meta.getMessage();
            if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) noteOff (m.getNoteNumber());
            else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * get (pBend);
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();
                if (cc == 1) modWheel = (float) m.getControllerValue() / 127.0f;
                else if (cc == 64)
                {
                    pedal = m.getControllerValue() >= 64;
                    if (! pedal)
                        for (auto& v : voices)
                            if (v.sustained) { v.sustained = false; v.amp.stop(); v.mod.stop(); }
                }
                else if (cc == 120) for (auto& v : voices) { v.amp.kill(); v.mod.kill(); v.held = false; }
                else if (cc == 123) { monoStack.clear(); for (auto& v : voices) { v.held = false; v.sustained = false; v.amp.stop(); v.mod.stop(); } }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& v : voices) { v.held = false; v.amp.stop(); v.mod.stop(); }
        }
    }
    if (pos < n) render (mixL.data() + pos, mixR.data() + pos, n - pos);

    // the notes sound on top of whatever comes in (a layer before it, or nothing)
    const float g = dbToGain (get (pLevel));
    buffer.addFrom (0, 0, mixL.data(), n, g);
    if (buffer.getNumChannels() > 1) buffer.addFrom (1, 0, mixR.data(), n, g);

    std::array<juce::uint32, 4> bits {};
    for (auto& v : voices) if (v.active() && v.note >= 0) bits[(size_t) (v.note >> 5)] |= 1u << (v.note & 31);
    for (size_t i = 0; i < 4; ++i) sounding[i].store (bits[i], std::memory_order_relaxed);
}

void NoteBlock::render (float* l, float* r, int n) noexcept
{
    const double sr = sampleRate;
    const int type = getChoice (pFilter);
    const float cut = get (pCutoff), res = get (pRes) * 0.01f, drive = get (pDrive) * 0.01f;
    const float key = get (pKeyTrack) * 0.01f, fenv = get (pFilterEnv) * 0.01f;
    const float velSens = get (pVel) * 0.01f;
    const float glideMs = get (pGlide);
    const float k = 2.0f - 1.96f * res;                       // 1 / Q
    const int mode = type == 2 ? 1 : type == 3 ? 2 : type == 4 ? 3 : 0;
    const bool twoPole = type == 1, filterOn = type != 5;
    const float driveGain = 1.0f + drive * 6.0f, driveNorm = 1.0f / dsp::fastTanh (driveGain);

    for (int vi = 0; vi < maxVoices; ++vi)
    {
        auto& v = voices[(size_t) vi];
        if (! v.active()) continue;
        const float velGain = (1.0f - velSens) + velSens * v.velocity * v.velocity;
        for (int s0 = 0; s0 < n; s0 += 32)
        {
            const int m = juce::jmin (32, n - s0);
            // glide, at control rate
            if (glideMs > 0.5f) v.pitch += (v.target - v.pitch) * (1.0 - std::exp (-(double) m / (glideMs * 0.001 * sr / 3.0)));
            else v.pitch = v.target;
            std::fill (vl.begin(), vl.begin() + m, 0.0f);
            std::fill (vr.begin(), vr.begin() + m, 0.0f);
            renderVoice (v, vi, vl.data(), vr.data(), m);
            // the filter follows the key and the second envelope
            const float oct = key * (float) (v.pitch - 60.0) / 12.0f + fenv * v.mod.level * 6.0f;
            const float hz = juce::jlimit (20.0f, (float) (sr * 0.45), cut * std::exp2 (oct));
            const float gc = (float) std::tan (juce::MathConstants<double>::pi * hz / sr);
            for (int i = 0; i < m; ++i)
            {
                float a = vl[(size_t) i], b = vr[(size_t) i];
                if (drive > 0.001f) { a = dsp::fastTanh (a * driveGain) * driveNorm; b = dsp::fastTanh (b * driveGain) * driveNorm; }
                if (filterOn)
                {
                    a = v.fl[0].process (a, gc, k, mode);
                    b = v.fl[1].process (b, gc, k, mode);
                    if (twoPole) { a = v.fl2[0].process (a, gc, 1.41f, 0); b = v.fl2[1].process (b, gc, 1.41f, 0); }
                }
                const float e = v.amp.next() * velGain;
                v.mod.next();
                l[s0 + i] += a * e;
                r[s0 + i] += b * e;
            }
            if (! v.amp.active()) { v.note = -1; v.held = false; break; }
        }
    }
}

//==============================================================================
static std::vector<ParamSpec> synthSpecs()
{
    std::vector<ParamSpec> s;
    auto tables = Wavetable::factoryNames();
    tables.add ("User");
    for (const char* o : { "a", "b" })
    {
        const juce::String O (o), U (juce::String (o).toUpperCase());
        const bool a = O == "a";
        s.push_back (Choice (O + "table", "Table " + U, tables, a ? 0 : 2));
        s.push_back (P (O + "pos", "Position " + U, 0.0f, 100.0f, a ? 66.0f : 30.0f, "%"));
        s.push_back (P (O + "semi", "Semi " + U, -24.0f, 24.0f, a ? 0.0f : 12.0f, "st").dec (0).rand (-12.0f, 12.0f));
        s.push_back (P (O + "fine", "Fine " + U, -100.0f, 100.0f, a ? 0.0f : 5.0f, "ct").dec (0).rand (-15.0f, 15.0f));
        s.push_back (P (O + "uni", "Unison " + U, 1.0f, 8.0f, a ? 3.0f : 1.0f).dec (0).rand (1.0f, 5.0f));
        s.push_back (P (O + "det", "Detune " + U, 0.0f, 100.0f, 22.0f, "%").rand (5.0f, 45.0f));
        s.push_back (P (O + "lvl", "Level " + U, 0.0f, 100.0f, a ? 80.0f : 0.0f, "%").rand (a ? 60.0f : 0.0f, a ? 90.0f : 70.0f));
        s.push_back (P (O + "pan", "Pan " + U, -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-30.0f, 30.0f));
    }
    s.push_back (P ("sub", "Sub", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 50.0f));
    s.push_back (Choice ("suboct", "Sub Octave", { "-1", "-2" }, 0));
    s.push_back (P ("noise", "Noise", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 15.0f));
    addCommon (s);
    s.push_back (P ("posenv", "Env 2 > Pos", -100.0f, 100.0f, 0.0f, "%").rand (-40.0f, 60.0f));
    addVoicing (s);
    return s;
}

SynthBlock::SynthBlock() : NoteBlock (NodeType::Synth, synthSpecs())
{
    pFilter = pFType; pCutoff = pFCut; pRes = pFRes; pDrive = pFDrive; pKeyTrack = pFKey; pFilterEnv = pFEnv;
    pAttack = pAAtk; pDecay = pADec; pSustain = pASus; pRelease = pARel; pVel = pAVel;
    pModA = pEAtk; pModD = pEDec; pModS = pESus; pModR = pERel;
    pVoices = pVVoices; pMode = pVMode; pGlide = pVGlide; pBend = pVBend; pLevel = pVLevel;
    groups = {
        { "OSC A", { pTableA, pPosA, pLevelA, pSemiA, pFineA, pUniA, pDetA, pPanA } },
        { "OSC B", { pTableB, pPosB, pLevelB, pSemiB, pFineB, pUniB, pDetB, pPanB } },
        { "SUB / NOISE", { pSubOct, pSub, pNoise } },
        { "FILTER", { pFType, pFCut, pFRes, pFDrive, pFKey, pFEnv } },
        { "AMP", { pAAtk, pADec, pASus, pARel, pAVel } },
        { "ENV 2", { pEAtk, pEDec, pESus, pERel, pEPos } },
        { "VOICE", { pVMode, pVVoices, pVGlide, pVBend, pVLevel } } };
    Wavetable::factory();   // built once, here (not on the audio thread)
}

juce::String SynthBlock::getDetail() const
{
    auto name = [this] (int o)
    {
        const auto* t = tableFor (o);
        return t != nullptr ? t->name : juce::String();
    };
    juce::String d = name (0);
    if (get (pLevelB) > 0.5f) d << " + " << name (1);
    return d;
}

const Wavetable* SynthBlock::tableFor (int o) const noexcept
{
    const int idx = getChoice (o == 0 ? pTableA : pTableB);
    const auto& f = Wavetable::factory();
    if (idx < (int) f.size()) return f[(size_t) idx].get();
    if (auto* u = userOsc[(size_t) juce::jlimit (0, 1, o)].load()) return u;
    if (auto* u = user.load()) return u;
    return f[0].get();
}

juce::String SynthBlock::tableName (int o) const
{
    const auto* t = tableFor (o);
    return t != nullptr ? t->name : juce::String();
}

bool SynthBlock::loadBankTable (const juce::String& ref, int o)
{
    const auto* e = keys::findBank (ref);
    if (e == nullptr || e->sample) return false;
    auto t = keys::bankTable (*e);
    if (t == nullptr) return false;
    o = juce::jlimit (0, 1, o);
    keep.push_back (t);
    userOsc[(size_t) o].store (t.get());
    oscRef[(size_t) o] = "bank:" + e->ref();
    set (o == 1 ? pTableB : pTableA, (float) Wavetable::factory().size());
    return true;
}

bool SynthBlock::loadWavetable (const juce::File& file, int o, juce::String& error)
{
    auto t = Wavetable::fromFile (file, error);
    if (t == nullptr) return false;
    keep.push_back (t);
    user.store (t.get());
    userFile = file;
    userOsc[(size_t) juce::jlimit (0, 1, o)].store (t.get());
    oscRef[(size_t) juce::jlimit (0, 1, o)] = file.getFullPathName();
    set (o == 1 ? pTableB : pTableA, (float) Wavetable::factory().size());
    return true;
}

juce::ValueTree SynthBlock::toState() const
{
    auto v = Block::toState();
    if (userFile != juce::File()) v.setProperty ("wavetable", userFile.getFullPathName(), nullptr);
    if (oscRef[0].isNotEmpty()) v.setProperty ("tableA", oscRef[0], nullptr);
    if (oscRef[1].isNotEmpty()) v.setProperty ("tableB", oscRef[1], nullptr);
    return v;
}

void SynthBlock::fromState (const juce::ValueTree& v)
{
    const auto a = getBase (pTableA), b = getBase (pTableB);
    Block::fromState (v);
    if (const auto path = v.getProperty ("wavetable").toString(); path.isNotEmpty())
    {
        juce::String err;
        const float wa = getBase (pTableA), wb = getBase (pTableB);
        if (juce::File (path).existsAsFile()) loadWavetable (juce::File (path), 0, err);
        set (pTableA, wa);
        set (pTableB, wb);
    }
    // each oscillator's own: a bank table or a file
    for (int o = 0; o < 2; ++o)
        if (const auto ref = v.getProperty (o == 0 ? "tableA" : "tableB").toString(); ref.isNotEmpty())
        {
            const float wa = getBase (pTableA), wb = getBase (pTableB);
            juce::String err;
            if (ref.startsWith ("bank:")) loadBankTable (ref.substring (5), o);
            else if (juce::File (ref).existsAsFile()) loadWavetable (juce::File (ref), o, err);
            set (pTableA, wa);
            set (pTableB, wb);
        }
    juce::ignoreUnused (a, b);
}

juce::String SynthBlock::formatValue (int i, float v) const
{
    if ((i == pTableA || i == pTableB) && juce::roundToInt (v) >= (int) Wavetable::factory().size())
    {
        if (auto* u = userOsc[i == pTableA ? 0u : 1u].load()) return u->name;
        return user.load() != nullptr ? user.load()->name : juce::String ("User (none)");
    }
    if (i == pUniA || i == pUniB) return juce::String (juce::roundToInt (v)) + (juce::roundToInt (v) == 1 ? " voice" : " voices");
    return Block::formatValue (i, v);
}

void SynthBlock::randomise (juce::Random& rng, float amount)
{
    const float tA = getBase (pTableA), tB = getBase (pTableB);
    Block::randomise (rng, amount);
    const int nf = (int) Wavetable::factory().size();
    if (amount >= 0.999f)
    {
        set (pTableA, (float) rng.nextInt (nf));
        set (pTableB, (float) rng.nextInt (nf));
        set (pFType, rng.nextFloat() < 0.75f ? 1.0f : (float) rng.nextInt (5));
        set (pSubOct, (float) rng.nextInt (2));
    }
    else
    {
        set (pTableA, tA);
        set (pTableB, tB);
    }
}

void SynthBlock::beginChunk() noexcept
{
    for (int o = 0; o < 2; ++o)
    {
        const int base = o == 0 ? pTableA : pTableB;
        auto& K = k[(size_t) o];
        K.table = tableFor (o);
        K.pos = get (base + 1) * 0.01f;
        K.semi = get (base + 2);
        K.fine = get (base + 3);
        K.uni = juce::jlimit (1, maxUnison, juce::roundToInt (get (base + 4)));
        K.det = get (base + 5) * 0.01f;
        K.level = get (base + 6) * 0.01f;
        K.pan = get (base + 7) * 0.01f;
        livePos[(size_t) o].store (K.pos, std::memory_order_relaxed);
    }
    subLevel = get (pSub) * 0.01f;
    subOct = getChoice (pSubOct) + 1;
    noiseLevel = get (pNoise) * 0.01f;
    posEnv = get (pEPos) * 0.01f;
}

void SynthBlock::startVoice (Voice& v, bool fresh) noexcept
{
    const int vi = (int) (&v - voices.data());
    if (! fresh) return;
    for (auto& o : osc[(size_t) vi])
        for (int u = 0; u < maxUnison; ++u)
            o.phase[(size_t) u] = u == 0 ? 0.0 : (double) v.rng.nextFloat();
    subPhase[(size_t) vi] = 0.0;
    for (auto& f : v.fl) f.reset();
    for (auto& f : v.fl2) f.reset();
}

void SynthBlock::renderVoice (Voice& v, int vi, float* l, float* r, int m) noexcept
{
    const double sr = sampleRate;
    for (int o = 0; o < 2; ++o)
    {
        const auto& K = k[(size_t) o];
        if (K.level <= 0.0005f || K.table == nullptr) continue;
        const float pos = juce::jlimit (0.0f, 1.0f, K.pos + posEnv * v.mod.level);
        if (vi == 0 || v.held) livePos[(size_t) o].store (pos, std::memory_order_relaxed);
        const double base = v.pitch + K.semi + K.fine * 0.01 + bend;
        const float norm = K.level / std::sqrt ((float) K.uni);
        auto& ph = osc[(size_t) vi][(size_t) o].phase;
        for (int u = 0; u < K.uni; ++u)
        {
            const float spread = K.uni > 1 ? (float) u / (float) (K.uni - 1) * 2.0f - 1.0f : 0.0f;
            const double hz = 440.0 * std::exp2 ((base + spread * K.det * 0.6 - 69.0) / 12.0);
            const double inc = hz / sr;
            const int mip = Wavetable::mipFor (hz, sr);
            const float p = juce::jlimit (-1.0f, 1.0f, K.pan + spread * 0.75f);
            const float gl = norm * std::cos ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
            const float gr = norm * std::sin ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
            double phase = ph[(size_t) u];
            for (int i = 0; i < m; ++i)
            {
                const float x = K.table->read (phase, pos, mip);
                l[i] += x * gl;
                r[i] += x * gr;
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
            }
            ph[(size_t) u] = phase;
        }
    }
    if (subLevel > 0.0005f)
    {
        const double inc = 440.0 * std::exp2 ((v.pitch - 12.0 * subOct + bend - 69.0) / 12.0) / sr;
        double phase = subPhase[(size_t) vi];
        for (int i = 0; i < m; ++i)
        {
            const float x = (float) std::sin (juce::MathConstants<double>::twoPi * phase) * subLevel * 0.8f;
            l[i] += x;
            r[i] += x;
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
        subPhase[(size_t) vi] = phase;
    }
    if (noiseLevel > 0.0005f)
        for (int i = 0; i < m; ++i)
        {
            l[i] += (v.rng.nextFloat() * 2.0f - 1.0f) * noiseLevel * 0.5f;
            r[i] += (v.rng.nextFloat() * 2.0f - 1.0f) * noiseLevel * 0.5f;
        }
}

//==============================================================================
static std::shared_ptr<const SamplerBlock::Sample> finishSample (std::shared_ptr<SamplerBlock::Sample> s)
{
    const int n = s->data.getNumSamples();
    s->peaks.assign (512, 0.0f);
    for (int c = 0; c < 512; ++c)
    {
        const int a = (int) ((juce::int64) n * c / 512), b = juce::jmax (a + 1, (int) ((juce::int64) n * (c + 1) / 512));
        float pk = 0.0f;
        for (int ch = 0; ch < s->data.getNumChannels(); ++ch)
            pk = juce::jmax (pk, s->data.getMagnitude (ch, a, juce::jmin (b, n) - a));
        s->peaks[(size_t) c] = pk;
    }
    return s;
}

const std::vector<std::shared_ptr<const SamplerBlock::Sample>>& SamplerBlock::factory()
{
    // Keysmith's own sounds, made here (so a Sampler plays before any library is set up). All at C3.
    static const std::vector<std::shared_ptr<const Sample>> sounds = []
    {
        constexpr double sr = 48000.0, tau = juce::MathConstants<double>::twoPi;
        const double f0 = 261.6256;   // C4 in Hz - the note we call C3 (60)
        std::vector<std::shared_ptr<const Sample>> out;
        auto make = [&] (const char* name, double seconds, std::function<void (float* l, float* r, int n)> fill)
        {
            auto s = std::make_shared<Sample>();
            s->name = name;
            s->rate = sr;
            const int n = (int) (seconds * sr);
            s->data.setSize (2, n);
            s->data.clear();
            fill (s->data.getWritePointer (0), s->data.getWritePointer (1), n);
            const float pk = juce::jmax (s->data.getMagnitude (0, 0, n), s->data.getMagnitude (1, 0, n));
            if (pk > 0.0f) s->data.applyGain (0.8f / pk);
            out.push_back (finishSample (s));
        };
        juce::Random rng (777);
        make ("Glass Pad", 5.0, [&] (float* l, float* r, int n)
        {
            for (int h = 1; h <= 14; ++h)
                for (int side = 0; side < 2; ++side)
                {
                    const double det = 1.0 + (side == 0 ? -0.0016 : 0.0019) * (1 + h % 3);
                    const double a = 0.5 / std::pow (h, 1.15), swell = 0.25 + rng.nextDouble() * 0.6, ph = rng.nextDouble() * tau;
                    float* dst = side == 0 ? l : r;
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / sr;
                        const double env = std::min (1.0, t / 0.9) * (0.75 + 0.25 * std::sin (tau * swell * t + ph));
                        dst[i] += (float) (a * env * std::sin (tau * f0 * h * det * t + ph));
                    }
                }
        });
        make ("Felt Keys", 4.0, [&] (float* l, float* r, int n)
        {
            for (int h = 1; h <= 18; ++h)
            {
                const double fh = f0 * h * std::sqrt (1.0 + 0.0004 * h * h);   // a little inharmonic, like strings
                const double a = 0.7 / std::pow (h, 1.4), dec = 1.6 / (1.0 + 0.35 * h), ph = rng.nextDouble() * tau;
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / sr;
                    const float x = (float) (a * std::exp (-t / dec) * std::min (1.0, t / 0.004) * std::sin (tau * fh * t + ph));
                    l[i] += x; r[i] += x * 0.96f;
                }
            }
            dsp::OnePole lp; lp.setCutoff (sr, 900.0);
            for (int i = 0; i < 2400; ++i) { const float k = lp.lp (rng.nextFloat() * 2.0f - 1.0f) * 0.25f * (1.0f - i / 2400.0f); l[i] += k; r[i] += k; }   // the hammer
        });
        make ("Choir Air", 5.0, [&] (float* l, float* r, int n)
        {
            static const float fmt[3] { 750.0f, 1150.0f, 2800.0f };
            for (int voice = 0; voice < 4; ++voice)
            {
                const double det = 1.0 + (voice - 1.5) * 0.004, vib = 4.5 + voice * 0.4;
                for (int h = 1; h <= 30; ++h)
                {
                    const double hz = f0 * h;
                    double g = 0.0;
                    for (int k = 0; k < 3; ++k) g += std::exp (-0.5 * std::pow ((hz - fmt[k]) / (90.0 + 50.0 * k), 2.0)) / (1.0 + k);
                    if (g < 0.003) continue;
                    const double ph = rng.nextDouble() * tau;
                    float* dst = voice % 2 == 0 ? l : r;
                    double phase = ph;
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / sr;
                        phase += tau * hz * det * (1.0 + 0.004 * std::sin (tau * vib * t)) / sr;
                        dst[i] += (float) (g * 0.12 * std::min (1.0, t / 0.35) * std::sin (phase));
                    }
                }
            }
            dsp::OnePole hp; hp.setCutoff (sr, 3000.0);
            for (int i = 0; i < n; ++i) { const float b = hp.hp (rng.nextFloat() * 2.0f - 1.0f) * 0.02f * std::min (1.0f, i / 20000.0f); l[i] += b; r[i] += b; }   // breath
        });
        make ("Bell Tines", 5.0, [&] (float* l, float* r, int n)
        {
            static const double ratio[] { 1.0, 2.0, 2.76, 4.07, 5.4, 8.93 };
            static const double amp[] { 1.0, 0.35, 0.5, 0.2, 0.25, 0.12 };
            for (int k = 0; k < 6; ++k)
            {
                const double dec = 3.2 / (1.0 + k * 0.8), ph = rng.nextDouble() * tau;
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / sr;
                    const float x = (float) (amp[k] * std::exp (-t / dec) * std::min (1.0, t / 0.002) * std::sin (tau * f0 * ratio[k] * t + ph));
                    l[i] += x; r[i] += k % 2 == 0 ? x : -x * 0.8f;
                }
            }
        });
        make ("Noise Swell", 4.0, [&] (float* l, float* r, int n)
        {
            keys::Svf fl, fr;
            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) n;
                const float hz = 300.0f + 5000.0f * t * t;
                const float g = std::tan (juce::MathConstants<float>::pi * hz / (float) sr);
                const float env = std::sin (juce::MathConstants<float>::pi * t);
                l[i] = fl.process (rng.nextFloat() * 2.0f - 1.0f, g, 0.4f, 1) * env;
                r[i] = fr.process (rng.nextFloat() * 2.0f - 1.0f, g, 0.4f, 1) * env;
            }
        });
        make ("Pluck", 2.5, [&] (float* l, float* r, int n)
        {
            // Karplus-Strong at C
            const int period = (int) std::round (sr / f0);
            std::vector<float> line ((size_t) period);
            for (auto& x : line) x = rng.nextFloat() * 2.0f - 1.0f;
            int at = 0;
            for (int i = 0; i < n; ++i)
            {
                const int nx = (at + 1) % period;
                const float y = 0.5f * (line[(size_t) at] + line[(size_t) nx]) * 0.996f;
                line[(size_t) at] = y;
                at = nx;
                l[i] = y; r[i] = y;
            }
        });
        return out;
    }();
    return sounds;
}

juce::StringArray SamplerBlock::factoryNames()
{
    juce::StringArray n;
    for (auto& s : factory()) n.add (s->name);
    return n;
}

bool SamplerBlock::isSoundFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

static std::vector<ParamSpec> samplerSpecs()
{
    std::vector<ParamSpec> s;
    auto sources = SamplerBlock::factoryNames();
    sources.add ("File");
    s.push_back (Choice ("source", "Sound", sources, 0));
    s.push_back (Choice ("mode", "Mode", { "Classic", "Stretch", "Granular" }, 0));
    s.push_back (P ("root", "Root", 24.0f, 96.0f, 60.0f).dec (0).fixed());
    s.push_back (P ("start", "Position", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 70.0f));
    s.push_back (P ("length", "Length", 1.0f, 100.0f, 100.0f, "%").rand (20.0f, 100.0f));
    s.push_back (Toggle ("loop", "Loop", true));
    s.push_back (Toggle ("reverse", "Reverse", false));
    s.push_back (P ("tune", "Tune", -24.0f, 24.0f, 0.0f, "st").dec (0).rand (-12.0f, 12.0f));
    s.push_back (P ("fine", "Fine", -100.0f, 100.0f, 0.0f, "ct").dec (0).rand (-10.0f, 10.0f));
    s.push_back (P ("speed", "Speed", -200.0f, 200.0f, 100.0f, "%").dec (0).rand (-50.0f, 150.0f));
    s.push_back (P ("grain", "Grain", 10.0f, 500.0f, 90.0f, "ms").mid (100.0f).rand (25.0f, 250.0f));
    s.push_back (P ("density", "Density", 1.0f, 8.0f, 4.0f, "x").rand (2.0f, 7.0f));
    s.push_back (P ("spray", "Spray", 0.0f, 100.0f, 8.0f, "%").rand (0.0f, 50.0f));
    s.push_back (P ("pspread", "Pitch Spread", 0.0f, 12.0f, 0.0f, "st").rand (0.0f, 3.0f));
    s.push_back (P ("spread", "Stereo", 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 100.0f));
    addCommon (s);
    s.push_back (P ("posenv", "Env 2 > Pos", -100.0f, 100.0f, 0.0f, "%").rand (-30.0f, 40.0f));
    addVoicing (s);
    s[(size_t) SamplerBlock::pFType].def = 5.0f;   // a sound plays as recorded until the filter's asked for
    return s;
}

SamplerBlock::SamplerBlock() : NoteBlock (NodeType::Sampler, samplerSpecs())
{
    pFilter = pFType; pCutoff = pFCut; pRes = pFRes; pDrive = pFDrive; pKeyTrack = pFKey; pFilterEnv = pFEnv;
    pAttack = pAAtk; pDecay = pADec; pSustain = pASus; pRelease = pARel; pVel = pAVel;
    pModA = pEAtk; pModD = pEDec; pModS = pESus; pModR = pERel;
    pVoices = pVVoices; pMode = pVMode; pGlide = pVGlide; pBend = pVBend; pLevel = pVLevel;
    set (pFType, 5.0f);
    groups = {
        { "SOUND", { pSource, pModeS, pLoop, pReverse, pStart, pLength, pRoot, pTune, pFine } },
        { "STRETCH / GRAINS", { pSpeed, pGrain, pDensity, pSpray, pPitchSpread, pSpread } },
        { "FILTER", { pFType, pFCut, pFRes, pFDrive, pFKey, pFEnv } },
        { "AMP", { pAAtk, pADec, pASus, pARel, pAVel } },
        { "ENV 2", { pEAtk, pEDec, pESus, pERel, pEPos } },
        { "VOICE", { pVMode, pVVoices, pVGlide, pVBend, pVLevel } } };
    factory();
    for (auto& p : playheads) p.store (-1.0f);
}

juce::String SamplerBlock::getDetail() const
{
    const auto* s = currentSample();
    return (s != nullptr ? s->name : juce::String ("no sound")) + "  -  " + getSpecs()[pModeS].choices[getChoice (pModeS)];
}

const SamplerBlock::Sample* SamplerBlock::currentSample() const noexcept
{
    const int idx = getChoice (pSource);
    const auto& f = factory();
    if (idx < (int) f.size()) return f[(size_t) idx].get();
    return loaded.load();
}

bool SamplerBlock::loadSample (const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = "Couldn't read " + f.getFileName(); return false; }
    const int len = (int) juce::jmin ((juce::int64) (r->sampleRate * 90.0), r->lengthInSamples);   // up to 90 s
    if (len < 32) { error = f.getFileName() + " is empty"; return false; }
    auto s = std::make_shared<Sample>();
    s->name = f.getFileNameWithoutExtension();
    s->rate = r->sampleRate > 0 ? r->sampleRate : 48000.0;
    s->data.setSize (2, len);
    r->read (&s->data, 0, len, 0, true, true);
    if (r->numChannels < 2) s->data.copyFrom (1, 0, s->data, 0, 0, len);
    auto done = finishSample (s);
    keep.push_back (done);
    loaded.store (done.get());
    file = f;
    bankRef = {};
    set (pSource, (float) factory().size());
    return true;
}

bool SamplerBlock::loadBankSample (const juce::String& ref)
{
    const auto* e = keys::findBank (ref);
    if (e == nullptr || ! e->sample) return false;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (e->data, e->size, false), true));
    if (r == nullptr || r->lengthInSamples < 32) return false;
    const int len = (int) r->lengthInSamples;
    auto s = std::make_shared<Sample>();
    s->name = e->name;
    s->rate = r->sampleRate > 0 ? r->sampleRate : 32000.0;
    s->data.setSize (2, len);
    r->read (&s->data, 0, len, 0, true, true);
    if (r->numChannels < 2) s->data.copyFrom (1, 0, s->data, 0, 0, len);
    auto done = finishSample (s);
    keep.push_back (done);
    loaded.store (done.get());
    file = juce::File();
    bankRef = e->ref();
    set (pSource, (float) factory().size());
    set (pRoot, (float) e->root);
    return true;
}

juce::ValueTree SamplerBlock::toState() const
{
    auto v = Block::toState();
    if (file != juce::File()) v.setProperty ("file", file.getFullPathName(), nullptr);
    if (bankRef.isNotEmpty()) v.setProperty ("bank", bankRef, nullptr);
    return v;
}

void SamplerBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    if (const auto path = v.getProperty ("file").toString(); path.isNotEmpty() && juce::File (path).existsAsFile())
    {
        const float src = getBase (pSource);
        juce::String err;
        loadSample (juce::File (path), err);
        set (pSource, src);
    }
    if (const auto ref = v.getProperty ("bank").toString(); ref.isNotEmpty())
    {
        const float src = getBase (pSource), root = getBase (pRoot);
        loadBankSample (ref);
        set (pSource, src);
        set (pRoot, root);
    }
}

juce::String SamplerBlock::formatValue (int i, float v) const
{
    if (i == pRoot) return keys::noteName (juce::roundToInt (v));
    if (i == pSource && juce::roundToInt (v) >= (int) factory().size())
        return loaded.load() != nullptr ? loaded.load()->name : juce::String ("File (none)");
    return Block::formatValue (i, v);
}

void SamplerBlock::randomise (juce::Random& rng, float amount)
{
    const float src = getBase (pSource), root = getBase (pRoot);
    Block::randomise (rng, amount);
    set (pRoot, root);
    if (amount >= 0.999f && ! (loaded.load() != nullptr && juce::roundToInt (src) >= (int) factory().size() && rng.nextBool()))
        set (pSource, (float) rng.nextInt ((int) factory().size()));
    else
        set (pSource, src);
    if (amount >= 0.999f) set (pFType, rng.nextFloat() < 0.5f ? 5.0f : (float) rng.nextInt (2));
}

void SamplerBlock::beginChunk() noexcept
{
    for (size_t i = 0; i < playheads.size(); ++i)
        if (! voices[i].active()) playheads[i].store (-1.0f, std::memory_order_relaxed);
    now = currentSample();
    modeNow = getChoice (pModeS);
    startNow = get (pStart) * 0.01f;
    lengthNow = get (pLength) * 0.01f;
    loopNow = get (pLoop) > 0.5f;
    reverseNow = get (pReverse) > 0.5f;
    speedNow = get (pSpeed) * 0.01f;
    sprayNow = modeNow == 2 ? get (pSpray) * 0.01f : 0.0f;
    pitchSpreadNow = modeNow == 2 ? get (pPitchSpread) : 0.0f;
    spreadNow = modeNow == 2 ? get (pSpread) * 0.01f : 0.0f;
    posEnvNow = get (pEPos) * 0.01f;
    grainLen = juce::jmax (64, (int) (get (modeNow == 1 ? pGrain : pGrain) * 0.001f * (float) sampleRate));
    if (modeNow == 1) grainLen = juce::jmax (grainLen, (int) (0.04 * sampleRate));
    const float dens = juce::jmax (1.0f, get (pDensity));
    grainGap = (double) grainLen / (modeNow == 1 ? juce::jmax (2.0f, dens) : dens);
}

void SamplerBlock::startVoice (Voice& v, bool fresh) noexcept
{
    const int vi = (int) (&v - voices.data());
    auto& p = play[(size_t) vi];
    if (! fresh && modeNow != 0) return;   // grains keep flowing through a legato change
    p.done = false;
    p.untilGrain = 0.0;
    for (auto& g : p.grains) g.on = false;
    const int n = now != nullptr ? now->data.getNumSamples() : 1;
    const double a = startNow * n, len = juce::jmax (64.0, lengthNow * (double) (n - (int) a));
    p.head = reverseNow ? a + len - 1.0 : a;
    p.regionHead = 0.0;
    for (auto& f : v.fl) f.reset();
    for (auto& f : v.fl2) f.reset();
}

void SamplerBlock::renderVoice (Voice& v, int vi, float* l, float* r, int m) noexcept
{
    const auto* S = now;
    auto& p = play[(size_t) vi];
    if (S == nullptr || p.done) { if (p.done) v.amp.kill(); return; }
    const int n = S->data.getNumSamples();
    const float* L = S->data.getReadPointer (0);
    const float* R = S->data.getReadPointer (1);
    const double rs = S->rate / sampleRate;
    const double a = juce::jlimit (0.0, (double) n - 64.0, (double) startNow * n);
    const double len = juce::jlimit (64.0, (double) n - a, (double) lengthNow * (n - a));
    const double ratio = std::exp2 ((v.pitch + get (pTune) + get (pFine) * 0.01 + bend - get (pRoot)) / 12.0);
    const double dir = reverseNow ? -1.0 : 1.0;
    auto readAt = [&] (double x, float& outL, float& outR)
    {
        x = std::fmod (x, (double) n);
        if (x < 0.0) x += n;
        const int i0 = (int) x, i1 = (i0 + 1) % n;
        const float fr = (float) (x - i0);
        outL = L[i0] + (L[i1] - L[i0]) * fr;
        outR = R[i0] + (R[i1] - R[i0]) * fr;
    };

    if (modeNow == 0)
    {
        // Classic: the sound, pitched by the note; looped over its region or played once
        const double step = ratio * rs * dir;
        for (int i = 0; i < m; ++i)
        {
            if (p.head < a || p.head >= a + len)
            {
                if (! loopNow) { p.done = true; v.amp.kill(); break; }
                p.head = reverseNow ? p.head + len : p.head - len;
                if (p.head < a || p.head >= a + len) p.head = reverseNow ? a + len - 1.0 : a;
            }
            float x, y;
            readAt (p.head, x, y);
            l[i] += x;
            r[i] += y;
            p.head += step;
        }
    }
    else
    {
        // Stretch / Granular: a playhead moving at its own speed, grains read at the note's pitch
        const double headStep = speedNow * rs * dir;
        const double offset = posEnvNow * v.mod.level * len;
        const float norm = 2.0f / (float) juce::jmax (1.0, (double) grainLen / grainGap);
        for (int i = 0; i < m; ++i)
        {
            p.untilGrain -= 1.0;
            if (p.untilGrain <= 0.0)
            {
                for (auto& g : p.grains)
                    if (! g.on)
                    {
                        double at = p.head + offset + (sprayNow > 0.0f ? ((double) v.rng.nextFloat() - 0.5) * sprayNow * len : 0.0);
                        at = a + std::fmod (std::fmod (at - a, len) + len, len);
                        g.pos = at;
                        const double jitter = pitchSpreadNow > 0.0f ? std::exp2 (((double) v.rng.nextFloat() * 2.0 - 1.0) * pitchSpreadNow / 12.0) : 1.0;
                        g.step = ratio * rs * jitter * dir;
                        g.length = grainLen;
                        g.age = 0;
                        const float pan = (v.rng.nextFloat() * 2.0f - 1.0f) * spreadNow;
                        g.panL = std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
                        g.panR = std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
                        g.on = true;
                        break;
                    }
                const double jit = modeNow == 2 && sprayNow > 0.0f ? 1.0 + ((double) v.rng.nextFloat() - 0.5) * 0.4 : 1.0;
                p.untilGrain += grainGap * jit;
            }
            float sl = 0.0f, sr = 0.0f;
            for (auto& g : p.grains)
            {
                if (! g.on) continue;
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.length);
                float x, y;
                readAt (g.pos, x, y);
                sl += x * w * g.panL;
                sr += y * w * g.panR;
                g.pos += g.step;
                if (++g.age >= g.length) g.on = false;
            }
            l[i] += sl * norm;
            r[i] += sr * norm;
            p.head += headStep;
            if (p.head >= a + len) p.head -= len;
            if (p.head < a) p.head += len;
        }
    }
    if (vi < (int) playheads.size()) playheads[(size_t) vi].store ((float) (p.head / juce::jmax (1, n)), std::memory_order_relaxed);
}

} // namespace rig
