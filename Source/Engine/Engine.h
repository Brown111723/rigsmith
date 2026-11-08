#pragma once

#include "Nodes.h"
#include "Blocks.h"
#include "Pedals.h"
#include "NamBlock.h"
#include "PluginBlock.h"
#include "Tuner.h"
#include "IniFile.h"
#include "Captures.h"
#include "Modulation.h"
#include "PolyTuner.h"

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
    PolyTuner polyTuner;

    //--- Modulation (v0.11) -----------------------------------------------------
    ModMatrix mods;
    /** Route a source to a knob (a block's id, or ModMatrix::targetFor (n) for a modulator).
        The same source on the same knob just gets the new amount. Returns the route's index. */
    int addModRoute (int source, const juce::String& target, int param, float amount);
    void removeModRoute (int index);
    void removeModRoutesTo (const juce::String& target, int param);
    void removeModRoutesFrom (int source);
    /** The block (or modulator) a route's target names, or null. Message thread. */
    Block* findModTarget (const juce::String& target);
    /** "Delay > Mix", "LFO 2 > Rate" */
    juce::String describeModTarget (const juce::String& target, int param);
    /** Host tempo and position (audio thread, before process). ppq < 0: not playing. */
    void setTransport (double bpm, double ppq) noexcept { hostPpq = ppq; mods.hostBpm.store (bpm); }

    // v0.4 input / output stage (session settings, shown when you click IN / OUT)
    std::atomic<float> inputPan { 0.0f };                    // -1..1 balance
    std::atomic<bool> inputPhase { false };                  // polarity flip
    std::atomic<float> outputPan { 0.0f };                   // -1..1 balance
    std::atomic<float> outputWidth { 100.0f };               // 0 (mono) .. 200 % stereo width
    std::atomic<float> limiterCeilingDb { -0.3f };           // brickwall ceiling
    std::atomic<float> limiterReleaseMs { 60.0f };

    /** Peak / RMS per channel; the UI takes (and resets) them. */
    struct Meter
    {
        std::atomic<float> peakL { 0 }, peakR { 0 }, rmsL { 0 }, rmsR { 0 };
        std::atomic<int> overs { 0 };   // samples at or above the clip point since the last reset
        void write (const float* l, const float* r, int n, float clipAt) noexcept;
    };
    Meter inMeter, outMeter;
    std::atomic<float> limiterGrDb { 0.0f };   // deepest gain reduction since the UI last looked (negative dB)

    //--- Graph (message thread only) -------------------------------------------
    Chain& getRoot() noexcept { return *root; }
    Node* findNode (const juce::String& id) const;

    struct Location { Chain* chain = nullptr; int index = -1; };
    Location locate (const juce::String& id) const;
    /** The Split + branch index that owns this chain, or nullptr for the main chain. */
    Split* findOwnerSplit (const Chain& chain, int* branchIndex = nullptr) const;

    std::unique_ptr<Node> createNode (NodeType type) const;
    std::unique_ptr<Node> createPlugin (const juce::PluginDescription& desc, juce::String& error);
    /** Rebuilds a node (and anything inside it) from saved state. */
    std::unique_ptr<Node> createFromState (const juce::ValueTree&);

    Node* insert (std::unique_ptr<Node> node, Chain& target, int index);
    /** Swaps a block for another in the same place, keeping its id (so scenes and MIDI still apply).
        keepMappings: the replacement has the same parameters (e.g. a block preset of the same type). */
    Node* replace (const juce::String& id, std::unique_ptr<Node> replacement, bool keepMappings = false);
    /** Exchanges the positions of two blocks (anywhere in the rig). */
    bool swap (const juce::String& idA, const juce::String& idB);
    /** Installs a whole new main chain. splice runs under the audio lock and may move
        nodes (e.g. locked blocks) from the old chain into the new one. */
    void installRoot (std::unique_ptr<Chain> newRoot, const std::function<void (Chain& oldRoot, Chain& newRoot)>& splice = {});
    void remove (const juce::String& id);
    bool move (const juce::String& id, Chain& target, int index);
    Node* duplicate (const juce::String& id);
    Split* wrapInSplit (const juce::String& id, int numBranches = 2, bool copyToEveryPath = false);
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
    /** Built-in starting points: 0 Init, 1 Classic Rig, 2 Dual Amp Rig. They're built fresh
        each time, keeping the amp and cab you have loaded (or the first ones in your library). */
    static const juce::StringArray& getFactoryNames();
    static juce::String getFactoryDescription (int index);
    void loadFactory (int index);
    int getFactoryIndex() const;   // -1 unless a starting point is loaded (and unsaved)

    juce::File getPresetFolder() const;
    juce::Array<juce::File> getPresetFiles() const;
    bool loadPreset (const juce::File&);
    bool savePreset (const juce::File&);
    /** Steps through Init followed by every saved preset. */
    void stepPreset (int delta);
    static constexpr const char* initName = "Init";
    bool isInit() const { return presetFile == juce::File() && presetName == initName; }
    juce::String presetName { initName };
    juce::File presetFile;
    bool dirty = false;
    void markDirty();

    //--- Undo --------------------------------------------------------------------
    /** Snapshot the rig before a big change (randomise, replace, remove...). */
    void checkpoint();
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    /** Groups several edits under one checkpoint. */
    struct UndoGroup
    {
        explicit UndoGroup (Engine& e) : engine (e) { engine.checkpoint(); ++engine.undoSuppress; }
        ~UndoGroup() { --engine.undoSuppress; }
        Engine& engine;
    };

    //--- Block presets ---------------------------------------------------------
    juce::File getBlockPresetFolder (const Node&) const;
    juce::Array<juce::File> getBlockPresets (const Node&) const;
    bool saveBlockPreset (const Node&, const juce::String& name);
    Node* loadBlockPreset (const juce::String& nodeId, const juce::File&);
    /** Back to the block's default settings (keeps the loaded model / IR). */
    Node* initBlock (const juce::String& nodeId);
    /** One of the block's built-in presets (FactoryPresets.h), applied in place (undoable). */
    bool applyFactoryPreset (const juce::String& nodeId, int index);

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
    juce::ApplicationProperties props;   // internal cache only: the scanned plug-in list
    /** Rigsmith.ini: folders, preferences and randomise exclusions, readable and editable.
        Lives in %APPDATA%\Rigsmith (or next to Rigsmith.exe for a portable copy). Created on
        first run if it isn't there, and read every time Rigsmith starts. */
    IniFile ini;
    static juce::File findSettingsFile();
    static juce::File portableSettingsFile();   // Rigsmith.ini next to the running exe
    static juce::File appDataSettingsFile();    // %APPDATA%\Rigsmith\Rigsmith.ini
    /** Saves the settings to another place and uses that from now on (portable <-> AppData). */
    bool moveSettingsTo (const juce::File&);
    juce::File getFolder (const juce::String& key) const;
    void setFolder (const juce::String& key, const juce::File&);
    /** Where block presets live (Settings; default Documents\Rigsmith\Block Presets). */
    juce::File getBlockPresetRoot() const;
    /** Where TONE3000 downloads go: a folder chosen in Settings, or a "TONE3000" folder inside
        the first NAM folder (models) / first IR folder (IRs). */
    juce::File getDownloadFolder (bool irs) const;
    bool hasCustomDownloadFolder() const;
    /** Library folders ("modelPaths", "irPaths"): several folders each, remembered between runs. */
    juce::FileSearchPath getPaths (const juce::String& key) const;
    void setPaths (const juce::String& key, const juce::FileSearchPath&);
    bool getSetting (const juce::String& key, bool def) const;
    void setSetting (const juce::String& key, bool value);
    int getIntSetting (const juce::String& key, int def) const;
    void setIntSetting (const juce::String& key, int value);
    /** Plug-in folders per format (VST3, VST2), remembered in the ini. */
    juce::FileSearchPath getPluginPaths (juce::AudioPluginFormat&) const;
    void setPluginPaths (juce::AudioPluginFormat&, const juce::FileSearchPath&);
    /** True once any plug-in scan has run: after that, startup scans are opt-in. */
    bool hasScannedPlugins() const;
    void markPluginsScanned();
    /** Every NAM capture and IR in the library folders, grouped by gear (see Captures.h). */
    CaptureLibrary captures;
    juce::String scanCrashNotice;
    /** Loads a .nam into a block with every capture of the same gear ready to sweep through
        (at most maxSweep, around the chosen one), positioned on the chosen file.
        diceOnly: leave out captures the dice may not pick. */
    bool loadGear (Sweepable&, const juce::File&, juce::String& error, bool diceOnly = false);
    static constexpr int maxSweep = Sweepable::maxCaptures;
    /** Adds captures to a sweep (ones already there are skipped), keeping the knob where it is. */
    bool addCaptures (Sweepable&, const juce::Array<juce::File>&, juce::String& error);
    /** Every .nam (or IR) in a folder and its sub-folders, in name order (at most maxSweep). */
    static juce::Array<juce::File> capturesInFolder (const juce::File& folder, bool irs);
    /** Plug-ins are scanned in this process with a note in the ini while each one is scanned:
        one that crashes is found there on the next start, listed under [Skipped Plug-ins] and
        never scanned again. Rigsmith itself is never scanned. */
    void setupPluginScanning (bool standalone);
    /** Rigsmith.exe: marks a start in the ini. Returns true if the last start never finished
        (the window never appeared): then Rigsmith starts safe - fresh rig, no plug-in scan. */
    bool beginStartup();
    void finishStartup();
    bool isSafeStart() const noexcept { return safeStart; }
    /** No Rigsmith.ini was found at startup (a first run, or the settings were reset). */
    bool isFirstRun() const noexcept { return firstRun; }
    juce::StringArray getSkippedPlugins() const;
    void clearSkippedPlugins();
    /** A plug-in that crashed Rigsmith during the last scan (once, then empty). */
    juce::String takeScanCrashNotice();
    bool shouldScanOnStartup() const { return ! hasScannedPlugins() || getSetting ("scanOnStartup", false); }

    //--- Randomise include / exclude (the ticks in the library) ---------------------
    bool isRandomExcluded (const juce::File&) const;
    bool isRandomExcluded (const juce::PluginDescription&) const;
    /** How long this plug-in took to load last time (ms): -1 never loaded, -2 failed, -3 hung or crashed
        Rigsmith while loading. The dice only use plug-ins that have loaded quickly before. */
    int getPluginLoadMs (const juce::PluginDescription&) const;
    static constexpr int diceMaxLoadMs = 2500;
    bool isDiceSafe (const juce::PluginDescription& d) const { const int ms = getPluginLoadMs (d); return ms >= 0 && ms <= diceMaxLoadMs; }
    /** Set at startup if a plug-in never finished loading last time. */
    juce::String takeLoadHangNotice() { auto s = loadHangNotice; loadHangNotice.clear(); return s; }
    void setRandomExcluded (const juce::Array<juce::File>&, bool excluded);
    void setRandomExcluded (const juce::Array<juce::PluginDescription>&, bool excluded);
    bool isRandomExcluded (NodeType) const;   // built-in effects
    void setRandomExcluded (const juce::Array<NodeType>&, bool excluded);
    int numRandomExcluded (int kind) const;   // 0 models, 1 IRs, 2 plug-ins, 3 built-ins
    int exclusionsVersion = 0;                // bumped on every change

    //--- Analyzer ---------------------------------------------------------------
    AnalyzerTap analyzer;
    void setAnalyzedNode (Node*);
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
    /** Switches cab IRs out after "Amp + Cab" models (see IrBlock::skipAfterFullRig).
        Runs on every graph change and a few times a second; returns true if anything changed. */
    bool updateAutoSkips();
    void message (const juce::String& text);

    juce::CriticalSection& getLock() noexcept { return lock; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openSettings();
    void loadExclusions();
    void saveExclusions();
    std::map<juce::String, juce::String> excludedFiles;        // lower-case path -> path as written
    std::set<juce::String> excludedPlugins;                     // identifier strings
    std::set<juce::String> excludedBuiltIns;                    // type strings ("chorus")
    std::map<juce::String, juce::String> excludedPluginNames;   // identifier -> "Name (Maker)" for the ini
    void handleMidi (int type, int channel, int number, int value);
    void applyMapping (const MidiMapping&, int value);

    std::unique_ptr<Node> buildNode (const juce::ValueTree&);
    std::unique_ptr<Chain> buildFactory (int index, juce::StringArray& sceneNames);
    juce::Array<juce::File> pickLibraryFiles (bool models, int count) const;
    std::vector<std::array<juce::String, 4>> factoryScenes;
    juce::ValueTree snapshot() const;
    void restore (const juce::ValueTree&);
    std::vector<juce::ValueTree> undoStack, redoStack;
    int undoSuppress = 0;
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
    std::vector<float> keyBuffer;   // the rig's input, for keyed gates and the modulators
    double hostPpq = -1.0;
    bool safeStart = false, firstRun = false;
    juce::String loadHangNotice;
    void pruneModRoutes();
    juce::SmoothedValue<float> inGain, outGain, inPanL, inPanR, outPanL, outPanR, outWidth;
    float limGain = 1.0f;   // zero-latency brickwall limiter state

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
