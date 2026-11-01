#include "PluginEditor.h"
#include "UI/Settings.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #define RIGSMITH_HAS_STANDALONE_HOLDER 1
#endif

using namespace rig;
using namespace rig::theme;

//==============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (PluginBlock& block, juce::LookAndFeel& lf, std::function<void (PluginWindow*)> closeFn)
        : DocumentWindow (block.getName() + " - Rigsmith", bg, DocumentWindow::closeButton),
          nodeId (block.id), onCloseFn (std::move (closeFn))
    {
        setLookAndFeel (&lf);
        auto* inst = block.getInstance();
        juce::AudioProcessorEditor* ed = inst->hasEditor() ? inst->createEditorAndMakeActive() : nullptr;
        if (ed == nullptr)
            ed = new juce::GenericAudioProcessorEditor (*inst);
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~PluginWindow() override
    {
        clearContentComponent();
        setLookAndFeel (nullptr);
    }

    void closeButtonPressed() override { onCloseFn (this); }

    const juce::String nodeId;

private:
    std::function<void (PluginWindow*)> onCloseFn;
};

//==============================================================================
RigsmithEditor::RigsmithEditor (RigsmithProcessor& p)
    : AudioProcessorEditor (p), processor (p), engine (p.engine),
      header (engine), graph (*this), inspector (*this), library (*this), tunerOverlay (engine)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    addAndMakeVisible (header);
    viewport.setViewedComponent (&graph, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);   // keys (scenes, presets, bypass) belong to the rig, not to scrolling
    graph.onBackgroundClick = [this] { select ({}); grabKeyboardFocus(); };
    addAndMakeVisible (viewport);
    addAndMakeVisible (inspector);
    addAndMakeVisible (library);
    addAndMakeVisible (divider);
    statusLabel.setFont (mono (11.0f));
    statusLabel.setColour (juce::Label::textColourId, dim);
    statusLabel.setInterceptsMouseClicks (false, false);
    statusLabel.setJustificationType (juce::Justification::bottomLeft);
    addAndMakeVisible (statusLabel);
    addChildComponent (tunerOverlay);

    divider.onStart = [this] { dragStartHeight = inspectorHeight; };
    divider.onDrag = [this] (int dy)
    {
        inspectorHeight = juce::jlimit (220, juce::jmax (220, getHeight() - 64 - 160), dragStartHeight - dy);
        resized();
    };
    divider.onDoubleClick = [this]
    {
        inspectorHeight = inspectorHeight < 420 ? juce::jmax (330, (getHeight() - 64) * 6 / 10) : 330;
        resized();
        setIntSetting ("inspectorHeight", inspectorHeight);
    };
    divider.setTooltip ("Drag to resize the panel. Double-click to make it tall / normal");

    libraryVisible = engine.getSetting ("libraryVisible", true);
    inspectorHeight = getIntSetting ("inspectorHeight", 330);
    randomiser.options.usePlugins = engine.getSetting ("randomUsePlugins", true);
    randomiser.options.allowSplits = engine.getSetting ("randomSplits", true);

    header.onUndo = [this] { doUndo (false); };
    header.onRedo = [this] { doUndo (true); };
    header.onRandom = [this] { randomiseRig(); };
    header.onRandomMenu = [this] { showRigRandomMenu(); };
    header.onLibrary = [this] { setLibraryVisible (! libraryVisible); };
    header.onFullScreen = [this] { setFullScreen (! isFullScreen()); };
    header.showFullScreenButton = processor.isStandalone();
    header.library.setToggleState (libraryVisible, juce::dontSendNotification);

    header.onPresetMenu = [this] { showPresetMenu(); };
    header.onSave = [this] { savePreset (false); };
    header.onMenu = [this] { showMainMenu(); };
    header.onTuner = [this] { engine.tunerActive = ! engine.tunerActive.load(); updateTuner(); };
    header.onSceneRightClick = [this] (int s) { showSceneMenu (s); };
    tunerOverlay.onClose = [this] { engine.tunerActive = false; updateTuner(); };

    inspector.onOpen = [this] (Node& n) { openNode (n); };
    inspector.onLearnParam = [this] (Node& n, int param)
    {
        MidiMapping m;
        m.target = MidiMapping::Target::Param;
        m.nodeId = n.id;
        m.index = param;
        engine.startLearn (m);
    };

    library.onActivateFile = [this] (const juce::File& f)
    {
        auto* sel = engine.findNode (selectedId);
        auto* target = sel != nullptr && ((sel->getType() == NodeType::Nam && f.hasFileExtension ("nam"))
                                          || (sel->getType() == NodeType::Ir && ! f.hasFileExtension ("nam"))) ? sel : nullptr;
        loadFileInto (target, target == nullptr ? &engine.getRoot() : nullptr, -1, f);
    };
    library.onActivatePlugin = [this] (const juce::String& ident) { insertPlugin (ident, engine.getRoot(), -1); };
    library.onScanPlugins = [this] { showPluginScanner(); };
    library.onManageFolders = [this] { showSettings(); };

    engine.addListener (this);
    engine.knownPlugins.addChangeListener (this);

   #if RIGSMITH_HAS_STANDALONE_HOLDER
    // a guitar rig wants to hear its input: JUCE's standalone wrapper mutes it by default
    if (processor.isStandalone())
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    if (processor.isStandalone() && engine.getSetting ("fullScreen", false))
        juce::Timer::callAfterDelay (400, [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
        {
            if (sp != nullptr) sp->setFullScreen (true);
        });

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1180, 740, 4000, 2600);
    setSize (1440, 900);
    updateTuner();
    startTimerHz (30);

    // once per run: pick up plug-ins installed since last time
    static bool scannedThisRun = false;
    if (! scannedThisRun && engine.getSetting ("scanOnStartup", true))
    {
        scannedThisRun = true;
        juce::Timer::callAfterDelay (1500, [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
        {
            if (sp != nullptr) sp->scanForNewPlugins (false);
        });
    }
}

RigsmithEditor::~RigsmithEditor()
{
    stopTimer();
    setIntSetting ("inspectorHeight", inspectorHeight);
    if (juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent())
        juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
    engine.cancelLearn();
    engine.knownPlugins.removeChangeListener (this);
    engine.removeListener (this);
    pluginWindows.clear();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void RigsmithEditor::paint (juce::Graphics& g) { g.fillAll (bg); }

void RigsmithEditor::paintOverChildren (juce::Graphics& g)
{
    if (toast.isEmpty()) return;
    const auto age = juce::Time::getMillisecondCounter() - toastTime;
    const float alpha = engine.isLearning() ? 1.0f : juce::jlimit (0.0f, 1.0f, (5000.0f - (float) age) / 600.0f);
    if (alpha <= 0) return;

    const auto f = font (14.0f, true);
    const int w = juce::jmin (viewport.getWidth() - 40, juce::GlyphArrangement::getStringWidthInt (f, toast) + 48);
    const auto r = juce::Rectangle<int> (viewport.getX() + (viewport.getWidth() - w) / 2, viewport.getBottom() - 60, w, 40).toFloat();
    g.setColour (card.withAlpha (0.97f * alpha));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour ((engine.isLearning() ? accent : borderHi).withAlpha (alpha));
    g.drawRoundedRectangle (r, 10.0f, 1.5f);
    g.setColour (text.withAlpha (alpha));
    g.setFont (f);
    g.drawFittedText (toast, r.toNearestInt().reduced (16, 0), juce::Justification::centred, 1);
}

void RigsmithEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (64));
    library.setVisible (libraryVisible);
    if (libraryVisible) library.setBounds (r.removeFromRight (300));
    const int ih = juce::jlimit (220, juce::jmax (220, r.getHeight() - 160), inspectorHeight);
    inspector.setBounds (r.removeFromBottom (ih));
    divider.setBounds (r.removeFromBottom (8));
    viewport.setBounds (r);
    statusLabel.setBounds (r.getX() + 12, r.getBottom() - 46, 260, 34);
    tunerOverlay.setBounds (r);
    graph.fitTo (viewport.getLocalBounds().withTrimmedBottom (viewport.getScrollBarThickness()));
}

void RigsmithEditor::showMessage (const juce::String& message)
{
    toast = message;
    toastTime = juce::Time::getMillisecondCounter();
    repaint();
}

void RigsmithEditor::updateTuner()
{
    tunerOverlay.setVisible (engine.tunerActive.load());
    if (tunerOverlay.isVisible()) tunerOverlay.toFront (false);
    header.repaint();
}

//==============================================================================
void RigsmithEditor::timerCallback()
{
    ++tick;
    inMeter = juce::jmax (engine.inPeak.exchange (0.0f), inMeter * 0.86f);
    outMeter = juce::jmax (engine.outPeak.exchange (0.0f), outMeter * 0.86f);
    graph.setMeters (inMeter, outMeter);

    if (engine.tunerActive.load() && tick % 2 == 0)
    {
        reading = engine.tuner.analyse();
        tunerOverlay.setReading (reading);
    }
    else if (! engine.tunerActive.load())
    {
        reading = {};
    }

    const double sr = engine.getSampleRate();
    const int lat = engine.getLatency();
    juce::String status;
    status << juce::String (sr / 1000.0, 1) << " kHz  " << engine.getBlockSize() << " smp  "
           << juce::String ((engine.getBlockSize() + lat) / sr * 1000.0, 1) << " ms\n"
           << "CPU " << juce::roundToInt (processor.getCpuLoad() * 100.0) << "%";
    if (lat > 0) status << "  PDC " << lat;
    header.refresh (reading, status);
    if (tick % 10 == 0) statusLabel.setText (status, juce::dontSendNotification);

    if (tick % 2 == 0)
        graph.repaint();

    if (toast.isNotEmpty() && ! engine.isLearning() && juce::Time::getMillisecondCounter() - toastTime > 5000)
        toast.clear();
    if (toast.isNotEmpty())
        repaint (viewport.getBounds().removeFromBottom (70));
}

void RigsmithEditor::handleAsyncUpdate()
{
    const auto viewPos = viewport.getViewPosition();
    graph.rebuild();
    graph.setVisible (true);
    if (isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
        grabKeyboardFocus();
    graph.fitTo (viewport.getLocalBounds().withTrimmedBottom (viewport.getScrollBarThickness()));
    viewport.setViewPosition (viewPos);
    auto* n = engine.findNode (selectedId);
    if (n == nullptr) selectedId.clear();
    if (inspector.getNode() != n) inspector.setNode (n);
    else if (dynamic_cast<Split*> (n) != nullptr) inspector.setNode (n);  // path count may have changed
}

void RigsmithEditor::nodeWillBeDeleted (Node& n)
{
    // The graph's cards hold references to nodes; stop them painting or reacting
    // until the async rebuild replaces them (it can't be torn down here, as we may be
    // inside one of its own callbacks).
    graph.setVisible (false);
    triggerAsyncUpdate();

    for (int i = pluginWindows.size(); --i >= 0;)
        if (pluginWindows[i]->nodeId == n.id)
            pluginWindows.remove (i);
    if (inspector.getNode() == &n)
        inspector.setNode (nullptr);
}

//==============================================================================
void RigsmithEditor::select (const juce::String& id)
{
    if (selectedId == id) return;
    selectedId = id;
    inspector.setNode (engine.findNode (id));
    graph.repaint();
}

rig::Chain* RigsmithEditor::chainById (const juce::String& id)
{
    return dynamic_cast<Chain*> (engine.findNode (id));
}

void RigsmithEditor::openNode (Node& n)
{
    if (auto* pb = dynamic_cast<PluginBlock*> (&n))
    {
        if (pb->isMissing()) showMessage (pb->getName() + " isn't installed on this computer");
        else openPluginWindow (*pb);
    }
    else if (n.getType() == NodeType::Nam || n.getType() == NodeType::Ir)
    {
        chooseFileFor (n);
    }
}

void RigsmithEditor::openPluginWindow (PluginBlock& pb)
{
    for (auto* w : pluginWindows)
        if (w->nodeId == pb.id) { w->toFront (true); return; }

    pluginWindows.add (new PluginWindow (pb, lnf, [this] (PluginWindow* w)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RigsmithEditor> (this), w]
        {
            if (sp != nullptr) sp->pluginWindows.removeObject (w);
        });
    }));
}

void RigsmithEditor::chooseFileFor (Node& n)
{
    const bool isNam = n.getType() == NodeType::Nam;
    const auto id = n.id;
    chooser = std::make_unique<juce::FileChooser> (isNam ? "Load a NAM model" : "Load a cab IR",
                                                   juce::File (engine.getPaths (isNam ? "modelPaths" : "irPaths").getRawString (0)),
                                                   isNam ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, id] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            if (auto* node = engine.findNode (id))
                loadFileInto (node, nullptr, -1, f);
    });
}

bool RigsmithEditor::loadFileInto (Node* target, Chain* chain, int index, const juce::File& f)
{
    if (f.hasFileExtension ("rig"))
        return engine.loadPreset (f);

    const bool isNam = f.hasFileExtension ("nam");
    const bool isIr = f.hasFileExtension ("wav;aif;aiff;flac");
    if (! isNam && ! isIr)
    {
        showMessage ("Rigsmith loads .nam models, .wav IRs and .rig presets");
        return false;
    }

    juce::String err;
    if (target != nullptr)
    {
        bool ok = false;
        if (auto* nb = dynamic_cast<NamBlock*> (target); nb != nullptr && isNam) ok = nb->loadModel (f, err);
        else if (auto* ir = dynamic_cast<IrBlock*> (target); ir != nullptr && isIr) ok = ir->loadIr (f, err);
        else err = "That file doesn't fit this block";

        if (! ok) { showMessage (err); return false; }
        engine.markDirty();
        showMessage ("Loaded " + f.getFileNameWithoutExtension());
        select (target->id);
        inspector.setNode (target);
        graph.repaint();
        return true;
    }

    if (chain == nullptr) chain = &engine.getRoot();
    std::unique_ptr<Node> n;
    if (isNam)
    {
        auto nb = std::make_unique<NamBlock>();
        if (! nb->loadModel (f, err)) { showMessage (err); return false; }
        n = std::move (nb);
    }
    else
    {
        auto ir = std::make_unique<IrBlock>();
        if (! ir->loadIr (f, err)) { showMessage (err); return false; }
        n = std::move (ir);
    }
    if (auto* added = engine.insert (std::move (n), *chain, index))
    {
        selectedId = added->id;
        showMessage ("Added " + f.getFileNameWithoutExtension());
    }
    return true;
}

bool RigsmithEditor::insertPlugin (const juce::String& identifier, Chain& chain, int index)
{
    auto desc = engine.knownPlugins.getTypeForIdentifierString (identifier);
    if (desc == nullptr) { showMessage ("Plug-in not found - try scanning again"); return false; }

    juce::String err;
    auto node = engine.createPlugin (*desc, err);
    if (node == nullptr) { showMessage ("Couldn't load " + desc->name + ": " + err); return false; }

    auto* added = engine.insert (std::move (node), chain, index);
    if (added == nullptr) return false;
    selectedId = added->id;
    if (auto* pb = dynamic_cast<PluginBlock*> (added))
        openPluginWindow (*pb);
    return true;
}

//==============================================================================
enum AddIds
{
    addSplit2 = 10, addSplit3, addXover, addStereo, addScan = 20,
    addBuiltInBase = 100,           // + (int) NodeType
    addPluginBase = 1000,
    insertOffset = 50000, replaceOffset = 70000
};

static const std::vector<std::pair<juce::String, std::vector<NodeType>>>& builtInGroups()
{
    static const std::vector<std::pair<juce::String, std::vector<NodeType>>> groups {
        { "Dynamics",       { NodeType::Gate, NodeType::Comp, NodeType::Limiter, NodeType::Expander, NodeType::Transient, NodeType::Swell } },
        { "Drive",          { NodeType::Overdrive, NodeType::Fuzz, NodeType::Boost, NodeType::Crusher } },
        { "EQ & Filter",    { NodeType::Eq, NodeType::Wah } },
        { "Modulation",     { NodeType::Chorus, NodeType::Flanger, NodeType::Phaser, NodeType::Tremolo, NodeType::Vibe, NodeType::Rotary } },
        { "Delay & Reverb", { NodeType::Delay, NodeType::Reverb } },
        { "Pitch",          { NodeType::Pitch } },
        { "Utility",        { NodeType::Gain } } };
    return groups;
}

static juce::String builtInMenuName (NodeType t)
{
    switch (t)
    {
        case NodeType::Gate:      return "Noise Gate";
        case NodeType::Comp:      return "Compressor";
        case NodeType::Limiter:   return "Limiter";
        case NodeType::Expander:  return "Expander";
        case NodeType::Transient: return "Transient Shaper";
        case NodeType::Swell:     return "Auto Swell (violin swells)";
        case NodeType::Overdrive: return "Overdrive";
        case NodeType::Fuzz:      return "Fuzz";
        case NodeType::Boost:     return "Clean Boost";
        case NodeType::Crusher:   return "Bit Crusher";
        case NodeType::Eq:        return "Studio EQ";
        case NodeType::Wah:       return "Wah / Auto-Wah";
        case NodeType::Chorus:    return "Chorus";
        case NodeType::Flanger:   return "Flanger";
        case NodeType::Phaser:    return "Phaser";
        case NodeType::Tremolo:   return "Tremolo";
        case NodeType::Vibe:      return "Uni-Vibe";
        case NodeType::Rotary:    return "Rotary Speaker";
        case NodeType::Delay:     return "Delay (digital / tape / ping-pong / dual)";
        case NodeType::Reverb:    return "Reverb (room / hall / plate / spring / shimmer)";
        case NodeType::Pitch:     return "Pitch / Octave / Doubler";
        case NodeType::Gain:      return "Volume / Pan";
        default:                  return "Block";
    }
}

void RigsmithEditor::populateAddMenu (juce::PopupMenu& m, int o, bool includeRouting)
{
    m.addSectionHeader ("Amp & cab");
    m.addItem (o + addBuiltInBase + (int) NodeType::Nam, "NAM Amp");
    m.addItem (o + addBuiltInBase + (int) NodeType::Ir, "Cab IR");
    m.addSectionHeader ("Built-in effects");
    for (auto& [group, types] : builtInGroups())
    {
        juce::PopupMenu sub;
        for (auto t : types) sub.addItem (o + addBuiltInBase + (int) t, builtInMenuName (t));
        m.addSubMenu (group, sub);
    }
    if (includeRouting)
    {
        m.addSectionHeader ("Routing");
        m.addItem (o + addSplit2, "Split - 2 paths");
        m.addItem (o + addSplit3, "Split - 3 paths");
        m.addItem (o + addXover, "Crossover - lows / mids / highs");
        m.addItem (o + addStereo, "Stereo L/R - dual amp");
    }
    m.addSectionHeader ("Plug-ins");

    auto types = engine.knownPlugins.getTypes();
    if (types.isEmpty())
    {
        m.addItem (o + addScan, "Scan for plug-ins...");
        return;
    }

    std::map<juce::String, juce::PopupMenu> groups;
    for (int i = 0; i < types.size(); ++i)
    {
        if (types[i].isInstrument) continue;
        juce::String key = PluginBlock::autoCategorise ? juce::String::formatted ("%02d", (int) categoriseDescription (types[i])) + categoryName (categoriseDescription (types[i]))
                                                       : (types[i].manufacturerName.isEmpty() ? juce::String ("Other") : types[i].manufacturerName);
        groups[key].addItem (o + addPluginBase + i, types[i].name + (PluginBlock::autoCategorise ? "  (" + types[i].manufacturerName + ")" : juce::String()));
    }
    for (auto& [key, sub] : groups)
        m.addSubMenu (PluginBlock::autoCategorise ? key.substring (2) : key, sub);
    m.addSeparator();
    m.addItem (o + addScan, "Scan for new plug-ins...");
}

std::unique_ptr<Node> RigsmithEditor::createFromMenu (int r, bool& openAfter)
{
    openAfter = false;
    if (r >= addPluginBase)
    {
        auto types = engine.knownPlugins.getTypes();
        const int i = r - addPluginBase;
        if (! juce::isPositiveAndBelow (i, types.size())) return nullptr;
        juce::String err;
        auto n = engine.createPlugin (types[i], err);
        if (n == nullptr) showMessage ("Couldn't load " + types[i].name + ": " + err);
        openAfter = n != nullptr;
        return n;
    }
    if (r >= addBuiltInBase)
    {
        const auto t = (NodeType) (r - addBuiltInBase);
        openAfter = t == NodeType::Nam || t == NodeType::Ir;
        return engine.createNode (t);
    }
    return nullptr;
}

void RigsmithEditor::handleAdd (int r, const juce::String& chainId, int index)
{
    auto* chain = chainById (chainId);
    if (r <= 0 || chain == nullptr) return;

    switch (r)
    {
        case addScan: showPluginScanner(); return;
        case addSplit2: case addSplit3: case addXover: case addStereo:
        {
            auto* s = engine.addSplit (*chain, index, r == addSplit2 || r == addStereo ? 2 : 3);
            if (s == nullptr) return;
            if (r == addXover)  s->setMode (SplitMode::Crossover);
            if (r == addStereo) s->setMode (SplitMode::Stereo);
            // pre-fill each path with an amp + cab so it makes sound straight away
            if (r != addXover)
                for (auto& b : s->branches)
                {
                    engine.insert (engine.createNode (NodeType::Nam), *b->chain, -1);
                    engine.insert (engine.createNode (NodeType::Ir), *b->chain, -1);
                }
            engine.storeScene();
            selectedId = s->id;
            return;
        }
        default: break;
    }

    bool openAfter = false;
    if (auto n = createFromMenu (r, openAfter))
        if (auto* added = engine.insert (std::move (n), *chain, index))
        {
            selectedId = added->id;
            if (openAfter) openNode (*added);
        }
}

void RigsmithEditor::handleReplace (int r, const juce::String& id)
{
    if (r == addScan) { showPluginScanner(); return; }
    bool openAfter = false;
    if (auto n = createFromMenu (r, openAfter))
        if (auto* replaced = engine.replace (id, std::move (n)))
        {
            selectedId = replaced->id;
            showMessage ("Replaced with " + replaced->getName());
            if (openAfter) openNode (*replaced);
        }
}

void RigsmithEditor::showReplaceMenu (Node& node, juce::Component* anchor)
{
    const auto id = node.id;
    juce::PopupMenu m;
    if (node.getType() == NodeType::Nam) { m.addItem (1, "Different model..."); m.addSeparator(); }
    if (node.getType() == NodeType::Ir)  { m.addItem (1, "Different IR...");    m.addSeparator(); }
    populateAddMenu (m, replaceOffset, false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, id] (int r)
    {
        auto* n = engine.findNode (id);
        if (n == nullptr || r == 0) return;
        if (r == 1) { chooseFileFor (*n); return; }
        handleReplace (r - replaceOffset, id);
    });
}

void RigsmithEditor::browseFile (Node& n, int delta)
{
    juce::File current;
    if (auto* nb = dynamic_cast<NamBlock*> (&n)) current = nb->getFile();
    else if (auto* ir = dynamic_cast<IrBlock*> (&n)) current = ir->getFile();
    const bool isNam = n.getType() == NodeType::Nam;

    juce::Array<juce::File> files;
    const auto folder = current.existsAsFile() ? current.getParentDirectory()
                                               : juce::File (engine.getPaths (isNam ? "modelPaths" : "irPaths").getRawString (0));
    files = folder.findChildFiles (juce::File::findFiles, false, isNam ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac");
    if (files.isEmpty()) { showMessage ("No other files in " + folder.getFileName()); return; }
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    int idx = files.indexOf (current);
    idx = idx < 0 ? 0 : (idx + delta + files.size()) % files.size();
    loadFileInto (&n, nullptr, -1, files[idx]);
}

void RigsmithEditor::showAddMenu (Chain& chain, int index, juce::Component* anchor)
{
    juce::PopupMenu m;
    populateAddMenu (m, 0, true);
    const auto chainId = chain.id;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                     [this, chainId, index] (int r) { handleAdd (r, chainId, index); });
}

void RigsmithEditor::showNodeMenu (Node& node, juce::Component* anchor)
{
    const auto id = node.id;
    auto* split = dynamic_cast<Split*> (&node);
    auto loc = engine.locate (id);

    juce::PopupMenu m;
    if (node.getType() == NodeType::Nam)    m.addItem (1, "Load model...");
    if (node.getType() == NodeType::Ir)     m.addItem (1, "Load IR...");
    if (node.getType() == NodeType::Plugin) m.addItem (1, "Open plug-in window");
    m.addItem (2, "Bypass", true, node.bypassed.load());
    m.addItem (30, "Gain compensation (auto level match)", true, node.autoGain.load());
    m.addItem (43, "Lock (keep when randomising the rig)", true, node.locked.load());
    m.addItem (8, "MIDI learn: bypass footswitch");
    m.addSeparator();
    m.addItem (40, "Randomise settings  (R)");
    m.addItem (41, "Mutate settings (subtle)");
    if (split == nullptr)
    {
        m.addItem (42, "Random " + categoryName (Randomiser::categoryOf (node)) + " block  (Shift+R)");
        m.addItem (44, "Block presets...");
    }
    m.addSeparator();
    if (split != nullptr)
    {
        m.addItem (20, "Add path", (int) split->branches.size() < kMaxBranches);
        juce::PopupMenu modes;
        for (int i = 0; i < 4; ++i)
            modes.addItem (21 + i, splitModeName ((SplitMode) i), true, (int) split->getMode() == i);
        m.addSubMenu ("Mode", modes);
        m.addItem (26, "MIDI learn: A/B blend (expression pedal)", split->branches.size() >= 2);
        m.addItem (25, "Remove split, keep path A");
    }
    else
    {
        m.addItem (4, "Wrap in split (2 paths)");
        m.addItem (5, "Wrap in split (3 paths)");
    }
    m.addItem (3, "Duplicate");
    m.addItem (6, "Move left", loc.index > 0);
    m.addItem (7, "Move right", loc.chain != nullptr && loc.index < (int) loc.chain->nodes.size() - 1);

    juce::PopupMenu insertBefore;
    populateAddMenu (insertBefore, insertOffset, true);
    m.addSubMenu ("Insert before", insertBefore);
    if (split == nullptr)
    {
        juce::PopupMenu replaceWith;
        populateAddMenu (replaceWith, replaceOffset, false);
        m.addSubMenu ("Replace with", replaceWith);
    }
    m.addSeparator();
    m.addItem (9, "Remove");

    const auto chainId = loc.chain != nullptr ? loc.chain->id : juce::String();
    const int index = loc.index;

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, id, chainId, index] (int r)
    {
        auto* n = engine.findNode (id);
        if (n == nullptr || r == 0) return;
        if (r >= replaceOffset) { handleReplace (r - replaceOffset, id); return; }
        if (r >= insertOffset)  { handleAdd (r - insertOffset, chainId, index); return; }
        auto* s = dynamic_cast<Split*> (n);
        auto* chain = chainById (chainId);
        switch (r)
        {
            case 1: openNode (*n); break;
            case 2: n->bypassed = ! n->bypassed.load(); engine.storeScene(); engine.markDirty(); break;
            case 3: if (auto* d = engine.duplicate (id)) selectedId = d->id; break;
            case 4: engine.wrapInSplit (id, 2); break;
            case 5: engine.wrapInSplit (id, 3); break;
            case 6: if (chain != nullptr) engine.move (id, *chain, index - 1); break;
            case 7: if (chain != nullptr) engine.move (id, *chain, index + 2); break;
            case 8: { MidiMapping mm; mm.target = MidiMapping::Target::Bypass; mm.nodeId = id; engine.startLearn (mm); break; }
            case 9: engine.remove (id); break;
            case 30: n->autoGain = ! n->autoGain.load(); if (n->autoGain.load()) n->requestRelearn(); engine.markDirty(); break;
            case 40: randomise (*n, RandomSettings); break;
            case 41: randomise (*n, RandomMutate); break;
            case 42: randomise (*n, RandomType); break;
            case 43: toggleLock (*n); break;
            case 44: showBlockPresetMenu (*n, &inspector); break;
            case 20: if (s) engine.addBranch (*s); break;
            case 21: case 22: case 23: case 24:
                if (s) { s->setMode ((SplitMode) (r - 21)); engine.storeScene(); engine.markDirty(); inspector.setNode (s); }
                break;
            case 25: if (s) engine.unwrapSplit (*s); break;
            case 26: { MidiMapping mm; mm.target = MidiMapping::Target::Blend; mm.nodeId = id; engine.startLearn (mm); break; }
            default: break;
        }
        graph.repaint();
    });
}

void RigsmithEditor::showBranchMenu (Split& split, int branch, juce::Component* anchor)
{
    if (! juce::isPositiveAndBelow (branch, (int) split.branches.size())) return;
    auto& b = *split.branches[(size_t) branch];
    const auto id = split.id;

    juce::PopupMenu m;
    m.addSectionHeader ("Path " + juce::String::charToString ((juce::juce_wchar) ('A' + branch)));
    m.addItem (1, "Mute", true, b.mute.load());
    m.addItem (3, "Solo");
    m.addItem (2, "Invert polarity", true, b.phase.load());
    m.addItem (4, "Pan left", true, b.pan.load() < -0.99f);
    m.addItem (5, "Pan centre", true, std::abs (b.pan.load()) < 0.01f);
    m.addItem (6, "Pan right", true, b.pan.load() > 0.99f);
    m.addItem (7, "MIDI learn: path level");
    m.addSeparator();
    m.addItem (8, "Add path", (int) split.branches.size() < kMaxBranches);
    m.addItem (9, "Remove this path", split.branches.size() > 1);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, id, branch] (int r)
    {
        auto* s = dynamic_cast<Split*> (engine.findNode (id));
        if (s == nullptr || r == 0 || ! juce::isPositiveAndBelow (branch, (int) s->branches.size())) return;
        auto& br = *s->branches[(size_t) branch];
        switch (r)
        {
            case 1: br.mute = ! br.mute.load(); break;
            case 2: br.phase = ! br.phase.load(); break;
            case 3: for (int i = 0; i < (int) s->branches.size(); ++i) s->branches[(size_t) i]->mute = i != branch; break;
            case 4: br.pan = -1.0f; break;
            case 5: br.pan = 0.0f; break;
            case 6: br.pan = 1.0f; break;
            case 7: { MidiMapping mm; mm.target = MidiMapping::Target::BranchLevel; mm.nodeId = id; mm.index = branch; engine.startLearn (mm); return; }
            case 8: engine.addBranch (*s); return;
            case 9: engine.removeBranch (*s, branch); return;
            default: return;
        }
        engine.storeScene();
        engine.markDirty();
        graph.repaint();
    });
}

//==============================================================================
void RigsmithEditor::showPresetMenu()
{
    auto files = engine.getPresetFiles();
    juce::PopupMenu m;
    m.addItem (3, juce::String (Engine::initName) + "  (gate, amp, cab)", true, engine.isInit());
    m.addSeparator();
    if (files.isEmpty())
        m.addItem (-1, "No presets yet - save one!", false);

    std::map<juce::String, juce::PopupMenu> folders;
    const auto root = engine.getPresetFolder();
    for (int i = 0; i < files.size(); ++i)
    {
        const bool current = files[i] == engine.presetFile;
        if (files[i].getParentDirectory() == root)
            m.addItem (100 + i, files[i].getFileNameWithoutExtension(), true, current);
        else
            folders[files[i].getParentDirectory().getFileName()].addItem (100 + i, files[i].getFileNameWithoutExtension(), true, current);
    }
    for (auto& [name, sub] : folders)
        m.addSubMenu (name, sub);
    m.addSeparator();
    m.addItem (1, "Save");
    m.addItem (2, "Save as...");
    m.addItem (4, "Open preset file...");
    m.addItem (5, "Show presets folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.presetButton), [this, files] (int r)
    {
        if (r == 1) savePreset (false);
        else if (r == 2) savePreset (true);
        else if (r == 3) { engine.loadDefaultRig(); selectedId.clear(); }
        else if (r == 4)
        {
            chooser = std::make_unique<juce::FileChooser> ("Open preset", engine.getPresetFolder(), "*.rig");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) engine.loadPreset (fc.getResult()); });
        }
        else if (r == 5) engine.getPresetFolder().startAsProcess();
        else if (r >= 100 && r - 100 < files.size()) engine.loadPreset (files[r - 100]);
    });
}

void RigsmithEditor::savePreset (bool askForName)
{
    if (! askForName && engine.presetFile.existsAsFile())
    {
        engine.savePreset (engine.presetFile);
        return;
    }

    auto* w = new juce::AlertWindow ("Save preset", "Give this rig a name", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", engine.isInit() || engine.presetName == "Random Rig" ? juce::String() : engine.presetName, "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1) return;
        const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty()) return;
        const auto f = engine.getPresetFolder().getChildFile (name + ".rig");
        if (f.existsAsFile())
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                "A preset called \"" + name + "\" already exists.", "Replace", "Cancel", this,
                juce::ModalCallbackFunction::create ([this, f] (int ok) { if (ok) engine.savePreset (f); }));
        }
        else
        {
            engine.savePreset (f);
        }
    }), true);
}

void RigsmithEditor::showSceneMenu (int scene)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Scene " + engine.sceneNames[scene]);
    m.addItem (1, "Rename...");
    m.addItem (2, "MIDI learn: select this scene");
    m.addItem (3, "Copy current sound to this scene", scene != engine.getScene());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.scenes[scene]), [this, scene] (int r)
    {
        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Rename scene", {}, juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", engine.sceneNames[scene]);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, scene] (int res)
            {
                if (res == 1)
                {
                    engine.sceneNames[scene] = w->getTextEditorContents ("name").substring (0, 8).trim();
                    if (engine.sceneNames[scene].isEmpty()) engine.sceneNames[scene] = juce::String::charToString ((juce::juce_wchar) ('A' + scene));
                    engine.markDirty();
                }
            }), true);
        }
        else if (r == 2)
        {
            MidiMapping mm; mm.target = MidiMapping::Target::Scene; mm.index = scene;
            engine.startLearn (mm);
        }
        else if (r == 3)
        {
            engine.copySceneTo (scene);
            showMessage ("Copied scene " + engine.sceneNames[engine.getScene()] + " to " + engine.sceneNames[scene]);
        }
    });
}

void RigsmithEditor::showMainMenu()
{
    juce::PopupMenu m;
    if (processor.isStandalone())
        m.addItem (1, "Audio / MIDI settings...");
    m.addItem (17, "Settings & folders...");
    m.addItem (2, "Scan plug-ins...");
    m.addSeparator();

    juce::PopupMenu input;
    const juce::StringArray inputs { "Input 1 (left)", "Input 2 (right)", "Inputs 1 + 2 summed", "Stereo" };
    for (int i = 0; i < inputs.size(); ++i)
        input.addItem (10 + i, inputs[i], true, engine.inputMode.load() == i);
    m.addSubMenu ("Guitar input", input);
    if (processor.isStandalone())
        m.addItem (18, "Full screen  (F11)", true, isFullScreen());
    m.addItem (19, "Show library  (L)", true, libraryVisible);
    m.addItem (3, "Output safety limiter", true, engine.limiterOn.load());
    m.addItem (4, "Tuner", true, engine.tunerActive.load());

    juce::PopupMenu midi;
    for (int i = 0; i < (int) engine.mappings.size(); ++i)
    {
        const auto& mm = engine.mappings[(size_t) i];
        juce::PopupMenu one;
        one.addItem (300 + i, "Latching switch (follow on/off)", true, mm.latching);
        one.addItem (200 + i, "Remove");
        midi.addSubMenu ("CC " + juce::String (mm.cc) + "  ->  " + engine.describe (mm), one);
    }
    if (! engine.mappings.empty()) midi.addSeparator();
    midi.addItem (5, "Learn: next preset");
    midi.addItem (6, "Learn: previous preset");
    midi.addItem (7, "Learn: tuner on/off");
    midi.addItem (8, "Learn: master volume (expression)");
    midi.addSeparator();
    midi.addItem (9, "Tip: right-click any block, knob, path or scene to MIDI-learn it", false);
    m.addSubMenu ("MIDI mappings", midi);

    m.addSeparator();
    m.addItem (15, "Keyboard shortcuts");
    m.addItem (16, "About Rigsmith");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.menu), [this] (int r)
    {
        auto learn = [this] (MidiMapping::Target t) { MidiMapping mm; mm.target = t; engine.startLearn (mm); };
        switch (r)
        {
            case 1:
               #if RIGSMITH_HAS_STANDALONE_HOLDER
                if (auto* h = juce::StandalonePluginHolder::getInstance()) h->showAudioSettingsDialog();
               #endif
                break;
            case 2: showPluginScanner(); break;
            case 17: showSettings(); break;
            case 18: setFullScreen (! isFullScreen()); break;
            case 19: setLibraryVisible (! libraryVisible); break;
            case 3: engine.limiterOn = ! engine.limiterOn.load(); break;
            case 4: engine.tunerActive = ! engine.tunerActive.load(); updateTuner(); break;
            case 5: learn (MidiMapping::Target::PresetNext); break;
            case 6: learn (MidiMapping::Target::PresetPrev); break;
            case 7: learn (MidiMapping::Target::Tuner); break;
            case 8: learn (MidiMapping::Target::MasterVolume); break;
            case 15:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Keyboard shortcuts",
                    "1-4   Scenes A-D\n"
                    "Space   Bypass selected block\n"
                    "Delete   Remove selected block\n"
                    "Ctrl+D   Duplicate selected block\n"
                    "R   Randomise selected block's settings\n"
                    "Shift+R   Random block of the same kind\n"
                    "Ctrl+Shift+R   Randomise the whole rig\n"
                    "Ctrl+Z / Ctrl+Y   Undo / redo\n"
                    "Ctrl+S   Save preset\n"
                    "Page Up / Down   Previous / next preset (Init first)\n"
                    "L   Show / hide library\n"
                    "F11   Full screen\n"
                    "T   Tuner\n"
                    "Esc   Close tuner / leave full screen / cancel MIDI learn", "OK", this);
                break;
            case 16:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Rigsmith " JucePlugin_VersionString,
                    "A standalone guitar rig and VST3 host built around Neural Amp Modeler.\n\n"
                    "NAM core by Steven Atkinson (MIT). Built with JUCE (AGPLv3).\n"
                    "github.com/brown111723/rigsmith", "OK", this);
                break;
            default:
                if (r >= 10 && r < 14) engine.inputMode = r - 10;
                else if (r >= 300) { auto i = (size_t) (r - 300); if (i < engine.mappings.size()) { engine.mappings[i].latching = ! engine.mappings[i].latching; engine.markDirty(); } }
                else if (r >= 200) engine.removeMapping (r - 200);
                break;
        }
    });
}

juce::PluginListComponent* RigsmithEditor::showPluginScanner()
{
    juce::DialogWindow::LaunchOptions o;
    auto* list = new juce::PluginListComponent (engine.formatManager, engine.knownPlugins, engine.getDeadMansPedalFile(),
                                                engine.props.getUserSettings(), true);
    list->setSize (760, 540);
    o.content.setOwned (list);
    o.dialogTitle = "Plug-ins - use Options to scan";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
    return list;
}

void RigsmithEditor::showSettings()
{
    juce::DialogWindow::LaunchOptions o;
    auto* s = new SettingsPanel (engine);
    s->onChanged = [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
    {
        if (sp == nullptr) return;
        sp->library.refresh();
        sp->graph.repaint();
        sp->inspector.setNode (sp->inspector.getNode());
    };
    s->onScanNow = [this] { scanForNewPlugins (true); };
    auto* view = new juce::Viewport();
    view->setViewedComponent (s, true);
    view->setScrollBarsShown (true, false);
    view->setSize (s->getWidth() + 10, juce::jmin (s->getHeight(), 760));
    o.content.setOwned (view);
    o.dialogTitle = "Rigsmith settings";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void RigsmithEditor::scanForNewPlugins (bool always)
{
    auto* settings = engine.props.getUserSettings();
    if (settings == nullptr) return;
    const auto& blacklist = engine.knownPlugins.getBlacklistedFiles();
    for (auto* f : engine.formatManager.getFormats())
    {
        if (! f->canScanForPlugins()) continue;
        auto path = juce::PluginListComponent::getLastSearchPath (*settings, *f);
        if (path.getNumPaths() == 0) path = f->getDefaultLocationsToSearch();
        juce::StringArray fresh;
        for (auto& file : f->searchPathsForPlugins (path, true, false))
            if (! blacklist.contains (file) && ! engine.knownPlugins.isListingUpToDate (file, *f))
                fresh.add (file);
        if (fresh.isEmpty()) continue;
        showMessage ("Found " + juce::String (fresh.size()) + " new " + f->getName() + " plug-in" + (fresh.size() == 1 ? "" : "s") + " - scanning");
        if (auto* list = showPluginScanner())
            list->scanFor (*f, fresh);
        return;   // one format at a time; the rest are found next launch or with Scan
    }
    if (always) showMessage ("No new plug-ins found");
}

//==============================================================================
bool RigsmithEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("nam;wav;aif;aiff;flac;rig"))
            return true;
    return false;
}

void RigsmithEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto pt = graph.getLocalPoint (this, juce::Point<int> (x, y));
    for (auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension ("rig")) { engine.loadPreset (f); return; }
        if (viewport.getBounds().contains (x, y)) graph.dropFile (f, pt);
        else loadFileInto (nullptr, &engine.getRoot(), -1, f);
    }
}

bool RigsmithEditor::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
    if (k == juce::KeyPress ('z', cmd, 0)) { doUndo (false); return true; }
    if (k == juce::KeyPress ('y', cmd, 0) || k == juce::KeyPress ('z', cmd | shift, 0)) { doUndo (true); return true; }
    if (k == juce::KeyPress ('r', cmd | shift, 0)) { randomiseRig(); return true; }
    if (k == juce::KeyPress (juce::KeyPress::F11Key)) { setFullScreen (! isFullScreen()); return true; }

    const auto c = k.getTextCharacter();
    if (! k.getModifiers().isCommandDown())
    {
        if (c == 'l' || c == 'L') { setLibraryVisible (! libraryVisible); return true; }
        if (auto* n = engine.findNode (selectedId))
        {
            if (c == 'r') { randomise (*n, RandomSettings); return true; }
            if (c == 'R') { randomise (*n, RandomType); return true; }
        }
    }
    if (c >= '1' && c <= '4') { engine.setScene (c - '1'); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (isFullScreen()) setFullScreen (false);
        if (engine.isLearning()) engine.cancelLearn();
        engine.tunerActive = false;
        updateTuner();
        return true;
    }
    if (c == 't' || c == 'T') { engine.tunerActive = ! engine.tunerActive.load(); updateTuner(); return true; }
    if (k == juce::KeyPress::pageUpKey)   { engine.stepPreset (-1); return true; }
    if (k == juce::KeyPress::pageDownKey) { engine.stepPreset (1); return true; }
    if (k == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { savePreset (false); return true; }

    if (auto* n = engine.findNode (selectedId))
    {
        if (k == juce::KeyPress::spaceKey) { n->bypassed = ! n->bypassed.load(); engine.storeScene(); engine.markDirty(); graph.repaint(); return true; }
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { engine.remove (selectedId); return true; }
        if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { if (auto* d = engine.duplicate (selectedId)) selectedId = d->id; return true; }
    }
    return false;
}

//==============================================================================
int RigsmithEditor::getIntSetting (const juce::String& key, int def) const
{
    if (auto* st = engine.props.getUserSettings()) return st->getIntValue (key, def);
    return def;
}

void RigsmithEditor::setIntSetting (const juce::String& key, int value)
{
    if (auto* st = engine.props.getUserSettings()) { st->setValue (key, value); st->saveIfNeeded(); }
}

void RigsmithEditor::setLibraryVisible (bool v)
{
    libraryVisible = v;
    header.library.setToggleState (v, juce::dontSendNotification);
    engine.setSetting ("libraryVisible", v);
    resized();
}

bool RigsmithEditor::isFullScreen() const
{
    auto* k = juce::Desktop::getInstance().getKioskModeComponent();
    return k != nullptr && k == getTopLevelComponent();
}

void RigsmithEditor::setFullScreen (bool fs)
{
    if (! processor.isStandalone()) return;
    auto* top = getTopLevelComponent();
    if (top == nullptr || top == this) return;
    juce::Desktop::getInstance().setKioskModeComponent (fs ? top : nullptr, false);
    engine.setSetting ("fullScreen", fs);
    header.fullScreen.setToggleState (fs, juce::dontSendNotification);
    header.fullScreen.setIcon (fs ? Icon::ExitFullScreen : Icon::FullScreen);
    if (fs) showMessage ("Full screen - press F11 or Esc to leave");
    grabKeyboardFocus();
}

void RigsmithEditor::doUndo (bool redo)
{
    const bool ok = redo ? engine.redo() : engine.undo();
    showMessage (ok ? juce::String (redo ? "Redo" : "Undo") : juce::String (redo ? "Nothing to redo" : "Nothing to undo"));
}

void RigsmithEditor::randomiseRig()
{
    randomiser.randomiseRig();
    selectedId.clear();
    inspector.setNode (nullptr);
    int blocks = 0;
    visitNodes (engine.getRoot(), [&blocks] (Node& n) { if (dynamic_cast<Block*> (&n) != nullptr) ++blocks; });
    showMessage ("New random rig (" + juce::String (blocks) + " blocks) - Ctrl+Z to go back, right-click > Lock to keep a block");
}

void RigsmithEditor::showRigRandomMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Randomise");
    m.addItem (1, "New random rig  (Ctrl+Shift+R)");
    m.addItem (2, "New blocks, same layout");
    m.addItem (3, "Randomise every block's settings");
    m.addItem (4, "Mutate every block's settings (subtle)");
    m.addSeparator();
    m.addItem (10, "Use VST plug-ins too", true, randomiser.options.usePlugins);
    m.addItem (11, "Allow dual-amp splits", true, randomiser.options.allowSplits);
    m.addSeparator();
    m.addItem (-1, "Locked blocks are kept. Ctrl+Z undoes any of these.", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.random), [this] (int r)
    {
        switch (r)
        {
            case 1: randomiseRig(); break;
            case 2: randomiser.reshuffleBlocks(); showMessage ("New blocks in the same layout - Ctrl+Z to go back"); break;
            case 3: randomiser.randomiseAllSettings (1.0f); showMessage ("Randomised every block's settings"); break;
            case 4: randomiser.randomiseAllSettings (0.25f); showMessage ("Mutated every block's settings"); break;
            case 10: randomiser.options.usePlugins = ! randomiser.options.usePlugins; engine.setSetting ("randomUsePlugins", randomiser.options.usePlugins); break;
            case 11: randomiser.options.allowSplits = ! randomiser.options.allowSplits; engine.setSetting ("randomSplits", randomiser.options.allowSplits); break;
            default: break;
        }
    });
}

void RigsmithEditor::randomise (Node& n, RandomWhat what)
{
    const auto id = n.id;
    const auto oldName = n.getName();
    switch (what)
    {
        case RandomSettings:
            randomiser.randomiseSettings (n, 1.0f);
            showMessage ("Randomised " + oldName + " - Ctrl+Z to go back");
            break;
        case RandomMutate:
            randomiser.randomiseSettings (n, 0.25f);
            showMessage ("Mutated " + oldName);
            break;
        case RandomType:
        {
            if (dynamic_cast<Block*> (&n) == nullptr) return;
            const auto cat = Randomiser::categoryOf (n);
            if (auto* r = randomiser.randomiseType (id))
            {
                selectedId = r->id;
                showMessage (oldName + "  ->  " + r->getName());
            }
            else showMessage ("No other " + categoryName (cat) + " blocks to choose from");
            break;
        }
    }
    graph.repaint();
}

void RigsmithEditor::showRandomMenu (Node& n, juce::Component* anchor)
{
    const auto id = n.id;
    juce::PopupMenu m;
    m.addItem (1, "Randomise settings  (R)");
    m.addItem (2, "Mutate settings (subtle)");
    if (dynamic_cast<Block*> (&n) != nullptr)
        m.addItem (3, "Random " + categoryName (Randomiser::categoryOf (n)) + " block  (Shift+R)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, id] (int r)
    {
        if (auto* node = engine.findNode (id); node != nullptr && r > 0)
            randomise (*node, r == 1 ? RandomSettings : (r == 2 ? RandomMutate : RandomType));
    });
}

void RigsmithEditor::showBlockPresetMenu (Node& n, juce::Component* anchor)
{
    const auto id = n.id;
    const auto files = engine.getBlockPresets (n);
    juce::PopupMenu m;
    m.addSectionHeader (n.getName() + " presets");
    m.addItem (1, "Save preset...");
    m.addItem (2, "Init (default settings)");
    m.addSeparator();
    if (files.isEmpty()) m.addItem (-1, "No saved presets for this block yet", false);
    for (int i = 0; i < files.size(); ++i)
        m.addItem (100 + i, files[i].getFileNameWithoutExtension());
    m.addSeparator();
    m.addItem (3, "Show folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, id, files] (int r)
    {
        auto* node = engine.findNode (id);
        if (node == nullptr || r == 0) return;
        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Save block preset", "Name this " + node->getName() + " setting", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", {}, "Name");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int res)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                if (res == 1 && name.isNotEmpty())
                    if (auto* nn = engine.findNode (id)) engine.saveBlockPreset (*nn, name);
            }), true);
        }
        else if (r == 2) { engine.initBlock (id); showMessage ("Reset " + node->getName() + " to its defaults"); }
        else if (r == 3)
        {
            auto folder = engine.getBlockPresetFolder (*node);
            folder.createDirectory();
            folder.startAsProcess();
        }
        else if (r >= 100 && r - 100 < files.size()) engine.loadBlockPreset (id, files[r - 100]);
    });
}

void RigsmithEditor::replaceWith (Node& target, const juce::String& d)
{
    const auto id = target.id;
    const auto oldName = target.getName();
    std::unique_ptr<Node> fresh;
    juce::String err;

    if (d.startsWith ("node:"))
    {
        const auto other = d.fromFirstOccurrenceOf ("node:", false, false);
        if (! engine.swap (id, other)) showMessage ("Can't swap a split with a block inside it");
        return;
    }
    if (d.startsWith ("plugin:"))
    {
        auto desc = engine.knownPlugins.getTypeForIdentifierString (d.fromFirstOccurrenceOf ("plugin:", false, false));
        if (desc == nullptr) { showMessage ("Plug-in not found - try scanning again"); return; }
        fresh = engine.createPlugin (*desc, err);
        if (fresh == nullptr) { showMessage ("Couldn't load " + desc->name + ": " + err); return; }
    }
    else if (d.startsWith ("file:"))
    {
        const juce::File f (d.fromFirstOccurrenceOf ("file:", false, false));
        if (f.hasFileExtension ("nam"))
        {
            auto nb = std::make_unique<NamBlock>();
            if (! nb->loadModel (f, err)) { showMessage (err); return; }
            fresh = std::move (nb);
        }
        else if (f.hasFileExtension ("wav;aif;aiff;flac"))
        {
            auto ir = std::make_unique<IrBlock>();
            if (! ir->loadIr (f, err)) { showMessage (err); return; }
            fresh = std::move (ir);
        }
    }
    if (fresh == nullptr) return;
    if (auto* r = engine.replace (id, std::move (fresh)))
    {
        selectedId = r->id;
        showMessage ("Replaced " + oldName + " with " + r->getName() + " - Ctrl+Z to undo");
    }
}

void RigsmithEditor::toggleLock (Node& n)
{
    n.locked = ! n.locked.load();
    engine.markDirty();
    graph.repaint();
    showMessage (n.locked.load() ? n.getName() + " locked - randomising the rig keeps it" : n.getName() + " unlocked");
}
