// Headless smoke tests for the Rigsmith engine. Build with -DRIGSMITH_TESTS=ON.
#include "Engine/Engine.h"
#include "Engine/Randomiser.h"
#include "T3K/Tone3000.h"
#include "Engine/PluginScanner.h"
#include "Engine/FactoryPresets.h"
#include <cstdio>

using namespace rig;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

/** A stand-in for tone3000.com: just enough of the API for the sign-in + download flow. */
struct MockTone3000 : juce::Thread
{
    juce::StreamingSocket listener;
    int port = 0;
    juce::MemoryBlock modelBytes;
    std::atomic<int> tokenCalls { 0 }, unauthorised { 0 };
    juce::String lastTokenBody;

    MockTone3000() : juce::Thread ("mock t3k") {}
    ~MockTone3000() override { signalThreadShouldExit(); listener.close(); stopThread (2000); }
    bool start() { if (! listener.createListener (0, "127.0.0.1")) return false; port = listener.getBoundPort(); startThread(); return true; }
    juce::String origin() const { return "http://127.0.0.1:" + juce::String (port); }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
            if (c == nullptr) continue;
            juce::MemoryOutputStream in;
            char buf[4096];
            int contentLength = -1;
            for (;;)
            {
                if (c->waitUntilReady (true, 2000) != 1) break;
                const int n = c->read (buf, sizeof (buf), false);
                if (n <= 0) break;
                in.write (buf, (size_t) n);
                const auto t = in.toString();
                const int hdrEnd = t.indexOf ("\r\n\r\n");
                if (hdrEnd < 0) continue;
                if (contentLength < 0)
                {
                    contentLength = 0;
                    for (auto& line : juce::StringArray::fromLines (t.substring (0, hdrEnd)))
                        if (line.startsWithIgnoreCase ("Content-Length:")) contentLength = line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
                }
                if ((int) in.getDataSize() >= hdrEnd + 4 + contentLength) break;
            }
            const auto req = in.toString();
            const auto line = req.upToFirstOccurrenceOf ("\r\n", false, false);
            const auto method = line.upToFirstOccurrenceOf (" ", false, false);
            const auto target = line.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
            const auto path = target.upToFirstOccurrenceOf ("?", false, false);
            const auto body = req.fromFirstOccurrenceOf ("\r\n\r\n", false, false);
            const bool authed = req.contains ("Authorization: Bearer AT1");

            int status = 200;
            juce::String type = "application/json";
            juce::MemoryBlock out;
            auto json = [&] (const juce::String& j) { out.replaceAll (j.toRawUTF8(), j.getNumBytesAsUTF8()); };
            const juce::String tone = R"({"id":42,"title":"Plexi Crunch","gear":"amp-cab","format":"nam","license":"t3k",)"
                                      R"("url":"https://www.tone3000.com/tones/plexi-42","images":[],"user":{"username":"tonemaker","avatar_url":""}})";
            if (method == "POST" && path == "/api/v1/oauth/token")
            {
                ++tokenCalls;
                lastTokenBody = body;
                if (body.contains ("grant_type=authorization_code") && body.contains ("code=CODE1") && body.contains ("code_verifier="))
                    json (R"({"access_token":"AT1","refresh_token":"RT1","token_type":"bearer","expires_in":3600})");
                else { status = 400; json (R"({"error":"invalid_grant"})"); }
            }
            else if (! authed) { status = 401; ++unauthorised; json (R"({"error":"unauthorized"})"); }
            else if (path == "/api/v1/user") json (R"({"id":1,"username":"guitarist","display_name":"Guitar Ist","avatar_url":""})");
            else if (path == "/api/v1/tones/42") json (tone);
            else if (path == "/api/v1/tones/favorited") json ("{\"data\":[" + tone + "],\"page\":1,\"total\":1}");
            else if (path == "/api/v1/models")
            {
                if (target.contains ("architecture=2"))
                    json ("{\"data\":[{\"id\":7,\"tone_id\":42,\"name\":\"Crunch 5\",\"architecture_version\":\"2\",\"model_url\":\"" + origin() + "/files/7.nam\"}]}");
                else json (R"({"data":[]})");
            }
            else if (path == "/files/7.nam") { type = "application/octet-stream"; out = modelBytes; }
            else { status = 404; json ("{}"); }

            juce::String head;
            head << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Error") << "\r\nContent-Type: " << type
                 << "\r\nContent-Length: " << (int) out.getSize() << "\r\nConnection: close\r\n\r\n";
            c->write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
            if (out.getSize() > 0) c->write (out.getData(), (int) out.getSize());
            c->close();
        }
    }
};

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
    // keep the tests' settings away from the real Rigsmith.ini
    const auto testDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rigsmith-tests");
    testDir.deleteRecursively();
    testDir.createDirectory();
    const auto iniPath = testDir.getChildFile ("Rigsmith.ini");
   #if JUCE_WINDOWS
    _putenv_s ("RIGSMITH_INI", iniPath.getFullPathName().toRawUTF8());
   #else
    setenv ("RIGSMITH_INI", iniPath.getFullPathName().toRawUTF8(), 1);
   #endif
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

    //==========================================================================
    std::printf ("--- v0.5: Rigsmith.ini, randomise ticks, starting points\n");
    {
        // the ini format itself
        IniFile a (testDir.getChildFile ("t.ini"));
        a.header = { "hello" };
        a.set ("General", "Name", "Rig = smith");
        a.setList ("NAM Folders", "Folder", { "C:\\Amps", "D:\\More Amps" });
        a.setBool ("Window", "FullScreen", true);
        IniFile b (testDir.getChildFile ("t.ini"));
        CHECK (b.get ("general", "name") == "Rig = smith", "ini keeps values containing '=' (case-insensitive lookup)");
        CHECK (b.getList ("NAM Folders") == juce::StringArray ({ "C:\\Amps", "D:\\More Amps" }), "ini numbered lists round trip");
        CHECK (b.getBool ("Window", "FullScreen", false), "ini booleans");
        CHECK (testDir.getChildFile ("t.ini").loadFileAsString().startsWith ("; hello"), "ini has its comment header");
        b.set ("General", "Other", "x");
        a.set ("General", "Third", "y");   // a's copy is stale: it must re-read before writing
        IniFile c (testDir.getChildFile ("t.ini"));
        CHECK (c.get ("General", "Other") == "x" && c.get ("General", "Third") == "y", "two writers don't undo each other");

        // created on first run, with startup scans off once a scan has happened
        {
            Engine e;
            CHECK (iniPath.existsAsFile(), "Rigsmith.ini is created when it isn't found");
            CHECK (e.ini.has ("Plug-ins", "ScanOnStartup") && ! e.getSetting ("scanOnStartup", true), "scan on startup defaults to off");
            CHECK (e.shouldScanOnStartup(), "...but the very first run scans");
            e.markPluginsScanned();
            CHECK (! e.shouldScanOnStartup(), "no startup scan once one has been done");
            e.setSetting ("scanOnStartup", true);
            CHECK (e.shouldScanOnStartup(), "ticking 'scan on startup' turns it back on");
            e.setSetting ("scanOnStartup", false);
        }

        // a library: two amps and two cabs
        const auto lib = testDir.getChildFile ("Library");
        lib.getChildFile ("Amps").createDirectory();
        lib.getChildFile ("Cabs").createDirectory();
        model.copyFileTo (lib.getChildFile ("Amps/1 Clean Amp.nam"));
        model.copyFileTo (lib.getChildFile ("Amps/2 Lead Amp.nam"));
        model.copyFileTo (lib.getChildFile ("Amps/3 Audio Repair.nam"));
        for (auto name : { "1 Greenback.wav", "2 V30.wav" })
        {
            juce::AudioBuffer<float> ir (1, 256);
            ir.clear();
            ir.setSample (0, 0, 1.0f);
            ir.setSample (0, 9, 0.3f);
            juce::WavAudioFormat wav;
            auto out = lib.getChildFile ("Cabs").getChildFile (name).createOutputStream();
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.release(), 48000.0, 1, 24, {}, 0)))
                w->writeFromAudioSampleBuffer (ir, 0, 256);
        }
        const auto excludedAmp = lib.getChildFile ("Amps/3 Audio Repair.nam");
        {
            Engine e;
            e.setPaths ("modelPaths", juce::FileSearchPath (lib.getChildFile ("Amps").getFullPathName()));
            e.setPaths ("irPaths", juce::FileSearchPath (lib.getChildFile ("Cabs").getFullPathName()));
            e.setRandomExcluded (juce::Array<juce::File> { excludedAmp }, true);
            e.setIntSetting ("inspectorHeight", 412);
        }
        const auto iniText = iniPath.loadFileAsString();
        CHECK (iniText.contains ("[NAM Folders]") && iniText.contains (lib.getChildFile ("Amps").getFullPathName()), "folders are written to the ini");
        CHECK (iniText.contains ("[Randomise - excluded NAM models]") && iniText.contains ("3 Audio Repair.nam"), "randomise exclusions are written to the ini");

        Engine e;   // a fresh start reads it all back
        e.prepare (48000.0, 256);
        CHECK (e.getPaths ("modelPaths").getRawString (0) == lib.getChildFile ("Amps").getFullPathName(), "model folders come back on startup");
        CHECK (e.isRandomExcluded (excludedAmp) && ! e.isRandomExcluded (lib.getChildFile ("Amps/1 Clean Amp.nam")), "exclusions come back on startup");
        CHECK (e.getIntSetting ("inspectorHeight", 0) == 412, "window preferences come back on startup");
        CHECK (e.numRandomExcluded (0) == 1 && e.numRandomExcluded (1) == 0, "exclusion counts");

        // the dice never pick an unticked model
        {
            Randomiser rz (e);
            bool pickedExcluded = false, pickedOthers = false;
            for (int i = 0; i < 12; ++i)
            {
                rz.randomiseRig();
                visitNodes (e.getRoot(), [&] (Node& n)
                {
                    if (auto* nb = dynamic_cast<NamBlock*> (&n))
                    {
                        if (nb->getFile() == excludedAmp) pickedExcluded = true;
                        else if (nb->getFile().existsAsFile()) pickedOthers = true;
                    }
                });
            }
            CHECK (! pickedExcluded && pickedOthers, "randomise skips unticked models");
            e.setRandomExcluded (juce::Array<juce::File> { excludedAmp }, false);
            CHECK (! e.isRandomExcluded (excludedAmp), "ticking it again lets the dice pick it");
            e.setRandomExcluded (juce::Array<juce::File> { excludedAmp }, true);
        }

        // built-in effects can be left out of the dice too
        {
            e.setRandomExcluded (juce::Array<NodeType> { NodeType::Reverb, NodeType::Chorus, NodeType::Gate }, true);
            CHECK (e.isRandomExcluded (NodeType::Reverb) && ! e.isRandomExcluded (NodeType::Delay), "built-in effect exclusions");
            CHECK (iniPath.loadFileAsString().contains ("[Randomise - excluded built-in effects]")
                   && iniPath.loadFileAsString().contains ("=reverb"), "built-in exclusions are written to the ini");
            Randomiser rz (e);
            rz.options.usePlugins = false;
            bool found = false;
            for (int i = 0; i < 25; ++i)
            {
                rz.randomiseRig();
                visitNodes (e.getRoot(), [&] (Node& n)
                {
                    const auto t = n.getType();
                    found |= t == NodeType::Reverb || t == NodeType::Chorus || t == NodeType::Gate;
                });
            }
            CHECK (! found, "randomise never adds unticked built-in effects");
            e.setRandomExcluded (juce::Array<NodeType> { NodeType::Reverb, NodeType::Chorus, NodeType::Gate }, false);
            CHECK (e.numRandomExcluded (3) == 0, "ticking them again");
        }

        // portable: settings move next to the exe and back
        {
            const auto elsewhere = testDir.getChildFile ("portable/Rigsmith.ini");
            CHECK (e.moveSettingsTo (elsewhere) && elsewhere.existsAsFile()
                   && e.isRandomExcluded (excludedAmp) && e.ini.getFile() == elsewhere, "settings move to a new file and keep everything");
            CHECK (e.moveSettingsTo (iniPath) && e.ini.getFile() == iniPath, "and back");
        }

        // starting points
        e.loadFactory (0);
        e.loadFactory (1);
        std::vector<NodeType> got;
        for (auto& n : e.getRoot().nodes) got.push_back (n->getType());
        const std::vector<NodeType> classic { NodeType::Gate, NodeType::Overdrive, NodeType::Nam, NodeType::Ir,
                                              NodeType::Chorus, NodeType::Delay, NodeType::Reverb };
        CHECK (got == classic, "Classic Rig: gate, drive, amp, cab, chorus, delay, reverb");
        auto* amp = dynamic_cast<NamBlock*> (e.getRoot().nodes[2].get());
        auto* cab = dynamic_cast<IrBlock*> (e.getRoot().nodes[3].get());
        CHECK (amp != nullptr && amp->getFile() == lib.getChildFile ("Amps/1 Clean Amp.nam"), "Classic Rig loads the first amp in the library");
        CHECK (cab != nullptr && cab->getFile().getFileName() == "1 Greenback.wav", "...and the first cab");
        CHECK (e.presetName == "Classic Rig" && e.getFactoryIndex() == 1 && ! e.dirty, "Classic Rig is a clean, named starting point");
        bool finite = false;
        const float pk = runNoise (e, 60, 256, &finite);
        CHECK (finite && pk > 1.0e-4f, "Classic Rig makes sound");

        e.loadFactory (2);
        auto* split = e.getRoot().nodes.size() == 6 ? dynamic_cast<Split*> (e.getRoot().nodes[2].get()) : nullptr;
        CHECK (split != nullptr && split->branches.size() == 2 && split->getMode() == SplitMode::Parallel, "Dual Amp Rig splits into two parallel paths");
        if (split != nullptr && split->branches.size() == 2)
        {
            auto* a0 = dynamic_cast<NamBlock*> (split->branches[0]->chain->nodes[0].get());
            auto* a1 = dynamic_cast<NamBlock*> (split->branches[1]->chain->nodes[0].get());
            auto* c0 = dynamic_cast<IrBlock*> (split->branches[0]->chain->nodes[1].get());
            auto* c1 = dynamic_cast<IrBlock*> (split->branches[1]->chain->nodes[1].get());
            CHECK (a0 && a1 && c0 && c1, "each path is NAM -> IR");
            if (a0 && a1 && c0 && c1)
            {
                CHECK (a0->getFile() == lib.getChildFile ("Amps/1 Clean Amp.nam") && a1->getFile() == lib.getChildFile ("Amps/2 Lead Amp.nam"),
                       "it keeps the loaded amp and adds the next one in the folder");
                CHECK (c0->getFile() != c1->getFile() && c1->getFile().existsAsFile(), "two different cabs");
            }
        }
        CHECK (e.getRoot().nodes.back()->getType() == NodeType::Reverb, "Dual Amp Rig ends with the reverb");
        CHECK (runNoise (e, 60, 256, &finite) > 1.0e-4f && finite, "Dual Amp Rig makes sound");

        // scenes: A clean (no drive), B crunch (drive, no chorus), D dry
        auto* drive = e.getRoot().nodes[1].get();
        auto* chorus = e.getRoot().nodes[3].get();
        CHECK (drive->bypassed.load() && ! chorus->bypassed.load() && e.sceneNames[0] == "Clean", "scene A is Clean");
        e.setScene (1);
        CHECK (! drive->bypassed.load() && chorus->bypassed.load() && e.sceneNames[1] == "Crunch", "scene B is Crunch");
        e.setScene (3);
        CHECK (drive->bypassed.load() && e.getRoot().nodes.back()->bypassed.load(), "scene D is Dry");

        // Page Up / Down walks Init -> Classic -> Dual -> your presets
        e.loadFactory (0);
        e.stepPreset (1);
        CHECK (e.presetName == "Classic Rig", "next after Init is Classic Rig");
        e.stepPreset (1);
        CHECK (e.presetName == "Dual Amp Rig", "then Dual Amp Rig");
        e.stepPreset (-2);
        CHECK (e.isInit(), "and back to Init");
    }
    //==========================================================================
    std::printf ("--- v0.6: gear types, amp + cab, TONE3000\n");
    {
        const auto lib = testDir.getChildFile ("Gear");
        auto put = [&] (const juce::String& rel) { auto f = lib.getChildFile (rel); f.getParentDirectory().createDirectory(); model.copyFileTo (f); return f; };
        const auto head = put ("Amps/Plexi head.nam");
        const auto full = put ("Full Rigs/JCM800 amp + cab.nam");
        const auto pedal = put ("Pedals/Klon boost.nam");
        CHECK (classifyFile (head) == Gear::AmpHead && classifyFile (full) == Gear::AmpCab && classifyFile (pedal) == Gear::Pedal,
               "models sorted by folder and file names (head, amp + cab, pedal)");
        CHECK (classifyFile (model) == Gear::Unknown, "a model with no clues stays uncategorised");

        // the model's own metadata wins over names
        auto meta = lib.getChildFile ("Misc/thing.nam");
        meta.getParentDirectory().createDirectory();
        meta.replaceWithText (R"({"version":"0.5.4","metadata":{"gear_type":"amp_cab","name":"x"},"architecture":"LSTM"})");
        CHECK (classifyFile (meta) == Gear::AmpCab, "NAM metadata gear_type amp_cab means Amp + Cab");
        CHECK (gearFromNamMetadata ("pedal") == Gear::Pedal && gearFromNamMetadata ("studio") == Gear::Outboard
               && gearFromNamMetadata ("pedal_amp") == Gear::AmpHead, "NAM gear types map to TONE3000's");

        // a TONE3000 sidecar wins over everything
        ToneInfo ti; ti.valid = true; ti.id = 5; ti.title = "Big Room"; ti.gear = "space"; ti.format = "ir"; ti.creator = "someone";
        const auto spaceIr = lib.getChildFile ("Downloads/hall.wav");
        spaceIr.getParentDirectory().createDirectory();
        testDir.getChildFile ("Library/Cabs/1 Greenback.wav").copyFileTo (spaceIr);
        ti.write (spaceIr.getParentDirectory());
        CHECK (classifyFile (spaceIr) == Gear::Space, "a TONE3000 sidecar sets the type");
        CHECK (classifyFile (testDir.getChildFile ("Library/Cabs/1 Greenback.wav")) == Gear::Cab, "IRs default to cabinets");
        const auto roomIr = lib.getChildFile ("Rooms/studio A.wav");
        roomIr.getParentDirectory().createDirectory();
        testDir.getChildFile ("Library/Cabs/1 Greenback.wav").copyFileTo (roomIr);
        CHECK (classifyFile (roomIr) == Gear::Space, "IRs in a Rooms folder are spaces");

        // an Amp + Cab model switches the following cab IR off; an amp head doesn't
        Engine e;
        e.prepare (48000.0, 256);
        e.loadFactory (0);
        auto* nam = dynamic_cast<NamBlock*> (e.getRoot().nodes[1].get());
        auto* ir = dynamic_cast<IrBlock*> (e.getRoot().nodes[2].get());
        juce::String err;
        ir->loadIr (testDir.getChildFile ("Library/Cabs/1 Greenback.wav"), err);
        nam->loadModel (full, err);
        e.updateAutoSkips();
        CHECK (nam->getGear() == Gear::AmpCab && ir->autoSkipped.load() && ! ir->bypassed.load(), "cab IR turns itself off after an Amp + Cab model");
        CHECK (ir->getDetail().contains ("amp includes a cab"), "...and says why");
        nam->loadModel (head, err);
        e.updateAutoSkips();
        CHECK (! ir->autoSkipped.load(), "back on with an amp head");
        nam->loadModel (full, err);
        ir->skipAfterFullRig = false;
        e.updateAutoSkips();
        CHECK (! ir->autoSkipped.load(), "unless that's turned off for the IR");
        ir->skipAfterFullRig = true;
        e.updateAutoSkips();
        // audio: with the IR auto-off, the output equals the rig without the IR
        bool finite = false;
        CHECK (runNoise (e, 40, 256, &finite) > 1.0e-4f && finite, "Amp + Cab rig makes sound");
        auto st = e.getState (false);
        Engine e2; e2.prepare (48000.0, 256); e2.setState (st, false);
        auto* ir2 = dynamic_cast<IrBlock*> (e2.getRoot().nodes[2].get());
        CHECK (ir2 != nullptr && ir2->skipAfterFullRig.load() && ir2->autoSkipped.load(), "IR auto-off survives a preset round trip");
        CHECK (dynamic_cast<NamBlock*> (e2.getRoot().nodes[1].get())->getCategory() == Category::Amp, "an Amp + Cab model is an amp");
        nam->loadModel (pedal, err);
        CHECK (nam->getCategory() == Category::Drive, "a NAM pedal capture counts as a drive");

        // the dice: amps from amp captures only; amp + cab gets no IR; pedals can be drives
        e.setPaths ("modelPaths", juce::FileSearchPath (lib.getFullPathName()));
        e.setPaths ("irPaths", juce::FileSearchPath (testDir.getChildFile ("Library/Cabs").getFullPathName()));
        Randomiser rz (e);
        rz.options.usePlugins = false;
        bool pedalAsAmp = false, cabAfterFullRig = false, pedalAsDrive = false;
        for (int i = 0; i < 40; ++i)
        {
            rz.randomiseRig();
            std::function<void (Chain&)> scan = [&] (Chain& c)
            {
                for (size_t k = 0; k < c.nodes.size(); ++k)
                {
                    if (auto* nb = dynamic_cast<NamBlock*> (c.nodes[k].get()))
                    {
                        const bool nextIsIr = k + 1 < c.nodes.size() && dynamic_cast<IrBlock*> (c.nodes[k + 1].get()) != nullptr;
                        if (nb->getGear() == Gear::Pedal) { if (nextIsIr) pedalAsAmp = true; else pedalAsDrive = true; }
                        if (nb->getGear() == Gear::AmpCab && nextIsIr) cabAfterFullRig = true;
                    }
                    if (auto* sp = dynamic_cast<Split*> (c.nodes[k].get()))
                        for (auto& b : sp->branches) scan (*b->chain);
                }
            };
            scan (e.getRoot());
        }
        CHECK (! pedalAsAmp, "the dice never use a pedal capture as the amp");
        CHECK (! cabAfterFullRig, "the dice don't add a cab IR after an Amp + Cab model");
        CHECK (pedalAsDrive, "the dice sometimes use a NAM pedal capture as the drive");

        // TONE3000: sign in + pick a tone (browser simulated) -> download -> categorised in the library
        MockTone3000 mock;
        model.loadFileAsData (mock.modelBytes);
        CHECK (mock.start(), "mock TONE3000 server starts");
        e.ini.set ("TONE3000", "ApiOrigin", mock.origin());
        e.ini.set ("TONE3000", "PublishableKey", "t3k_pub_test");
        t3k::Client client (e);
        CHECK (! client.isConnected(), "not signed in to start with");

        juce::URL opened;
        client.openBrowser = [&] (const juce::URL& u)
        {
            opened = u;
            // the "browser": TONE3000 redirects back to Rigsmith's loopback address
            const auto params = u.getParameterNames(), values = u.getParameterValues();
            const auto redirect = values[params.indexOf ("redirect_uri")], state = values[params.indexOf ("state")];
            juce::Thread::launch ([redirect, state]
            {
                juce::URL back (redirect + "?code=CODE1&state=" + state + "&tone_id=42");
                back.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (3000));
            });
        };
        int pickedTone = -1;
        juce::String flowError = "pending";
        client.startFlow ("select_tone", "nam", {}, [&] (int id, juce::String error) { pickedTone = id; flowError = error; });
        for (int i = 0; i < 300 && flowError == "pending"; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto names = opened.getParameterNames(), vals = opened.getParameterValues();
        CHECK (vals[names.indexOf ("client_id")] == "t3k_pub_test" && vals[names.indexOf ("code_challenge_method")] == "S256"
               && vals[names.indexOf ("prompt")] == "select_tone" && vals[names.indexOf ("architecture")] == "2"
               && vals[names.indexOf ("redirect_uri")].startsWith ("http://127.0.0.1:"), "authorize URL: PKCE, select_tone, A2, loopback redirect");
        CHECK (flowError.isEmpty() && pickedTone == 42, "picking a tone on TONE3000 comes back to Rigsmith");
        CHECK (client.isConnected() && client.userName == "Guitar Ist", "signed in, with the user's name");
        CHECK (mock.lastTokenBody.contains ("code_verifier="), "the code is exchanged with the PKCE verifier");
        CHECK (iniPath.getSiblingFile ("tone3000.dat").existsAsFile()
               && ! iniPath.getSiblingFile ("tone3000.dat").loadFileAsString().contains ("AT1"), "the sign-in is stored, not in plain text");

        auto pump = [] (const std::function<bool()>& doneYet) { for (int i = 0; i < 500 && ! doneYet(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10); };
        t3k::Tone got; juce::String terr = "pending";
        client.getTone (42, [&] (t3k::Tone t, juce::String er) { got = t; terr = er; });
        pump ([&] { return terr != "pending"; });
        CHECK (terr.isEmpty() && got.title == "Plexi Crunch" && got.getGear() == Gear::AmpCab && got.creator == "tonemaker", "tone details");

        juce::Array<t3k::Model> models; juce::String merr = "pending";
        client.listModels (42, [&] (juce::Array<t3k::Model> m, juce::String er) { models = m; merr = er; });
        pump ([&] { return merr != "pending"; });
        CHECK (merr.isEmpty() && models.size() == 1 && models[0].name == "Crunch 5" && models[0].extension() == ".nam", "the tone's A2 models");

        juce::File downloaded; juce::String derr = "pending";
        client.download (got, models[0], [&] (juce::File f, juce::String er) { downloaded = f; derr = er; });
        pump ([&] { return derr != "pending"; });
        CHECK (derr.isEmpty() && downloaded.existsAsFile() && downloaded.getSize() == (juce::int64) mock.modelBytes.getSize(), "model downloaded with the sign-in");
        CHECK (client.libraryFolder() == juce::File (e.getPaths ("modelPaths").getRawString (0)).getChildFile ("TONE3000"),
               "downloads go to a TONE3000 folder inside the first NAM folder");
        CHECK (downloaded.isAChildOf (client.libraryFolder()) && downloaded.getParentDirectory().getParentDirectory().getFileName() == "Amp + Cab",
               "saved in <NAM folder>/TONE3000/Amp + Cab/<tone>");
        CHECK (classifyFile (downloaded) == Gear::AmpCab, "and categorised as Amp + Cab in the library");
        bool inPaths = false;
        auto mp = e.getPaths ("modelPaths");
        for (int i = 0; i < mp.getNumPaths(); ++i)
            inPaths |= client.libraryFolder().isAChildOf (mp.getRawString (i)) || juce::File (mp.getRawString (i)) == client.libraryFolder();
        CHECK (inPaths && mp.getNumPaths() == 1, "already inside the model folders: nothing added");
        NamBlock fromT3k;
        CHECK (fromT3k.loadModel (downloaded, err) && fromT3k.getName() == "Plexi Crunch" && fromT3k.getDetail() == "by tonemaker",
               "the block shows the tone's title and creator (the capture names no gear)");

        juce::Array<t3k::Tone> favs; juce::String ferr = "pending";
        client.listTones ("favorited", 1, [&] (juce::Array<t3k::Tone> t, juce::String er) { favs = t; ferr = er; });
        pump ([&] { return ferr != "pending"; });
        CHECK (ferr.isEmpty() && favs.size() == 1 && client.isInLibrary (favs[0]), "favourites list (and it knows it's downloaded)");

        client.signOut();
        CHECK (! client.isConnected() && ! iniPath.getSiblingFile ("tone3000.dat").existsAsFile(), "sign out forgets the sign-in");
        client.libraryFolder().getChildFile ("Amp + Cab").deleteRecursively();
    }
    testDir.deleteRecursively();

    //==========================================================================
    std::printf ("--- v0.7: captures grouped by gear, capture knob, dice icons\n");
    {
        // what a .nam says about itself, at the start (A1) or after the weights (A2)
        const auto dir = testDir.getChildFile ("Captures");
        dir.createDirectory();
        juce::String big;
        for (int i = 0; i < 30000; ++i) big << "0.123456,";
        const auto tailMeta = dir.getChildFile ("tail.nam");
        tailMeta.replaceWithText (R"({"version":"0.7.0","architecture":"SlimmableContainer","config":{"submodels":[{"model":{"metadata":{"name":"inner"}}}]},"weights":[)"
                                  + big + R"(0],"sample_rate":48000,"metadata":{"gear_make":"Darkglass Alpha Omega Ultra","gear_model":"Darkglass Alpha Omega Ultra","tone_type":"metal","gear_type":"pedal","t3k_id":413653,"name":"Omega - Omega-BITE G3","modeled_by":"leenykeany","t3k_gear":"pedal"}})");
        const auto ci = readCaptureInfo (tailMeta);
        CHECK (ci.name == "Omega - Omega-BITE G3" && ci.toneType == "metal" && ci.t3kId == 413653 && ci.modeledBy == "leenykeany",
               "metadata after the weights is found (A2), not a sub-model's");
        CHECK (ci.gearTitle() == "Darkglass Alpha Omega Ultra", "make and model the same: the name isn't said twice");
        CHECK (classifyFile (tailMeta) == Gear::Pedal, "gear_type at the end of the file sets the type");
        CHECK (readCaptureInfo (models.getChildFile ("wavenet.nam")).gearTitle() == "Darkglass Electronics Microtubes 900 v2",
               "different make and model are joined");
        CHECK (toneTypeName ("hi_gain") == "Hi-gain" && toneTypeName ("2000s") == "2000s" && toneTypeName ("metal") == "Metal", "tone type names");

        // a small library: one amp with three captures (two folders), a pedal with two, one plain file
        auto writeCapture = [&] (const juce::String& rel, const juce::String& make, const juce::String& type,
                                 const juce::String& tone, const juce::String& name)
        {
            auto v = juce::JSON::parse (models.getChildFile ("wavenet.nam"));
            auto* meta = v.getProperty ("metadata", {}).getDynamicObject();
            meta->setProperty ("gear_make", make);
            meta->setProperty ("gear_model", make);
            meta->setProperty ("gear_type", type);
            meta->setProperty ("tone_type", tone);
            meta->setProperty ("name", name);
            meta->setProperty ("modeled_by", "tester");
            auto f = dir.getChildFile ("Lib").getChildFile (rel);
            f.getParentDirectory().createDirectory();
            f.replaceWithText (juce::JSON::toString (v, true));
            return f;
        };
        const juce::String jcm ("Marshall JCM800 Zakk Wyle Signature");
        const auto lead = writeCapture ("Pack A/jcm 3.nam", jcm, "amp", "2000s", "JCM - Lead");
        const auto clean = writeCapture ("Pack A/jcm 1.nam", jcm, "amp", "clean", "JCM - Clean");
        const auto crunch = writeCapture ("Pack B/zw crunch.nam", jcm, "amp", "2000s", "JCM - Crunch");
        const auto g3 = writeCapture ("Pedals/omega g3.nam", "Darkglass Alpha Omega Ultra", "pedal", "metal", "Omega - Omega-BITE G3");
        const auto g5 = writeCapture ("Pedals/omega g5.nam", "Darkglass Alpha Omega Ultra", "pedal", "metal", "Omega - Omega-BITE G5");
        const auto plain = dir.getChildFile ("Lib/Amps/plain head.nam");
        plain.getParentDirectory().createDirectory();
        model.copyFileTo (plain);

        Engine e;
        e.prepare (48000.0, 256);
        e.setPaths ("modelPaths", juce::FileSearchPath (dir.getChildFile ("Lib").getFullPathName()));
        const auto& set = e.captures.get (false);
        CHECK (set.files.size() == 6 && set.groups.size() == 3, "6 files, 3 pieces of gear");
        auto amp = e.captures.groupFor (crunch);
        CHECK (amp != nullptr && amp->title == jcm && amp->size() == 3 && amp->gear == Gear::AmpHead, "one amp head, three captures from two folders");
        CHECK (amp != nullptr && amp->files[0] == clean && amp->files[1] == crunch && amp->files[2] == lead, "captures ordered by name");
        CHECK (amp != nullptr && amp->shortLabels == juce::StringArray ({ "Clean", "Crunch", "Lead" }), "knob labels drop the shared part");
        CHECK (amp != nullptr && amp->toneTypes.size() == 2 && amp->creatorText() == "tester", "tone types and creator collected");
        auto pedal = e.captures.groupFor (g5);
        CHECK (pedal != nullptr && pedal->size() == 2 && pedal->gear == Gear::Pedal
               && pedal->shortLabels == juce::StringArray ({ "G3", "G5" }), "the pedal's captures, labelled G3 and G5");
        CHECK (set.groups.front()->gear == Gear::AmpHead && set.groups.back()->gear == Gear::Pedal, "groups sorted by type: amps first");
        auto single = e.captures.groupFor (plain);
        CHECK (single != nullptr && single->size() == 1, "a file without metadata stands alone");
        const auto outside = testDir.getChildFile ("Elsewhere/x.nam");
        outside.getParentDirectory().createDirectory();
        lead.copyFileTo (outside);
        CHECK (e.captures.groupFor (outside) != nullptr, "a file outside the library still gets a group (its folder)");

        // the block shows the gear, the capture is the detail
        NamBlock nb;
        juce::String err;
        nb.prepare (48000.0, 256);
        CHECK (nb.loadModel (crunch, err), "loads a grouped capture");
        CHECK (nb.getName() == jcm && nb.getCaptureName() == "JCM - Crunch", "block name is the gear, detail the capture");
        CHECK (nb.getInfo().toneType == "2000s" && nb.getInfo().modeledBy == "tester", "tone type and creator on the block");

        // the cache survives a restart
        saveCaptureCache();
        CHECK (testDir.getChildFile ("Rigsmith.captures").existsAsFile(), "Rigsmith.captures written next to the ini");

        // dice: every piece of gear gets a fair chance, excluded captures never come up
        e.setRandomExcluded (juce::Array<juce::File> { lead }, true);
        Randomiser rz (e);
        rz.options.usePlugins = false;
        auto* slot = e.insert (std::make_unique<NamBlock>(), e.getRoot(), 0);
        auto id = slot->id;
        int jcmPicks = 0, plainPicks = 0, leadPicks = 0, pedalPicks = 0;
        for (int i = 0; i < 60; ++i)
        {
            auto* r = rz.randomiseType (id);
            if (r == nullptr) continue;
            id = r->id;
            auto* n = dynamic_cast<NamBlock*> (r);
            if (n == nullptr) continue;
            const auto f = n->getFile();
            if (amp->files.contains (f)) ++jcmPicks;
            if (f == plain) ++plainPicks;
            if (f == lead) ++leadPicks;
            if (pedal->files.contains (f)) ++pedalPicks;
        }
        CHECK (leadPicks == 0, "a capture left out of the dice is never picked");
        CHECK (jcmPicks > 8 && plainPicks > 8, "the amp with several captures and the single one both come up");
        CHECK (pedalPicks == 0, "a pedal capture isn't rolled as the amp");
    }

    //==========================================================================
    std::printf ("--- v0.8: where things are saved\n");
    {
        Engine e;
        const auto nam = testDir.getChildFile ("Where/NAM"), irs = testDir.getChildFile ("Where/IRs");
        nam.createDirectory(); irs.createDirectory();
        e.setPaths ("modelPaths", juce::FileSearchPath (nam.getFullPathName()));
        e.setPaths ("irPaths", juce::FileSearchPath (irs.getFullPathName()));
        CHECK (! e.hasCustomDownloadFolder() && e.getDownloadFolder (false) == nam.getChildFile ("TONE3000")
               && e.getDownloadFolder (true) == irs.getChildFile ("TONE3000"), "TONE3000 downloads go inside the NAM / IR folders");
        e.setFolder ("downloadFolder", testDir.getChildFile ("Where/Downloads"));
        CHECK (e.hasCustomDownloadFolder() && e.getDownloadFolder (true) == testDir.getChildFile ("Where/Downloads"), "or a folder of your choosing");
        e.setFolder ("downloadFolder", {});
        CHECK (! e.hasCustomDownloadFolder(), "and back to the NAM / IR folders");

        e.setFolder ("presetFolder", nam.getChildFile ("Presets"));
        e.setFolder ("blockPresetFolder", nam.getChildFile ("Block Presets"));
        CHECK (e.getPresetFolder() == nam.getChildFile ("Presets"), "rig presets can live with the NAM models");
        auto* comp = e.insert (e.createNode (NodeType::Comp), e.getRoot(), 0);
        CHECK (e.saveBlockPreset (*comp, "Squash") && nam.getChildFile ("Block Presets").findChildFiles (juce::File::findFiles, true, "*.rbp").size() == 1,
               "block presets are saved in the chosen folder");
        Engine e2;
        CHECK (e2.getBlockPresetRoot() == nam.getChildFile ("Block Presets") && e2.getPresetFolder() == nam.getChildFile ("Presets"),
               "preset folders come back on startup (Rigsmith.ini)");

        // an older download in Documents\Rigsmith\TONE3000 is still recognised
        t3k::Client client (e);
        t3k::Tone t; t.id = 777; t.title = "Old Tone"; t.creator = "x"; t.gear = "amp"; t.format = "nam";
        const auto oldFolder = t3k::Client::legacyLibraryFolder().getChildFile ("Amp head").getChildFile ("Old Tone - x [777]");
        ToneInfo ti; ti.valid = true; ti.id = 777; ti.title = "Old Tone"; ti.gear = "amp"; ti.format = "nam";
        ti.write (oldFolder);
        CHECK (client.isInLibrary (t) && client.folderFor (t) == oldFolder, "tones downloaded to the old place still count as downloaded");
        oldFolder.deleteRecursively();
        CHECK (client.folderFor (t).isAChildOf (nam.getChildFile ("TONE3000")), "new downloads go to the NAM folder");
    }

    //==========================================================================
    std::printf ("--- v0.9: capture sweep, Gain, trims and meters, safe scanning\n");
    {
        // render the same input through a block (fresh state each time)
        auto render = [] (Block& b, int blocks, float amp = 0.25f)
        {
            b.prepare (48000.0, 256);
            juce::Random rnd (7);
            juce::AudioBuffer<float> buf (2, 256);
            std::vector<float> out;
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < 256; ++i)
                {
                    // a guitar-ish signal: a decaying sine plus a little noise
                    const float x = amp * (std::sin ((float) (k * 256 + i) * 0.031f) * std::exp (-(float) (k * 256 + i) / 30000.0f)
                                           + (rnd.nextFloat() - 0.5f) * 0.05f);
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
                b.process (buf, 256);
                for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
            }
            return out;
        };
        auto rmsDb = [] (const std::vector<float>& y)
        {
            double s = 0; for (size_t i = y.size() / 4; i < y.size(); ++i) s += (double) y[i] * y[i];
            return 10.0 * std::log10 (s / (double) (y.size() - y.size() / 4) + 1.0e-12);
        };

        const auto a1 = models.getChildFile ("wavenet_a1_standard.nam"), lstm = models.getChildFile ("lstm.nam"),
                   a2 = models.getChildFile ("A2.nam");
        juce::String err;
        for (auto f : { a1, a2 })
        {
            NamBlock t;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            t.loadModel (f, err);
            std::printf ("      loading %s (with its gain curve): %.0f ms\n", f.getFileName().toRawUTF8(),
                         juce::Time::getMillisecondCounterHiRes() - t0);
        }

        // the sweep: halfway between two captures is half of each
        const juce::Array<juce::File> pair { a1, lstm };
        NamBlock at0, at1, half;
        CHECK (at0.loadCaptures (pair, 0.0f, err) && at1.loadCaptures (pair, 1.0f, err) && half.loadCaptures (pair, 0.5f, err),
               "loads two captures to sweep between");
        CHECK (half.getNumCaptures() == 2 && std::abs (half.getCapturePosition() - 0.5f) < 1.0e-4f, "position 0.5 of 2 captures");
        const auto y0 = render (at0, 40), y1 = render (at1, 40), yh = render (half, 40);
        float worst = 0, level = 0;
        for (size_t i = 0; i < yh.size(); ++i) { worst = juce::jmax (worst, std::abs (yh[i] - 0.5f * (y0[i] + y1[i]))); level = juce::jmax (level, std::abs (yh[i])); }
        CHECK (level > 1.0e-3f && worst < 1.0e-3f * juce::jmax (1.0f, level), "halfway is an even blend of the two captures (a morph, not a switch)");
        CHECK (half.formatValue (NamBlock::pCapture).contains ("50%"), "the knob reads as a blend");

        // sweeping while playing stays finite and click-free (no jumps bigger than the signal itself)
        {
            NamBlock sweep;
            sweep.loadCaptures (pair, 0.0f, err);
            sweep.prepare (48000.0, 128);
            juce::AudioBuffer<float> buf (2, 128);
            float prev = 0, maxJump = 0, peak = 0;
            bool finite = true;
            for (int k = 0; k < 400; ++k)
            {
                sweep.setCapturePosition ((float) k / 399.0f);
                for (int i = 0; i < 128; ++i) { const float x = 0.2f * std::sin ((float) (k * 128 + i) * 0.02f); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                sweep.process (buf, 128);
                for (int i = 0; i < 128; ++i)
                {
                    const float y = buf.getSample (0, i);
                    finite &= std::isfinite (y);
                    if (k > 4) maxJump = juce::jmax (maxJump, std::abs (y - prev));
                    peak = juce::jmax (peak, std::abs (y));
                    prev = y;
                }
            }
            CHECK (finite && peak > 1.0e-3f && maxJump < peak * 0.5f, "a full sweep while playing: no clicks");
        }

        // edit the list: reorder, drop one, keep the knob on the same capture
        NamBlock list;
        list.loadCaptures ({ a1, lstm, a2 }, 1.0f, err);
        CHECK (list.getNumCaptures() == 3 && list.getFile() == lstm, "three captures, on the second");
        list.loadCaptures ({ lstm, a1 }, 0.0f, err);
        CHECK (list.getNumCaptures() == 2 && list.getCaptureFiles()[0] == lstm && list.getFile() == lstm, "reordered and trimmed, same capture");

        // saved with the rig, fractions and all
        list.setCapturePosition (0.25f);
        NamBlock back;
        back.fromState (list.toState());
        CHECK (back.getCaptureFiles() == list.getCaptureFiles() && std::abs (back.getCapturePosition() - 0.25f) < 1.0e-3f,
               "the sweep and its position are saved with the rig");

        // Gain: much more drive, much less of a volume jump than turning the input up
        NamBlock g5, g10, hot;
        g5.loadModel (a1, err); g10.loadModel (a1, err); hot.loadModel (a1, err);
        g10.set (NamBlock::pGain, 10.0f);
        hot.set (NamBlock::pInput, 24.0f);
        CHECK (std::abs (g5.getGainCompensationDb()) < 1.0e-3f && g10.getGainDb() > 23.0f, "Gain 5 is as captured, 10 is +24 dB of drive");
        const double l5 = rmsDb (render (g5, 60)), l10 = rmsDb (render (g10, 60)), lHot = rmsDb (render (hot, 60));
        std::printf ("      Gain 5: %.1f dB, Gain 10: %.1f dB, Input +24: %.1f dB\n", l5, l10, lHot);
        CHECK (l10 - l5 < 7.6 && l10 - l5 > -3.0, "turning Gain up adds drive, the level only rises a little");
        NamBlock g0;
        g0.loadModel (a1, err);
        g0.set (NamBlock::pGain, 0.0f);
        const double l0 = rmsDb (render (g0, 60));
        std::printf ("      Gain 0: %.1f dB (compensation %+.1f dB)\n", l0, g0.getGainCompensationDb());
        CHECK (g0.getGainCompensationDb() >= 0.0f && l0 > l5 - 7.6, "turned down, it cleans up without vanishing (the measured curve)");
        {
            // a clean, linear "amp": the curve has to hold the level up when Gain goes down
            NamBlock lin;
            lin.loadModel (models.getChildFile ("lstm.nam"), err);
            lin.set (NamBlock::pGain, 0.0f);
            CHECK (lin.getGainCompensationDb() >= 0.0f, "never quieter than the capture itself would be");
        }

        // trims and meters on any block
        auto gainBlock = std::make_unique<GainBlock>();
        auto& gb = *gainBlock;
        const auto plain = render (gb, 20);
        gb.outTrimDb = -6.0f;
        const auto trimmed = render (gb, 20);
        CHECK (std::abs ((rmsDb (trimmed) - rmsDb (plain)) + 6.0) < 0.2, "the OUT handle trims the block's output");
        CHECK (gb.peakIn.exchange (0.0f) > 0.0f && gb.peakOut.exchange (0.0f) > 0.0f, "in / out levels are metered");
        auto saved = gb.toState();
        GainBlock gb2;
        gb2.fromState (saved);
        CHECK (std::abs (gb2.outTrimDb.load() + 6.0f) < 1.0e-4f, "trims are saved with the rig");

        // the engine loads a gear's captures ready to sweep
        Engine e;
        e.setPaths ("modelPaths", juce::FileSearchPath (testDir.getChildFile ("Captures/Lib").getFullPathName()));
        NamBlock gear;
        const auto crunch = testDir.getChildFile ("Captures/Lib/Pack B/zw crunch.nam");
        CHECK (e.loadGear (gear, crunch, err) && gear.getNumCaptures() == 3 && gear.getFile() == crunch,
               "loading one capture brings its gear's other captures into the sweep, on the one chosen");

        // plug-in scanning in this process: what's being scanned is written down first, then cleared
        {
            juce::VST3PluginFormat vst3;
            const auto notAPlugin = testDir.getChildFile ("Fake.vst3");
            notAPlugin.replaceWithText ("not a plug-in");
            juce::StringArray skipped, log;
            ScanPolicy policy;
            policy.isSkipped = [&] (const juce::String& f) { return skipped.contains (f); };
            policy.onStarting = [&] (const juce::String& f) { log.add ("start " + f); };
            policy.onFinished = [&] { log.add ("done"); };
            InProcessScanner sc (policy);
            juce::OwnedArray<juce::PluginDescription> found;
            CHECK (sc.findPluginTypesFor (vst3, found, notAPlugin.getFullPathName()) && found.isEmpty()
                   && log.size() == 2 && log[0].endsWith ("Fake.vst3") && log[1] == "done",
                   "an in-process scan writes the plug-in down before touching it and clears it after");
            skipped.add (notAPlugin.getFullPathName());
            log.clear();
            CHECK (! sc.findPluginTypesFor (vst3, found, notAPlugin.getFullPathName()) && log.isEmpty(),
                   "a plug-in that crashed before is not scanned again");
            log.clear();
            CHECK (sc.findPluginTypesFor (vst3, found, "C:\\VST3\\Rigsmith.vst3") && log.isEmpty(), "Rigsmith is never loaded by a scan");
        }
        {
            // in the DAW: the plug-in being scanned is in Rigsmith.ini until it's done, so a crash is found next start
            Engine pedal;
            pedal.setupPluginScanning (false);
            const auto victim = testDir.getChildFile ("Crashy.vst3").getFullPathName();
            {
                IniFile direct (testDir.getChildFile ("Rigsmith.ini"));
                direct.set ("Plug-ins", "Scanning", victim);   // as if the DAW died mid-scan
            }
            Engine next;
            next.setupPluginScanning (false);
            CHECK (next.getSkippedPlugins().contains (victim) && next.takeScanCrashNotice() == victim,
                   "a crash during an in-DAW scan is found on the next start and that plug-in is skipped from then on");
            next.clearSkippedPlugins();
            CHECK (next.getSkippedPlugins().isEmpty(), "and can be tried again from the menu");
        }
        {
            juce::AudioPluginFormatManager fm;
            addPluginFormats (fm);
            bool names = fm.getNumFormats() > 0;
            for (auto* f : fm.getFormats()) names &= ! f->fileMightContainThisPluginType ("C:\\Program Files\\Common Files\\VST3\\Rigsmith.vst3");
            CHECK (names, "Rigsmith.vst3 is never even looked at by a scan");
        }
        CHECK (isRigsmithPlugin ("C:\\Program Files\\Common Files\\VST3\\Rigsmith.vst3")
               && ! isRigsmithPlugin ("C:\\Program Files\\Common Files\\VST3\\ValhallaRoom.vst3"), "Rigsmith never scans itself");
    }

    //==========================================================================
    std::printf ("--- v0.10: IR sweeps, adding captures, folders\n");
    {
        juce::String err;
        // two IRs: a plain impulse and a two-tap one
        const auto dir = testDir.getChildFile ("IrSweep");
        dir.createDirectory();
        auto writeIr = [&] (const juce::String& name, int tap)
        {
            juce::AudioBuffer<float> ir (1, 256);
            ir.clear();
            ir.setSample (0, 0, 1.0f);
            if (tap > 0) ir.setSample (0, tap, 0.8f);
            juce::WavAudioFormat wav;
            const auto f = dir.getChildFile (name);
            auto out = f.createOutputStream();
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.release(), 48000.0, 1, 24, {}, 0)))
                w->writeFromAudioSampleBuffer (ir, 0, 256);
            return f;
        };
        const auto irA = writeIr ("A close.wav", 0), irB = writeIr ("B room.wav", 40);

        auto render = [] (IrBlock& b)
        {
            b.prepare (48000.0, 256);
            juce::AudioBuffer<float> buf (2, 256);
            // let the convolvers load their IRs (they do it in the background)
            for (int i = 0; i < 40; ++i) { buf.clear(); b.process (buf, 256); juce::Thread::sleep (10); }
            std::vector<float> out;
            juce::Random rnd (3);
            for (int k = 0; k < 20; ++k)
            {
                for (int i = 0; i < 256; ++i) { const float x = (rnd.nextFloat() - 0.5f) * 0.5f; buf.setSample (0, i, x); buf.setSample (1, i, x); }
                b.process (buf, 256);
                for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
            }
            return out;
        };
        const juce::Array<juce::File> pair { irA, irB };
        IrBlock a0, a1, half;
        CHECK (a0.loadCaptures (pair, 0.0f, err) && a1.loadCaptures (pair, 1.0f, err) && half.loadCaptures (pair, 0.5f, err),
               "an IR block takes several IRs");
        const auto y0 = render (a0), y1 = render (a1), yh = render (half);
        float worst = 0, level = 0, diff = 0;
        for (size_t i = 0; i < yh.size(); ++i)
        {
            worst = juce::jmax (worst, std::abs (yh[i] - 0.5f * (y0[i] + y1[i])));
            level = juce::jmax (level, std::abs (yh[i]));
            diff = juce::jmax (diff, std::abs (y0[i] - y1[i]));
        }
        CHECK (level > 1.0e-3f && diff > 1.0e-3f && worst < 2.0e-3f * juce::jmax (1.0f, level), "halfway between two IRs is an even blend");
        CHECK (half.formatValue (IrBlock::pCapture).contains ("50%") && half.getNumCaptures() == 2, "and the knob reads as a blend");
        IrBlock back;
        back.fromState (half.toState());
        CHECK (back.getCaptureFiles() == pair && std::abs (back.getCapturePosition() - 0.5f) < 1.0e-3f, "an IR sweep is saved with the rig");

        // adding captures: new ones go on the end, the knob stays put, nothing twice, only the right kind
        Engine e;
        const auto a1nam = models.getChildFile ("wavenet_a1_standard.nam"), lstm = models.getChildFile ("lstm.nam");
        NamBlock amp;
        amp.loadCaptures ({ a1nam }, 0.0f, err);
        CHECK (e.addCaptures (amp, { lstm, a1nam, irA }, err) && amp.getCaptureFiles() == juce::Array<juce::File> ({ a1nam, lstm })
               && amp.getFile() == a1nam, "adding captures from other gear: appended, no duplicates, the knob stays on its capture");
        IrBlock cab;
        cab.loadCaptures ({ irA }, 0.0f, err);
        CHECK (e.addCaptures (cab, { irB }, err) && cab.getNumCaptures() == 2, "IRs can be added to an IR sweep");

        // a whole folder: every capture in it, in name order
        const auto folder = Engine::capturesInFolder (dir, true);
        CHECK (folder.size() == 2 && folder[0] == irA && folder[1] == irB, "a folder brings all its IRs, in order");
        const auto lib = Engine::capturesInFolder (testDir.getChildFile ("Captures/Lib"), false);
        CHECK (lib.size() == 6, "and all its .nam captures, sub-folders included");
    }

    //==========================================================================
    std::printf ("--- v0.11: modulation, the new gate\n");
    {
        auto feed = [] (Engine& e, int blocks, std::function<float (int)> sig)
        {
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            static int t = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 256; ++i) { const float x = sig (t++); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                e.process (buf, midi);
            }
        };

        // a knob's own value vs what the sound uses
        {
            GainBlock g;
            const int p = 0;
            const float base = g.getBase (p);
            g.modulate (p, 0.25f);
            CHECK (g.get (p) != base && g.getBase (p) == base && g.isModulated (p), "modulation moves the sound, not the knob");
            g.set (p, g.getSpecs()[0].min);
            CHECK (g.getBase (p) == g.getSpecs()[0].min && g.get (p) != g.getBase (p), "turning a modulated knob keeps the modulation on top");
            g.unmodulate (p);
            CHECK (g.get (p) == g.getBase (p) && ! g.isModulated (p), "removing it puts the sound back on the knob");
            CHECK ((float) g.toState().getProperty ("p_" + g.getSpecs()[0].id) == g.getBase (p), "presets save the knob, not the modulated value");
        }

        Engine e;
        e.prepare (48000.0, 256);
        auto* eq = dynamic_cast<Block*> (e.insert (e.createNode (NodeType::Eq), e.getRoot(), 0));
        const int mid = EqBlock::pMid;
        {
            // LFO 1 (sine, 1 Hz) on the EQ's mid
            auto& lfo = e.mods.source (0);
            lfo.set (LfoMod::pRate, 5.0f);
            e.addModRoute (0, eq->id, mid, 0.5f);
            float lo = 1e9f, hi = -1e9f;
            for (int k = 0; k < 60; ++k)
            {
                feed (e, 2, [] (int) { return 0.0f; });
                lo = juce::jmin (lo, eq->get (mid)); hi = juce::jmax (hi, eq->get (mid));
            }
            CHECK (hi - lo > 10.0f && eq->getBase (mid) == 0.0f, "an LFO sweeps a block's knob while the knob stays put");
            CHECK (e.mods.routesTo (eq->id, mid).size() == 1 && e.mods.reach (eq->id, mid).getLength() > 0.99f, "the route is listed, with its reach");
            e.addModRoute (0, eq->id, mid, 0.2f);
            CHECK (e.mods.routes.size() == 1 && std::abs (e.mods.routes[0].amount - 0.2f) < 1e-6f, "dropping the same source again just changes the amount");

            // saved with the preset, and back
            const auto state = e.getState (false);
            e.removeModRoute (0);
            CHECK (e.mods.routes.empty() && eq->get (mid) == eq->getBase (mid), "removing a route returns the knob to normal");
            lfo.set (LfoMod::pRate, 1.0f);
            e.setState (state, false);
            auto* eq2 = dynamic_cast<Block*> (e.findNode (eq->id));
            CHECK (eq2 != nullptr && e.mods.routes.size() == 1 && e.mods.routes[0].target == eq2->id
                   && std::abs (e.mods.source (0).getBase (LfoMod::pRate) - 5.0f) < 1e-4f, "routes and source settings are saved with the preset");
            eq = eq2;
        }
        {
            // an envelope speeding up an LFO: modulators modulate each other
            auto& env = e.mods.source (3);
            env.set (EnvMod::pAttack, 0.0f);
            env.set (EnvMod::pSustain, 100.0f);
            env.set (EnvMod::pThreshold, -40.0f);
            auto& lfo2 = e.mods.source (1);
            e.addModRoute (3, ModMatrix::targetFor (1), LfoMod::pRate, 0.6f);
            feed (e, 40, [] (int) { return 0.0f; });
            const float quiet = lfo2.get (LfoMod::pRate);
            feed (e, 40, [] (int t) { return 0.5f * std::sin (t * 0.05f); });
            const float playing = lfo2.get (LfoMod::pRate);
            CHECK (playing > quiet * 3.0f && std::abs (lfo2.getBase (LfoMod::pRate) - quiet) < 1.0e-3f, "playing opens the envelope, which speeds up the LFO");
            CHECK (env.value() > 0.9f, "the envelope sustains while the note rings");
            feed (e, 200, [] (int) { return 0.0f; });
            CHECK (env.value() < 0.05f, "and releases when it stops");
            CHECK (e.describeModTarget (ModMatrix::targetFor (1), LfoMod::pRate) == "LFO 2 > Rate", "routes describe where they go");
        }
        {
            // the sequencer steps through its values in time
            auto& seq = dynamic_cast<SeqMod&> (e.mods.source (6));
            seq.set (SeqMod::pSteps, 4.0f);
            seq.set (SeqMod::pSync, 0.0f);   // free, 8 steps a second
            seq.set (SeqMod::pRate, 8.0f);
            for (int i = 0; i < 4; ++i) seq.set (SeqMod::pStep1 + i, (float) i / 3.0f);
            e.addModRoute (6, eq->id, EqBlock::pLow, 0.5f);
            std::set<int> seen;
            for (int k = 0; k < 60; ++k) { feed (e, 1, [] (int) { return 0.0f; }); seen.insert (juce::roundToInt (seq.value() * 3.0f)); }
            CHECK (seen.size() == 4, "the sequencer plays all its steps");
        }
        {
            // the input level, and the pitch you play
            auto& lvl = e.mods.source (7);
            feed (e, 40, [] (int t) { return 0.3f * std::sin (t * 0.02f); });
            const float loud = lvl.value();
            feed (e, 300, [] (int t) { return 0.003f * std::sin (t * 0.02f); });
            CHECK (loud > 0.6f && lvl.value() < 0.2f, "LEVEL follows how hard you play");
            e.mods.watching = true;
            for (int k = 0; k < 20; ++k)
            {
                feed (e, 4, [] (int t) { return 0.4f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) t / 48000.0f); });
                e.mods.analysePitch();
            }
            CHECK (std::abs (e.mods.pitchHz.load() - 220.0f) < 3.0f, "PITCH hears the note (220 Hz)");
            auto& pm = dynamic_cast<PitchMod&> (e.mods.source (8));
            const float expect = std::log (220.0f / pm.get (PitchMod::pLow)) / std::log (pm.get (PitchMod::pHigh) / pm.get (PitchMod::pLow));
            CHECK (std::abs (pm.value() - expect) < 0.05f, "and maps it between its low and high notes");
            e.mods.watching = false;
        }
        {
            // a block that's deleted takes its routes with it
            const auto id = eq->id;
            e.remove (id);
            for (int k = 0; k < 60; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            bool any = false;
            for (auto& r : e.mods.routes) any |= r.target == id;
            CHECK (! any, "routes to a deleted block are dropped");
        }

        // the gate: Basic and Advanced
        {
            GateBlock gate;
            CHECK (gate.panelOrder().size() == 4, "Basic: View, Threshold, Release, Range");
            gate.set (GateBlock::pView, 1.0f);
            const auto adv = gate.panelOrder().size();
            gate.set (GateBlock::pBands, 1.0f);
            CHECK (adv > 10 && gate.panelOrder().size() == adv + 2, "Advanced shows the rest, and Dual adds the high band's range and crossover");
            gate.set (GateBlock::pBands, 0.0f);

            gate.prepare (48000.0, 256);
            juce::AudioBuffer<float> buf (2, 256);
            auto run = [&] (float amp, int blocks, const float* key = nullptr)
            {
                float pk = 0;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 256; ++i) { const float x = amp * std::sin ((float) i * 0.3f); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                    RigKey::data = key; RigKey::length = key != nullptr ? 256 : 0;
                    gate.process (buf, 256);
                    RigKey::data = nullptr; RigKey::length = 0;
                    if (b >= blocks - 5) pk = juce::jmax (pk, buf.getMagnitude (0, 0, 256));
                }
                return pk;
            };
            CHECK (run (0.0005f, 40) < 0.0001f, "quiet hiss is gated");
            CHECK (run (0.3f, 20) > 0.25f, "playing opens it");

            // keyed from the rig's input: a loud signal stays shut while the guitar is silent
            gate.set (GateBlock::pKey, 1.0f);
            std::vector<float> silent (256, 0.0f), loud (256, 0.5f);
            CHECK (run (0.3f, 150, silent.data()) < 0.01f, "keyed from the rig input: closed while the guitar is silent");
            CHECK (run (0.3f, 20, loud.data()) > 0.25f, "open while it plays");
            gate.set (GateBlock::pKey, 0.0f);

            gate.set (GateBlock::pLookahead, 5.0f);
            run (0.3f, 1);
            CHECK (gate.getLatency() == 240, "lookahead is reported as latency (5 ms at 48k)");
            gate.set (GateBlock::pLookahead, 0.0f);
            run (0.3f, 1);
        }
        {
            // dual band: the highs shut further than the lows
            GateBlock gate;
            gate.set (GateBlock::pView, 1.0f);
            gate.set (GateBlock::pBands, 1.0f);
            gate.set (GateBlock::pRange, -6.0f);
            gate.set (GateBlock::pHighRange, -60.0f);
            gate.set (GateBlock::pCrossover, 1000.0f);
            gate.prepare (48000.0, 256);
            auto level = [&] (float hz)
            {
                juce::AudioBuffer<float> buf (2, 256);
                float pk = 0;
                int t = 0;
                for (int b = 0; b < 80; ++b)
                {
                    for (int i = 0; i < 256; ++i, ++t)
                    {
                        const float x = 0.0005f * std::sin (juce::MathConstants<float>::twoPi * hz * (float) t / 48000.0f);
                        buf.setSample (0, i, x); buf.setSample (1, i, x);
                    }
                    gate.process (buf, 256);
                    if (b > 60) pk = juce::jmax (pk, buf.getMagnitude (0, 0, 256));
                }
                return gainToDb (pk / 0.0005f);
            };
            const float lowDb = level (100.0f), highDb = level (8000.0f);
            CHECK (lowDb > -9.0f && lowDb < -3.0f && highDb < -40.0f, "dual band: closed, the lows drop by their range, the highs by theirs");
        }
    }

    //==========================================================================
    std::printf ("--- v0.11.1: factory presets, splits\n");
    {
        Engine e;
        e.prepare (48000.0, 256);
        int types = 0, presets = 0;
        juce::StringArray bad;
        for (auto& [group, list] : builtInGroups())
            for (auto t : list)
            {
                auto n = e.createNode (t);
                auto* b = dynamic_cast<Block*> (n.get());
                if (b == nullptr || t == NodeType::Tuner) continue;
                const auto& fp = factoryPresets (t);
                ++types;
                if (fp.size() < 5) bad.add (builtInShortName (t) + ": only " + juce::String ((int) fp.size()));
                for (auto& p : fp)
                {
                    ++presets;
                    for (auto& [pid, v] : p.values)
                    {
                        const int i = b->indexOf (pid);
                        if (i < 0) { bad.add (builtInShortName (t) + " / " + p.name + ": no parameter '" + pid + "'"); continue; }
                        const auto& sp = b->getSpecs()[(size_t) i];
                        if (v < sp.min || v > sp.max) bad.add (builtInShortName (t) + " / " + p.name + ": " + pid + " out of range");
                    }
                }
            }
        for (auto& b : bad) std::printf ("    %s\n", b.toRawUTF8());
        CHECK (bad.isEmpty() && types >= 20 && presets >= 150, "every built-in has at least 5 factory presets, all valid");

        auto* delay = dynamic_cast<Block*> (e.insert (e.createNode (NodeType::Delay), e.getRoot(), 0));
        e.addModRoute (0, delay->id, delay->indexOf ("mix"), 0.3f);
        CHECK (e.applyFactoryPreset (delay->id, 0) && std::abs (delay->getBase (delay->indexOf ("time")) - 110.0f) < 0.01f
               && e.findNode (delay->id) == delay && e.mods.routes.size() == 1, "a factory preset changes the block in place, keeping its modulation");
        const auto delayId = delay->id;
        const int timeParam = delay->indexOf ("time");
        e.undo();
        auto* undone = dynamic_cast<Block*> (e.findNode (delayId));
        CHECK (undone != nullptr && std::abs (undone->getBase (timeParam) - 400.0f) < 0.01f, "and undoes");

        // split here: the block on path A (or on both), nothing else added
        auto* amp = e.insert (e.createNode (NodeType::Gain), e.getRoot(), 0);
        const auto ampId = amp->id;
        auto* s = e.wrapInSplit (ampId, 2);
        CHECK (s != nullptr && s->branches.size() == 2 && s->branches[0]->chain->nodes.size() == 1
               && s->branches[0]->chain->nodes[0]->id == ampId && s->branches[1]->chain->nodes.empty(), "split here keeps the block on path A, B empty");
        auto* g2 = e.insert (e.createNode (NodeType::Gain), e.getRoot(), 0);
        auto* s2 = e.wrapInSplit (g2->id, 2, true);
        CHECK (s2 != nullptr && s2->branches[1]->chain->nodes.size() == 1 && s2->branches[1]->chain->nodes[0]->id != g2->id
               && s2->branches[1]->chain->nodes[0]->getType() == NodeType::Gain, "or copies it onto both paths");
    }

    //==========================================================================
    std::printf ("--- v0.11.2: the dice and slow plug-ins\n");
    {
        Engine e;
        e.prepare (48000.0, 256);
        juce::PluginDescription d;
        d.name = "NoSuchPlugin";
        d.pluginFormatName = "VST3";
        d.fileOrIdentifier = testDir.getChildFile ("NoSuch.vst3").getFullPathName();
        d.uniqueId = 1234;
        CHECK (e.getPluginLoadMs (d) == -1 && ! e.isDiceSafe (d), "a plug-in that has never loaded isn't used by the dice");
        juce::String err;
        auto n = e.createPlugin (d, err);
        CHECK (n == nullptr && e.getPluginLoadMs (d) == -2 && ! e.isDiceSafe (d), "one that failed to load isn't either");
        CHECK (e.ini.get ("Plug-ins", "Loading").isEmpty(), "the 'loading' note is cleared once it returns");
        e.ini.set ("Plug-ins", "Loading", d.createIdentifierString() + "|NoSuchPlugin");
        e.setupPluginScanning (true);
        CHECK (e.getPluginLoadMs (d) == -3 && e.takeLoadHangNotice() == "NoSuchPlugin", "one that never finished loading is remembered at the next start");
        e.ini.set ("Plug-in load times", d.createIdentifierString(), "120");
        CHECK (e.isDiceSafe (d), "one that loaded quickly is fair game");
        Randomiser r (e);
        CHECK (! r.options.usePlugins, "the dice don't use plug-ins unless asked");
    }

    //==========================================================================
    std::printf ("--- v0.11.3: loading plug-ins through the format wrapper\n");
    {
        // a format that creates its "plug-in" synchronously, like VST3 does
        struct FakeFormat final : juce::AudioPluginFormat
        {
            juce::String getName() const override { return "VST3"; }
            void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
            bool fileMightContainThisPluginType (const juce::String&) override { return true; }
            juce::String getNameOfPluginFromIdentifier (const juce::String& f) override { return f; }
            bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
            bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
            bool canScanForPlugins() const override { return true; }
            bool isTrivialToScan() const override { return true; }
            juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
            juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
            bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
            void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback cb) override
            {
                ++created;
                cb (nullptr, "made synchronously");
            }
            int created = 0;
        };
        auto fake = std::make_unique<FakeFormat>();
        auto* raw = fake.get();
        auto wrapped = withoutRigsmith (std::move (fake));
        juce::PluginDescription d;
        d.name = "Fake";
        d.pluginFormatName = "VST3";
        d.fileOrIdentifier = "C:\\VST3\\Fake.vst3";
        juce::String err;
        std::atomic<bool> done { false };
        std::thread guard ([&done] { for (int i = 0; i < 100 && ! done; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (50));
                                     if (! done) { std::printf ("FAIL: loading a plug-in on the message thread deadlocked\n"); std::fflush (stdout); std::_Exit (2); } });
        auto inst = wrapped->createInstanceFromDescription (d, 48000.0, 256, err);   // on the message thread, as Rigsmith does
        done = true;
        guard.join();
        CHECK (raw->created == 1 && err == "made synchronously", "loading a plug-in on the message thread returns (no deadlock)");
    }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
