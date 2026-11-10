// Measurements for reviewing the engine:  RigsmithTests <models> --bench   (CPU per block)
//                                         RigsmithTests <models> --audit   (levels of every block, model and preset)
#include "Engine/Engine.h"
#include "Engine/FactoryPresets.h"
#include <chrono>
#include <cstdio>

using namespace rig;

std::vector<float> makeStrum (double sr, double seconds, bool bass = false);
int checkLevels();

/** A guitar-ish (or bass-ish) test signal: plucked strings (Karplus-Strong), strummed once a second,
    alternately soft and hard. Peaks around -12 dBFS. */
std::vector<float> makeStrum (double sr, double seconds, bool bass)
{
    const int total = (int) (sr * seconds);
    std::vector<float> out ((size_t) total, 0.0f);
    const int guitar[] { 40, 45, 50, 55, 59, 64 };
    const int bassNotes[] { 28, 33, 38, 43 };
    juce::Random rnd (7);
    const int numStrings = bass ? 4 : 6;
    for (int s = 0; s < numStrings; ++s)
    {
        const double hz = 440.0 * std::pow (2.0, ((bass ? bassNotes[s] : guitar[s]) - 69) / 12.0);
        const int period = juce::jmax (2, (int) std::round (sr / hz));
        std::vector<float> line ((size_t) period, 0.0f);
        int pos = 0;
        float lp = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            const int strum = (int) (i / sr);
            const int start = (int) (strum * sr) + (int) (s * 0.012 * sr);
            if (i == start)
            {
                const float level = (strum % 2 == 0 ? 0.35f : 1.0f);
                float prev = 0.0f;
                float mean = 0.0f;
                for (auto& v : line) { const float w = rnd.nextFloat() * 2.0f - 1.0f; prev = 0.5f * (w + prev); v = prev * level; mean += v; }
                mean /= (float) period;
                for (auto& v : line) v -= mean;
            }
            const float y = line[(size_t) pos];
            const float next = line[(size_t) ((pos + 1) % period)];
            lp = 0.996f * (0.5f * (y + next));
            line[(size_t) pos] = lp;
            pos = (pos + 1) % period;
            out[(size_t) i] += y;
        }
    }
    float peak = 0.0f;
    for (auto v : out) peak = juce::jmax (peak, std::abs (v));
    const float g = 0.25f / juce::jmax (1.0e-6f, peak);
    for (auto& v : out) v *= g;
    return out;
}

namespace
{
struct Level { double rmsDb = -200, peakDb = -200, dc = 0; bool finite = true; };

double toDb (double lin) { return 20.0 * std::log10 (juce::jmax (1.0e-10, lin)); }

/** Runs the signal through one node and measures what comes out (after a short settle). */
Level through (Node& n, const std::vector<float>& sig, double sr, int block = 128)
{
    n.prepare (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    Level lv;
    double sum = 0, dc = 0; float peak = 0; long count = 0;
    const size_t skip = (size_t) (sr * 0.5);
    for (size_t off = 0; off + (size_t) block <= sig.size(); off += (size_t) block)
    {
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, sig.data() + off, block);
        n.process (buf, block);
        if (off < skip) continue;
        for (int i = 0; i < block; ++i)
        {
            const float y = buf.getSample (0, i), y2 = buf.getSample (1, i);
            if (! std::isfinite (y) || ! std::isfinite (y2)) lv.finite = false;
            sum += 0.5 * ((double) y * y + (double) y2 * y2); dc += y; peak = juce::jmax (peak, std::abs (y), std::abs (y2)); ++count;
        }
    }
    lv.rmsDb = toDb (std::sqrt (sum / juce::jmax (1L, count)));
    lv.peakDb = toDb (peak);
    lv.dc = dc / juce::jmax (1L, count);
    return lv;
}

Level levelOf (const std::vector<float>& sig, double sr)
{
    double sum = 0; float peak = 0; long count = 0;
    for (size_t i = (size_t) (sr * 0.5); i < sig.size(); ++i) { sum += (double) sig[i] * sig[i]; peak = juce::jmax (peak, std::abs (sig[i])); ++count; }
    Level lv; lv.rmsDb = toDb (std::sqrt (sum / juce::jmax (1L, count))); lv.peakDb = toDb (peak);
    return lv;
}

/** Realtime cost of a node: percent of one core at 48 kHz, 128-sample buffers. */
double costOf (Node& n, const std::vector<float>& sig, double sr, int block = 128)
{
    n.prepare (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    juce::ScopedNoDenormals nd;
    double best = 1.0e9;
    for (int pass = 0; pass < 3; ++pass)
    {
        const auto t0 = std::chrono::steady_clock::now();
        size_t done = 0;
        for (size_t off = 0; off + (size_t) block <= sig.size(); off += (size_t) block)
        {
            for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, sig.data() + off, block);
            n.process (buf, block);
            done += (size_t) block;
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        best = juce::jmin (best, 100.0 * secs / ((double) done / sr));
    }
    return best;
}

std::vector<NodeType> allBuiltIns()
{
    std::vector<NodeType> v;
    for (auto& g : builtInGroups()) for (auto t : g.second) v.push_back (t);
    return v;
}
} // namespace

int runBench (const juce::File& models)
{
    const double sr = 48000.0;
    const auto sig = makeStrum (sr, 6.0);
    Engine e;
    e.prepare (sr, 128);
    std::printf ("%-28s %8s\n", "block", "% core");
    double fxTotal = 0;
    for (auto t : allBuiltIns())
    {
        auto n = e.createNode (t);
        if (n == nullptr) continue;
        const double c = costOf (*n, sig, sr);
        fxTotal += c;
        std::printf ("%-28s %8.2f\n", builtInShortName (t).toRawUTF8(), c);
        if (auto* b = dynamic_cast<Block*> (n.get()); b != nullptr && b->hasModels())
        {
            // the dearest model
            double worst = 0; int worstI = 0;
            const int count = (int) fxModelsFor (t).size();
            for (int m = 1; m < count; ++m)
            {
                b->set (b->getModelParam(), (float) m); b->applyModelStock();
                const double cm = costOf (*n, sig, sr);
                if (cm > worst) { worst = cm; worstI = m; }
            }
            if (count > 1) std::printf ("   dearest model: %-22s %6.2f\n", fxModelsFor (t)[(size_t) worstI].name.toRawUTF8(), worst);
        }
    }
    std::printf ("%-28s %8.2f\n", "all built-ins in a row", fxTotal);
    for (auto name : { "wavenet_a1_standard.nam", "lstm.nam", "A2.nam", "wavenet.nam" })
    {
        NamBlock nb; juce::String err;
        if (! nb.loadModel (models.getChildFile (name), err)) { std::printf ("%-28s (not loaded: %s)\n", name, err.toRawUTF8()); continue; }
        std::printf ("NAM %-24s %8.2f\n", name, costOf (nb, sig, sr));
        std::printf ("NAM %-24s %8.2f  (44.1 kHz, resampled)\n", name, costOf (nb, makeStrum (44100.0, 6.0), 44100.0));
    }
    {
        // the whole engine with the default rig, with and without modulation
        juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer midi;
        auto run = [&] { const auto t0 = std::chrono::steady_clock::now();
            for (size_t off = 0; off + 128 <= sig.size(); off += 128) { for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, sig.data() + off, 128); e.process (buf, midi); }
            return 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 6.0; };
        juce::ScopedNoDenormals nd;
        e.loadFactory (0);
        std::printf ("%-28s %8.2f\n", "engine: Init (no amp)", run());
        e.loadFactory (1);
        std::printf ("%-28s %8.2f\n", "engine: Classic Rig (no amp)", run());
        for (auto& n : e.getRoot().nodes)
            std::printf ("   %-25s %8.2f %s\n", n->getName().toRawUTF8(), costOf (*n, sig, sr), n->bypassed.load() ? "(bypassed)" : "");
        std::printf ("%-28s %8.2f\n", "engine: same, tuner open", (e.tunerActive = true, run())); e.tunerActive = false;
    }
    return 0;
}

int runAudit (const juce::File&)
{
    const double sr = 48000.0;
    const auto sig = makeStrum (sr, 8.0);
    const auto in = levelOf (sig, sr);
    const auto bassSig = makeStrum (sr, 8.0, true);
    const auto bassIn = levelOf (bassSig, sr);
    std::printf ("test strum: rms %.1f dB, peak %.1f dB   (bass: rms %.1f dB)\n\n", in.rmsDb, in.peakDb, bassIn.rmsDb);
    std::printf ("%-12s %-34s %7s %7s  %s\n", "block", "setting", "gain", "peak", "notes");
    Engine e;
    e.prepare (sr, 128);
    int flagged = 0;
    auto report = [&] (NodeType t, const juce::String& what, Node& n, double tolerance)
    {
        const bool bass = t == NodeType::BassDrive || (dynamic_cast<CabBlock*> (&n) != nullptr && dynamic_cast<CabBlock&> (n).cab().bass);
        const auto lv = through (n, bass ? bassSig : sig, sr);
        const double gain = lv.rmsDb - (bass ? bassIn : in).rmsDb;
        juce::String notes;
        if (! lv.finite) notes << " NOT-FINITE";
        if (std::abs (gain) > tolerance) notes << (gain > 0 ? " LOUD" : " QUIET");
        if (lv.peakDb > 0.0) notes << " CLIPS";
        if (std::abs (lv.dc) > 0.002) notes << " DC";
        if (notes.isNotEmpty()) ++flagged;
        std::printf ("%-12s %-34s %+7.1f %+7.1f %s\n", builtInShortName (t).toRawUTF8(), what.substring (0, 34).toRawUTF8(), gain, lv.peakDb, notes.toRawUTF8());
    };
    for (auto t : allBuiltIns())
    {
        if (t == NodeType::Tuner) continue;
        const bool drive = categoryForBuiltIn (t) == Category::Drive;
        const bool dyn = t == NodeType::Swell || t == NodeType::Gate || t == NodeType::Expander;
        const double tol = drive ? 6.0 : (dyn ? 8.0 : 3.5);
        { auto n = e.createNode (t); if (n == nullptr) continue; report (t, "default", *n, tol); }
        if (auto* pb = dynamic_cast<PedalBlock*> (e.createNode (t).release()))
        {
            std::unique_ptr<PedalBlock> own (pb);
            for (int m = 0; m < (int) pb->models().size(); ++m)
            {
                auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
                b->set (PedalBlock::pModel, (float) m); b->onChoiceChanged (PedalBlock::pModel);
                report (t, "model: " + pb->models()[(size_t) m].name, *n, tol);
            }
        }
        if (t == NodeType::Cab)
            for (int cab = 0; cab < (int) CabBlock::cabs().size(); ++cab)
                for (int mic = 0; mic < (int) CabBlock::mics().size(); ++mic)
                {
                    auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
                    b->set (CabBlock::pCab, (float) cab); b->set (CabBlock::pMic, (float) mic);
                    report (t, "cab: " + CabBlock::cabs()[(size_t) cab].name + " / " + CabBlock::mics()[(size_t) mic].name, *n, 5.0);
                }
        const auto& fm = fxModelsFor (t);
        for (int m = 1; m < (int) fm.size(); ++m)
        {
            auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
            b->set (b->getModelParam(), (float) m); b->applyModelStock();
            report (t, "model: " + fm[(size_t) m].name, *n, tol);
        }
        const auto& fp = factoryPresets (t);
        for (auto& p : fp)
        {
            auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
            if (b == nullptr) continue;
            for (auto& [id, v] : p.values) { const int i = b->indexOf (id); if (i >= 0) b->set (i, v); }
            report (t, "preset: " + p.name, *n, tol);
        }
    }
    std::printf ("\n%d flagged\n", flagged);
    return 0;
}

/** Measures every drive, dynamics and modulation model at its stock settings and prints
    Source/Engine/Calibration.cpp: the trims that bring each to its target level. */
int runCalibrate (const juce::File&)
{
    const double sr = 48000.0;
    const auto sig = makeStrum (sr, 8.0);
    const auto in = levelOf (sig, sr);
    Engine e;
    e.prepare (sr, 128);
    auto target = [] (NodeType t) -> float
    {
        switch (t)
        {
            // only what's all wet: a trim on a mixed effect would be wrong at every other mix setting
            // (the compressor's auto make-up and the chorus / flanger mix are fixed in their own code)
            case NodeType::Overdrive: case NodeType::Distortion: case NodeType::Fuzz: return 4.0f;
            case NodeType::Rotary: case NodeType::Phaser: case NodeType::Vibe: return 0.0f;
            default: return 1000.0f;   // not calibrated
        }
    };
    auto trimFor = [&] (NodeType t, Node& n)
    {
        dynamic_cast<Block&> (n).calibrated = false;
        const float err = target (t) - (float) (through (n, sig, sr).rmsDb - in.rmsDb);
        return std::abs (err) < 0.75f ? 0.0f : std::round (err * 10.0f) / 10.0f;
    };
    // bass drives against a bass: a little above level (+2 dB), where a bassist leaves an always-on preamp
    const auto bassSig = makeStrum (sr, 8.0, true);
    const auto bassIn = levelOf (bassSig, sr);
    auto trimForBass = [&] (Node& n)
    {
        dynamic_cast<Block&> (n).calibrated = false;
        const float err = 2.0f - (float) (through (n, bassSig, sr).rmsDb - bassIn.rmsDb);
        return std::abs (err) < 0.75f ? 0.0f : std::round (err * 10.0f) / 10.0f;
    };
    std::printf ("// Level calibration tables. Regenerate with:  RigsmithTests <models> --calibrate\n"
                 "// (a strummed guitar at -12 dBFS peak through every model at its stock settings: drives and fuzzes\n"
                 "// are trimmed to +4 dB RMS against bypass, the rotary speaker to level)\n"
                 "#include \"Nodes.h\"\n#include <map>\n\nnamespace rig\n{\n");
    for (auto t : { NodeType::Overdrive, NodeType::Distortion, NodeType::BassDrive })
    {
        std::unique_ptr<PedalBlock> probe (dynamic_cast<PedalBlock*> (e.createNode (t).release()));
        std::printf ("static const float %sTrims[][3] {\n", t == NodeType::Overdrive ? "overdrive" : t == NodeType::BassDrive ? "bass" : "distortion");
        for (int m = 0; m < (int) probe->models().size(); ++m)
        {
            std::printf ("    {");
            for (int v = 0; v < 3; ++v)
            {
                float trim = 0.0f;
                if (v < juce::jmax (1, probe->models()[(size_t) m].voices.size()))
                {
                    auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
                    b->set (PedalBlock::pModel, (float) m); b->onChoiceChanged (PedalBlock::pModel);
                    b->set (PedalBlock::pVoice, (float) v);
                    trim = t == NodeType::BassDrive ? trimForBass (*n) : trimFor (t, *n);
                }
                std::printf (" %.1ff,", trim);
            }
            std::printf (" },   // %s\n", probe->models()[(size_t) m].name.toRawUTF8());
        }
        std::printf ("};\n\n");
    }
    std::printf ("float pedalTrimDb (NodeType t, int model, int voice)\n{\n"
                 "    const auto* table = t == NodeType::Overdrive ? overdriveTrims : t == NodeType::BassDrive ? bassTrims : distortionTrims;\n"
                 "    const int count = (int) (t == NodeType::Overdrive ? std::size (overdriveTrims) : t == NodeType::BassDrive ? std::size (bassTrims) : std::size (distortionTrims));\n"
                 "    if (model < 0 || model >= count || voice < 0 || voice > 2) return 0.0f;\n"
                 "    return table[model][voice];\n}\n\n"
                 "float fxTrimDb (NodeType t, int model)\n{\n"
                 "    static const std::map<NodeType, std::vector<float>> table {\n");
    for (auto t : allBuiltIns())
    {
        if (target (t) > 100.0f || fxModelsFor (t).empty()) continue;
        std::printf ("        { nodeTypeFromString (\"%s\"), {", nodeTypeToString (t).toRawUTF8());
        for (int m = 0; m < (int) fxModelsFor (t).size(); ++m)
        {
            auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
            b->set (b->getModelParam(), (float) m); b->applyModelStock();
            std::printf (" %.1ff,", trimFor (t, *n));
        }
        std::printf (" } },\n");
    }
    std::printf ("    };\n    if (const auto it = table.find (t); it != table.end() && model >= 0 && model < (int) it->second.size())\n"
                 "        return it->second[(size_t) model];\n    return 0.0f;\n}\n\n");
    std::printf ("float ampTrimDb (int model, int voice)\n{\n    static const float t[][5] {\n#include \"AmpTrims.inc\"\n    };\n"
                 "    if (model < 0 || model >= (int) std::size (t) || voice < 0 || voice > 4) return 0.0f;\n    return t[model][voice];\n}\n"
                 "} // namespace rig\n");
    return 0;
}

/** Prints Source/Engine/AmpTrims.inc: each amp and switch position at its stock settings, trimmed to
    unity against a played guitar (bass amps against a bass). */
int runCalibrateAmps()
{
    const double sr = 48000.0;
    const auto sig = makeStrum (sr, 8.0);
    const auto in = levelOf (sig, sr);
    const auto bassSig = makeStrum (sr, 8.0, true);
    const auto bassIn = levelOf (bassSig, sr);
    std::printf ("    // RigsmithTests <models> --calibrate-amps\n");
    const auto& ms = AmpBlock::models();
    for (int m = 0; m < (int) ms.size(); ++m)
    {
        std::printf ("    {");
        for (int v = 0; v < 5; ++v)
        {
            float trim = 0.0f;
            if (v < juce::jmax (1, (int) ms[(size_t) m].voices.size()))
            {
                AmpBlock a;
                a.set (AmpBlock::pModel, (float) m); a.onChoiceChanged (AmpBlock::pModel);
                a.set (AmpBlock::pVoice, (float) v);
                const bool bass = ms[(size_t) m].bass;
                // measured without the current table's trim
                const float old = ampTrimDb (m, v);
                const float err = 0.0f - (float) (through (a, bass ? bassSig : sig, sr).rmsDb - (bass ? bassIn : in).rmsDb - old);
                trim = std::round (err * 10.0f) / 10.0f;
            }
            std::printf (" %.1ff,", trim);
        }
        std::printf (" },   // %s\n", ms[(size_t) m].name.toRawUTF8());
    }
    return 0;
}

/** The amps: how distorted each is at its stock settings (THD of a 220 Hz sine played at a guitar's
    level), and its level. For voicing. */
int runAmpReport()
{
    const double sr = 48000.0;
    const auto sig = makeStrum (sr, 6.0);
    const auto in = levelOf (sig, sr);
    const auto& ms = AmpBlock::models();
    std::printf ("%-28s %-12s %7s %7s %7s %7s %7s\n", "amp", "switch", "thd%", "thd g2", "thd g8", "level", "cpu%");
    auto thd = [&] (AmpBlock& a, float gain)
    {
        const float keep = a.get (AmpBlock::pGain);
        a.set (AmpBlock::pGain, gain);
        const int len = (int) sr;
        std::vector<float> s ((size_t) len);
        for (int i = 0; i < len; ++i) s[(size_t) i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) sr);
        a.prepare (sr, 128);
        juce::AudioBuffer<float> buf (2, 128);
        std::vector<float> out;
        for (int off = 0; off + 128 <= len; off += 128)
        {
            for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, s.data() + off, 128);
            a.process (buf, 128);
            for (int i = 0; i < 128; ++i) out.push_back (buf.getSample (0, i));
        }
        a.set (AmpBlock::pGain, keep);
        // harmonic energy against the fundamental, over the last half second (an exact number of cycles)
        const int start = len - 24000;
        double h[11] {};
        for (int k = 1; k <= 10; ++k)
        {
            double re = 0, im = 0;
            for (int i = start; i < len; ++i)
            {
                const double ph = juce::MathConstants<double>::twoPi * 220.0 * k * i / sr;
                re += out[(size_t) i] * std::cos (ph); im += out[(size_t) i] * std::sin (ph);
            }
            h[k] = std::sqrt (re * re + im * im);
        }
        double hs = 0; for (int k = 2; k <= 10; ++k) hs += h[k] * h[k];
        return 100.0 * std::sqrt (hs) / juce::jmax (1.0e-9, h[1]);
    };
    for (int m = 0; m < (int) ms.size(); ++m)
        for (int v = 0; v < juce::jmax (1, (int) ms[(size_t) m].voices.size()); ++v)
        {
            AmpBlock a;
            a.set (AmpBlock::pModel, (float) m); a.onChoiceChanged (AmpBlock::pModel);
            a.set (AmpBlock::pVoice, (float) v);
            const double t = thd (a, a.get (AmpBlock::pGain)), t2 = thd (a, 2.0f), t8 = thd (a, 8.0f);
            const double lv = through (a, sig, sr).rmsDb - in.rmsDb;
            const double cpu = v == 0 ? costOf (a, sig, sr) : 0.0;
            std::printf ("%-28s %-12s %7.1f %7.1f %7.1f %+7.1f %7.2f\n", ms[(size_t) m].name.substring (0, 28).toRawUTF8(),
                         ms[(size_t) m].voices.empty() ? "" : ms[(size_t) m].voices[(size_t) v].name.substring (0, 12).toRawUTF8(), t, t2, t8, lv, cpu);
        }
    return 0;
}

/** For the test suite: every block at its default and every model at its stock settings comes out
    finite, without a DC offset and at its target level against a played guitar (or bass). Returns
    how many don't. */
int checkLevels()
{
    const double sr = 48000.0;
    const auto sig = makeStrum (sr, 4.0), bassSig = makeStrum (sr, 4.0, true);
    const auto in = levelOf (sig, sr), bassIn = levelOf (bassSig, sr);
    Engine e;
    e.prepare (sr, 128);
    int bad = 0, checked = 0;
    auto check = [&] (NodeType t, const juce::String& what, Node& n)
    {
        const bool bass = t == NodeType::BassDrive || (dynamic_cast<CabBlock*> (&n) != nullptr && dynamic_cast<CabBlock&> (n).cab().bass)
                       || (dynamic_cast<AmpBlock*> (&n) != nullptr && dynamic_cast<AmpBlock&> (n).isBassModel());
        const auto lv = through (n, bass ? bassSig : sig, sr);
        const double gain = lv.rmsDb - (bass ? bassIn : in).rmsDb;
        double lo = -2.0, hi = 3.0;   // effects sit at level
        switch (t)
        {
            case NodeType::Overdrive: case NodeType::Distortion: case NodeType::Fuzz: lo = 2.5; hi = 5.5; break;   // drives: a few dB up
            case NodeType::BassDrive: lo = 0.5; hi = 3.5; break;
            case NodeType::Boost:     lo = 3.0; hi = 14.0; break;    // a boost boosts
            case NodeType::Limiter:   lo = -1.0; hi = 9.0; break;    // input drive into the ceiling
            case NodeType::Gate: case NodeType::Expander: case NodeType::Swell: lo = -12.0; hi = 1.5; break;
            case NodeType::Cab:       lo = -4.0; hi = 4.0; break;
            case NodeType::Wah:       lo = -4.5; hi = 3.0; break;    // a band-pass takes level away
            default: break;
        }
        ++checked;
        if (lv.finite && std::abs (lv.dc) < 0.002 && gain >= lo && gain <= hi) return;
        ++bad;
        std::printf ("    %s  %s: %+.1f dB (wanted %+.1f to %+.1f)%s%s\n", builtInShortName (t).toRawUTF8(), what.toRawUTF8(), gain, lo, hi,
                     lv.finite ? "" : " NOT FINITE", std::abs (lv.dc) < 0.002 ? "" : " DC");
    };
    for (auto t : allBuiltIns())
    {
        if (t == NodeType::Tuner) continue;
        { auto n = e.createNode (t); if (n == nullptr) continue; check (t, "default", *n); }
        if (auto* probe = dynamic_cast<PedalBlock*> (e.createNode (t).release()))
        {
            std::unique_ptr<PedalBlock> own (probe);
            for (int m = 0; m < (int) probe->models().size(); ++m)
                for (int v = 0; v < juce::jmax (1, probe->models()[(size_t) m].voices.size()); ++v)
                {
                    auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
                    b->set (PedalBlock::pModel, (float) m); b->onChoiceChanged (PedalBlock::pModel);
                    b->set (PedalBlock::pVoice, (float) v);
                    check (t, probe->models()[(size_t) m].name + " / " + probe->models()[(size_t) m].voices[v], *n);
                }
        }
        if (t == NodeType::Cab)
            for (int cab = 0; cab < (int) CabBlock::cabs().size(); ++cab)
                for (int mic = 0; mic < (int) CabBlock::mics().size(); ++mic)
                {
                    auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
                    b->set (CabBlock::pCab, (float) cab); b->set (CabBlock::pMic, (float) mic);
                    check (t, CabBlock::cabs()[(size_t) cab].name + " / " + CabBlock::mics()[(size_t) mic].name, *n);
                }
        if (t == NodeType::Amp)
            for (int m = 0; m < (int) AmpBlock::models().size(); ++m)
                for (int v = 0; v < juce::jmax (1, (int) AmpBlock::models()[(size_t) m].voices.size()); ++v)
                {
                    AmpBlock a;
                    a.set (AmpBlock::pModel, (float) m); a.onChoiceChanged (AmpBlock::pModel); a.set (AmpBlock::pVoice, (float) v);
                    check (t, AmpBlock::models()[(size_t) m].name + " / " + a.formatValue (AmpBlock::pVoice), a);
                }
        const auto& fm = fxModelsFor (t);
        for (int m = 1; m < (int) fm.size(); ++m)
        {
            auto n = e.createNode (t); auto* b = dynamic_cast<Block*> (n.get());
            b->set (b->getModelParam(), (float) m); b->applyModelStock();
            check (t, fm[(size_t) m].name, *n);
        }
    }
    std::printf ("    %d blocks, models and cabinets measured\n", checked);
    return bad;
}

namespace
{
/** Renders a signal through a fresh copy of a node's settings and keeps what comes out (both channels). */
std::vector<float> render (Engine& e, NodeType t, const std::vector<float>& values, const std::vector<float>& sig, double sr)
{
    auto n = e.createNode (t);
    auto* b = dynamic_cast<Block*> (n.get());
    for (int k = 0; k < b->numParams(); ++k) b->set (k, values[(size_t) k]);
    const int block = 128;
    n->prepare (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> out;
    out.reserve (sig.size() * 2);
    for (size_t off = 0; off + (size_t) block <= sig.size(); off += (size_t) block)
    {
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, sig.data() + off, block);
        n->process (buf, block);
        for (int i = 0; i < block; ++i) { out.push_back (buf.getSample (0, i)); out.push_back (buf.getSample (1, i)); }
    }
    return out;
}

double differenceDb (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0, s = 0;
    for (size_t i = 0; i < a.size(); ++i) { d += ((double) a[i] - b[i]) * ((double) a[i] - b[i]); s += (double) a[i] * a[i] + (double) b[i] * b[i]; }
    return 10.0 * std::log10 (juce::jmax (1.0e-20, d) / juce::jmax (1.0e-20, s * 0.5));
}
}

/** Every knob a panel shows must change the sound: for every block, every model, and (on the
    Studio model) every setting of each selector, each visible knob is turned from one end to the
    other and the outputs compared. Returns the number that made no difference. */
int auditKnobs (bool verbose)
{
    const double sr = 48000.0;
    // half a second of near-silence (so swells and gates arm), a strum that dies away over its last
    // second, then 1.5 s for the tails, all over a faint hiss for the gates and expanders to act on
    std::vector<float> sig ((size_t) (sr * 0.5), 0.0f);
    {
        auto strum = makeStrum (sr, 2.5);
        const size_t fade = (size_t) sr;
        for (size_t i = 0; i < fade; ++i) strum[strum.size() - fade + i] *= std::pow (0.001f, (float) i / (float) fade);
        sig.insert (sig.end(), strum.begin(), strum.end());
        sig.resize (sig.size() + (size_t) (sr * 1.5), 0.0f);
        juce::Random rnd (7);
        for (auto& x : sig) x += 0.0003f * (rnd.nextFloat() - 0.5f);
    }
    auto loud = sig;
    for (auto& x : loud) x *= 6.0f;    // +15.5 dB: where limiters and compressors work hard

    // a voice for the vocal tools: a slightly flat sung line (a buzzy tone with vibrato) with "s"
    // sounds between the notes
    std::vector<float> voiceSig (sig.size(), 0.0f);
    {
        juce::Random rnd (3);
        const float notes[] { 57.6f, 59.7f, 61.3f, 62.8f, 64.6f, 62.2f };
        double ph = 0;
        for (size_t i = 0; i < voiceSig.size(); ++i)
        {
            const double t = (double) i / sr;
            const int k = (int) (t / 0.5) % 6;
            const double within = std::fmod (t, 0.5);
            const double hz = 440.0 * std::pow (2.0, (notes[k] - 69.0) / 12.0) * (1.0 + 0.004 * std::sin (6.0 * juce::MathConstants<double>::twoPi * t));
            ph += hz / sr;
            float v = 0;
            for (int h = 1; h <= 8; ++h) v += (float) std::sin (juce::MathConstants<double>::twoPi * ph * h) / (float) h;
            v *= 0.12f * (within < 0.42 ? 1.0f : 0.0f);
            if (within >= 0.42) v = 0.1f * (rnd.nextFloat() - 0.5f);   // the "s"
            voiceSig[i] = v;
        }
    }
    auto loudVoice = voiceSig;
    for (auto& x : loudVoice) x *= 4.0f;
    Engine e;
    e.prepare (sr, 128);
    int dead = 0, checked = 0;
    for (auto t : allBuiltIns())
    {
        if (t == NodeType::Tuner) continue;
        auto probeNode = e.createNode (t);
        auto* probe = dynamic_cast<Block*> (probeNode.get());
        if (probe == nullptr) continue;
        auto* pedal = dynamic_cast<PedalBlock*> (probe);
        const bool amp = t == NodeType::Amp;
        const int modelParam = pedal != nullptr ? (int) PedalBlock::pModel : amp ? (int) AmpBlock::pModel : probe->getModelParam();
        const int numModels = pedal != nullptr ? (int) pedal->models().size() : amp ? (int) AmpBlock::models().size() : (int) fxModelsFor (t).size();

        struct Setup { juce::String label; std::vector<float> values; std::vector<int> panel; };
        std::vector<Setup> setups;
        auto snapshot = [&] (Block& b, const juce::String& label)
        {
            Setup s; s.label = label;
            for (int k = 0; k < b.numParams(); ++k) s.values.push_back (b.getBase (k));
            s.panel = b.panelOrder();
            if (s.panel.empty()) for (int k = 0; k < b.numParams(); ++k) s.panel.push_back (k);
            setups.push_back (std::move (s));
        };
        for (int m = 0; m < juce::jmax (1, numModels); ++m)
        {
            auto n = e.createNode (t); auto& b = dynamic_cast<Block&> (*n);
            if (modelParam >= 0 && numModels > 0) { b.set (modelParam, (float) m); b.onChoiceChanged (modelParam); }
            const juce::String name = numModels > 0 ? (pedal != nullptr ? pedal->models()[(size_t) m].name
                                                       : amp ? AmpBlock::models()[(size_t) m].name : fxModelsFor (t)[(size_t) m].name) : juce::String ("default");
            snapshot (b, name);
            if (m != 0) continue;
            // the studio model: each selector setting changes which knobs matter
            for (int c = 0; c < b.numParams(); ++c)
            {
                const auto& sp = b.getSpecs()[(size_t) c];
                if (! sp.isChoice() || c == modelParam || sp.choices.size() > 8) continue;
                for (int v = 1; v < sp.choices.size(); ++v)
                {
                    auto n2 = e.createNode (t); auto& b2 = dynamic_cast<Block&> (*n2);
                    b2.set (c, (float) v); b2.onChoiceChanged (c);
                    snapshot (b2, name + ", " + sp.name + " " + sp.choices[v]);
                }
            }
        }
        for (auto& s : setups)
        {
            for (int k : s.panel)
            {
                if (k == modelParam) continue;
                // what can't be heard from one block alone: a key from elsewhere, an auto-off that waits for the pedal to stop
                const auto id = probe->getSpecs()[(size_t) k].id;
                if ((t == NodeType::Gate && (id == "key" || id == "view" || id == "listen")) || (t == NodeType::Wah && id == "auto")) continue;
                const auto& sp = probe->getSpecs()[(size_t) k];
                auto lo = s.values, hi = s.values;
                if (sp.isChoice() || sp.toggle) { lo[(size_t) k] = 0; hi[(size_t) k] = sp.isChoice() ? (float) (sp.id == "key" ? 3 : sp.choices.size() - 1) : 1.0f; }
                else { lo[(size_t) k] = sp.fromProportion (0.1f); hi[(size_t) k] = sp.fromProportion (0.9f); }
                // some knobs only act on what another sets (an EQ band's frequency at 0 dB, the gate's
                // key with no other input in the test): those are given a setting where they act
                auto useful = [&] (std::vector<float>& v)
                {
                    if (t == NodeType::Eq)
                        for (auto id : { "low", "mid", "high" }) { const int g = probe->indexOf (id); if (g >= 0 && g != k) v[(size_t) g] = 6.0f; }
                    if (pedal != nullptr && k != PedalBlock::pMid) v[(size_t) PedalBlock::pMid] = juce::jmax (v[(size_t) PedalBlock::pMid], 8.0f);
                    if (t == NodeType::Harmonizer && k != HarmonizerBlock::pVoice2) v[(size_t) HarmonizerBlock::pVoice2] = 2.0f;
                    if (t == NodeType::AcousticPre)
                    {
                        if (k != AcousticPreBlock::pNotchDepth) v[(size_t) AcousticPreBlock::pNotchDepth] = -12.0f;
                        if (k != AcousticPreBlock::pMid) v[(size_t) AcousticPreBlock::pMid] = -6.0f;
                    }
                };
                useful (lo); useful (hi);
                const bool vocal = t == NodeType::PitchCorrect || t == NodeType::Harmonizer || t == NodeType::DeEsser || t == NodeType::VoiceStrip;
                const auto& s1 = vocal ? voiceSig : sig;
                const auto& s2 = vocal ? loudVoice : loud;
                const double d = juce::jmax (differenceDb (render (e, t, lo, s1, sr), render (e, t, hi, s1, sr)),
                                             differenceDb (render (e, t, lo, s2, sr), render (e, t, hi, s2, sr)));
                ++checked;
                // (a gate's release only touches the quiet ends of notes, so anything audible counts)
                const bool isDead = d < -90.0;
                if (isDead) ++dead;
                if (isDead || verbose)
                    std::printf ("%s %-10s %-44s %-16s %6.1f dB\n", isDead ? "DEAD" : "    ", builtInShortName (t).toRawUTF8(),
                                 s.label.substring (0, 44).toRawUTF8(), probe->getParamName (k).substring (0, 16).toRawUTF8(), d);
            }
        }
    }
    std::printf ("\n%d of %d knobs made no difference\n", dead, checked);
    return dead;
}
