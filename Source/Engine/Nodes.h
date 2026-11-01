#pragma once

#include "Common.h"

namespace rig
{
enum class NodeType { Chain, Split, Nam, Ir, Gate, Eq, Gain, Comp, Plugin };

juce::String nodeTypeToString (NodeType);
NodeType nodeTypeFromString (const juce::String&);

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

    virtual juce::String getName() const = 0;
    virtual juce::String getDetail() const { return {}; }

    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    virtual int getLatency() const noexcept { return 0; }
    virtual void collectLatency() noexcept {}

    virtual juce::ValueTree toState() const;
    virtual void fromState (const juce::ValueTree&);

    juce::String id = juce::Uuid().toString();
    std::atomic<bool> bypassed { false };

    double getSampleRate() const noexcept { return sampleRate; }

protected:
    virtual void prepareImpl (double sampleRate, int maxBlockSize) = 0;
    virtual void processImpl (juce::AudioBuffer<float>& buffer, int numSamples) noexcept = 0;

    double sampleRate = 48000.0;
    int maxBlock = 512;

private:
    const NodeType type;
    juce::AudioBuffer<float> dry;
    juce::SmoothedValue<float> wetGain;
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
/** Parameter description for built-in blocks; the inspector builds its knobs from these. */
struct ParamSpec
{
    juce::String id, name;
    float min = 0, max = 1, def = 0;
    juce::String unit;
    bool toggle = false;
    float centre = -1.0f;   // skew so this value sits mid-knob (frequencies)
    int decimals = 1;
};

class Block : public Node
{
public:
    Block (NodeType t, std::vector<ParamSpec> specs);

    const std::vector<ParamSpec>& getSpecs() const noexcept { return specs; }
    int numParams() const noexcept { return (int) specs.size(); }
    float get (int i) const noexcept { return values[(size_t) i].load (std::memory_order_relaxed); }
    void set (int i, float v) noexcept { values[(size_t) i].store (juce::jlimit (specs[(size_t) i].min, specs[(size_t) i].max, v)); }
    int indexOf (const juce::String& pid) const;
    juce::String formatValue (int i) const;

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;

protected:
    std::vector<ParamSpec> specs;
    std::unique_ptr<std::atomic<float>[]> values;
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

    /** Creates a prepared branch ready to be inserted under the engine lock. */
    std::unique_ptr<Branch> makeBranch() const;

    /** In Split mode the levels always sum to 100%: moving one rebalances the rest. */
    void setLevel (int branch, float level, bool linked);
    void setMode (SplitMode m);
    SplitMode getMode() const noexcept { return (SplitMode) mode.load(); }

    std::vector<std::unique_ptr<Branch>> branches;
    std::atomic<int> mode { (int) SplitMode::Split };
    std::atomic<float> xoverLow { 250.0f }, xoverHigh { 2500.0f };
    std::atomic<bool> autoGain { true };

protected:
    void prepareImpl (double sr, int block) override;
    void processImpl (juce::AudioBuffer<float>&, int) noexcept override;

private:
    juce::AudioBuffer<float> input, mix;
};

//==============================================================================
/** Visits every node in a tree (depth first). */
void visitNodes (Node& root, const std::function<void (Node&)>& fn);

} // namespace rig
