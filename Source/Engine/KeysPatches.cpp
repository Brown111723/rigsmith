#include "Engine.h"

// Keysmith's starting points: the rig with a Synth or Sampler (or layers of them on a split) at
// the front instead of an amp. Every patch is built from the blocks themselves, so the dice,
// scenes, modulation and presets all work on them as they do on a guitar rig.
namespace rig
{
namespace
{
struct KeysPatch { const char* name; const char* category; std::function<std::unique_ptr<Chain> (Engine&)> build; };

void setP (Block& b, const char* id, float v)
{
    const int i = b.indexOf (id);
    if (i >= 0) b.set (i, v);
}

void setChoice (Block& b, const char* id, const juce::String& name)
{
    const int i = b.indexOf (id);
    if (i < 0) return;
    const auto names = b.getChoiceNames (i);
    if (const int k = names.indexOf (name); k >= 0) b.set (i, (float) k);
}

template <typename T> T& add (Chain& c, Engine& e, NodeType t)
{
    c.nodes.push_back (e.createNode (t));
    return *dynamic_cast<T*> (c.nodes.back().get());
}

Block& fx (Chain& c, Engine& e, NodeType t) { return add<Block> (c, e, t); }

void reverb (Chain& c, Engine& e, const char* type, float decay, float mix, float shimmer = 0.0f)
{
    auto& r = fx (c, e, NodeType::Reverb);
    setChoice (r, "type", type);
    setP (r, "decay", decay);
    setP (r, "mix", mix);
    setP (r, "shimmer", shimmer);
}

void delay (Chain& c, Engine& e, const char* mode, float ms, float fb, float mix)
{
    auto& d = fx (c, e, NodeType::Delay);
    setChoice (d, "mode", mode);
    setP (d, "time", ms);
    setP (d, "feedback", fb);
    setP (d, "mix", mix);
}

/** A split whose paths are layers, all playing every note. */
[[maybe_unused]] Split& layers (Chain& c, int paths)
{
    auto s = std::make_unique<Split>();
    for (int i = 0; i < paths; ++i) s->branches.push_back (std::make_unique<Split::Branch>());
    s->setMode (SplitMode::Parallel);
    for (auto& b : s->branches) b->level = 0.8f;
    auto& ref = *s;
    c.nodes.push_back (std::move (s));
    return ref;
}

const std::vector<KeysPatch>& patches()
{
    static const std::vector<KeysPatch> list {
        { "Piano", "Keys", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              s.loadBankSample ("Keys/Upright Piano", 0);
              setChoice (s, "asmode", "Gate"); setP (s, "auni", 1.0f); setChoice (s, "ftype", "Off");
              setP (s, "sustain", 0.0f); setP (s, "decay", 4500.0f); setP (s, "release", 420.0f); setP (s, "velocity", 75.0f);
              reverb (*c, e, "Room", 1.3f, 14.0f);
              return c;
          } },
        { "Electric Piano", "Keys", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "FM Bell"); setP (s, "apos", 10.0f); setP (s, "auni", 1.0f); setP (s, "posenv", 22.0f);
              setP (s, "matk", 1.0f); setP (s, "mdec", 600.0f); setP (s, "msus", 0.0f);
              setChoice (s, "ftype", "Off");
              setP (s, "attack", 2.0f); setP (s, "decay", 3200.0f); setP (s, "sustain", 15.0f); setP (s, "release", 380.0f); setP (s, "velocity", 80.0f);
              auto& t = fx (*c, e, NodeType::Tremolo);
              setP (t, "rate", 4.5f); setP (t, "depth", 35.0f); setP (t, "stereo", 1.0f);
              reverb (*c, e, "Plate", 1.6f, 16.0f);
              return c;
          } },
        { "Organ", "Keys", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Organ"); setP (s, "apos", 65.0f); setP (s, "auni", 1.0f);
              setChoice (s, "ftype", "Off"); setP (s, "velocity", 0.0f);
              setP (s, "attack", 3.0f); setP (s, "sustain", 100.0f); setP (s, "release", 60.0f);
              fx (*c, e, NodeType::Rotary);
              reverb (*c, e, "Room", 1.1f, 12.0f);
              return c;
          } },
        { "Strings", "Pad", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Basic Shapes"); setP (s, "apos", 66.0f); setP (s, "auni", 6.0f); setP (s, "adet", 24.0f);
              setChoice (s, "btable", "Basic Shapes"); setP (s, "bpos", 66.0f); setP (s, "blvl", 28.0f); setP (s, "bsemi", 12.0f); setP (s, "buni", 3.0f);
              setChoice (s, "ftype", "LP 12"); setP (s, "cutoff", 3200.0f); setP (s, "res", 8.0f); setP (s, "fenv", 10.0f);
              setP (s, "attack", 380.0f); setP (s, "decay", 1500.0f); setP (s, "sustain", 90.0f); setP (s, "release", 950.0f);
              fx (*c, e, NodeType::Chorus);
              reverb (*c, e, "Hall", 3.2f, 30.0f);
              return c;
          } },
        { "Bass", "Bass", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Basic Shapes"); setP (s, "apos", 55.0f); setP (s, "auni", 1.0f); setP (s, "asemi", -12.0f);
              setP (s, "sub", 60.0f);
              setP (s, "cutoff", 650.0f); setP (s, "res", 22.0f); setP (s, "fenv", 35.0f); setP (s, "mdec", 260.0f); setP (s, "msus", 10.0f); setP (s, "keytrack", 30.0f);
              setP (s, "attack", 2.0f); setP (s, "decay", 500.0f); setP (s, "sustain", 85.0f); setP (s, "release", 90.0f);
              setChoice (s, "vmode", "Mono"); setP (s, "glide", 0.0f);
              return c;
          } },
        { "Init Synth", "Synth", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              add<SynthBlock> (*c, e, NodeType::Synth);
              reverb (*c, e, "Room", 1.2f, 12.0f);
              return c;
          } },
        { "Lead", "Lead", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Basic Shapes"); setP (s, "apos", 66.0f); setP (s, "auni", 7.0f); setP (s, "adet", 34.0f);
              setChoice (s, "btable", "Sync"); setP (s, "bpos", 35.0f); setP (s, "blvl", 35.0f); setP (s, "bsemi", 12.0f);
              setP (s, "cutoff", 5200.0f); setP (s, "res", 18.0f); setP (s, "fenv", 25.0f);
              setP (s, "attack", 3.0f); setP (s, "sustain", 85.0f); setP (s, "release", 260.0f);
              setChoice (s, "vmode", "Legato"); setP (s, "glide", 45.0f);
              delay (*c, e, "Ping-Pong", 375.0f, 32.0f, 22.0f);
              reverb (*c, e, "Plate", 1.8f, 18.0f);
              return c;
          } },
        { "Pad", "Pad", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Harmonics"); setP (s, "apos", 35.0f); setP (s, "auni", 4.0f); setP (s, "adet", 18.0f);
              setChoice (s, "btable", "Formant"); setP (s, "bpos", 20.0f); setP (s, "blvl", 45.0f); setP (s, "bsemi", 0.0f); setP (s, "bfine", 7.0f);
              setP (s, "posenv", 45.0f); setP (s, "matk", 1800.0f); setP (s, "mdec", 3000.0f); setP (s, "msus", 60.0f);
              setP (s, "cutoff", 1800.0f); setP (s, "res", 12.0f); setP (s, "fenv", 30.0f);
              setP (s, "attack", 900.0f); setP (s, "decay", 2000.0f); setP (s, "sustain", 90.0f); setP (s, "release", 1800.0f);
              fx (*c, e, NodeType::Chorus);
              reverb (*c, e, "Hall", 5.5f, 38.0f);
              return c;
          } },
        { "Glass Grains", "Texture", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atype", "Sample"); setChoice (s, "asrc", "Glass Pad"); setP (s, "auni", 2.0f); setP (s, "adet", 8.0f);
              setP (s, "agran", 1.0f); setP (s, "agpos", 30.0f); setP (s, "agsize", 140.0f); setP (s, "agdens", 6.0f);
              setP (s, "agposrand", 35.0f); setP (s, "agspread", 90.0f); setChoice (s, "ftype", "Off");
              setP (s, "attack", 600.0f); setP (s, "release", 2200.0f);
              delay (*c, e, "Ping-Pong", 500.0f, 45.0f, 25.0f);
              reverb (*c, e, "Shimmer", 7.0f, 40.0f, 35.0f);
              return c;
          } },
        { "Layered Keys", "Keys", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              // oscillator A a sample, B a wavetable: two layers in one engine
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atype", "Sample"); setChoice (s, "asrc", "Felt Keys"); setChoice (s, "asmode", "Gate"); setP (s, "auni", 1.0f);
              setChoice (s, "btable", "Basic Shapes"); setP (s, "bpos", 8.0f); setP (s, "buni", 2.0f); setP (s, "bdet", 10.0f); setP (s, "blvl", 40.0f); setP (s, "bsemi", 0.0f); setP (s, "bfine", 0.0f);
              setP (s, "cutoff", 2600.0f); setP (s, "res", 6.0f);
              setP (s, "sustain", 35.0f); setP (s, "decay", 3500.0f); setP (s, "release", 900.0f);
              fx (*c, e, NodeType::Chorus);
              reverb (*c, e, "Plate", 2.2f, 22.0f);
              return c;
          } },
        { "Choir Stretch", "Pad", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atype", "Sample"); setChoice (s, "asrc", "Choir Air"); setP (s, "auni", 1.0f);
              setP (s, "asstart", 15.0f); setP (s, "asend", 85.0f); setP (s, "astretch", 400.0f); setChoice (s, "ftype", "Off");
              setP (s, "attack", 400.0f); setP (s, "release", 1600.0f);
              reverb (*c, e, "Hall", 4.0f, 32.0f);
              return c;
          } },
        { "Bell Layers", "Keys", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atype", "Sample"); setChoice (s, "asrc", "Bell Tines"); setChoice (s, "asmode", "Gate"); setP (s, "auni", 1.0f);
              setChoice (s, "btable", "FM Bell"); setP (s, "bpos", 20.0f); setP (s, "blvl", 45.0f); setP (s, "bsemi", 12.0f); setP (s, "bfine", 0.0f);
              setP (s, "posenv", 60.0f); setP (s, "matk", 1.0f); setP (s, "mdec", 700.0f); setP (s, "msus", 0.0f); setChoice (s, "ftype", "Off");
              setP (s, "sustain", 0.0f); setP (s, "decay", 3500.0f); setP (s, "release", 900.0f);
              delay (*c, e, "Digital", 330.0f, 28.0f, 18.0f);
              reverb (*c, e, "Hall", 3.0f, 28.0f);
              return c;
          } },
        { "Growl Bass", "Bass", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Growl"); setP (s, "apos", 30.0f); setP (s, "auni", 2.0f); setP (s, "adet", 8.0f); setP (s, "asemi", -12.0f);
              setP (s, "sub", 55.0f); setP (s, "posenv", 50.0f); setP (s, "matk", 2.0f); setP (s, "mdec", 350.0f); setP (s, "msus", 20.0f);
              setP (s, "cutoff", 700.0f); setP (s, "res", 35.0f); setP (s, "fenv", 55.0f); setP (s, "fdrive", 30.0f);
              setP (s, "attack", 2.0f); setP (s, "sustain", 90.0f); setP (s, "release", 120.0f);
              setChoice (s, "vmode", "Mono"); setP (s, "glide", 60.0f);
              return c;
          } },
        { "Pluck", "Pluck", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atype", "Sample"); setChoice (s, "asrc", "Pluck"); setChoice (s, "asmode", "Gate"); setP (s, "auni", 1.0f);
              setP (s, "sustain", 0.0f); setP (s, "decay", 1500.0f);
              setChoice (s, "ftype", "LP 12"); setP (s, "cutoff", 3000.0f); setP (s, "fenv", 30.0f); setP (s, "mdec", 300.0f); setP (s, "msus", 0.0f);
              delay (*c, e, "Ping-Pong", 250.0f, 48.0f, 30.0f);
              reverb (*c, e, "Room", 1.4f, 18.0f);
              return c;
          } },
        { "Two-Engine Atmos", "Texture", [] (Engine& e)
          {
              // a choir stretched slow on A, a moving spectral table on B, a little noise
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atype", "Sample"); setChoice (s, "asrc", "Choir Air"); setP (s, "astretch", 300.0f); setP (s, "auni", 2.0f); setP (s, "adet", 10.0f);
              setChoice (s, "btable", "Spectral"); setP (s, "bpos", 10.0f); setP (s, "blvl", 55.0f); setP (s, "bsemi", 12.0f); setP (s, "bfine", 0.0f);
              setP (s, "posenv", 80.0f); setP (s, "matk", 4000.0f); setP (s, "mdec", 6000.0f); setP (s, "noise", 8.0f);
              setP (s, "attack", 1500.0f); setP (s, "release", 3000.0f); setP (s, "cutoff", 2500.0f);
              reverb (*c, e, "Shimmer", 9.0f, 45.0f, 40.0f);
              return c;
          } },
        { "Cello Ensemble", "Strings", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              s.loadBankSample ("Strings/Cellos", 0);
              setChoice (s, "asmode", "Loop"); setP (s, "alstart", 35.0f); setP (s, "alend", 85.0f); setP (s, "alxfade", 60.0f);
              setP (s, "auni", 2.0f); setP (s, "adet", 6.0f); setChoice (s, "ftype", "Off");
              setP (s, "attack", 180.0f); setP (s, "sustain", 95.0f); setP (s, "release", 600.0f);
              reverb (*c, e, "Hall", 3.0f, 26.0f);
              return c;
          } },
        { "FM Keys", "Keys", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Basic Shapes"); setP (s, "apos", 0.0f); setP (s, "auni", 1.0f);
              setP (s, "afm", 38.0f); setP (s, "afmratio", 2.0f); setP (s, "afmfb", 10.0f);
              setChoice (s, "ftype", "LP 12"); setP (s, "cutoff", 6000.0f); setP (s, "keytrack", 60.0f);
              setP (s, "attack", 2.0f); setP (s, "decay", 2200.0f); setP (s, "sustain", 20.0f); setP (s, "release", 500.0f);
              fx (*c, e, NodeType::Chorus);
              reverb (*c, e, "Plate", 1.8f, 18.0f);
              return c;
          } },
        { "Harmonic Pad", "Pad", [] (Engine& e)
          {
              auto c = std::make_unique<Chain>();
              auto& s = add<SynthBlock> (*c, e, NodeType::Synth);
              setChoice (s, "atable", "Basic Shapes"); setP (s, "apos", 0.0f); setP (s, "auni", 5.0f); setP (s, "adet", 20.0f);
              setP (s, "ahamt", 60.0f); setP (s, "ahcount", 14.0f); setP (s, "ahoddeven", -60.0f); setP (s, "ahdecay", 40.0f);
              setChoice (s, "ftype", "Vowel O"); setP (s, "cutoff", 1100.0f); setP (s, "res", 40.0f);
              setP (s, "attack", 900.0f); setP (s, "sustain", 90.0f); setP (s, "release", 1800.0f);
              reverb (*c, e, "Hall", 5.0f, 34.0f);
              return c;
          } },
    };
    return list;
}
} // namespace

const juce::StringArray& Engine::getKeysPatchNames()
{
    static const juce::StringArray names = [] { juce::StringArray n; for (auto& p : patches()) n.add (p.name); return n; }();
    return names;
}

const juce::StringArray& Engine::getKeysPatchCategories()
{
    static const juce::StringArray cats = [] { juce::StringArray n; for (auto& p : patches()) n.add (p.category); return n; }();
    return cats;
}

void Engine::loadKeysPatch (int index)
{
    if (dirty) checkpoint();
    index = juce::jlimit (0, (int) patches().size() - 1, index);
    factoryScenes.clear();
    auto r = patches()[(size_t) index].build (*this);
    r->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        std::swap (root, r);
    }
    if (r != nullptr) announceDeletion (*r);
    r.reset();

    presetName = patches()[(size_t) index].name;
    presetFile = juce::File();
    for (int i = 0; i < kNumScenes; ++i) sceneNames[i] = juce::String::charToString ((juce::juce_wchar) ('A' + i));
    for (auto& s : scenes) s = captureScene();
    activeScene = 0;
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [] (const MidiMapping& m) { return ! m.isGlobal(); }), mappings.end());
    dirty = false;
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); });
}

juce::Array<juce::File> Engine::getSampleFiles (bool rescan)
{
    if (sampleFilesScanned && ! rescan) return sampleFiles;
    sampleFiles.clear();
    const auto paths = getSamplePaths();
    for (int i = 0; i < paths.getNumPaths(); ++i)
        for (const auto& entry : juce::RangedDirectoryIterator (paths[i], true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", juce::File::findFiles))
        {
            sampleFiles.add (entry.getFile());
            if (sampleFiles.size() >= 20000) break;
        }
    sampleFilesScanned = true;
    return sampleFiles;
}

} // namespace rig
