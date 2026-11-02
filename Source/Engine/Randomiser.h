#pragma once

#include "Engine.h"

namespace rig
{
/** Dice for the whole rig, for one block's type, and for one block's settings.
    Locked blocks are never touched. Every operation is a single undo step. */
class Randomiser
{
public:
    explicit Randomiser (Engine&);

    struct Options
    {
        bool usePlugins = true;    // mix scanned VSTs in with the built-ins
        bool allowSplits = true;   // dual-amp rigs
    };
    Options options;

    /** A whole new rig: dynamics, drives, amps (NAM models from your folders), cabs (your IRs),
        modulation, delays and reverbs, sometimes split into parallel amps. Locked blocks stay. */
    void randomiseRig();
    /** Keep the layout; swap every unlocked block for a random one of the same kind. */
    void reshuffleBlocks();
    /** Every unlocked block's settings. amount 1 = fresh settings, less = subtle mutation. */
    void randomiseAllSettings (float amount);
    /** One block's (or split's) settings. */
    void randomiseSettings (Node&, float amount);
    /** Replace one block with a random block of the same category (a delay becomes another delay,
        a NAM amp gets another model...). */
    Node* randomiseType (const juce::String& nodeId);

    /** The category randomising this block's type will choose from. */
    static Category categoryOf (const Node&);

    juce::Random& random() noexcept { return rng; }

private:
    std::unique_ptr<Node> makeRandom (Category, const Node* avoid = nullptr);
    /** variant 1: a NAM pedal capture (Nam) or a space IR (Ir). */
    std::unique_ptr<Node> makeBuiltIn (NodeType, int variant = 0);
    std::unique_ptr<Node> makePlugin (const juce::PluginDescription&);
    std::unique_ptr<Node> makeAmp();
    std::unique_ptr<Node> makeCab();
    void randomiseSplit (Split&, float amount);
    /** Files the dice can pick, grouped by gear so an amp with 40 captures isn't 40 times as likely. */
    struct Pool
    {
        juce::Array<juce::File> files;
        std::vector<juce::Array<juce::File>> groups;
        void add (const juce::File& f) { files.add (f); }
        void clear() { files.clear(); groups.clear(); }
        bool isEmpty() const noexcept { return files.isEmpty(); }
        void finish();
    };
    void collectFiles();
    juce::File pick (const Pool&);

    Engine& engine;
    juce::Random rng;
    Pool models, irs;            // amp captures / cab IRs
    Pool pedalModels, spaceIrs;  // used for drive and reverb slots
    juce::uint32 lastScan = 0;
    int lastExclusions = -1;
};

} // namespace rig
