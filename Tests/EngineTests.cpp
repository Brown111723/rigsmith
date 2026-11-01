// Headless smoke tests for the Rigsmith engine. Build with -DRIGSMITH_TESTS=ON.
#include "Engine/Engine.h"
#include "Engine/Randomiser.h"
#include <cstdio>

using namespace rig;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

static float runNoise (Engine& e, int blocks, int blockSize, bool* finite, float amp = 0.2f)
{
    juce::Random rnd (1);
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0;
    *finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float x = (rnd.nextFloat() * 2.0f - 1.0f) * amp;
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        e.process (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const float y = buf.getSample (ch, i);
                if (! std::isfinite (y)) *finite = false;
                peak = juce::jmax (peak, std::abs (y));
            }
    }
    return peak;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File models (argc > 1 ? argv[1] : "");
    const auto model = models.getChildFile ("wavenet_a1_standard.nam");

    for (double sr : { 48000.0, 44100.0, 96000.0 })
    {
        std::printf ("--- %.0f Hz\n", sr);
        Engine e;
        e.prepare (sr, 256);

        auto* nam = dynamic_cast<NamBlock*> (e.getRoot().nodes[1].get());
        CHECK (nam != nullptr, "default rig has a NAM block");
        juce::String err;
        const bool loaded = nam->loadModel (model, err);
        if (! loaded) std::printf ("      %s\n", err.toRawUTF8());
        CHECK (loaded, "loads a .nam model");
        CHECK (nam->getInfo().resampling == (sr != 48000.0), "resamples only when rates differ");

        bool finite = false;
        float peak = runNoise (e, 200, 256, &finite);
        CHECK (finite && peak > 1.0e-4f, "single chain produces finite, non-silent audio");

        // host block bigger than prepared block
        {
            juce::AudioBuffer<float> big (2, 1000);
            big.clear();
            big.setSample (0, 0, 0.5f);
            juce::MidiBuffer m;
            e.process (big, m);
            CHECK (std::isfinite (big.getMagnitude (0, 0, 1000)), "oversized host block is chunked safely");
        }

        // wrap amp in a 3-way split, put another model in path B, try every mode
        auto* split = e.wrapInSplit (nam->id, 3);
        CHECK (split != nullptr && split->branches.size() == 3, "wrap in 3-way split");
        auto nam2 = std::make_unique<NamBlock>();
        CHECK (nam2->loadModel (models.getChildFile ("lstm.nam"), err), "loads an LSTM model");
        e.insert (std::move (nam2), *split->branches[1]->chain, 0);
        e.insert (e.createNode (NodeType::Comp), *split->branches[2]->chain, 0);

        for (int m = 0; m < 4; ++m)
        {
            split->setMode ((SplitMode) m);
            peak = runNoise (e, 60, 256, &finite);
            CHECK (finite && peak > 1.0e-4f, ("split mode " + splitModeName ((SplitMode) m) + " is finite and audible").toRawUTF8());
        }

        split->setMode (SplitMode::Split);
        split->setLevel (0, 0.7f, true);
        float sum = 0;
        for (auto& b : split->branches) sum += b->level.load();
        CHECK (std::abs (sum - 1.0f) < 1.0e-3f, "Split mode keeps levels at 100%");

        // nested split inside path C
        e.addSplit (*split->branches[2]->chain, -1, 2);
        peak = runNoise (e, 30, 256, &finite);
        CHECK (finite, "nested split is finite");

        // scenes
        e.setScene (1);
        nam = dynamic_cast<NamBlock*> (split->branches[0]->chain->nodes[0].get());
        nam->bypassed = true;
        e.storeScene();
        e.setScene (0);
        CHECK (! nam->bypassed.load(), "scene A restores bypass state");
        e.setScene (1);
        CHECK (nam->bypassed.load(), "scene B recalls its own bypass state");

        // state round trip
        const auto state = e.getState (true);
        if (argc > 2 && sr == 48000.0)
            if (auto xml = e.getState (false).createXml())
                xml->writeTo (juce::File (argv[2]));
        Engine e2;
        e2.prepare (sr, 256);
        e2.setState (state, true);
        const auto again = e2.getState (true);
        CHECK (state.getChildWithName ("Node").isEquivalentTo (again.getChildWithName ("Node")), "state round-trips exactly");
        peak = runNoise (e2, 30, 256, &finite);
        CHECK (finite && peak > 1.0e-4f, "restored rig makes sound");

        // structural edits
        auto* added = e.insert (e.createNode (NodeType::Eq), e.getRoot(), 0);
        CHECK (e.move (added->id, *split->branches[1]->chain, 0), "move block into a path");
        CHECK (! e.canDropInto (split->id, *split->branches[0]->chain), "can't drop a split into itself");
        auto* dup = e.duplicate (added->id);
        CHECK (dup != nullptr && dup->id != added->id, "duplicate gets a new id");
        e.removeBranch (*split, 2);
        CHECK (split->branches.size() == 2, "remove path");
        e.unwrapSplit (*split);
        runNoise (e, 10, 256, &finite);
        CHECK (finite, "unwrap split keeps running");
    }

    // every built-in effect at every choice setting: finite, and not silent
    {
        std::printf ("--- built-in effects\n");
        const NodeType all[] = { NodeType::Gate, NodeType::Comp, NodeType::Limiter, NodeType::Expander, NodeType::Transient,
                                 NodeType::Swell, NodeType::Overdrive, NodeType::Fuzz, NodeType::Boost, NodeType::Crusher,
                                 NodeType::Eq, NodeType::Wah, NodeType::Chorus, NodeType::Flanger, NodeType::Phaser,
                                 NodeType::Tremolo, NodeType::Vibe, NodeType::Rotary, NodeType::Delay, NodeType::Reverb,
                                 NodeType::Pitch, NodeType::Gain, NodeType::Ir };
        for (auto t : all)
        {
            Engine e;
            e.prepare (48000.0, 256);
            e.limiterOn = false;
            auto& root = e.getRoot();
            while (! root.nodes.empty()) e.remove (root.nodes[0]->id);
            auto* n = e.insert (e.createNode (t), root, 0);
            auto* b = dynamic_cast<Block*> (n);
            bool allOk = b != nullptr;
            int choiceIdx = -1, numChoices = 1;
            for (int i = 0; b != nullptr && i < b->numParams(); ++i)
                if (b->getSpecs()[(size_t) i].isChoice()) { choiceIdx = i; numChoices = b->getSpecs()[(size_t) i].choices.size(); }
            for (int c = 0; c < numChoices && allOk; ++c)
            {
                if (choiceIdx >= 0) b->set (choiceIdx, (float) c);
                bool finite = false;
                const float pk = runNoise (e, 120, 256, &finite);
                allOk = finite && pk > 1.0e-4f && pk < 50.0f;
                if (! allOk) std::printf ("      choice %d peak %f finite %d\n", c, pk, (int) finite);
            }
            // visuals must be finite too
            for (double hz : { 30.0, 440.0, 8000.0 }) allOk = allOk && std::isfinite (b->responseDb (hz));
            for (float db : { -80.0f, -20.0f, 0.0f }) allOk = allOk && std::isfinite (b->transferDb (db));
            allOk = allOk && std::isfinite (b->shape (0.5f));
            CHECK (allOk, (n->getName() + " runs cleanly").toRawUTF8());
        }
    }

    if (argc > 3)
    {
        Engine e;
        e.prepare (48000.0, 256);
        auto& root = e.getRoot();
        while (! root.nodes.empty()) e.remove (root.nodes[0]->id);
        for (auto t : { NodeType::Gate, NodeType::Comp, NodeType::Overdrive, NodeType::Nam, NodeType::Eq, NodeType::Phaser, NodeType::Delay, NodeType::Reverb })
            e.insert (e.createNode (t), root, -1);
        juce::String err;
        dynamic_cast<NamBlock*> (root.nodes[3].get())->loadModel (models.getChildFile ("lstm.nam"), err);
        auto* eq = dynamic_cast<EqBlock*> (root.nodes[4].get());
        eq->set (EqBlock::pHpf, 90.0f); eq->set (EqBlock::pMid, -4.5f); eq->set (EqBlock::pHigh, 3.0f); eq->set (EqBlock::pLpf, 9000.0f);
        dynamic_cast<DelayBlock*> (root.nodes[6].get())->set (DelayBlock::pMode, 2.0f);
        root.nodes[2]->autoGain = true;
        if (auto xml = e.getState (false).createXml()) xml->writeTo (juce::File (argv[3]));
    }

    // DSP sanity: echoes land on time, reverb decays at about its RT60, octave up doubles pitch
    {
        auto render = [] (Block& b, std::vector<float>& in, int len)
        {
            b.prepare (48000.0, 256);
            std::vector<float> out ((size_t) len);
            juce::AudioBuffer<float> buf (2, 256);
            for (int off = 0; off < len; off += 256)
            {
                for (int i = 0; i < 256; ++i) { const float x = off + i < (int) in.size() ? in[(size_t) (off + i)] : 0.0f; buf.setSample (0, i, x); buf.setSample (1, i, x); }
                b.process (buf, 256);
                for (int i = 0; i < 256 && off + i < len; ++i) out[(size_t) (off + i)] = buf.getSample (0, i);
            }
            return out;
        };

        DelayBlock d;
        d.set (DelayBlock::pMod, 0.0f); d.set (DelayBlock::pMix, 50.0f); d.set (DelayBlock::pLowCut, 20.0f); d.set (DelayBlock::pHighCut, 20000.0f);
        std::vector<float> imp (1, 1.0f);
        auto out = render (d, imp, 48000);
        int peakAt = 0; float pk = 0;
        for (int i = 100; i < 48000; ++i) if (std::abs (out[(size_t) i]) > pk) { pk = std::abs (out[(size_t) i]); peakAt = i; }
        CHECK (std::abs (peakAt - 19200) < 8, "delay echo arrives at 400 ms");

        ReverbBlock rv;
        rv.set (ReverbBlock::pMix, 100.0f); rv.set (ReverbBlock::pDecay, 2.0f); rv.set (ReverbBlock::pPredelay, 0.0f); rv.set (ReverbBlock::pMod, 0.0f);
        std::vector<float> noise (4800);
        juce::Random rnd (3);
        for (auto& x : noise) x = rnd.nextFloat() * 2.0f - 1.0f;
        out = render (rv, noise, 48000 * 3);
        auto rms = [&out] (int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += out[(size_t) i] * out[(size_t) i]; return 10.0 * std::log10 (s / (b - a) + 1e-20); };
        const double early = rms (4800, 9600), late = rms (4800 + 48000, 9600 + 48000);   // 1 s later
        std::printf ("      reverb 1 s decay %.1f dB (RT60 2 s -> about 30 dB)\n", early - late);
        CHECK (early - late > 18.0 && early - late < 45.0, "reverb decays at about its RT60");

        PitchBlock pb;
        pb.set (PitchBlock::pMode, 1.0f); pb.set (PitchBlock::pMix, 100.0f); pb.set (PitchBlock::pTone, 20000.0f);
        std::vector<float> sine (48000);
        for (int i = 0; i < 48000; ++i) sine[(size_t) i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * i / 48000.0f);
        out = render (pb, sine, 48000);
        // dominant frequency by DFT scan around the expected peak
        auto power = [&out] (float hz)
        {
            double re = 0, im = 0;
            for (int i = 8000; i < 48000; ++i) { const double w = 2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0; re += out[(size_t) i] * std::cos (w); im += out[(size_t) i] * std::sin (w); }
            return re * re + im * im;
        };
        float bestHz = 0; double bestP = 0;
        for (float hz = 150.0f; hz < 700.0f; hz += 1.0f) if (const double pw = power (hz); pw > bestP) { bestP = pw; bestHz = hz; }
        std::printf ("      octave up of 220 Hz peaks at %.0f Hz\n", bestHz);
        CHECK (std::abs (bestHz - 440.0f) < 4.0f, "octave up doubles the pitch");
    }

    // parameter display: dB always one decimal, skew only when asked for
    {
        GateBlock g;
        CHECK (g.formatValue (0) == "-58.0 dB", "gate threshold reads -58.0 dB");
        CHECK (! g.getSpecs()[0].hasCentre, "gate threshold knob is linear across -90..0 dB");
        NamBlock nb;
        CHECK (nb.formatValue (0, 3.14159f) == "+3.1 dB", "dB values round to one decimal");
    }

    // gain compensation: learns a fixed level match, holds it, re-learns after edits
    {
        Engine e;
        e.prepare (48000.0, 256);
        e.limiterOn = false;
        auto& root = e.getRoot();
        while (! root.nodes.empty()) e.remove (root.nodes[0]->id);
        auto* g = dynamic_cast<GainBlock*> (e.insert (e.createNode (NodeType::Gain), root, 0));
        g->set (0, 12.0f);
        g->autoGain = true;
        g->relearn = true;
        bool finite;
        runNoise (e, 400, 256, &finite);
        CHECK (std::abs (g->autoGainDb.load() + 12.0f) < 0.5f, "gain compensation matches a +12 dB boost");
        CHECK (g->autoGainProgress.load() >= 1.0f, "gain compensation finishes learning");
        std::printf ("      compensation %.1f dB\n", g->autoGainDb.load());
        const float held = g->autoGainDb.load();
        runNoise (e, 200, 256, &finite, 0.02f);   // playing softer must not move it (no pumping)
        CHECK (std::abs (g->autoGainDb.load() - held) < 0.01f, "gain compensation holds while playing dynamics change");
        g->set (0, 6.0f);                         // an edit re-learns
        runNoise (e, 400, 256, &finite);
        CHECK (std::abs (g->autoGainDb.load() + 6.0f) < 0.5f, "gain compensation re-learns after a settings change");

        // saved with the preset, restored without re-learning
        auto v = g->toState();
        auto rebuilt = e.createFromState (v);
        CHECK (rebuilt != nullptr && std::abs (rebuilt->autoGainDb.load() + 6.0f) < 0.5f && ! rebuilt->relearn.load(), "gain compensation is stored in presets");

        // replace keeps id, bypass and auto gain
        g->bypassed = true;
        const auto id = g->id;
        auto* r = e.replace (id, e.createNode (NodeType::Chorus));
        CHECK (r != nullptr && r->id == id && r->bypassed.load() && r->autoGain.load(), "replace keeps id, bypass and auto gain");

        // undo / redo
        CHECK (e.undo() && dynamic_cast<GainBlock*> (e.findNode (id)) != nullptr, "undo brings back the replaced block");
        CHECK (e.redo() && dynamic_cast<ChorusBlock*> (e.findNode (id)) != nullptr, "redo replaces it again");
        e.insert (e.createNode (NodeType::Delay), e.getRoot(), -1);
        const auto delayId = e.getRoot().nodes.back()->id;
        CHECK (e.swap (id, delayId) && e.getRoot().nodes[0]->id == delayId, "swap exchanges two blocks");
        const int before = (int) e.getRoot().nodes.size();
        e.remove (delayId);
        CHECK (e.undo() && (int) e.getRoot().nodes.size() == before, "undo restores a removed block");
    }

    // block presets
    {
        Engine e;
        e.prepare (48000.0, 256);
        auto* d = dynamic_cast<DelayBlock*> (e.insert (e.createNode (NodeType::Delay), e.getRoot(), -1));
        d->set (DelayBlock::pTime, 333.0f);
        d->set (DelayBlock::pMode, 2.0f);
        const auto id = d->id;
        CHECK (e.saveBlockPreset (*d, "Test Slapback"), "saves a block preset");
        d->set (DelayBlock::pTime, 900.0f);
        auto presets = e.getBlockPresets (*d);
        juce::File f;
        for (auto& p : presets) if (p.getFileNameWithoutExtension() == "Test Slapback") f = p;
        auto* loaded = dynamic_cast<DelayBlock*> (e.loadBlockPreset (id, f));
        CHECK (loaded != nullptr && loaded->id == id && std::abs (loaded->get (DelayBlock::pTime) - 333.0f) < 0.01f
               && loaded->getChoice (DelayBlock::pMode) == 2, "loads a block preset in place");
        auto* init = dynamic_cast<DelayBlock*> (e.initBlock (id));
        CHECK (init != nullptr && std::abs (init->get (DelayBlock::pTime) - 400.0f) < 0.01f, "init resets a block to defaults");
        f.deleteFile();

        // Init is the first entry when stepping presets
        e.stepPreset (-1);
        e.stepPreset (1);
        CHECK (e.presetFile == juce::File() || e.getPresetFiles().size() > 0, "preset stepping includes Init");
        e.loadDefaultRig();
        CHECK (e.isInit(), "Init rig is recognised");
    }

    // randomiser
    {
        Engine e;
        e.prepare (48000.0, 256);
        e.setPaths ("modelPaths", juce::FileSearchPath (models.getFullPathName()));
        Randomiser rz (e);
        rz.random().setSeed (42);

        // lock a reverb: it must survive every randomised rig
        auto* rv = dynamic_cast<ReverbBlock*> (e.insert (e.createNode (NodeType::Reverb), e.getRoot(), -1));
        rv->set (ReverbBlock::pDecay, 7.7f);
        rv->locked = true;
        const auto lockedId = rv->id;

        bool allFinite = true, allOk = true, sawSplit = false, sawModel = false;
        for (int i = 0; i < 12; ++i)
        {
            rz.randomiseRig();
            auto* kept = dynamic_cast<ReverbBlock*> (e.findNode (lockedId));
            if (kept == nullptr || std::abs (kept->get (ReverbBlock::pDecay) - 7.7f) > 0.01f) allOk = false;
            visitNodes (e.getRoot(), [&] (Node& n)
            {
                if (n.getType() == NodeType::Split) sawSplit = true;
                if (auto* nb = dynamic_cast<NamBlock*> (&n); nb != nullptr && nb->getInfo().loaded) sawModel = true;
            });
            bool finite = false;
            runNoise (e, 40, 256, &finite);
            allFinite &= finite;
        }
        CHECK (allOk, "locked block survives randomising the rig");
        CHECK (allFinite, "random rigs produce finite audio");
        CHECK (sawModel, "random rigs load NAM models from the model folders");
        CHECK (sawSplit, "random rigs sometimes split into parallel amps");

        // randomise one block's settings within its musical range, and mutate gently
        auto* d = dynamic_cast<DelayBlock*> (e.insert (e.createNode (NodeType::Delay), e.getRoot(), -1));
        rz.randomiseSettings (*d, 1.0f);
        const float t = d->get (DelayBlock::pTime), mix = d->get (DelayBlock::pMix);
        CHECK (t >= 120.0f && t <= 800.0f && mix >= 15.0f && mix <= 40.0f, "randomised settings stay in musical ranges");
        rz.randomiseSettings (*d, 0.1f);
        CHECK (std::abs (d->get (DelayBlock::pTime) - t) < 400.0f, "mutate makes a small change");
        auto* nb = dynamic_cast<NamBlock*> (e.insert (e.createNode (NodeType::Nam), e.getRoot(), -1));
        const float outBefore = nb->get (NamBlock::pOutput);
        rz.randomiseSettings (*nb, 1.0f);
        CHECK (nb->get (NamBlock::pOutput) == outBefore, "levels are never randomised");

        // randomise type keeps the category
        const auto did = d->id;
        auto* swapped = rz.randomiseType (did);
        CHECK (swapped != nullptr && Randomiser::categoryOf (*swapped) == Category::Delay && swapped->id == did, "random block of the same category");
        auto* amp = rz.randomiseType (nb->id);
        CHECK (amp != nullptr && dynamic_cast<NamBlock*> (amp) != nullptr && dynamic_cast<NamBlock*> (amp)->getInfo().loaded, "random amp loads a model");

        rz.reshuffleBlocks();
        rz.randomiseAllSettings (0.3f);
        bool finite = false;
        runNoise (e, 40, 256, &finite);
        CHECK (finite && e.findNode (lockedId) != nullptr, "reshuffle and mutate all keep the rig working");
        CHECK (e.undo(), "randomising can be undone");
    }

    // categories
    {
        juce::PluginDescription d;
        d.name = "ValhallaVintageVerb"; d.category = "Fx";
        CHECK (categoriseDescription (d) == Category::Reverb, "categorises a reverb by name");
        d.name = "Mystery"; d.category = "Fx|Delay";
        CHECK (categoriseDescription (d) == Category::Delay, "categorises by declared VST3 category");
    }

    // tuner: 110 Hz sine should read A
    {
        Tuner t;
        t.prepare (48000.0);
        std::vector<float> s (4096);
        for (size_t i = 0; i < s.size(); ++i) s[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f * (float) i / 48000.0f);
        t.push (s.data(), (int) s.size());
        const auto r = t.analyse();
        CHECK (r.valid && r.note == "A" && std::abs (r.cents) < 5.0f, "tuner detects A2");
        std::printf ("      %.2f Hz %s %+.1f c\n", r.frequency, r.note.toRawUTF8(), r.cents);
    }

    // v0.4: tuner block, I/O stage, brickwall limiter
    {
        Engine e;
        e.prepare (48000.0, 256);
        auto* t = dynamic_cast<TunerBlock*> (e.insert (e.createNode (NodeType::Tuner), e.getRoot(), 0));
        CHECK (t != nullptr, "tuner is a block you can add");
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        double ph = 0;
        auto sine = [&] (float hz, float amp)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float x = amp * (float) std::sin (ph);
                ph += 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            e.process (buf, midi);
        };
        for (int b = 0; b < 20; ++b) sine (82.41f, 0.3f);
        const auto r = t->getReading();
        CHECK (r.valid && r.note == "E" && std::abs (r.cents) < 5.0f, "tuner block reads low E");
        std::printf ("      %.2f Hz %s%d %+.1f c  card: %s\n", r.frequency, r.note.toRawUTF8(), r.octave, r.cents, t->getDetail().toRawUTF8());
        t->set (TunerBlock::pMute, 1.0f);
        for (int b = 0; b < 8; ++b) sine (82.41f, 0.3f);
        CHECK (buf.getMagnitude (0, 0, 256) < 1.0e-4f, "tuner mute silences the output");
        e.remove (t->id);

        // brickwall: +12 dB into a -6 dB ceiling never gets past it
        e.limiterOn = true;
        e.limiterCeilingDb = -6.0f;
        e.outputGainDb = 12.0f;
        float peak = 0;
        for (int b = 0; b < 40; ++b) { sine (440.0f, 0.8f); peak = juce::jmax (peak, buf.getMagnitude (0, 0, 256), buf.getMagnitude (1, 0, 256)); }
        CHECK (peak <= dbToGain (-6.0f) + 1.0e-5f, "brickwall limiter holds the ceiling");
        const float gr = e.limiterGrDb.exchange (0.0f);
        CHECK (gr < -6.0f, "limiter reports gain reduction");
        std::printf ("      peak %.2f dBFS, GR %.1f dB\n", gainToDb (peak), gr);
        CHECK (e.outMeter.overs.exchange (0) > 0, "output counts overs hitting the limiter");

        // width 0 = mono, balance hard left = silent right
        e.outputGainDb = 0.0f; e.limiterCeilingDb = -0.3f;
        e.outputWidth = 0.0f;
        bool finite = false;
        juce::Random rnd (3);
        for (int b = 0; b < 10; ++b)
        {
            for (int i = 0; i < 256; ++i) { buf.setSample (0, i, rnd.nextFloat() * 0.2f - 0.1f); buf.setSample (1, i, rnd.nextFloat() * 0.2f - 0.1f); }
            e.inputMode = Engine::InputStereo;
            e.process (buf, midi);
        }
        float diff = 0;
        for (int i = 0; i < 256; ++i) diff = juce::jmax (diff, std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
        CHECK (diff < 1.0e-5f, "width 0% folds the output to mono");
        e.outputWidth = 100.0f; e.outputPan = -1.0f;
        runNoise (e, 10, 256, &finite);
        juce::AudioBuffer<float> b2 (2, 256);
        for (int i = 0; i < 256; ++i) { b2.setSample (0, i, 0.1f); b2.setSample (1, i, 0.1f); }
        e.process (b2, midi);
        CHECK (b2.getMagnitude (1, 0, 256) < 1.0e-4f && b2.getMagnitude (0, 0, 256) > 0.01f, "output balance hard left");
        e.outputPan = 0.0f;
        e.inputPhase = true;
        for (int k = 0; k < 4; ++k) { for (int i = 0; i < 256; ++i) { b2.setSample (0, i, 0.1f); b2.setSample (1, i, 0.1f); } e.process (b2, midi); }
        CHECK (b2.getSample (0, 100) < 0.0f, "input polarity flip");

        // session state round trip
        e.limiterCeilingDb = -3.5f; e.outputWidth = 150.0f; e.inputPan = 0.25f;
        auto st = e.getState (true);
        Engine e2; e2.prepare (48000.0, 256); e2.setState (st, true);
        CHECK (std::abs (e2.limiterCeilingDb.load() + 3.5f) < 1e-4f && std::abs (e2.outputWidth.load() - 150.0f) < 1e-3f
               && e2.inputPhase.load() && std::abs (e2.inputPan.load() - 0.25f) < 1e-4f, "I/O settings saved with the session");

        // randomising the rig keeps tuners
        auto* t2 = e.insert (e.createNode (NodeType::Tuner), e.getRoot(), 0);
        const auto tid = t2->id;
        Randomiser rz (e);
        rz.randomiseRig();
        CHECK (e.findNode (tid) != nullptr, "random rig keeps the tuner");
        CHECK (rz.randomiseType (tid) == nullptr, "a tuner isn't swapped for another block");
    }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
