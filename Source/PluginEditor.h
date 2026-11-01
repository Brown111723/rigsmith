#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/GraphView.h"
#include "UI/Inspector.h"
#include "UI/Library.h"
#include "UI/Header.h"

class PluginWindow;

class RigsmithEditor : public juce::AudioProcessorEditor,
                       public rig::GraphHost,
                       public juce::DragAndDropContainer,
                       public juce::FileDragAndDropTarget,
                       private rig::Engine::Listener,
                       private juce::ChangeListener,
                       private juce::Timer,
                       private juce::AsyncUpdater
{
public:
    explicit RigsmithEditor (RigsmithProcessor&);
    ~RigsmithEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // GraphHost
    rig::Engine& getEngine() override { return engine; }
    juce::String getSelectedId() const override { return selectedId; }
    void select (const juce::String& id) override;
    void openNode (rig::Node&) override;
    void showAddMenu (rig::Chain&, int index, juce::Component* anchor) override;
    void showNodeMenu (rig::Node&, juce::Component* anchor) override;
    void showBranchMenu (rig::Split&, int branch, juce::Component* anchor) override;
    void showReplaceMenu (rig::Node&, juce::Component* anchor) override;
    void browseFile (rig::Node&, int delta) override;
    bool loadFileInto (rig::Node* target, rig::Chain* chain, int index, const juce::File&) override;
    bool insertPlugin (const juce::String& identifier, rig::Chain& chain, int index) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    // Engine::Listener
    void graphChanged() override { triggerAsyncUpdate(); }
    void nodeWillBeDeleted (rig::Node&) override;
    void sceneChanged() override { repaint(); inspector.repaint(); }
    void presetChanged() override {}
    void midiMapChanged() override {}
    void tunerToggled() override { updateTuner(); }
    void showMessage (const juce::String& text) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { library.refresh(); }
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void populateAddMenu (juce::PopupMenu&, int idOffset, bool includeRouting);
    std::unique_ptr<rig::Node> createFromMenu (int result, bool& openAfter);
    void handleAdd (int result, const juce::String& chainId, int index);
    void handleReplace (int result, const juce::String& nodeId);
    void showSettings();
    void scanForNewPlugins (bool reportNone);
    void showPresetMenu();
    void showMainMenu();
    void showSceneMenu (int scene);
    void savePreset (bool askForName);
    juce::PluginListComponent* showPluginScanner();
    void openPluginWindow (rig::PluginBlock&);
    void chooseFileFor (rig::Node&);
    void updateTuner();
    rig::Chain* chainById (const juce::String& id);

    RigsmithProcessor& processor;
    rig::Engine& engine;
    rig::RigLookAndFeel lnf;

    rig::Header header;
    juce::Viewport viewport;
    rig::GraphView graph;
    rig::Inspector inspector;
    rig::Library library;
    rig::TunerOverlay tunerOverlay;
    juce::TooltipWindow tooltips { this, 700 };

    juce::OwnedArray<PluginWindow> pluginWindows;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String selectedId, toast;
    juce::uint32 toastTime = 0;
    float inMeter = 0, outMeter = 0;
    rig::Tuner::Reading reading;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigsmithEditor)
};
