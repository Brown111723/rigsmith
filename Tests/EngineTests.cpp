// Headless smoke tests for the Rigsmith engine. Build with -DRIGSMITH_TESTS=ON.
#include "Engine/Engine.h"
#include <cstdio>

using namespace rig;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

static float runNoise (Engine& e, int blocks, int blockSize, bool* finite)
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
            const float x = (rnd.nextFloat() * 2.0f - 1.0f) * 0.2f;
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

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
