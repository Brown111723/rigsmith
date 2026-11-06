#pragma once

#include "Nodes.h"
#include "Tuner.h"

namespace rig
{
/** The kinds of modulation source. */
enum class ModKind { Lfo = 0, Env, Seq, Level, Pitch };

/** What every modulator sees on each control tick (at most 64 samples). */
struct ModContext
{
    double sampleRate = 48000.0;
    int numSamples = 0;
    const float* input = nullptr;   // the guitar as it enters the rig (mono)
    double bpm = 120.0;             // the host's tempo, else Rigsmith's own
    double ppq = -1.0;              // host position in beats at the start of this tick (< 0: not playing)
    bool pick = false;              // a note was picked during this tick
    float levelDb = -100.0f;        // fast input envelope at the end of the tick
    float pitchHz = 0.0f;           // last detected pitch (0 = none)
};

//==============================================================================
/** A modulation source. Its settings are ordinary block parameters, so they get knobs,
    MIDI learn and presets for free - and other modulators can modulate them
    (an envelope speeding up an LFO, an LFO sweeping a sequencer's glide...). */
class Modulator : public Block
{
public:
    Modulator (ModKind, int number, std::vector<ParamSpec>);

    ModKind getKind() const noexcept { return kind; }
    /** "LFO 1", "ENV 2", "SEQ", "LEVEL", "PITCH" */
    juce::String getName() const override;
    juce::String getDetail() const override;
    juce::String describe() const;       // one line on what it does
    juce::Colour getColour() const noexcept { return colour; }
    Category getCategory() const override { return Category::Modulation; }
    Visual getVisual() const override { return Visual::Modulator; }

    /** Bipolar sources swing -1..1 around the knob; unipolar ones push 0..1 one way. */
    virtual bool isBipolar() const noexcept { return false; }
    /** Audio thread: this tick's value. */
    virtual void tick (const ModContext&) noexcept = 0;
    virtual void restart() noexcept {}

    /** The current output (-1..1 bipolar, 0..1 unipolar, depth applied). */
    float value() const noexcept { return out.load (std::memory_order_relaxed); }

    /** The last few seconds of output for the analyzer, oldest first (one point every 10 ms). */
    static constexpr int historySize = 600;
    void readHistory (std::vector<float>& dest) const;

    /** For the editors: where it is now (LFO phase 0..1, sequencer step, envelope stage). */
    std::atomic<float> livePos { 0.0f };
    std::atomic<int> liveStage { 0 };

    int getNumber() const noexcept { return number; }

protected:
    void prepareImpl (double, int) override {}
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override {}
    void publish (float v, const ModContext&) noexcept;

private:
    const ModKind kind;
    const int number;
    juce::Colour colour;
    std::atomic<float> out { 0.0f };
    std::unique_ptr<std::atomic<float>[]> hist { new std::atomic<float>[historySize] };
    std::atomic<int> histWrite { 0 };
    double histAcc = 0;
};

//==============================================================================
class LfoMod : public Modulator
{
public:
    explicit LfoMod (int number);
    enum Params { pShape = 0, pSync, pRate, pPhase, pDepth, pPolarity, pRetrig, pSmooth };
    bool isBipolar() const noexcept override { return getChoice (pPolarity) == 0; }
    void tick (const ModContext&) noexcept override;
    void restart() noexcept override { phase = 0.0; }
    float lfoShape (double phase01) const override;     // -1..1, for drawing
    static double syncBeats (int choice);                // beats per cycle (0 = free)
    juce::String formatValue (int, float) const override;
    using Block::formatValue;

private:
    double phase = 0.0;
    float held = 0, prevHeld = 0, smoothed = 0;
    int lastCycle = -1;
    juce::Random rng;
    float shapeAt (double p) const noexcept;
};

class EnvMod : public Modulator
{
public:
    explicit EnvMod (int number);
    enum Params { pThreshold = 0, pAttack, pDecay, pSustain, pRelease, pDepth, pInvert };
    void tick (const ModContext&) noexcept override;
    void restart() noexcept override { level = 0; stage = 0; }
    enum Stage { Idle = 0, Attack, Decay, Sustain, Release };

private:
    float level = 0;
    int stage = Idle;
    bool gate = false;
};

class SeqMod : public Modulator
{
public:
    explicit SeqMod (int number);
    static constexpr int maxSteps = 16;
    enum Params { pSteps = 0, pSync, pRate, pGlide, pDepth, pPolarity, pRetrig, pStep1 };
    bool isBipolar() const noexcept override { return getChoice (pPolarity) == 1; }
    void tick (const ModContext&) noexcept override;
    void restart() noexcept override { pos = 0.0; }
    static double stepBeats (int choice);   // 0 = free
    int numSteps() const noexcept { return juce::jlimit (1, maxSteps, juce::roundToInt (get (pSteps))); }
    juce::String formatValue (int, float) const override;
    using Block::formatValue;

private:
    double pos = 0.0;
    float smoothed = 0;
};

class LevelMod : public Modulator
{
public:
    explicit LevelMod (int number);
    enum Params { pAttack = 0, pRelease, pFloor, pCeiling, pDepth, pInvert };
    void tick (const ModContext&) noexcept override;
    std::atomic<float> liveDb { -100.0f };

private:
    float env = 0;
};

class PitchMod : public Modulator
{
public:
    explicit PitchMod (int number);
    enum Params { pLow = 0, pHigh, pGlide, pDepth, pNoNote };
    void tick (const ModContext&) noexcept override;
    std::atomic<float> liveHz { 0.0f };

private:
    float smoothed = 0, target = 0;
};

//==============================================================================
/** One knob moved by one source. amount: -1..1 of the knob's travel. */
struct ModRoute
{
    int source = 0;
    juce::String target;   // a block's id, or "mod:N" for another modulator
    int param = 0;
    float amount = 0.5f;
};

/** The modulation section: three LFOs, three envelopes, a step sequencer, the input level and
    the input pitch, routed to any block knob (or each other's knobs). Owned by the engine.
    Routes are edited on the message thread under the engine lock; the audio thread reads them
    under the same lock, so a block can never disappear under it. */
class ModMatrix
{
public:
    ModMatrix();

    static constexpr int numSources = 9;
    static constexpr int maxRoutes = 64;
    Modulator& source (int i) const noexcept { return *sources[(size_t) i]; }
    int indexOf (const Modulator*) const noexcept;
    static juce::String targetFor (int sourceIndex) { return "mod:" + juce::String (sourceIndex); }
    Modulator* sourceFromTarget (const juce::String&) const noexcept;

    std::vector<ModRoute> routes;   // edited under the engine lock (use the Engine's helpers)
    /** Change a route's depth while playing (no lock: the audio thread reads a copy). */
    void setAmount (int routeIndex, float amount) noexcept;

    void prepare (double sampleRate, int maxBlock);
    /** Audio thread, engine lock held: find this callback's targets. */
    void resolve (Node& root) noexcept;
    /** Audio thread: run the sources on this chunk of input and move the knobs. */
    void process (const float* input, int n, double ppqAtStart) noexcept;
    /** Should the engine tick the sources at all (routes, or the panel is watching)? */
    bool isActive() const noexcept { return hasRoutes.load() || watching.load(); }
    std::atomic<bool> watching { false };   // the MOD panel is open: keep the scopes moving
    std::atomic<bool> hasRoutes { false };
    void routesChanged() noexcept;         // call after editing routes (lock held)

    /** Message thread, ~30 times a second: the pitch tracker. */
    void analysePitch();
    std::atomic<float> pitchHz { 0.0f };

    /** Tempo: the host's when there is one, else this (saved with the preset). */
    std::atomic<float> bpm { 120.0f };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> pickedFlash { false };   // the UI's pick light

    juce::ValueTree toState() const;
    void fromState (const juce::ValueTree&);   // invalid tree: defaults
    void resetToDefaults();

    /** Routes into one knob, for drawing rings. */
    std::vector<const ModRoute*> routesTo (const juce::String& target, int param) const;
    /** How far (in knob travel) the sources could move a knob: lo..hi around 0. */
    juce::Range<float> reach (const juce::String& target, int param) const;

private:
    std::vector<std::unique_ptr<Modulator>> sources;
    double sampleRate = 48000.0;
    Tuner tuner;

    // pick detection
    float fastEnv = 0, slowEnv = 0;
    int refractory = 0;

    // resolved for this callback
    struct Slot { Block* block; int param; float sum; };
    std::array<Slot, maxRoutes> slots {};
    std::array<int, maxRoutes> routeSlot {};
    int numSlots = 0, numResolved = 0;
    std::array<std::atomic<float>, maxRoutes> liveAmounts {};
    std::array<int, maxRoutes> resolvedIndex {};
    std::array<Modulator*, maxRoutes> resolvedSources {};
};

} // namespace rig
