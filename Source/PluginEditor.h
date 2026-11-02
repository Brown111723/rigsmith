#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/GraphView.h"
#include "UI/Inspector.h"
#include "UI/IoPanel.h"
#include "UI/Library.h"
#include "UI/Header.h"
#include "Engine/Randomiser.h"

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
    void switchCapture (rig::Node&, const juce::File&) override;
    bool loadFileInto (rig::Node* target, rig::Chain* chain, int index, const juce::File&) override;
    bool insertPlugin (const juce::String& identifier, rig::Chain& chain, int index) override;
    bool insertBuiltIn (rig::NodeType, rig::Chain& chain, int index) override;
    /** Where a double-clicked library item goes: after the selected block, else the end of the rig. */
    std::pair<rig::Chain*, int> insertionPoint();
    /** Loads a model / IR where it belongs (see the .cpp). */
    void placeFile (const juce::File&);
    /** TONE3000 catalog in the browser; the picked tone downloads and goes to `into`. */
    void browseTone3000 (const juce::String& format, std::function<void (const juce::File&)> into);
    void randomise (rig::Node&, RandomWhat) override;
    void showRandomMenu (rig::Node&, juce::Component* anchor) override;
    void showBlockPresetMenu (rig::Node&, juce::Component* anchor) override;
    void replaceWith (rig::Node& target, const juce::String& dragDescription) override;
    void toggleLock (rig::Node&) override;

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

    // v0.3: layout, full screen, randomise, undo
    void setLibraryVisible (bool);
    void setFullScreen (bool);
    bool isFullScreen() const;
    void showRigRandomMenu();
    void randomiseRig();
    void doUndo (bool redo);
    int getIntSetting (const juce::String& key, int def) const;
    void setIntSetting (const juce::String& key, int value);

    struct Divider : public juce::Component, public juce::SettableTooltipClient
    {
        Divider() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setRepaintsOnMouseActivity (true); }
        std::function<void (int)> onDrag;
        std::function<void()> onStart, onDoubleClick;
        void paint (juce::Graphics& g) override
        {
            g.fillAll (rig::theme::panel);
            g.setColour (rig::theme::border);
            g.drawHorizontalLine (0, 0.0f, (float) getWidth());
            g.setColour (isMouseOverOrDragging() ? rig::theme::accent : rig::theme::borderHi);
            g.fillRoundedRectangle ((float) getWidth() * 0.5f - 22.0f, (float) getHeight() * 0.5f - 1.5f, 44.0f, 3.0f, 1.5f);
        }
        void mouseDown (const juce::MouseEvent&) override { if (onStart) onStart(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY()); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    };

    RigsmithProcessor& processor;
    rig::Engine& engine;
    rig::RigLookAndFeel lnf;

    rig::Header header;
    juce::Viewport viewport;
    rig::GraphView graph;
    rig::Inspector inspector;
    rig::IoPanel ioPanel { *this };
    std::unique_ptr<rig::t3k::Client> tone3000;   // before the library, which shows it
    rig::Library library;
    rig::TunerOverlay tunerOverlay;
    rig::Randomiser randomiser { engine };
    Divider divider;
    juce::Label statusLabel;
    bool libraryVisible = true;
    int inspectorHeight = 330, dragStartHeight = 330;
    juce::TooltipWindow tooltips { this, 700 };

    juce::OwnedArray<PluginWindow> pluginWindows;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String selectedId, toast;
    juce::uint32 toastTime = 0;
    rig::IoLevels levels;
    bool isIoSelected() const { return selectedId == rig::kInputId || selectedId == rig::kOutputId; }
    void showPanelFor (const juce::String& id);
    rig::Tuner::Reading reading;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigsmithEditor)
};
