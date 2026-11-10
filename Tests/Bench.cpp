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
                 "        return it->second[(size_t) model];\n    return 0.0f;\n}\n} // namespace rig\n");
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
        const bool bass = t == NodeType::BassDrive || (dynamic_cast<CabBlock*> (&n) != nullptr && dynamic_cast<CabBlock&> (n).cab().bass);
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
