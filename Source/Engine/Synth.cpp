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
    // (new types go on the end: patches keep their numbers)
    static const juce::StringArray t { "LP 12", "LP 24", "Band", "HP 12", "Notch", "Off",
                                       "LP 6", "HP 24", "Band 24", "Peak", "Ladder", "Comb +", "Comb -",
                                       "Vowel A", "Vowel E", "Vowel I", "Vowel O", "Vowel U", "Phaser" };
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
    for (auto& v : voices)
    {
        v.amp.kill(); v.mod.kill(); v.note = -1;
        for (auto& f : v.fl) f.reset();
        for (auto& f : v.fl2) f.reset();
        for (auto& fv : v.fv) for (auto& f : fv) f.reset();
        for (auto& a : v.ap) a[0] = a[1] = 0.0f;
        v.onePole[0] = v.onePole[1] = 0.0f;
        for (auto& c : v.comb) c.assign (4096, 0.0f);
        v.combAt = 0;
    }
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
        if (holdsThroughNoteOff()) return;   // a one-shot plays to its end
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
    // 0 LP12 1 LP24 2 Band 3 HP12 4 Notch 5 Off 6 LP6 7 HP24 8 Band24 9 Peak 10 Ladder 11 Comb+ 12 Comb- 13-17 Vowels 18 Phaser
    const int mode = type == 2 || type == 8 ? 1 : type == 3 || type == 7 ? 2 : type == 4 ? 3 : 0;
    const bool twoPole = type == 1 || type == 7 || type == 8 || type == 10, filterOn = type != 5;
    const bool svfType = type <= 4 || type == 7 || type == 8 || type == 10;
    // vowels: three formants (Hz) for a male voice; the cutoff shifts them (1 kHz = as written)
    static const float vowels[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
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
            // per-type coefficients, once per 32 samples
            const float onePoleA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sr);
            const int combLen = juce::jlimit (2, 4095, (int) (sr / hz));
            const float combFb = (type == 11 ? 1.0f : -1.0f) * res * 0.95f;
            float gv[3] {};
            if (type >= 13 && type <= 17)
                for (int f = 0; f < 3; ++f)
                    gv[f] = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (60.0, sr * 0.45, (double) vowels[type - 13][f] * hz / 1000.0) / sr);
            const float apC = (gc - 1.0f) / (gc + 1.0f);
            for (int i = 0; i < m; ++i)
            {
                float a = vl[(size_t) i], b = vr[(size_t) i];
                if (drive > 0.001f) { a = dsp::fastTanh (a * driveGain) * driveNorm; b = dsp::fastTanh (b * driveGain) * driveNorm; }
                if (filterOn)
                {
                    float* ch[2] { &a, &b };
                    for (int c = 0; c < 2; ++c)
                    {
                        float& x = *ch[c];
                        if (svfType)
                        {
                            if (type == 10)
                            {
                                // ladder-ish: two resonant low passes, the second through a soft clip (squelch)
                                x = v.fl[c].process (x, gc, k, 0);
                                x = v.fl2[c].process (dsp::fastTanh (x * 1.5f) * 0.8f, gc, 2.0f - 1.2f * res, 0);
                            }
                            else
                            {
                                x = v.fl[c].process (x, gc, k, mode);
                                if (twoPole) x = v.fl2[c].process (x, gc, 1.41f, mode);
                            }
                        }
                        else if (type == 6) { v.onePole[c] += onePoleA * (x - v.onePole[c]); x = v.onePole[c]; }
                        else if (type == 9) x = x + res * 3.0f * v.fl[c].process (x, gc, 0.7f, 1);   // peak: a bell at the cutoff
                        else if (type == 11 || type == 12)
                        {
                            auto& line = v.comb[c];
                            if (line.size() < 4096) continue;
                            const int at = v.combAt;
                            const float d = line[(size_t) ((at - combLen + 4096) & 4095)];
                            const float y = x + combFb * d;
                            line[(size_t) at] = y;
                            x = (x + d) * 0.5f;
                            if (c == 1) v.combAt = (at + 1) & 4095;
                        }
                        else if (type >= 13 && type <= 17)
                        {
                            // vowel: three band passes in parallel
                            float y = 0.0f;
                            const float kq = 0.18f - 0.12f * res;
                            for (int f = 0; f < 3; ++f) y += v.fv[f][c].process (x, gv[f], kq, 1) * (f == 0 ? 1.0f : f == 1 ? 0.7f : 0.4f);
                            x = y * 1.6f;
                        }
                        else if (type == 18)
                        {
                            // phaser: four all-passes at the cutoff, mixed with the dry sound, resonance feeds back
                            float y = x + res * 0.7f * v.ap[3][c];
                            for (int st = 0; st < 4; ++st)
                            {
                                const float out = apC * y + v.ap[st][c];
                                v.ap[st][c] = y - apC * out;
                                y = out;
                            }
                            x = (x + y) * 0.5f;
                        }
                    }
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

//==============================================================================
// Keysmith's engine: SynthBlock. Two oscillators, each a wavetable or a sample.
static juce::StringArray sampleSources()
{
    auto s = SamplerBlock::factoryNames();
    s.add ("Bank / File");
    return s;
}

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
        s.push_back (P (O + "semi", "Semi " + U, -24.0f, 24.0f, a ? 0.0f : 12.0f, "st").dec (0).fixed());
        s.push_back (P (O + "fine", "Fine " + U, -100.0f, 100.0f, a ? 0.0f : 5.0f, "ct").dec (0).fixed());
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
    // each oscillator's own (after the rest: older patches keep their numbers)
    for (const char* o : { "a", "b" })
    {
        const juce::String O (o), U (juce::String (o).toUpperCase());
        s.push_back (Choice (O + "type", "Type " + U, { "Wave", "Sample" }, 0).fixed());
        s.push_back (P (O + "phase", "Phase " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 100.0f));
        s.push_back (P (O + "phrand", "Phase Random " + U, 0.0f, 100.0f, 100.0f, "%"));
        s.push_back (P (O + "sym", "Symmetry " + U, -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-40.0f, 40.0f));
        s.push_back (P (O + "sync", "Hard Sync " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 50.0f));
        s.push_back (P (O + "uspread", "Spread " + U, 0.0f, 100.0f, 75.0f, "%").rand (30.0f, 100.0f));
        s.push_back (P (O + "width", "Width " + U, 0.0f, 100.0f, 100.0f, "%").rand (50.0f, 100.0f));
        s.push_back (P (O + "blend", "Blend " + U, 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 80.0f));
        s.push_back (P (O + "hamt", "Harmonics " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 40.0f));
        s.push_back (P (O + "hcount", "Count " + U, 1.0f, 31.0f, 8.0f).dec (0).rand (2.0f, 16.0f));
        s.push_back (P (O + "htilt", "Tilt " + U, -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-60.0f, 60.0f));
        s.push_back (P (O + "hoddeven", "Odd / Even " + U, -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-80.0f, 80.0f));
        s.push_back (P (O + "hdecay", "Decay " + U, 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 90.0f));
        s.push_back (P (O + "fm", "FM Amount " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 35.0f));
        s.push_back (P (O + "fmratio", "Ratio " + U, 0.5f, 16.0f, 2.0f).dec (2).rand (1.0f, 6.0f));
        s.push_back (P (O + "fmfine", "FM Fine " + U, -50.0f, 50.0f, 0.0f, "ct").dec (0).fixed());
        s.push_back (Choice (O + "fmshape", "Mod Shape " + U, { "Sine", "Triangle", "Saw", "Square" }, 0));
        s.push_back (P (O + "fmfb", "Feedback " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 40.0f));
        s.push_back (Choice (O + "ws", "Shape " + U, { "Off", "Fold", "Clip", "Bend", "Sine", "Saturate" }, 0));
        s.push_back (P (O + "wsdrive", "Drive " + U, 0.0f, 100.0f, 30.0f, "%").rand (0.0f, 70.0f));
        s.push_back (P (O + "wsbias", "Bias " + U, -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-30.0f, 30.0f));
        s.push_back (P (O + "wssym", "Symmetry " + U, -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-40.0f, 40.0f));
        s.push_back (P (O + "wsmix", "Mix " + U, 0.0f, 100.0f, 100.0f, "%").rand (30.0f, 100.0f));
        s.push_back (Choice (O + "src", "Sample " + U, sampleSources(), 0));
        s.push_back (P (O + "sstart", "Start " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 40.0f));
        s.push_back (P (O + "send", "End " + U, 0.0f, 100.0f, 100.0f, "%").rand (60.0f, 100.0f));
        s.push_back (P (O + "root", "Root Note " + U, 24.0f, 96.0f, 60.0f).dec (0).fixed());
        s.push_back (P (O + "sgain", "Gain " + U, -24.0f, 12.0f, 0.0f, "dB").fixed());
        s.push_back (Toggle (O + "srev", "Reverse " + U, false));
        s.push_back (Choice (O + "smode", "Mode " + U, { "One-shot", "Gate", "Loop", "Trigger" }, 2));
        s.push_back (P (O + "sspeed", "Speed " + U, 25.0f, 400.0f, 100.0f, "%").mid (100.0f).fixed());
        s.push_back (Choice (O + "sdir", "Direction " + U, { "Forward", "Reverse", "Ping-pong" }, 0));
        s.push_back (P (O + "srand", "Start Random " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 30.0f));
        s.push_back (Choice (O + "sretrig", "Retrigger " + U, { "Restart", "Continue" }, 0));
        s.push_back (P (O + "track", "Pitch Tracking " + U, 0.0f, 100.0f, 100.0f, "%").fixed());
        s.push_back (P (O + "penv", "Pitch Env " + U, -24.0f, 24.0f, 0.0f, "st").dec (1).fixed());
        s.push_back (P (O + "stretch", "Stretch " + U, 0.0f, 400.0f, 0.0f, "%").rand (0.0f, 200.0f));
        s.push_back (P (O + "formant", "Formant " + U, -12.0f, 12.0f, 0.0f, "st").dec (1).rand (-4.0f, 4.0f));
        s.push_back (Toggle (O + "loop", "Loop " + U, true));
        s.push_back (P (O + "lstart", "Loop Start " + U, 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 50.0f));
        s.push_back (P (O + "lend", "Loop End " + U, 0.0f, 100.0f, 100.0f, "%").rand (55.0f, 100.0f));
        s.push_back (P (O + "lxfade", "Crossfade " + U, 0.0f, 100.0f, 10.0f, "%").rand (0.0f, 50.0f));
        s.push_back (Choice (O + "lmode", "Loop Mode " + U, { "Forward", "Reverse", "Ping-pong" }, 0));
        s.push_back (Toggle (O + "gran", "Granular " + U, false));
        s.push_back (P (O + "gsize", "Grain Size " + U, 10.0f, 500.0f, 90.0f, "ms").mid (100.0f).rand (25.0f, 250.0f));
        s.push_back (P (O + "gdens", "Density " + U, 1.0f, 8.0f, 4.0f, "x").rand (2.0f, 7.0f));
        s.push_back (P (O + "gpos", "Position " + U, 0.0f, 100.0f, 30.0f, "%").rand (0.0f, 90.0f));
        s.push_back (P (O + "gposrand", "Position Random " + U, 0.0f, 100.0f, 10.0f, "%").rand (0.0f, 60.0f));
        s.push_back (P (O + "gpitch", "Grain Pitch " + U, -24.0f, 24.0f, 0.0f, "st").dec (1).fixed());
        s.push_back (P (O + "gover", "Overlap " + U, 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 90.0f));
        s.push_back (P (O + "gspread", "Spread " + U, 0.0f, 100.0f, 50.0f, "%").rand (20.0f, 100.0f));
    }
    s.push_back (P ("mpan", "Pan", -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-20.0f, 20.0f));
    s.push_back (P ("mix", "Mix A / B", -100.0f, 100.0f, 0.0f, "%").dec (0).rand (-60.0f, 60.0f));
    s.push_back (P ("transpose", "Transpose", -24.0f, 24.0f, 0.0f, "st").dec (0).fixed());
    jassert ((int) s.size() == SynthBlock::pTranspose + 1);
    return s;
}

bool SynthBlock::isTuning (const juce::String& id)
{
    return id.endsWith ("semi") || id.endsWith ("fine") || id == "tune" || id.endsWith ("root") || id == "transpose" || id == "bend"
        || id.endsWith ("track") || id.endsWith ("penv") || id.endsWith ("sspeed") || id.endsWith ("gpitch");
}

SynthBlock::SynthBlock() : NoteBlock (NodeType::Synth, synthSpecs())
{
    pFilter = pFType; pCutoff = pFCut; pRes = pFRes; pDrive = pFDrive; pKeyTrack = pFKey; pFilterEnv = pFEnv;
    pAttack = pAAtk; pDecay = pADec; pSustain = pASus; pRelease = pARel; pVel = pAVel;
    pModA = pEAtk; pModD = pEDec; pModS = pESus; pModR = pERel;
    pVoices = pVVoices; pMode = pVMode; pGlide = pVGlide; pBend = pVBend; pLevel = pVLevel;
    groups = {
        { "OSC A", { oscParam (0, oType), pTableA, pPosA, pLevelA, pSemiA, pFineA, pUniA, pDetA, pPanA } },
        { "OSC B", { oscParam (1, oType), pTableB, pPosB, pLevelB, pSemiB, pFineB, pUniB, pDetB, pPanB } },
        { "SUB / NOISE", { pSubOct, pSub, pNoise } },
        { "FILTER", { pFType, pFCut, pFRes, pFDrive, pFKey, pFEnv } },
        { "AMP", { pAAtk, pADec, pASus, pARel, pAVel } },
        { "ENV 2", { pEAtk, pEDec, pESus, pERel, pEPos } },
        { "MASTER", { pVLevel, pMPan, pMix, pTranspose, pVMode, pVVoices, pVGlide, pVBend } } };
    Wavetable::factory();      // built once, here (not on the audio thread)
    SamplerBlock::factory();
    for (auto& h : liveHead) h.store (-1.0f);
}

std::vector<int> SynthBlock::pageParams (int o, const juce::String& page) const
{
    auto P_ = [o] (int kk) { return oscParam (o, kk); };
    const int base = o == 0 ? pTableA : pTableB;   // table, pos, semi, fine, uni, det, lvl, pan
    if (page == "WAVE") return { base + 1, P_ (oPhase), P_ (oPhaseRand), P_ (oSym), P_ (oSync), base + 2, base + 3 };
    if (page == "UNISON") return { base + 4, base + 5, P_ (oUSpread), P_ (oWidth), P_ (oBlend) };
    if (page == "HARM") return { P_ (oHAmt), P_ (oHCount), P_ (oHTilt), P_ (oHOddEven), P_ (oHDecay) };
    if (page == "FM") return { P_ (oFm), P_ (oFmRatio), P_ (oFmFine), P_ (oFmShape), P_ (oFmFb) };
    if (page == "WS") return { P_ (oWs), P_ (oWsDrive), P_ (oWsBias), P_ (oWsSym), P_ (oWsMix) };
    if (page == "SAMPLE") return { P_ (oSStart), P_ (oSEnd), P_ (oRoot), P_ (oSGain), P_ (oSRev) };
    if (page == "PLAYBACK") return { P_ (oSMode), P_ (oSSpeed), P_ (oSDir), P_ (oSRand), P_ (oSRetrig), base + 4, base + 5 };
    if (page == "PITCH") return { base + 2, base + 3, P_ (oTrack), P_ (oPEnv), P_ (oStretch), P_ (oFormant) };
    if (page == "LOOP") return { P_ (oLoop), P_ (oLStart), P_ (oLEnd), P_ (oLXfade), P_ (oLMode) };
    if (page == "GRANULAR") return { P_ (oGran), P_ (oGSize), P_ (oGDens), P_ (oGPos), P_ (oGPosRand), P_ (oGPitch), P_ (oGOver), P_ (oGSpread) };
    return {};
}

juce::String SynthBlock::getDetail() const
{
    auto name = [this] (int o)
    {
        if (isSampleOsc (o)) { const auto* s = sampleFor (o); return s != nullptr ? s->name : juce::String ("no sample"); }
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

const SamplerBlock::Sample* SynthBlock::sampleFor (int o) const noexcept
{
    const int idx = getChoice (oscParam (o, oSrc));
    const auto& f = SamplerBlock::factory();
    if (idx < (int) f.size()) return f[(size_t) idx].get();
    return smpLoaded[(size_t) juce::jlimit (0, 1, o)].load();
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

static std::shared_ptr<const SamplerBlock::Sample> readSample (juce::AudioFormatReader& r, const juce::String& name, int maxLen)
{
    const int len = (int) juce::jmin ((juce::int64) maxLen, r.lengthInSamples);
    if (len < 32) return nullptr;
    auto s = std::make_shared<SamplerBlock::Sample>();
    s->name = name;
    s->rate = r.sampleRate > 0 ? r.sampleRate : 48000.0;
    s->data.setSize (2, len);
    r.read (&s->data, 0, len, 0, true, true);
    if (r.numChannels < 2) s->data.copyFrom (1, 0, s->data, 0, 0, len);
    return finishSample (s);
}

bool SynthBlock::loadSample (const juce::File& f, int o, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = "Couldn't read " + f.getFileName(); return false; }
    auto s = readSample (*r, f.getFileNameWithoutExtension(), (int) (r->sampleRate * 90.0));
    if (s == nullptr) { error = f.getFileName() + " is empty"; return false; }
    o = juce::jlimit (0, 1, o);
    keepSmp.push_back (s);
    smpLoaded[(size_t) o].store (s.get());
    smpRef[(size_t) o] = f.getFullPathName();
    set (oscParam (o, oSrc), (float) SamplerBlock::factory().size());
    set (oscParam (o, oType), 1.0f);
    return true;
}

bool SynthBlock::loadBankSample (const juce::String& ref, int o)
{
    const auto* e = keys::findBank (ref);
    if (e == nullptr || ! e->sample) return false;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (e->data, e->size, false), true));
    if (r == nullptr) return false;
    auto s = readSample (*r, e->name, (int) r->lengthInSamples);
    if (s == nullptr) return false;
    o = juce::jlimit (0, 1, o);
    keepSmp.push_back (s);
    smpLoaded[(size_t) o].store (s.get());
    smpRef[(size_t) o] = "bank:" + e->ref();
    set (oscParam (o, oSrc), (float) SamplerBlock::factory().size());
    set (oscParam (o, oRoot), (float) e->root);
    set (oscParam (o, oType), 1.0f);
    return true;
}

juce::ValueTree SynthBlock::toState() const
{
    auto v = Block::toState();
    if (userFile != juce::File()) v.setProperty ("wavetable", userFile.getFullPathName(), nullptr);
    if (oscRef[0].isNotEmpty()) v.setProperty ("tableA", oscRef[0], nullptr);
    if (oscRef[1].isNotEmpty()) v.setProperty ("tableB", oscRef[1], nullptr);
    if (smpRef[0].isNotEmpty()) v.setProperty ("sampleA", smpRef[0], nullptr);
    if (smpRef[1].isNotEmpty()) v.setProperty ("sampleB", smpRef[1], nullptr);
    return v;
}

void SynthBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    // loading a table or sound sets its knobs: keep the saved ones
    std::vector<float> saved;
    for (int i = 0; i < (int) getSpecs().size(); ++i) saved.push_back (getBase (i));
    auto restore = [&] { for (int i = 0; i < (int) saved.size(); ++i) set (i, saved[(size_t) i]); };
    if (const auto path = v.getProperty ("wavetable").toString(); path.isNotEmpty())
    {
        juce::String err;
        if (juce::File (path).existsAsFile()) loadWavetable (juce::File (path), 0, err);
    }
    for (int o = 0; o < 2; ++o)
    {
        juce::String err;
        if (const auto ref = v.getProperty (o == 0 ? "tableA" : "tableB").toString(); ref.isNotEmpty())
        {
            if (ref.startsWith ("bank:")) loadBankTable (ref.substring (5), o);
            else if (juce::File (ref).existsAsFile()) loadWavetable (juce::File (ref), o, err);
        }
        if (const auto ref = v.getProperty (o == 0 ? "sampleA" : "sampleB").toString(); ref.isNotEmpty())
        {
            if (ref.startsWith ("bank:")) loadBankSample (ref.substring (5), o);
            else if (juce::File (ref).existsAsFile()) loadSample (juce::File (ref), o, err);
        }
    }
    restore();
}

juce::String SynthBlock::formatValue (int i, float v) const
{
    if ((i == pTableA || i == pTableB) && juce::roundToInt (v) >= (int) Wavetable::factory().size())
    {
        if (auto* u = userOsc[i == pTableA ? 0u : 1u].load()) return u->name;
        return user.load() != nullptr ? user.load()->name : juce::String ("User (none)");
    }
    for (int o = 0; o < 2; ++o)
    {
        if (i == oscParam (o, oSrc) && juce::roundToInt (v) >= (int) SamplerBlock::factory().size())
            return smpLoaded[(size_t) o].load() != nullptr ? smpLoaded[(size_t) o].load()->name : juce::String ("(none)");
        if (i == oscParam (o, oRoot)) return keys::noteName (juce::roundToInt (v));
        if (i == oscParam (o, oStretch) && v < 1.0f) return "Off";
        if (i == oscParam (o, oFmRatio)) return juce::String (v, 2) + "x";
    }
    if (i == pUniA || i == pUniB) return juce::String (juce::roundToInt (v)) + (juce::roundToInt (v) == 1 ? " voice" : " voices");
    return Block::formatValue (i, v);
}

void SynthBlock::randomise (juce::Random& rng, float amount)
{
    // tuning, the sounds chosen and the oscillator types stay where they are
    std::vector<std::pair<int, float>> keepNow;
    for (int i = 0; i < (int) getSpecs().size(); ++i)
    {
        const auto& id = getSpecs()[(size_t) i].id;
        if (isTuning (id) || i == pTableA || i == pTableB || id.endsWith ("type") || id.endsWith ("src")) keepNow.push_back ({ i, getBase (i) });
    }
    Block::randomise (rng, amount);
    for (auto& [i, v] : keepNow) set (i, v);
    for (int o = 0; o < 2; ++o) set (oscParam (o, oFmRatio), std::round (get (oscParam (o, oFmRatio)) * 2.0f) * 0.5f);   // musical ratios
    if (amount >= 0.999f)
    {
        const int nf = (int) Wavetable::factory().size();
        if (! isSampleOsc (0)) set (pTableA, (float) rng.nextInt (nf));
        if (! isSampleOsc (1)) set (pTableB, (float) rng.nextInt (nf));
        set (pFType, rng.nextFloat() < 0.6f ? 1.0f : (float) rng.nextInt (keys::filterTypes().size()));
        if (getChoice (pFType) == 5) set (pFType, 0.0f);
    }
}

void SynthBlock::beginChunk() noexcept
{
    for (int o = 0; o < 2; ++o)
    {
        const int base = o == 0 ? pTableA : pTableB;
        auto P_ = [o] (int kk) { return oscParam (o, kk); };
        auto& K = k[(size_t) o];
        K.sample = getChoice (P_ (oType)) == 1;
        K.table = K.sample ? nullptr : tableFor (o);
        K.smp = K.sample ? sampleFor (o) : nullptr;
        K.pos = get (base + 1) * 0.01f;
        K.semi = get (base + 2);
        K.fine = get (base + 3);
        K.uni = juce::jlimit (1, maxUnison, juce::roundToInt (get (base + 4)));
        K.det = get (base + 5) * 0.01f;
        K.level = get (base + 6) * 0.01f;
        K.pan = get (base + 7) * 0.01f;
        K.phase = get (P_ (oPhase)) * 0.01f;
        K.phaseRand = get (P_ (oPhaseRand)) * 0.01f;
        K.sym = get (P_ (oSym)) * 0.01f;
        K.sync = get (P_ (oSync)) * 0.01f;
        K.uspread = get (P_ (oUSpread)) * 0.01f;
        K.width = get (P_ (oWidth)) * 0.01f;
        K.blend = get (P_ (oBlend)) * 0.01f;
        K.hAmt = get (P_ (oHAmt)) * 0.01f;
        K.hCount = juce::jlimit (1, 31, juce::roundToInt (get (P_ (oHCount))));
        K.hTilt = get (P_ (oHTilt)) * 0.01f;
        K.hOdd = get (P_ (oHOddEven)) * 0.01f;
        K.hDecay = get (P_ (oHDecay)) * 0.01f;
        if (K.hAmt > 0.0005f)
        {
            // the added harmonics (2nd to count + 1): falling away by decay, tilted, odd / even balanced
            float sum = 0.0f;
            K.hGain.fill (0.0f);
            for (int h = 2; h <= K.hCount + 1 && h <= 32; ++h)
            {
                float g = std::pow ((float) h, -(0.5f + K.hDecay * 2.0f)) * std::pow ((float) h, K.hTilt * 1.2f);
                g *= (h % 2 == 1) ? (1.0f - juce::jmax (0.0f, K.hOdd)) : (1.0f + juce::jmin (0.0f, K.hOdd));
                K.hGain[(size_t) h] = g;
                sum += g;
            }
            if (sum > 0.0f) for (auto& g : K.hGain) g /= sum;
        }
        K.fm = get (P_ (oFm)) * 0.01f;
        K.fmRatio = get (P_ (oFmRatio)) * std::exp2 (get (P_ (oFmFine)) / 1200.0f);
        K.fmShape = getChoice (P_ (oFmShape));
        K.fmFb = get (P_ (oFmFb)) * 0.01f;
        K.ws = getChoice (P_ (oWs));
        K.wsDrive = get (P_ (oWsDrive)) * 0.01f;
        K.wsBias = get (P_ (oWsBias)) * 0.01f;
        K.wsSym = get (P_ (oWsSym)) * 0.01f;
        K.wsMix = get (P_ (oWsMix)) * 0.01f;
        K.sStart = get (P_ (oSStart)) * 0.01f;
        K.sEnd = get (P_ (oSEnd)) * 0.01f;
        K.root = get (P_ (oRoot));
        K.gain = dbToGain (get (P_ (oSGain)));
        K.rev = get (P_ (oSRev)) > 0.5f;
        K.mode = getChoice (P_ (oSMode));
        K.speed = get (P_ (oSSpeed)) * 0.01f;
        K.dir = getChoice (P_ (oSDir));
        K.sRand = get (P_ (oSRand)) * 0.01f;
        K.retrig = getChoice (P_ (oSRetrig));
        K.track = get (P_ (oTrack)) * 0.01f;
        K.pEnv = get (P_ (oPEnv));
        K.stretch = get (P_ (oStretch)) * 0.01f;
        K.formant = get (P_ (oFormant));
        K.loop = get (P_ (oLoop)) > 0.5f;
        K.lStart = get (P_ (oLStart)) * 0.01f;
        K.lEnd = get (P_ (oLEnd)) * 0.01f;
        K.lXfade = get (P_ (oLXfade)) * 0.01f;
        K.lMode = getChoice (P_ (oLMode));
        K.gran = get (P_ (oGran)) > 0.5f;
        K.gSize = get (P_ (oGSize)) * 0.001f;
        K.gDens = get (P_ (oGDens));
        K.gPos = get (P_ (oGPos)) * 0.01f;
        K.gPosRand = get (P_ (oGPosRand)) * 0.01f;
        K.gPitch = get (P_ (oGPitch));
        K.gOver = get (P_ (oGOver)) * 0.01f;
        K.gSpread = get (P_ (oGSpread)) * 0.01f;
        if (! K.sample) livePos[(size_t) o].store (K.pos, std::memory_order_relaxed);
    }
    subLevel = get (pSub) * 0.01f;
    subOct = getChoice (pSubOct) + 1;
    noiseLevel = get (pNoise) * 0.01f;
    posEnv = get (pEPos) * 0.01f;
    mixAB = get (pMix) * 0.01f;
    mPan = get (pMPan) * 0.01f;
    transpose = get (pTranspose);
    bool anyOn = false;
    for (int o = 0; o < 2; ++o) if (k[(size_t) o].level > 0.0005f) anyOn = true;
    if (! anyOn) for (auto& h : liveHead) h.store (-1.0f, std::memory_order_relaxed);
}

bool SynthBlock::holdsThroughNoteOff() const noexcept
{
    // one-shot: only when everything sounding is a one-shot sample
    bool oneShot = false;
    for (int o = 0; o < 2; ++o)
    {
        const auto& K = k[(size_t) o];
        if (K.level <= 0.0005f) continue;
        if (! K.sample || K.mode != 0) return false;
        oneShot = true;
    }
    return oneShot && subLevel <= 0.0005f && noiseLevel <= 0.0005f;
}

void SynthBlock::startVoice (Voice& v, bool fresh) noexcept
{
    const int vi = (int) (&v - voices.data());
    for (int o = 0; o < 2; ++o)
    {
        const auto& K = k[(size_t) o];
        auto& O = osc[(size_t) vi][(size_t) o];
        if (fresh)
            for (int u = 0; u < maxUnison; ++u)
            {
                const double ph = K.phase + (double) v.rng.nextFloat() * K.phaseRand;
                O.phase[(size_t) u] = ph - std::floor (ph);
                O.slave[(size_t) u] = 0.0;
                O.fmPhase[(size_t) u] = 0.0;
                O.fmLast[(size_t) u] = 0.0f;
            }
        // the sample: back to its start (a fresh note, Restart, or Trigger on every note)
        if (K.sample && (fresh || K.mode == 3))
        {
            const auto* S = K.smp;
            const int n = S != nullptr ? S->data.getNumSamples() : 1;
            const double a = juce::jlimit (0.0, (double) n - 64.0, (double) K.sStart * n);
            const double b = juce::jlimit (a + 64.0, (double) n, (double) K.sEnd * n);
            const bool backwards = (K.dir == 1) != K.rev;
            double at = backwards ? b - 1.0 - (double) v.rng.nextFloat() * K.sRand * (b - a) : a + (double) v.rng.nextFloat() * K.sRand * (b - a);
            if (K.retrig == 1 && lastHead[(size_t) o] > a && lastHead[(size_t) o] < b) at = lastHead[(size_t) o];   // Continue
            O.head.fill (at);
            O.dir.fill (backwards ? -1 : 1);
            O.gHead = at;
            O.untilGrain = 0.0;
            O.done = false;
            for (auto& g : O.grains) g.on = false;
            O.fz1[0] = O.fz1[1] = O.fz2[0] = O.fz2[1] = 0.0f;
        }
    }
    if (fresh) subPhase[(size_t) vi] = 0.0;
    if (fresh) { for (auto& f : v.fl) f.reset(); for (auto& f : v.fl2) f.reset(); }
}

static inline float fmWave (int shape, double p) noexcept
{
    const float x = (float) (p - std::floor (p));
    switch (shape)
    {
        case 1: return x < 0.5f ? x * 4.0f - 1.0f : 3.0f - x * 4.0f;
        case 2: return x * 2.0f - 1.0f;
        case 3: return x < 0.5f ? 1.0f : -1.0f;
        default: return (float) std::sin (juce::MathConstants<double>::twoPi * p);
    }
}

static inline float waveshape (int type, float x, float drive, float bias, float sym) noexcept
{
    const float g = 1.0f + drive * 7.0f;
    float y = (x + bias) * g;
    switch (type)
    {
        case 1: { y = y * 0.25f + 0.25f; y = 4.0f * std::abs (y - std::floor (y + 0.5f)) - 1.0f; y = -y; break; }   // fold
        case 2: y = juce::jlimit (-1.0f, 1.0f, y); break;                                                           // clip
        case 3: y = juce::jlimit (-1.0f, 1.0f, y + 0.6f * drive * y * y * y) ; y = juce::jlimit (-1.0f, 1.0f, y); break;   // bend
        case 4: y = (float) std::sin (juce::MathConstants<float>::halfPi * y); break;                               // sine
        case 5: y = dsp::fastTanh (y); break;                                                                       // saturate
        default: return x;
    }
    y *= y >= 0.0f ? (1.0f + sym) : (1.0f - sym);
    return y / juce::jmax (1.0f, 0.5f + 0.5f * g * 0.4f);
}

void SynthBlock::renderWave (Voice& v, int vi, int o, float* l, float* r, int m) noexcept
{
    const auto& K = k[(size_t) o];
    if (K.table == nullptr) return;
    const double sr = sampleRate;
    const float pos = juce::jlimit (0.0f, 1.0f, K.pos + posEnv * v.mod.level);
    if (vi == 0 || v.held) livePos[(size_t) o].store (pos, std::memory_order_relaxed);
    const double base = v.pitch + K.semi + K.fine * 0.01 + bend + transpose;
    auto& O = osc[(size_t) vi][(size_t) o];
    const double syncRatio = std::exp2 ((double) K.sync * 3.0);
    const bool syncing = K.sync > 0.001f;
    const float w = 0.5f + K.sym * 0.45f;   // symmetry: where the middle of the cycle falls
    const bool warp = std::abs (K.sym) > 0.001f;
    const int uni = K.uni;
    const float norm = 1.0f / std::sqrt ((float) uni);
    for (int u = 0; u < uni; ++u)
    {
        const float spread = uni > 1 ? (float) u / (float) (uni - 1) * 2.0f - 1.0f : 0.0f;
        const double hz = 440.0 * std::exp2 ((base + spread * K.det * 0.6 - 69.0) / 12.0);
        const double inc = hz / sr;
        const int mip = Wavetable::mipFor (hz * (syncing ? syncRatio : 1.0), sr);
        // blend: the middle voice (or two) against the rest
        const bool centre = uni == 1 || std::abs (spread) <= 1.0f / (float) (uni - 1) + 1.0e-4f;
        const float bw = uni == 1 ? 1.0f : (centre ? (1.0f - K.blend) * 2.0f : K.blend * 2.0f);
        const float p = juce::jlimit (-1.0f, 1.0f, spread * K.uspread);
        const float gl = norm * bw * std::cos ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
        const float gr = norm * bw * std::sin ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
        const int hMax = juce::jmin (K.hCount + 1, 32, (int) (0.45 * sr / juce::jmax (1.0, hz)));
        const double fmInc = inc * K.fmRatio;
        double ph = O.phase[(size_t) u], sl = O.slave[(size_t) u], fp = O.fmPhase[(size_t) u];
        float fl = O.fmLast[(size_t) u];
        for (int i = 0; i < m; ++i)
        {
            double rp = syncing ? sl : ph;
            if (warp)
            {
                const float x = (float) (rp - std::floor (rp));
                rp = x < w ? x * 0.5f / w : 0.5f + (x - w) * 0.5f / (1.0f - w);
            }
            if (K.fm > 0.0005f)
            {
                const float mo = fmWave (K.fmShape, fp + K.fmFb * fl * 0.3);
                fl = mo;
                rp += K.fm * 1.2 * mo;
                fp += fmInc;
                if (fp >= 1.0) fp -= 1.0;
            }
            rp -= std::floor (rp);
            float x = K.table->read (rp, pos, mip);
            if (K.hAmt > 0.0005f && hMax >= 2)
            {
                // the added harmonics, by the sine recurrence
                const double th = juce::MathConstants<double>::twoPi * rp;
                const float s1 = (float) std::sin (th), c2 = 2.0f * (float) std::cos (th);
                float sPrev = 0.0f, sCur = s1, h = 0.0f;
                for (int hh = 2; hh <= hMax; ++hh)
                {
                    const float sNext = c2 * sCur - sPrev;
                    sPrev = sCur; sCur = sNext;
                    h += K.hGain[(size_t) hh] * sCur;
                }
                x += K.hAmt * h * 1.5f;
            }
            l[i] += x * gl;
            r[i] += x * gr;
            ph += inc;
            if (ph >= 1.0) { ph -= 1.0; if (syncing) sl = ph * syncRatio; }
            else if (syncing) { sl += inc * syncRatio; if (sl >= 1.0) sl -= std::floor (sl); }
        }
        O.phase[(size_t) u] = ph;
        O.slave[(size_t) u] = sl;
        O.fmPhase[(size_t) u] = fp;
        O.fmLast[(size_t) u] = fl;
    }
    // the shaper
    if (K.ws != 0)
        for (int i = 0; i < m; ++i)
        {
            l[i] = l[i] + (waveshape (K.ws, l[i], K.wsDrive, K.wsBias, K.wsSym) - l[i]) * K.wsMix;
            r[i] = r[i] + (waveshape (K.ws, r[i], K.wsDrive, K.wsBias, K.wsSym) - r[i]) * K.wsMix;
        }
}

void SynthBlock::renderSample (Voice& v, int vi, int o, float* l, float* r, int m) noexcept
{
    const auto& K = k[(size_t) o];
    const auto* S = K.smp;
    auto& O = osc[(size_t) vi][(size_t) o];
    if (S == nullptr || O.done) return;
    const double sr = sampleRate;
    const int n = S->data.getNumSamples();
    const float* L = S->data.getReadPointer (0);
    const float* R = S->data.getReadPointer (1);
    const double rs = S->rate / sr;
    const double a = juce::jlimit (0.0, (double) n - 64.0, (double) K.sStart * n);
    const double b = juce::jlimit (a + 64.0, (double) n, (double) K.sEnd * n);
    const double len = b - a;
    const double semis = (v.pitch + transpose + bend - K.root) * K.track + K.semi + K.fine * 0.01 + K.pEnv * v.mod.level;
    const double ratio = std::exp2 (semis / 12.0) * K.speed;
    const double la = a + K.lStart * len, lb = juce::jmax (la + 64.0, a + K.lEnd * len);
    const double llen = lb - la;
    const bool looping = K.mode == 2 && K.loop;
    auto readAt = [&] (double x, float& outL, float& outR)
    {
        x = juce::jlimit (0.0, (double) n - 1.001, x);
        const int i0 = (int) x, i1 = juce::jmin (n - 1, i0 + 1);
        const float fr = (float) (x - i0);
        outL = L[i0] + (L[i1] - L[i0]) * fr;
        outR = R[i0] + (R[i1] - R[i0]) * fr;
    };
    const int uni = K.uni;
    const float norm = K.gain / std::sqrt ((float) uni);
    auto finish = [&] { O.done = true; };

    if (K.gran || K.stretch >= 0.01f)
    {
        // grains: Granular takes them from a position; Stretch walks through the sound at its own speed
        const int grainLen = juce::jmax (64, (int) ((K.gran ? K.gSize : 0.08f) * sr));
        const double gap = (double) grainLen / juce::jmax (1.0f, K.gran ? K.gDens * (0.5f + K.gOver * 1.5f) : 3.0f);
        const double headStep = K.gran ? 0.0 : rs / juce::jmax (0.05, (double) K.stretch) * (O.dir[0] < 0 ? -1.0 : 1.0);
        const double gStep0 = ratio * rs * (K.gran ? std::exp2 (K.gPitch / 12.0) : 1.0) * (O.dir[0] < 0 ? -1.0 : 1.0);
        const float spreadAmt = K.gran ? K.gSpread : K.uspread * 0.5f;
        const float gnorm = 1.6f * norm / (float) juce::jmax (1.0, (double) grainLen / gap);
        const float taper = 0.5f + (1.0f - K.gOver) * 0.45f;   // overlap: soft, long fades (100%) to short ones (0%)
        for (int i = 0; i < m; ++i)
        {
            O.untilGrain -= 1.0;
            if (O.untilGrain <= 0.0)
            {
                for (auto& g : O.grains)
                    if (! g.on)
                    {
                        double at = K.gran ? a + K.gPos * len : O.gHead;
                        at += ((double) v.rng.nextFloat() - 0.5) * (K.gran ? K.gPosRand : 0.02f) * len;
                        g.pos = a + std::fmod (std::fmod (at - a, len) + len, len);
                        const int uu = uni > 1 ? v.rng.nextInt (uni) : 0;
                        const float spread = uni > 1 ? (float) uu / (float) (uni - 1) * 2.0f - 1.0f : 0.0f;
                        g.step = gStep0 * std::exp2 ((double) spread * K.det * 0.6 / 12.0);
                        g.length = grainLen;
                        g.age = 0;
                        const float pan = juce::jlimit (-1.0f, 1.0f, (v.rng.nextFloat() * 2.0f - 1.0f) * spreadAmt);
                        g.panL = std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
                        g.panR = std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
                        g.on = true;
                        break;
                    }
                O.untilGrain += gap;
            }
            float sL = 0.0f, sR = 0.0f;
            for (auto& g : O.grains)
            {
                if (! g.on) continue;
                const float t = (float) g.age / (float) g.length;
                const float wgt = t < taper ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t / taper)
                                : (t > 1.0f - taper ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - t) / taper) : 1.0f);
                float x, y;
                readAt (a + std::fmod (std::fmod (g.pos - a, len) + len, len), x, y);
                sL += x * wgt * g.panL;
                sR += y * wgt * g.panR;
                g.pos += g.step;
                if (++g.age >= g.length) g.on = false;
            }
            l[i] += sL * gnorm;
            r[i] += sR * gnorm;
            if (! K.gran)
            {
                O.gHead += headStep;
                if (O.gHead >= b || O.gHead < a)
                {
                    if (looping) O.gHead = headStep > 0 ? la + std::fmod (O.gHead - la, llen) : lb - std::fmod (lb - O.gHead, llen);
                    else { finish(); break; }
                }
            }
        }
        lastHead[(size_t) o] = O.gHead;
        if (vi == 0 || v.held) liveHead[(size_t) o].store ((float) ((K.gran ? a + K.gPos * len : O.gHead) / n), std::memory_order_relaxed);
    }
    else
    {
        // pitched playback: a head per unison voice, detuned and spread
        const double xf = looping && K.lMode == 0 ? juce::jlimit (0.0, llen * 0.5, (double) K.lXfade * llen * 0.5) : 0.0;
        for (int u = 0; u < uni && ! O.done; ++u)
        {
            const float spread = uni > 1 ? (float) u / (float) (uni - 1) * 2.0f - 1.0f : 0.0f;
            const double step = ratio * rs * std::exp2 ((double) spread * K.det * 0.6 / 12.0);
            const float p = juce::jlimit (-1.0f, 1.0f, spread * K.uspread);
            const float gl = norm * std::cos ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
            const float gr = norm * std::sin ((p + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41f;
            double h = O.head[(size_t) u];
            int d = O.dir[(size_t) u] == 0 ? 1 : O.dir[(size_t) u];
            bool ended = false;
            for (int i = 0; i < m; ++i)
            {
                if (looping)
                {
                    // into the loop, then round it: forward, backward or back and forth
                    if (K.lMode == 2) { if (h >= lb) { h = lb - (h - lb); d = -1; } else if (h < la && d < 0) { h = la + (la - h); d = 1; } }
                    else if (K.lMode == 1) { if (h >= lb) h = lb - 1.0; if (h < la) h += llen; d = -1; }
                    else { if (h >= lb) h -= llen; }
                }
                else if (h >= b || h < a)
                {
                    if (K.dir == 2 && d > 0 && h >= b) { h = b - (h - b) - 1.0; d = -1; }   // ping-pong: once back
                    else { ended = true; break; }
                }
                float x, y;
                readAt (h, x, y);
                if (xf > 1.0 && d > 0 && h > lb - xf)
                {
                    // the loop's crossfade: the start of the loop fades in as its end fades out
                    float x2, y2;
                    readAt (h - llen, x2, y2);
                    const float t = (float) ((h - (lb - xf)) / xf);
                    x = x * (1.0f - t) + x2 * t;
                    y = y * (1.0f - t) + y2 * t;
                }
                l[i] += x * gl;
                r[i] += y * gr;
                h += step * d;
            }
            O.head[(size_t) u] = h;
            O.dir[(size_t) u] = d;
            if (ended && u == 0) finish();
        }
        lastHead[(size_t) o] = O.head[0];
        if (vi == 0 || v.held) liveHead[(size_t) o].store ((float) (O.head[0] / n), std::memory_order_relaxed);
    }
    // the formant: a resonance that moves with the knob (a vocal-ish colour), not the pitch
    if (std::abs (K.formant) > 0.05f)
    {
        const float fc = juce::jlimit (80.0f, (float) (sr * 0.4), 900.0f * std::exp2 (K.formant / 12.0f));
        const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr), kq = 0.7f;
        const float a1 = 1.0f / (1.0f + g * (g + kq));
        float* ch[2] { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < m; ++i)
            {
                const float x = ch[c][i];
                const float v3 = x - O.fz2[c];
                const float v1 = a1 * O.fz1[c] + g * a1 * v3;
                const float v2 = O.fz2[c] + g * v1;
                O.fz1[c] = 2.0f * v1 - O.fz1[c];
                O.fz2[c] = 2.0f * v2 - O.fz2[c];
                ch[c][i] = x * 0.7f + v1 * kq * 1.6f;
            }
    }
}

void SynthBlock::renderVoice (Voice& v, int vi, float* l, float* r, int m) noexcept
{
    float tl[64], tr[64];
    bool sounding = false, samplesLeft = false, anySample = false;
    for (int o = 0; o < 2; ++o)
    {
        const auto& K = k[(size_t) o];
        if (K.level <= 0.0005f) continue;
        const float g = K.level * (o == 0 ? (mixAB > 0.0f ? 1.0f - mixAB : 1.0f) : (mixAB < 0.0f ? 1.0f + mixAB : 1.0f));
        if (g <= 0.0005f) continue;
        std::fill (tl, tl + m, 0.0f);
        std::fill (tr, tr + m, 0.0f);
        if (K.sample)
        {
            anySample = true;
            renderSample (v, vi, o, tl, tr, m);
            if (! osc[(size_t) vi][(size_t) o].done) samplesLeft = true;
        }
        else { renderWave (v, vi, o, tl, tr, m); sounding = true; }
        // width (mid / side) and the oscillator's pan
        const float pl = K.pan <= 0.0f ? 1.0f : 1.0f - K.pan, pr = K.pan >= 0.0f ? 1.0f : 1.0f + K.pan;
        for (int i = 0; i < m; ++i)
        {
            const float mid = (tl[i] + tr[i]) * 0.5f, side = (tl[i] - tr[i]) * 0.5f * K.width;
            l[i] += (mid + side) * g * pl;
            r[i] += (mid - side) * g * pr;
        }
    }
    const double base = v.pitch + bend + transpose;
    if (subLevel > 0.0005f)
    {
        sounding = true;
        const double inc = 440.0 * std::exp2 ((base - 12.0 * subOct - 69.0) / 12.0) / sampleRate;
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
    {
        sounding = true;
        for (int i = 0; i < m; ++i)
        {
            l[i] += (v.rng.nextFloat() * 2.0f - 1.0f) * noiseLevel * 0.5f;
            r[i] += (v.rng.nextFloat() * 2.0f - 1.0f) * noiseLevel * 0.5f;
        }
    }
    // the master pan
    if (std::abs (mPan) > 0.001f)
    {
        const float pl = mPan <= 0.0f ? 1.0f : 1.0f - mPan, pr = mPan >= 0.0f ? 1.0f : 1.0f + mPan;
        for (int i = 0; i < m; ++i) { l[i] *= pl; r[i] *= pr; }
    }
    // only samples, and they've all ended: the voice is done
    if (anySample && ! samplesLeft && ! sounding) v.amp.kill();
}

} // namespace rig
