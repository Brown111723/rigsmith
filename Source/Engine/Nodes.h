#pragma once

#include "Common.h"

namespace rig
{
enum class NodeType
{
    Chain, Split, Nam, Ir, Gate, Eq, Gain, Comp, Plugin,
    // built-in effects (v0.2)
    Limiter, Expander, Transient, Swell,
    Overdrive, Fuzz, Boost, Crusher,
    Wah, Chorus, Flanger, Phaser, Tremolo, Vibe, Rotary,
    Delay, Reverb, Pitch,
    // v0.4
    Tuner,
    // v0.11: a modulation source (lives in the MOD section, not in the rig)
    Modulator
};

juce::String nodeTypeToString (NodeType);
/** The built-in effects, grouped as in the + menu and the library. */
const std::vector<std::pair<juce::String, std::vector<NodeType>>>& builtInGroups();
juce::String builtInMenuName (NodeType);    // "Delay (digital / tape / ping-pong / dual)"
juce::String builtInShortName (NodeType);   // "Delay"
juce::String builtInBlurb (NodeType);       // one line on what it does
NodeType nodeTypeFromString (const juce::String&);

/** Families used for colour coding, library grouping and the add menu. */
enum class Category { Dynamics = 0, Drive, Amp, Cab, Eq, Filter, Modulation, Delay, Reverb, Pitch, Utility, Routing, Other };
juce::String categoryName (Category);
juce::Colour categoryColour (Category);
Category categoryForBuiltIn (NodeType);
/** Best guess from a plug-in's declared category and its name. */
Category categoriseDescription (const juce::PluginDescription&);

//==============================================================================
/** Pre/post taps of the selected block, read by the UI's spectrum display. */
class AnalyzerTap
{
public:
    static constexpr int size = 16384;
    void push (bool post, const juce::AudioBuffer<float>& b, int n) noexcept;
    int read (bool post, float* dest, int max);    // newest samples, message thread
    void reset() { pre.fifo.reset(); post.fifo.reset(); }

private:
    struct Stream
    {
        juce::AbstractFifo fifo { size };
        std::vector<float> data = std::vector<float> ((size_t) size, 0.0f);
    };
    Stream pre, post;
};

//==============================================================================
/** The guitar as it came into the rig, for the stretch of audio being processed right now
    (a gate later in the chain can be keyed from it). Set by the engine on the audio thread. */
struct RigKey
{
    static inline thread_local const float* data = nullptr;
    static inline thread_local int length = 0;
};

//==============================================================================
/** Anything that processes a stereo buffer in place. Owned by a Chain or Split.
    Structure is only mutated on the message thread while holding the engine lock;
    parameters are atomics so the UI, scenes and MIDI can touch them freely. */
class Node
{
public:
    explicit Node (NodeType t) : type (t) {}
    virtual ~Node() = default;

    NodeType getType() const noexcept { return type; }
    virtual Category getCategory() const { return categoryForBuiltIn (type); }

    virtual juce::String getName() const = 0;
    virtual juce::String getDetail() const { return {}; }

    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    virtual int getLatency() const noexcept { return 0; }

    virtual juce::ValueTree toState() const;
    virtual void fromState (const juce::ValueTree&);

    /** Set by the engine, not the user: e.g. a cab IR after an amp model that already
        includes a cab. Processed like bypass, but kept apart from it so scenes don't change. */
    std::atomic<bool> autoSkipped { false };
    juce::String id = juce::Uuid().toString();
    std::atomic<bool> bypassed { false };

    /** Gain compensation, like normalising each block: while you play it measures
        loudness in vs out, then holds one fixed make-up gain. It re-learns only after
        the block's settings change (or on request), so it never pumps with dynamics. */
    std::atomic<bool> autoGain { false };
    std::atomic<float> autoGainDb { 0.0f };          // the held compensation
    std::atomic<float> autoGainProgress { 1.0f };    // 0..1 while learning
    std::atomic<bool> relearn { false };             // ask the audio thread to measure again
    void requestRelearn() noexcept { autoGainProgress.store (0.0f); relearn.store (true); }

    /** Bumped whenever the block's sound is edited; drives gain-compensation re-learning. */
    std::atomic<juce::uint32> editCount { 0 };
    void touched() noexcept { editCount.fetch_add (1, std::memory_order_relaxed); }
    /** Call after restoring state: the stored compensation already matches these settings. */
    void syncEdits() noexcept { learnedEdit.store (editCount.load()); }

    /** Locked blocks are kept when the rig is randomised. */
    std::atomic<bool> locked { false };

    /** Level trims around the block (the handles on the panel's IN / OUT meters), in dB. */
    std::atomic<float> inTrimDb { 0.0f }, outTrimDb { 0.0f };
    /** Peak levels in and out of the block since the UI last took them (linear). */
    std::atomic<float> peakIn { 0.0f }, peakOut { 0.0f };

    std::atomic<AnalyzerTap*> tap { nullptr };

    double getSampleRate() const noexcept { return sampleRate; }

protected:
    virtual void prepareImpl (double sampleRate, int maxBlockSize) = 0;
    virtual void processImpl (juce::AudioBuffer<float>& buffer, int numSamples) noexcept = 0;

    double sampleRate = 48000.0;
    int maxBlock = 512;

private:
    const NodeType type;
    juce::AudioBuffer<float> dry;
    juce::SmoothedValue<float> wetGain, compGain, inTrim, outTrim;
    std::atomic<juce::uint32> learnedEdit { 0 };
    double accIn = 0, accOut = 0;
    int accSamples = 0;
    bool learningNow = false;
};

//==============================================================================
/** Series list of nodes. */
class Chain : public Node
{
public:
    Chain() : Node (NodeType::Chain) {}

    juce::String getName() const override { return "Chain"; }
    int getLatency() const noexcept override;

    juce::ValueTree toState() const override;

    std::vector<std::unique_ptr<Node>> nodes;

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;
};

//==============================================================================
/** Parameter description for built-in blocks; the inspector builds its controls from these. */
struct ParamSpec
{
    juce::String id, name;
    float min = 0, max = 1, def = 0;
    juce::String unit;
    bool toggle = false;
    bool hasCentre = false;
    float centre = 0;          // value shown at mid-knob (log-ish feel for Hz / ms)
    int decimals = 1;
    juce::StringArray choices; // non-empty: a selector (value = index)

    // randomiser hints: a musical range, or never randomised (levels, outputs)
    bool hasRand = false, noRand = false;
    float randMin = 0, randMax = 1;

    ParamSpec& mid (float c)   { hasCentre = true; centre = c; return *this; }
    ParamSpec& dec (int d)     { decimals = d; return *this; }
    ParamSpec& rand (float lo, float hi) { hasRand = true; randMin = lo; randMax = hi; return *this; }
    ParamSpec& fixed()         { noRand = true; return *this; }

    bool isChoice() const noexcept { return ! choices.isEmpty(); }
    bool isDb() const noexcept { return unit == "dB"; }

    /** Knob position 0..1 <-> value, using the same skew as the inspector's knobs. */
    float toProportion (float v) const noexcept;
    float fromProportion (float p) const noexcept;
};

inline ParamSpec P (const juce::String& id, const juce::String& name, float mn, float mx, float def, const juce::String& unit = {})
{
    ParamSpec s;
    s.id = id; s.name = name; s.min = mn; s.max = mx; s.def = def; s.unit = unit;
    s.decimals = unit == "Hz" || unit == "ms" || unit == "%" ? 0 : 1;
    return s;
}

inline ParamSpec Toggle (const juce::String& id, const juce::String& name, bool def)
{
    auto s = P (id, name, 0.0f, 1.0f, def ? 1.0f : 0.0f);
    s.toggle = true;
    return s;
}

inline ParamSpec Choice (const juce::String& id, const juce::String& name, const juce::StringArray& options, int def = 0)
{
    auto s = P (id, name, 0.0f, (float) juce::jmax (0, options.size() - 1), (float) def);
    s.choices = options;
    s.decimals = 0;
    return s;
}

//==============================================================================
/** How a block's display draws what its knobs are doing. */
enum class Visual { None, Spectrum, Response, Dynamics, Shaper, Lfo, Delay, Reverb, Envelope, Tuner, Gate, Modulator };

struct DelayTap { float timeSec, gain; int side; };   // side: -1 left, 0 centre, 1 right

class Block : public Node
{
public:
    Block (NodeType t, std::vector<ParamSpec> specs);

    const std::vector<ParamSpec>& getSpecs() const noexcept { return specs; }
    int numParams() const noexcept { return (int) specs.size(); }
    /** The value the sound uses: the knob, moved by any modulation routed to it. */
    float get (int i) const noexcept { return effective[(size_t) i].load (std::memory_order_relaxed); }
    float get (const char* pid) const noexcept { return get (indexOf (pid)); }
    int getChoice (int i) const noexcept { return juce::roundToInt (get (i)); }
    /** Where the knob is (what's saved, shown and randomised), whatever modulation is doing. */
    float getBase (int i) const noexcept { return values[(size_t) i].load (std::memory_order_relaxed); }
    void set (int i, float v) noexcept
    {
        const float c = juce::jlimit (specs[(size_t) i].min, specs[(size_t) i].max, v);
        if (values[(size_t) i].exchange (c) != c) touched();
        if (! modulated[(size_t) i].load (std::memory_order_relaxed)) effective[(size_t) i].store (c, std::memory_order_relaxed);
    }

    /** Audio thread: the knob moved by `offset` (in knob travel, -1..1) for now. */
    void modulate (int i, float offset) noexcept;
    /** Back to the knob (a route was removed). */
    void unmodulate (int i) noexcept;
    bool isModulated (int i) const noexcept { return modulated[(size_t) i].load (std::memory_order_relaxed); }
    int indexOf (const juce::String& pid) const;
    juce::String formatValue (int i) const;
    virtual juce::String formatValue (int i, float v) const;
    /** Which parameters the panel shows as knobs, in order (empty: all of them, as declared). */
    virtual std::vector<int> panelOrder() const { return {}; }

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

    /** amount 1 = a fresh musical setting, small amounts = a subtle mutation of the current one. */
    virtual void randomise (juce::Random&, float amount);

    //--- Visuals (message thread; computed from current parameter values) -----
    virtual Visual getVisual() const { return Visual::Spectrum; }
    virtual float responseDb (double /*hz*/) const { return 0.0f; }
    virtual float transferDb (float inDb) const { return inDb; }
    virtual float shape (float x) const { return x; }
    virtual float lfoShape (double /*phase01*/) const { return 0.0f; }
    virtual double lfoPhase() const { return livePhase.load(); }
    virtual float lfoRate() const { return 1.0f; }
    virtual float lfoDepth() const { return 1.0f; }
    virtual void getTaps (std::vector<DelayTap>&) const {}
    virtual void getDecay (float& predelayMs, float& rt60, float& damping, float& size) const
    {
        predelayMs = 0; rt60 = 1; damping = 0.5f; size = 0.5f;
    }
    /** Optional moving marker for Response visuals (e.g. a wah's current frequency). */
    virtual float liveFrequency() const { return 0.0f; }

    // live meters written by the audio thread
    std::atomic<float> meterIn { -100.0f }, meterOut { -100.0f }, meterGr { 0.0f };
    std::atomic<double> livePhase { 0.0 };

protected:
    void updateMeters (float inPeak, float outPeak, float grDb) noexcept;

    std::vector<ParamSpec> specs;
    std::unique_ptr<std::atomic<float>[]> values, effective;
    std::unique_ptr<std::atomic<bool>[]> modulated;
};

//==============================================================================
enum class SplitMode { Split = 0, Parallel, Crossover, Stereo };
juce::String splitModeName (SplitMode);

/** Sends one signal down several Chains and mixes them back together.
    Branches can contain further Splits, so arbitrary trees are possible. */
class Split : public Node
{
public:
    struct Branch
    {
        Branch();
        std::unique_ptr<Chain> chain;
        std::atomic<float> level { 1.0f };   // 0..1 (shown as %)
        std::atomic<float> pan { 0.0f };     // -1..1
        std::atomic<bool> mute { false }, phase { false };

        // audio-thread state
        juce::AudioBuffer<float> buffer;
        juce::SmoothedValue<float> gainL, gainR;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> delay { 1 };
        juce::dsp::LinkwitzRileyFilter<float> xA, xB;
        int compensation = 0;

        void prepare (double sr, int maxBlock);
    };

    Split();

    juce::String getName() const override { return "Split"; }
    juce::String getDetail() const override;
    int getLatency() const noexcept override;

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

    std::unique_ptr<Branch> makeBranch() const;

    /** In Split mode the levels always sum to 100%: moving one rebalances the rest. */
    void setLevel (int branch, float level, bool linked);
    void setMode (SplitMode m);
    SplitMode getMode() const noexcept { return (SplitMode) mode.load(); }

    std::vector<std::unique_ptr<Branch>> branches;
    std::atomic<int> mode { (int) SplitMode::Split };
    std::atomic<float> xoverLow { 250.0f }, xoverHigh { 2500.0f };
    std::atomic<bool> autoGainSum { true };

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    juce::AudioBuffer<float> input, mix;
};

//==============================================================================
void visitNodes (Node& root, const std::function<void (Node&)>& fn);

} // namespace rig
