#pragma once

#include "Nodes.h"
#include "Blocks.h"
#include "NamBlock.h"
#include "PluginBlock.h"
#include "Tuner.h"

namespace rig
{
class Engine;

//==============================================================================
struct MidiMapping
{
    enum class Target { Scene = 0, Bypass, BranchLevel, Blend, Param, PresetNext, PresetPrev, Tuner, MasterVolume };

    int cc = -1;
    int channel = 0;          // 0 = omni
    Target target = Target::Scene;
    juce::String nodeId;
    int index = 0;            // scene, branch or param index
    bool latching = false;    // false: toggle on every press (value >= 64); true: follow the switch state

    bool isGlobal() const noexcept;
    juce::ValueTree toState() const;
    static MidiMapping fromState (const juce::ValueTree&);
};

//==============================================================================
class Engine : private juce::Timer,
               private juce::ChangeListener
{
public:
    Engine();
    ~Engine() override;

    //--- Audio -----------------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    int getLatency() const noexcept;

    enum InputMode { Input1 = 0, Input2, InputSum, InputStereo };
    std::atomic<int> inputMode { Input1 };
    std::atomic<float> inputGainDb { 0.0f }, outputGainDb { 0.0f };
    std::atomic<bool> limiterOn { true }, tunerMute { false }, tunerActive { false };
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    Tuner tuner;

    //--- Graph (message thread only) -------------------------------------------
    Chain& getRoot() noexcept { return *root; }
    Node* findNode (const juce::String& id) const;

    struct Location { Chain* chain = nullptr; int index = -1; };
    Location locate (const juce::String& id) const;
    /** The Split + branch index that owns this chain, or nullptr for the main chain. */
    Split* findOwnerSplit (const Chain& chain, int* branchIndex = nullptr) const;

    std::unique_ptr<Node> createNode (NodeType type) const;
    std::unique_ptr<Node> createPlugin (const juce::PluginDescription& desc, juce::String& error);

    Node* insert (std::unique_ptr<Node> node, Chain& target, int index);
    void remove (const juce::String& id);
    bool move (const juce::String& id, Chain& target, int index);
    Node* duplicate (const juce::String& id);
    Split* wrapInSplit (const juce::String& id, int numBranches = 2);
    Split* addSplit (Chain& target, int index, int numBranches);
    void addBranch (Split& split);
    void removeBranch (Split& split, int branchIndex);
    void unwrapSplit (Split& split);
    bool canDropInto (const juce::String& movingId, const Chain& target) const;

    //--- Scenes -----------------------------------------------------------------
    int getScene() const noexcept { return activeScene; }
    void setScene (int index);
    void storeScene();                  // capture current bypass/levels into the active scene
    void copySceneTo (int index);
    juce::String sceneNames[kNumScenes] { "A", "B", "C", "D" };

    //--- State & presets --------------------------------------------------------
    juce::ValueTree getState (bool includeSession) const;
    void setState (const juce::ValueTree&, bool includeSession);
    void loadDefaultRig();

    juce::File getPresetFolder() const;
    juce::Array<juce::File> getPresetFiles() const;
    bool loadPreset (const juce::File&);
    bool savePreset (const juce::File&);
    void stepPreset (int delta);
    juce::String presetName { "Init Rig" };
    juce::File presetFile;
    bool dirty = false;
    void markDirty();

    //--- MIDI -------------------------------------------------------------------
    std::vector<MidiMapping> mappings;
    void startLearn (MidiMapping prototype);
    void cancelLearn();
    bool isLearning() const noexcept { return learning; }
    juce::String describe (const MidiMapping&) const;
    void removeMapping (int index);

    //--- Plug-ins & folders -----------------------------------------------------
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::ApplicationProperties props;
    juce::File getFolder (const juce::String& key) const;
    void setFolder (const juce::String& key, const juce::File&);
    juce::File getDeadMansPedalFile() const;

    //--- Host glue --------------------------------------------------------------
    std::function<void (int)> onLatencyChanged;
    std::function<void (int)> onSceneChanged;
    std::atomic<int> pendingHostScene { -1 };

    //--- Listeners --------------------------------------------------------------
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void graphChanged() {}
        virtual void nodeWillBeDeleted (Node&) {}
        virtual void sceneChanged() {}
        virtual void presetChanged() {}
        virtual void midiMapChanged() {}
        virtual void tunerToggled() {}
        virtual void showMessage (const juce::String&) {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }
    void notifyGraphChanged();
    void message (const juce::String& text);

    juce::CriticalSection& getLock() noexcept { return lock; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleMidi (int type, int channel, int number, int value);
    void applyMapping (const MidiMapping&, int value);

    std::unique_ptr<Node> buildNode (const juce::ValueTree&);
    void prepareNode (Node&);
    void announceDeletion (Node&);
    std::unique_ptr<Node> extract (const juce::String& id, Location* where = nullptr);

    juce::ValueTree captureScene() const;
    void recallScene (const juce::ValueTree&);

    std::unique_ptr<Chain> root;
    juce::CriticalSection lock;
    juce::ListenerList<Listener> listeners;

    double sampleRate = 48000.0;
    int blockSize = 512;
    juce::dsp::Limiter<float> limiter;
    juce::SmoothedValue<float> inGain, outGain;

    juce::ValueTree scenes[kNumScenes];
    int activeScene = 0;

    juce::AbstractFifo midiFifo { 512 };
    std::array<juce::uint32, 512> midiData {};
    bool learning = false;
    MidiMapping learnProto;
    juce::uint8 lastCcValue[16][128] {};

    int reportedLatency = -1;
    int tick = 0;
};

} // namespace rig
