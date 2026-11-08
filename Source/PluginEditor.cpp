#include "PluginEditor.h"
#include "UI/Settings.h"
#include "Engine/PluginScanner.h"
#include "Engine/FactoryPresets.h"

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
//==============================================================================
struct RigsmithEditor::BusyCard : public juce::Component, private juce::Timer
{
    BusyCard() { setInterceptsMouseClicks (true, true); startTimerHz (30); }
    juce::String title, detail;
    float progress = -1.0f;
    float phase = 0.0f;
    void timerCallback() override { phase += 0.08f; repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.45f));
        auto card = getLocalBounds().withSizeKeepingCentre (juce::jmin (440, getWidth() - 40), 118).toFloat();
        g.setColour (cardHi);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (card, 10.0f, 1.2f);
        auto r = card.reduced (22.0f, 18.0f);
        // spinner
        auto sp = r.removeFromLeft (34.0f).withSizeKeepingCentre (28.0f, 28.0f);
        juce::Path arc;
        arc.addCentredArc (sp.getCentreX(), sp.getCentreY(), 12.0f, 12.0f, phase * 6.0f, 0.0f, 4.2f, true);
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        r.removeFromLeft (14.0f);
        g.setColour (text);
        g.setFont (font (15.0f, true));
        g.drawText (title, r.removeFromTop (24.0f), juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (font (12.0f));
        g.drawFittedText (detail, r.removeFromTop (34.0f).toNearestInt(), juce::Justification::topLeft, 2);
        if (progress >= 0.0f)
        {
            auto bar = r.removeFromTop (8.0f).withTrimmedTop (2.0f);
            g.setColour (juce::Colour (0xff111215));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress))), 3.0f);
        }
    }
};

//==============================================================================
struct RigsmithEditor::LibraryLoader : private juce::Thread
{
    LibraryLoader (juce::FileSearchPath m, juce::FileSearchPath i) : juce::Thread ("Rigsmith library"), models (std::move (m)), irs (std::move (i))
    {
        startThread (juce::Thread::Priority::normal);
    }
    ~LibraryLoader() override { stopThread (5000); }

    std::atomic<int> found { 0 }, read { 0 }, total { 0 };
    std::atomic<bool> listing { true }, finished { false };

    void run() override
    {
        // find them all first (so there's a total), then read what each capture says it is
        juce::Array<juce::File> nams;
        for (int p = 0; p < models.getNumPaths() && ! threadShouldExit(); ++p)
            for (const auto& e : juce::RangedDirectoryIterator (models.getRawString (p), true, "*.nam", juce::File::findFiles))
            {
                if (threadShouldExit() || nams.size() >= CaptureLibrary::maxFiles) break;
                nams.add (e.getFile());
                ++found;
            }
        for (int p = 0; p < irs.getNumPaths() && ! threadShouldExit(); ++p)
            for (const auto& e : juce::RangedDirectoryIterator (irs.getRawString (p), true, "*.wav;*.aif;*.aiff;*.flac", juce::File::findFiles))
                if (threadShouldExit()) break;   // warms the file system cache
        total = nams.size();
        listing = false;
        for (auto& f : nams)
        {
            if (threadShouldExit()) return;
            readCaptureInfo (f);
            ++read;
        }
        finished = true;
    }

    juce::FileSearchPath models, irs;
};

RigsmithEditor::RigsmithEditor (RigsmithProcessor& p)
    : AudioProcessorEditor (p), processor (p), engine (p.engine),
      header (engine), graph (*this), inspector (*this), library (*this), tunerOverlay (engine)
{
    trace ("editor: start");
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    addAndMakeVisible (header);
    viewport.setViewedComponent (&graph, false);
    viewport.setScrollBarsShown (true, false);   // sideways: the rig map below (or the wheel)
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);   // keys (scenes, presets, bypass) belong to the rig, not to scrolling
    graph.onBackgroundClick = [this] { select ({}); grabKeyboardFocus(); };
    addAndMakeVisible (viewport);
    addChildComponent (rigMap);
    addAndMakeVisible (inspector);
    addAndMakeVisible (modPanel);
    if (processor.isStandalone()) watchdog = std::make_unique<Watchdog>();
    if (juce::SystemStats::getEnvironmentVariable ("RIGSMITH_DEMO_AUDIO", {}).isNotEmpty())
        demoAudio = std::make_unique<DemoAudio> (processor);
    modPanel.onCollapsedChanged = [this] (bool c)
    {
        modVisible = ! c;
        header.modButton.setToggleState (modVisible, juce::dontSendNotification);
        engine.setSetting ("modVisible", modVisible);
        resized();
    };
    modPanel.onSelectBlock = [this] (const juce::String& id) { select (id); };
    addChildComponent (ioPanel);
    ioPanel.onResetOvers = [this] { levels.inOvers = levels.outOvers = 0; };
    addAndMakeVisible (library);
    addAndMakeVisible (divider);
    statusLabel.setFont (mono (11.0f));
    statusLabel.setColour (juce::Label::textColourId, dim);
    statusLabel.setInterceptsMouseClicks (false, false);
    statusLabel.setJustificationType (juce::Justification::bottomLeft);
    statusLabel.setVisible (false);   // the audio / CPU readout lives in the top bar now
    addChildComponent (tunerOverlay);

    divider.onStart = [this] { dragStartHeight = inspectorHeight; };
    divider.onDrag = [this] (int dy)
    {
        inspectorHeight = juce::jlimit (220, juce::jmax (220, getHeight() - 64 - 160), dragStartHeight - dy);
        resized();
    };
    divider.onDoubleClick = [this]
    {
        inspectorHeight = inspectorHeight < 460 ? juce::jmax (370, (getHeight() - 64) * 6 / 10) : 370;
        resized();
        setIntSetting ("inspectorHeight", inspectorHeight);
    };
    divider.setTooltip ("Drag to resize the panel. Double-click to make it tall / normal");

    libraryVisible = engine.getSetting ("libraryVisible", true);
    modVisible = engine.getSetting ("modVisible", false);
    modPanel.setCollapsed (! modVisible);
    inspectorHeight = getIntSetting ("inspectorHeight", 370);
    if (inspectorHeight == 330) inspectorHeight = 370;   // the old default: a little taller now
    randomiser.options.usePlugins = engine.getSetting ("randomUsePlugins", false);
    randomiser.options.allowSplits = engine.getSetting ("randomSplits", true);

    header.onUndo = [this] { doUndo (false); };
    header.onRedo = [this] { doUndo (true); };
    header.onRandom = [this] { randomiseRig(); };
    header.onRandomMenu = [this] { showRigRandomMenu(); };
    header.onLibrary = [this] { setLibraryVisible (! libraryVisible); };
    header.onMod = [this] { setModVisible (! modVisible); };
    header.onFullScreen = [this] { setFullScreen (! isFullScreen()); };
    header.showFullScreenButton = processor.isStandalone();
    header.library.setToggleState (libraryVisible, juce::dontSendNotification);

    header.onPresetMenu = [this] { showPresetMenu(); };
    header.onSave = [this] { savePreset (false); };
    header.onMenu = [this] { showMainMenu(); };
    if (const auto hung = engine.takeLoadHangNotice(); hung.isNotEmpty())
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RigsmithEditor> (this), hung]
        {
            if (sp != nullptr)
                sp->showMessage (hung + " never finished loading last time: the dice won't pick it (you can still add it yourself)");
        });
    if (const auto crashed = engine.takeScanCrashNotice(); crashed.isNotEmpty())
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RigsmithEditor> (this), crashed]
        {
            if (sp != nullptr)
                sp->showMessage (juce::File (crashed).getFileName() + " crashed while being scanned: it won't be scanned again (menu > Retry skipped plug-ins)");
        });
    header.onTooltips = [this] { setTooltipsEnabled (tooltips == nullptr); };
    setTooltipsEnabled (engine.getSetting ("tooltips", false));
    learnClose.onClick = [this] { engine.cancelLearn(); toast.clear(); learnClose.setVisible (false); repaint(); };
    addChildComponent (learnClose);
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

    library.onActivateFile = [this] (const juce::File& f) { placeFile (f); };
    library.onActivateFiles = [this] (const juce::Array<juce::File>& f) { placeFiles (f); };
    tone3000 = std::make_unique<rig::t3k::Client> (engine);
    library.setTone3000 (*tone3000);
    library.getTone3000Panel()->onFileReady = [this] (const juce::File& f) { placeFile (f); };
    library.onActivatePlugin = [this] (const juce::String& ident) { auto [c, i] = insertionPoint(); insertPlugin (ident, *c, i); };
    library.onActivateBuiltIn = [this] (NodeType t) { auto [c, i] = insertionPoint(); insertBuiltIn (t, *c, i); };
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
    trace ("editor: built");

    // once per run: pick up plug-ins installed since last time
    static bool scannedThisRun = false;
    // (the first run always looks; after that only if "scan on startup" is ticked)
    if (engine.isSafeStart())
        juce::Timer::callAfterDelay (1200, [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
        {
            if (sp != nullptr)
                sp->showMessage ("Rigsmith didn't finish opening last time, so it started with a fresh rig and no plug-in scan. "
                                 "Your last session was kept (Menu > Session state > Load a saved state: \"Rigsmith - session that didn't open\", "
                                 "beside Rigsmith.ini), and Rigsmith.log there shows what it was doing");
        });
    // no Rigsmith.ini: a first run (or a reset) - start with the audio / MIDI settings
    // then the folders (NAM, IRs, presets), then the VST3 question - one at a time
    bool firstRunSetup = false;
   #if RIGSMITH_HAS_STANDALONE_HOLDER
    if (processor.isStandalone() && engine.isFirstRun() && ! scannedThisRun)
    {
        firstRunSetup = true;
        scannedThisRun = true;
        juce::Timer::callAfterDelay (700, [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
        {
            if (sp == nullptr) return;
            if (auto* h = juce::StandalonePluginHolder::getInstance()) h->showAudioSettingsDialog();
            sp->afterDialogs ([sp]
            {
                if (sp == nullptr) return;
                sp->showMessage ("Welcome to Rigsmith. Next: where your NAM models, IRs and presets live");
                sp->showSettings();
                sp->afterDialogs ([sp]
                {
                    if (sp != nullptr && ! sp->engine.hasScannedPlugins()) sp->askToScan();
                });
            });
        });
    }
   #endif
    if (! firstRunSetup && ! scannedThisRun && engine.shouldScanOnStartup() && ! engine.isSafeStart())
    {
        scannedThisRun = true;
        const bool firstTime = ! engine.hasScannedPlugins();
        juce::Timer::callAfterDelay (1500, [sp = juce::Component::SafePointer<RigsmithEditor> (this), firstTime]
        {
            if (sp == nullptr) return;
            if (! firstTime) { sp->scanForNewPlugins (false); return; }
            if (sp->audioSetupOpen()) sp->askScanWhenAudioSetupCloses();
            else sp->askToScan();
        });
    }
}

void RigsmithEditor::askToScan()
{
    // the very first start: ask, rather than surprise anyone with a scan
    {
        juce::Component::SafePointer<RigsmithEditor> sp (this);
        {
            auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                            .withTitle ("Scan for VST3 plug-ins?")
                            .withMessage ("Rigsmith can host your VST3 effects in the rig, next to its own blocks and NAM amps.\n\n"
                                          "Scan your plug-in folders now? It takes a moment the first time. You can do it any time "
                                          "from the VST3 tab or Menu > Plug-ins.")
                            .withButton ("Scan now").withButton ("Not now")
                            .withAssociatedComponent (sp.getComponent());
            juce::AlertWindow::showAsync (opts, [sp] (int r)
            {
                if (sp == nullptr) return;
                if (r == 1) sp->scanForNewPlugins (true);
                else sp->engine.markPluginsScanned();
            });
        }
    }
}

bool RigsmithEditor::audioSetupOpen() const
{
    return juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
}

void RigsmithEditor::afterDialogs (std::function<void()> next)
{
    juce::Timer::callAfterDelay (400, [sp = juce::Component::SafePointer<RigsmithEditor> (this), next]
    {
        if (sp == nullptr) return;
        if (sp->audioSetupOpen()) sp->afterDialogs (next);
        else next();
    });
}

void RigsmithEditor::askScanWhenAudioSetupCloses()
{
    // the VST3 question waits until the audio settings have been closed
    juce::Timer::callAfterDelay (400, [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
    {
        if (sp == nullptr) return;
        if (sp->audioSetupOpen()) sp->askScanWhenAudioSetupCloses();
        else sp->askToScan();
    });
}

RigsmithEditor::~RigsmithEditor()
{
    demoAudio.reset();
    watchdog.reset();
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
void RigsmithEditor::paint (juce::Graphics& g)
{
    static bool first = true;
    if (first)
    {
        first = false;
        trace ("editor: first paint");
        // the window is up: this start counts as finished (after a moment, in case something stalls straight after)
        juce::Timer::callAfterDelay (4000, [sp = juce::Component::SafePointer<RigsmithEditor> (this)]
        {
            if (sp != nullptr && sp->processor.isStandalone()) sp->engine.finishStartup();
        });
    }
    g.fillAll (bg);
}

void RigsmithEditor::setTooltipsEnabled (bool on)
{
    if (on && tooltips == nullptr) tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    if (! on) tooltips.reset();
    header.tips.setToggleState (on, juce::dontSendNotification);
    engine.setSetting ("tooltips", on);
}

juce::Rectangle<int> RigsmithEditor::toastBounds() const
{
    const auto f = font (14.0f, true);
    const int extra = engine.isLearning() ? 36 : 0;   // room for the x
    const int w = juce::jmin (viewport.getWidth() - 40, juce::GlyphArrangement::getStringWidthInt (f, toast) + 48 + extra);
    return { viewport.getX() + (viewport.getWidth() - w) / 2, viewport.getBottom() - 74, w, 40 };
}

void RigsmithEditor::paintOverChildren (juce::Graphics& g)
{
    if (toast.isEmpty()) return;
    const auto age = juce::Time::getMillisecondCounter() - toastTime;
    const bool learning = engine.isLearning();
    const float alpha = learning ? 1.0f : juce::jlimit (0.0f, 1.0f, (5000.0f - (float) age) / 600.0f);
    if (alpha <= 0) return;

    const auto f = font (14.0f, true);
    auto r = toastBounds().toFloat();
    g.setColour (card.withAlpha (0.97f * alpha));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour ((learning ? accent : borderHi).withAlpha (alpha));
    g.drawRoundedRectangle (r, 10.0f, 1.5f);
    if (learning)
    {
        // the x that cancels (Esc does too)
        const auto x = learnClose.getBounds().toFloat();
        if (learnClose.isMouseOver())
        {
            g.setColour (accent.withAlpha (0.2f));
            g.fillEllipse (x.reduced (2.0f));
        }
        drawIcon (g, Icon::Close, x.reduced (7.0f), learnClose.isMouseOver() ? accent : muted);
        r.removeFromRight (34.0f);
    }
    g.setColour (text.withAlpha (alpha));
    g.setFont (f);
    g.drawFittedText (toast, r.toNearestInt().reduced (16, 0), juce::Justification::centred, 1);
}

void RigsmithEditor::resized()
{
    if (busy != nullptr) busy->setBounds (getLocalBounds().withTrimmedTop (64));
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (64));
    library.setVisible (libraryVisible);
    if (libraryVisible) library.setBounds (r.removeFromRight (300));
    const int modH = modPanel.getPreferredHeight();
    const int ih = juce::jlimit (220, juce::jmax (220, r.getHeight() - 160 - modH), inspectorHeight);
    inspector.setBounds (r.removeFromBottom (ih));
    ioPanel.setBounds (inspector.getBounds());
    modPanel.setBounds (r.removeFromBottom (modH));
    divider.setBounds (r.removeFromBottom (8));
    graphArea = r;
    layoutGraph (r);
    statusLabel.setBounds (r.getX() + 12, r.getBottom() - 46, 260, 34);
    tunerOverlay.setBounds (r);
}

void RigsmithEditor::layoutGraph (juce::Rectangle<int> r)
{
    // a long (or tall) rig gets the mini map at the bottom instead of a horizontal scrollbar
    // a small map floating at the bottom centre, over the graph
    const auto need = graph.contentSize();
    const bool map = need.x > r.getWidth() || need.y > r.getHeight();
    rigMap.setVisible (map);
    viewport.setScrollBarsShown (true, false);
    viewport.setBounds (r);
    if (map)
    {
        const int w = juce::jlimit (220, 380, r.getWidth() / 4);
        rigMap.setBounds (r.getCentreX() - w / 2, r.getBottom() - 30 - 12, w, 30);
        rigMap.toFront (false);
    }
    graph.fitTo (viewport.getLocalBounds().withTrimmedRight (graph.contentSize().y > r.getHeight() ? viewport.getScrollBarThickness() : 0));
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
struct RigsmithEditor::DemoAudio : private juce::Timer
{
    explicit DemoAudio (RigsmithProcessor& p) : proc (p)
    {
        proc.prepareToPlay (48000.0, 480);
        buffer.setSize (2, 480);
        startTimer (10);
    }
    ~DemoAudio() override { stopTimer(); }
    void timerCallback() override
    {
        static const float notes[] { 82.4f, 110.0f, 146.8f, 196.0f, 329.6f, 220.0f, 164.8f, 123.5f };
        if (strum)
        {
            // six open strings, a little out: for the polyphonic tuner
            static const float midi[] { 40.0f, 45.0f, 50.0f, 55.0f, 59.0f, 64.0f };
            static const float off[] { -12.0f, 4.0f, 0.5f, -6.0f, 1.0f, 18.0f };
            for (int i = 0; i < buffer.getNumSamples(); ++i, ++t)
            {
                if (t % 96000 == 0) env = 1.0f;
                env *= 0.99998f;
                float x = 0.0f;
                for (int s = 0; s < 6; ++s)
                {
                    const float f = 440.0f * std::pow (2.0f, (midi[s] - 69.0f + off[s] / 100.0f) / 12.0f);
                    for (int h = 1; h <= 4; ++h) x += 0.04f / (float) h * std::sin (juce::MathConstants<float>::twoPi * f * (float) h * (float) t / 48000.0f);
                }
                buffer.setSample (0, i, x * env);
                buffer.setSample (1, i, x * env);
            }
            juce::MidiBuffer midi2;
            proc.processBlock (buffer, midi2);
            return;
        }
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (--untilNext <= 0)
            {
                untilNext = 18000 + rng.nextInt (20000);
                hz = notes[rng.nextInt (8)];
                amp = 0.25f + rng.nextFloat() * 0.4f;
            }
            phase += hz / 48000.0f;
            phase -= std::floor (phase);
            const float x = amp * (2.0f * phase - 1.0f);
            amp *= 0.99993f;
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
    }
    RigsmithProcessor& proc;
    juce::AudioBuffer<float> buffer;
    juce::Random rng;
    bool strum = juce::SystemStats::getEnvironmentVariable ("RIGSMITH_DEMO_AUDIO", {}) == "strum";
    juce::int64 t = 0;
    float env = 1.0f;
    int untilNext = 0;
    float hz = 110.0f, amp = 0.0f, phase = 0.0f;
};

struct RigsmithEditor::Watchdog : private juce::Thread
{
    Watchdog() : juce::Thread ("Rigsmith watchdog") { startThread (juce::Thread::Priority::low); }
    ~Watchdog() override { stopThread (3000); }
    std::atomic<juce::uint32> beat { 0 };
    void run() override
    {
        juce::uint32 last = beat.load();
        int still = 0;
        bool reported = false;
        while (! threadShouldExit())
        {
            wait (1000);
            const auto b = beat.load();
            if (b != last)
            {
                if (reported) trace ("the window is responding again");
                last = b; still = 0; reported = false;
            }
            else if (++still >= 6 && ! reported)
            {
                trace ("the window has not responded for 6 s - last step: " + lastTrace());
                reported = true;
            }
        }
    }
};

void RigsmithEditor::showBusy (const juce::String& title, const juce::String& detail, float progress)
{
    if (busy == nullptr)
    {
        busy = std::make_unique<BusyCard>();
        addAndMakeVisible (*busy);
    }
    busy->title = title;
    busy->detail = detail;
    busy->progress = progress;
    busy->setBounds (getLocalBounds().withTrimmedTop (header.getBottom()));
    busy->toFront (false);
    busy->repaint();
}

void RigsmithEditor::hideBusy() { busy.reset(); }

void RigsmithEditor::whileBusy (const juce::String& title, const juce::String& detail, std::function<void()> work)
{
    showBusy (title, detail);
    juce::Timer::callAfterDelay (60, [sp = juce::Component::SafePointer<RigsmithEditor> (this), work]
    {
        if (sp == nullptr) return;
        work();
        if (sp != nullptr) sp->hideBusy();
    });
}

void RigsmithEditor::loadLibraryInBackground()
{
    libraryLoader = std::make_unique<LibraryLoader> (engine.getPaths ("modelPaths"), engine.getPaths ("irPaths"));
    showBusy ("Loading your library", "Looking for NAM captures and IRs in your folders...", 0.0f);
}

void RigsmithEditor::hideStandaloneOptionsButton()
{
    // the standalone window's own "Options" button: its items live in the menu instead
    if (optionsHidden || ! processor.isStandalone()) return;
    auto* top = getTopLevelComponent();
    if (top == nullptr || top == this) return;
    for (auto* c : top->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Options")
        {
            b->setVisible (false);
            b->setEnabled (false);
            optionsHidden = true;
        }
}

void RigsmithEditor::timerCallback()
{
    ++tick;
    if (watchdog != nullptr) ++watchdog->beat;
    if (libraryLoader != nullptr)
    {
        auto& l = *libraryLoader;
        if (l.finished.load())
        {
            const int n = l.total.load();
            libraryLoader.reset();
            hideBusy();
            engine.captures.rescan();
            library.refresh();
            graph.repaint();
            inspector.setNode (inspector.getNode());
            saveCaptureCache();
            showMessage ("Library loaded: " + juce::String (n) + " NAM captures");
        }
        else if (l.listing.load())
            showBusy ("Loading your library", "Looking for NAM captures in your folders: " + juce::String (l.found.load()) + " found so far...", 0.0f);
        else
            showBusy ("Loading your library",
                      "Reading what each capture is (amp, pedal, make and model): " + juce::String (l.read.load()) + " of " + juce::String (l.total.load())
                          + ". This is only slow the first time.",
                      l.total.load() > 0 ? (float) l.read.load() / (float) l.total.load() : 1.0f);
    }
    if (tick % 15 == 1) hideStandaloneOptionsButton();
    {
        auto fall = [] (float& v, std::atomic<float>& src) { v = juce::jmax (src.exchange (0.0f), v * 0.86f); };
        auto ease = [] (float& v, std::atomic<float>& src) { const float x = src.load(); v += (x - v) * (x > v ? 0.5f : 0.15f); };
        fall (levels.inPeakL, engine.inMeter.peakL);   fall (levels.inPeakR, engine.inMeter.peakR);
        fall (levels.outPeakL, engine.outMeter.peakL); fall (levels.outPeakR, engine.outMeter.peakR);
        ease (levels.inRmsL, engine.inMeter.rmsL);     ease (levels.inRmsR, engine.inMeter.rmsR);
        ease (levels.outRmsL, engine.outMeter.rmsL);   ease (levels.outRmsR, engine.outMeter.rmsR);
        levels.inOvers += engine.inMeter.overs.exchange (0);
        levels.outOvers += engine.outMeter.overs.exchange (0);
        const float gr = engine.limiterGrDb.exchange (0.0f);
        levels.grDb = gr < levels.grDb ? gr : levels.grDb * 0.8f;
        engine.inPeak = 0; engine.outPeak = 0;

        GraphView::Levels gl;
        gl.inL = levels.inPeakL; gl.inR = levels.inPeakR; gl.outL = levels.outPeakL; gl.outR = levels.outPeakR;
        gl.grDb = levels.grDb;
        graph.setMeters (gl);
        if (ioPanel.isVisible()) ioPanel.setLevels (levels);
    }

    if (engine.tunerActive.load() && tick % 2 == 0)
    {
        reading = engine.tuner.analyse();
        tunerOverlay.setReading (reading);
    }
    if (engine.tunerActive.load() && tick % 3 == 1 && tunerOverlay.isVisible() && tunerOverlay.getMode() != rig::TunerOverlay::Mono)
        tunerOverlay.setPolyReading (engine.polyTuner.analyse());
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
    if (tick % 10 == 0) header.setStatus (sr, engine.getBlockSize(), lat, (float) processor.getCpuLoad());

    if (tick % 2 == 0)
        graph.repaint();

    if (toast.isNotEmpty() && ! engine.isLearning() && juce::Time::getMillisecondCounter() - toastTime > 5000)
        toast.clear();
    {
        const bool showX = engine.isLearning() && toast.isNotEmpty();
        if (showX)
        {
            const auto t = toastBounds();
            learnClose.setBounds (t.getRight() - 40, t.getY() + 4, 32, 32);
            learnClose.toFront (false);
        }
        if (showX != learnClose.isVisible()) learnClose.setVisible (showX);
    }
    if (toast.isNotEmpty())
        repaint (viewport.getBounds().removeFromBottom (84));
}

void RigsmithEditor::handleAsyncUpdate()
{
    const auto viewPos = viewport.getViewPosition();
    graph.rebuild();
    graph.setVisible (true);
    if (isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
        grabKeyboardFocus();
    layoutGraph (graphArea);
    viewport.setViewPosition (viewPos);
    auto* n = engine.findNode (selectedId);
    if (n == nullptr && ! isIoSelected()) selectedId.clear();
    showPanelFor (selectedId);
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
void RigsmithEditor::showPanelFor (const juce::String& id)
{
    const bool io = id == kInputId || id == kOutputId;
    if (io) ioPanel.setOutput (id == kOutputId);
    ioPanel.setVisible (io);
    inspector.setVisible (! io);
}

void RigsmithEditor::select (const juce::String& id)
{
    showPanelFor (id);
    if (selectedId == id) return;
    selectedId = id;
    inspector.setNode (engine.findNode (id));
    graph.repaint();
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
    chooser = std::make_unique<juce::FileChooser> (isNam ? "Load NAM captures (pick several to sweep between them)" : "Load IRs (pick several to sweep between them)",
                                                   juce::File (engine.getPaths (isNam ? "modelPaths" : "irPaths").getRawString (0)),
                                                   isNam ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac");
    // pick one capture, or several (Ctrl / Shift-click) to sweep between: exactly those are loaded
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, id] (const juce::FileChooser& fc)
    {
        juce::StringArray paths;
        for (auto& f : fc.getResults()) if (f.existsAsFile()) paths.add (f.getFullPathName());
        if (paths.isEmpty()) return;
        if (auto* node = engine.findNode (id))
            dropCaptures (node, nullptr, -1, CaptureDrop::fromPaths (paths), DropMode::Replace);
    });
}

void RigsmithEditor::browseTone3000 (const juce::String& format, std::function<void (const juce::File&)> into)
{
    auto* panel = library.getTone3000Panel();
    if (panel == nullptr) return;
    if (! libraryVisible) setLibraryVisible (true);
    library.setTab (Library::Tone3000);   // progress shows there
    if (! tone3000->hasKey()) { showMessage ("Add a TONE3000 key first - see the TONE3000 tab"); return; }
    panel->browse (format, std::move (into));
}

void RigsmithEditor::placeFile (const juce::File& f) { placeFiles ({ f }); }

void RigsmithEditor::placeFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return;
    const auto f = files[0];
    CaptureDrop drop;
    for (auto& x : files) (Sweepable::isNamFile (x) ? drop.nams : drop.irs).addIfNotAlreadyThere (x);
    // exactly these captures (one, or a few picked in the library)
    auto put = [this, &drop] (Node* target, Chain* chain, int index) { dropCaptures (target, chain, index, drop, DropMode::Replace); };

    // Where a double-clicked (or downloaded) model / IR goes:
    //  1. into the selected block, if it's the same kind (amp for amp, pedal for pedal, IR for IR)
    //  2. into an empty block of that kind in the main chain
    //  3. otherwise a new block where it belongs: pedals before the amp, amps before the cab,
    //     IRs after the amp (or after the selection, if something is selected)
    const bool isNam = f.hasFileExtension ("nam");
    const auto gear = classifyFile (f);
    const bool isPedal = isNam && gear == Gear::Pedal;
    auto role = [] (Node& n) -> int
    {
        if (auto* nb = dynamic_cast<NamBlock*> (&n)) return nb->getGear() == Gear::Pedal ? 1 : 0;
        if (dynamic_cast<IrBlock*> (&n) != nullptr) return 2;
        return -1;
    };
    const int want = isNam ? (isPedal ? 1 : 0) : 2;

    if (auto* sel = engine.findNode (selectedId))
    {
        if (role (*sel) == want || (want != 2 && sel->getType() == NodeType::Nam && dynamic_cast<NamBlock*> (sel)->getFile() == juce::File()))
        {
            put (sel, nullptr, -1);
            return;
        }
        auto [c, i] = insertionPoint();
        put (nullptr, c, i);
        return;
    }

    auto& root = engine.getRoot();
    for (auto& n : root.nodes)
    {
        const bool empty = (n->getType() == NodeType::Nam && dynamic_cast<NamBlock*> (n.get())->getFile() == juce::File())
                        || (n->getType() == NodeType::Ir && dynamic_cast<IrBlock*> (n.get())->getFile() == juce::File());
        if (empty && ((isNam && n->getType() == NodeType::Nam) || (! isNam && n->getType() == NodeType::Ir)))
        {
            put (n.get(), nullptr, -1);
            return;
        }
    }
    int at = -1;
    for (int i = 0; i < (int) root.nodes.size(); ++i)
    {
        const int r = role (*root.nodes[(size_t) i]);
        if (want == 1 && r == 0) { at = i; break; }       // pedal: before the amp
        if (want == 0 && r == 2) { at = i; break; }       // amp: before the cab
        if (want == 2 && r == 0) at = i + 1;              // IR: after the (last) amp
    }
    put (nullptr, &root, at);
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
        if (auto* nb = dynamic_cast<NamBlock*> (target); nb != nullptr && isNam) ok = engine.loadGear (*nb, f, err);
        else if (auto* ir = dynamic_cast<IrBlock*> (target); ir != nullptr && isIr) ok = engine.loadGear (*ir, f, err);
        else err = "That file doesn't fit this block";

        if (! ok) { showMessage (err); return false; }
        engine.markDirty();
        if (auto* nb = dynamic_cast<NamBlock*> (target)) showMessage ("Loaded " + nb->getName() + (nb->getCaptureName() != nb->getName() ? " - " + nb->getCaptureName() : juce::String()));
        else showMessage ("Loaded " + f.getFileNameWithoutExtension());
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
        if (! engine.loadGear (*nb, f, err)) { showMessage (err); return false; }
        n = std::move (nb);
    }
    else
    {
        auto ir = std::make_unique<IrBlock>();
        if (! engine.loadGear (*ir, f, err)) { showMessage (err); return false; }
        n = std::move (ir);
    }
    if (auto* added = engine.insert (std::move (n), *chain, index))
    {
        selectedId = added->id;
        showMessage ("Added " + f.getFileNameWithoutExtension());
    }
    return true;
}

static std::unique_ptr<Node> blockWithCaptures (const juce::Array<juce::File>& files, bool nam, juce::String& err)
{
    std::unique_ptr<Node> n;
    if (nam) n = std::make_unique<NamBlock>();
    else     n = std::make_unique<IrBlock>();
    if (! dynamic_cast<Sweepable*> (n.get())->loadCaptures (files, 0.0f, err)) return nullptr;
    return n;
}

void RigsmithEditor::dropCaptures (Node* target, Chain* chain, int index, const CaptureDrop& drop, DropMode mode)
{
    if (drop.isEmpty()) return;
    juce::String err;
    const bool irs = drop.nams.isEmpty() || (target != nullptr && target->getType() == NodeType::Ir);
    auto count = [irs] (int n) { return juce::String (n) + (irs ? (n == 1 ? " IR" : " IRs") : (n == 1 ? " capture" : " captures")); };

    if (target != nullptr)
    {
        auto* sw = dynamic_cast<Sweepable*> (target);
        const auto files = drop.forBlock (*target);
        if (sw != nullptr && ! files.isEmpty())
        {
            // a whole folder, or an empty block, just loads; otherwise ask: replace, or add to the sweep?
            if (mode == DropMode::Ask && (drop.folder || sw->getNumCaptures() == 0)) mode = DropMode::Replace;
            if (mode == DropMode::Ask)
            {
                // ask in a little card where it was dropped: replace the sweep, or add to it
                const auto id = target->id;
                const int total = juce::jmin (Engine::maxSweep, sw->getNumCaptures() + files.size());
                dropPrompt = std::make_unique<DropPrompt> (count (files.size()) + " onto " + target->getName(),
                                                           files.size() == 1 ? juce::String ("Replace") : juce::String ("Replace with these"),
                                                           "Add to the sweep (" + juce::String (total) + ")");
                dropPrompt->onChoice = [sp = juce::Component::SafePointer<RigsmithEditor> (this), id, drop] (int choice)
                {
                    if (sp == nullptr) return;
                    juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->dropPrompt.reset(); });
                    if (choice == 0) return;
                    if (auto* n = sp->engine.findNode (id))
                        sp->dropCaptures (n, nullptr, -1, drop, choice == 1 ? DropMode::Replace : DropMode::Add);
                };
                addAndMakeVisible (*dropPrompt);
                const auto mouse = getMouseXYRelative();
                const auto size = dropPrompt->getPreferredSize();
                dropPrompt->setBounds (juce::jlimit (8, getWidth() - size.x - 8, mouse.x - size.x / 2),
                                       juce::jlimit (72, getHeight() - size.y - 8, mouse.y + 14), size.x, size.y);
                dropPrompt->toFront (true);
                return;
            }

            const int before = sw->getNumCaptures();
            const bool ok = mode == DropMode::Add ? engine.addCaptures (*sw, files, err) : sw->loadCaptures (files, 0.0f, err);
            if (! ok) { showMessage (err); return; }
            engine.updateAutoSkips();
            engine.markDirty();
            select (target->id);
            inspector.setNode (target);
            graph.repaint();
            if (mode == DropMode::Add)
            {
                const int added = sw->getNumCaptures() - before;
                showMessage (added > 0 ? "Added " + count (added) + " to the sweep (" + juce::String (sw->getNumCaptures()) + ")"
                                       : juce::String ("Already in the sweep"));
            }
            else showMessage ("Loaded " + count (sw->getNumCaptures()) + (drop.folder ? " from the folder" : ""));
            return;
        }

        // a different kind of block: replace it with one that holds these
        const bool nam = ! drop.nams.isEmpty();
        auto fresh = blockWithCaptures (nam ? drop.nams : drop.irs, nam, err);
        if (fresh == nullptr) { showMessage (err); return; }
        const auto oldName = target->getName();
        if (auto* r = engine.replace (target->id, std::move (fresh)))
        {
            selectedId = r->id;
            showMessage ("Replaced " + oldName + " with " + r->getName() + " - Ctrl+Z to undo");
        }
        return;
    }

    // new blocks where they were dropped: the amp first, then its cab
    if (chain == nullptr) chain = &engine.getRoot();
    Node* last = nullptr;
    for (bool nam : { true, false })
    {
        const auto& files = nam ? drop.nams : drop.irs;
        if (files.isEmpty()) continue;
        auto fresh = blockWithCaptures (files, nam, err);
        if (fresh == nullptr) { showMessage (err); continue; }
        int at = index;
        if (last != nullptr)
            for (int i = 0; i < (int) chain->nodes.size(); ++i)
                if (chain->nodes[(size_t) i].get() == last) at = i + 1;
        last = engine.insert (std::move (fresh), *chain, at);
        if (last != nullptr) selectedId = last->id;
    }
    if (last != nullptr)
        showMessage ("Added " + count (drop.nams.size() + drop.irs.size()) + (drop.folder ? " from the folder" : ""));
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

bool RigsmithEditor::insertBuiltIn (NodeType t, Chain& chain, int index)
{
    auto* added = engine.insert (engine.createNode (t), chain, index);
    if (added == nullptr) return false;
    selectedId = added->id;
    select (added->id);
    return true;
}

std::pair<Chain*, int> RigsmithEditor::insertionPoint()
{
    const auto where = engine.locate (selectedId);
    if (where.chain != nullptr) return { where.chain, where.index + 1 };
    return { &engine.getRoot(), -1 };
}

//==============================================================================
enum AddIds
{
    addSplit2 = 10, addSplit3, addXover, addStereo, addScan = 20, addT3kNam = 30, addT3kIr,
    addBuiltInBase = 100,           // + (int) NodeType
    addPluginBase = 1000,
    insertOffset = 50000, replaceOffset = 70000
};

void RigsmithEditor::populateAddMenu (juce::PopupMenu& m, int o, bool includeRouting)
{
    m.addSectionHeader ("Amp & cab");
    m.addItem (o + addBuiltInBase + (int) NodeType::Nam, "NAM Amp");
    m.addItem (o + addBuiltInBase + (int) NodeType::Ir, "Cab IR");
    m.addItem (o + addT3kNam, "Browse TONE3000: amps, pedals, full rigs...");
    m.addItem (o + addT3kIr, "Browse TONE3000: cab and space IRs...");
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
        case addT3kNam: case addT3kIr:
            browseTone3000 (r == addT3kNam ? "nam" : "ir", [this, chainId, index] (const juce::File& f)
            {
                if (auto* c = chainById (chainId)) loadFileInto (nullptr, c, index, f);
            });
            return;
        case addSplit2: case addSplit3: case addXover: case addStereo:
        {
            auto* s = engine.addSplit (*chain, index, r == addSplit2 || r == addStereo ? 2 : 3);
            if (s == nullptr) return;
            if (r == addXover)  s->setMode (SplitMode::Crossover);
            if (r == addStereo) s->setMode (SplitMode::Stereo);
            // empty paths: drop amps, IRs or effects into them (an empty path passes the signal straight through)
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
        if (r - replaceOffset == addT3kNam || r - replaceOffset == addT3kIr)
        {
            const bool fits = (n->getType() == NodeType::Nam && r - replaceOffset == addT3kNam)
                           || (n->getType() == NodeType::Ir && r - replaceOffset == addT3kIr);
            browseTone3000 (r - replaceOffset == addT3kNam ? "nam" : "ir", [this, id, fits] (const juce::File& f)
            {
                auto* target = engine.findNode (id);
                if (target == nullptr) { placeFile (f); return; }
                if (fits) { loadFileInto (target, nullptr, -1, f); return; }
                const auto loc = engine.locate (id);   // a different kind: swap the block for a new one
                if (loc.chain != nullptr && loadFileInto (nullptr, loc.chain, loc.index, f)) engine.remove (id);
            });
            return;
        }
        handleReplace (r - replaceOffset, id);
    });
}

void RigsmithEditor::browseFile (Node& n, int delta)
{
    juce::File current;
    if (auto* nb = dynamic_cast<NamBlock*> (&n)) current = nb->getFile();
    else if (auto* ir = dynamic_cast<IrBlock*> (&n)) current = ir->getFile();
    const bool isNam = n.getType() == NodeType::Nam;

    // through the gear in the library, in the library's order (the capture knob picks within one)
    const auto& groups = engine.captures.get (! isNam).groups;
    if (! groups.empty())
    {
        int at = -1;
        for (size_t i = 0; i < groups.size() && at < 0; ++i)
            if (groups[i]->files.contains (current)) at = (int) i;
        if (at >= 0 || ! current.existsAsFile())
        {
            const int count = (int) groups.size();
            const int next = at < 0 ? (delta > 0 ? 0 : count - 1) : (at + delta + count) % count;
            loadFileInto (&n, nullptr, -1, groups[(size_t) next]->files[0]);
            return;
        }
    }

    // a file from outside the library: its folder
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

void RigsmithEditor::switchCapture (Node& n, const juce::File& f)
{
    juce::String err;
    bool ok = false;
    if (auto* nb = dynamic_cast<NamBlock*> (&n))
    {
        // in the sweep already: just go there; otherwise load it (with its gear's captures)
        if (const int i = nb->getCaptureFiles().indexOf (f); i >= 0) { nb->setCapturePosition ((float) i); ok = true; }
        else ok = engine.loadGear (*nb, f, err);
    }
    else if (auto* ir = dynamic_cast<IrBlock*> (&n)) ok = ir->getFile() == f || ir->loadIr (f, err);
    if (! ok) { showMessage (err); return; }
    engine.updateAutoSkips();
    engine.markDirty();
    graph.repaint();
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
        juce::PopupMenu sp;
        sp.addItem (4, "2 paths: this on A, B empty");
        sp.addItem (5, "3 paths: this on A, B and C empty");
        sp.addItem (12, "2 paths: this on both (then change one)");
        m.addSubMenu ("Split here", sp);
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
            case 12: engine.wrapInSplit (id, 2, true); break;
            case 6: if (chain != nullptr) engine.move (id, *chain, index - 1); break;
            case 7: if (chain != nullptr) engine.move (id, *chain, index + 2); break;
            case 8: { MidiMapping mm; mm.target = MidiMapping::Target::Bypass; mm.nodeId = id; engine.startLearn (mm); break; }
            case 9: engine.remove (id); break;
            case 30: n->autoGain = ! n->autoGain.load(); if (n->autoGain.load()) n->requestRelearn(); engine.markDirty(); break;
            case 40: randomise (*n, RandomSettings); break;
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
    m.addSectionHeader ("Starting points");
    const int fi = engine.getFactoryIndex();
    for (int i = 0; i < Engine::getFactoryNames().size(); ++i)
        m.addItem (50 + i, Engine::getFactoryNames()[i] + "  (" + Engine::getFactoryDescription (i) + ")", true, fi == i);
    m.addSeparator();
    m.addSectionHeader ("Your presets");
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
        else if (r >= 50 && r < 60) { engine.loadFactory (r - 50); selectedId.clear(); }
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
    m.addItem (1, "Rename (or double-click it)");
    m.addItem (2, "MIDI learn: select this scene");
    m.addItem (3, "Copy current sound to this scene", scene != engine.getScene());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.scenes[scene]), [this, scene] (int r)
    {
        if (r == 1)
        {
            header.renameScene (scene);
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
    {
        m.addItem (1, "Audio / MIDI settings...");
        juce::PopupMenu state;
        state.addItem (21, "Save the current state as...");
        state.addItem (22, "Load a saved state...");
        state.addSeparator();
        state.addItem (23, "Reset to the default state");
        state.addSeparator();
        state.addItem (27, "Show Rigsmith.log (what happened while starting)");
        m.addSubMenu ("Session state", state);
    }
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
    m.addItem (20, "Tooltips", true, tooltips != nullptr);
    if (const int skipped = engine.getSkippedPlugins().size(); skipped > 0)
        m.addItem (24, "Retry skipped plug-ins (" + juce::String (skipped) + " crashed while being scanned)");
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
           #if RIGSMITH_HAS_STANDALONE_HOLDER
            case 21: if (auto* h = juce::StandalonePluginHolder::getInstance()) h->askUserToSaveState(); break;
            case 22: if (auto* h = juce::StandalonePluginHolder::getInstance()) h->askUserToLoadState(); break;
            case 23:
                if (auto* w = dynamic_cast<juce::StandaloneFilterWindow*> (getTopLevelComponent())) w->resetToDefaultState();
                break;
           #endif
            case 18: setFullScreen (! isFullScreen()); break;
            case 19: setLibraryVisible (! libraryVisible); break;
            case 20: setTooltipsEnabled (tooltips == nullptr); break;
            case 27: engine.ini.getFile().getSiblingFile ("Rigsmith.log").revealToUser(); break;
            case 24: engine.clearSkippedPlugins(); showMessage ("They'll be scanned again next time you scan"); break;
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
    auto* s = new SettingsPanel (engine, processor.isStandalone());
    const auto foldersBefore = engine.getPaths ("modelPaths").toString() + "|" + engine.getPaths ("irPaths").toString();
    s->onChanged = [sp = juce::Component::SafePointer<RigsmithEditor> (this), foldersBefore]
    {
        if (sp == nullptr) return;
        const auto now = sp->engine.getPaths ("modelPaths").toString() + "|" + sp->engine.getPaths ("irPaths").toString();
        if (now != foldersBefore)
        {
            // new folders: read them in the background, with progress, then refresh
            sp->loadLibraryInBackground();
            return;
        }
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
    trace ("scanForNewPlugins");
    const auto& blacklist = engine.knownPlugins.getBlacklistedFiles();
    for (auto* f : engine.formatManager.getFormats())
    {
        if (! f->canScanForPlugins()) continue;
        const auto path = engine.getPluginPaths (*f);   // the folders in Rigsmith.ini
        juce::StringArray fresh;
        for (auto& file : f->searchPathsForPlugins (path, true, false))
            if (! blacklist.contains (file) && ! isRigsmithPlugin (file) && ! engine.knownPlugins.isListingUpToDate (file, *f))
                fresh.add (file);
        if (fresh.isEmpty()) continue;
        showMessage ("Found " + juce::String (fresh.size()) + " new " + f->getName() + " plug-in" + (fresh.size() == 1 ? "" : "s") + " - scanning");
        if (auto* list = showPluginScanner())
            list->scanFor (*f, fresh);
        return;   // one format at a time; the rest are found next launch or with Scan
    }
    engine.markPluginsScanned();
    if (always) showMessage ("No new plug-ins found");
}

//==============================================================================
bool RigsmithEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).isDirectory() || juce::File (f).hasFileExtension ("nam;wav;aif;aiff;flac;rig"))
            return true;
    return false;
}

void RigsmithEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    for (auto& path : files)
        if (juce::File (path).hasFileExtension ("rig")) { engine.loadPreset (juce::File (path)); return; }

    // captures (and whole folders of them): where they land in the rig
    const auto drop = CaptureDrop::fromPaths (files);
    if (drop.isEmpty()) { showMessage ("No .nam captures or IRs there"); return; }
    if (viewport.getBounds().contains (x, y)) graph.dropFiles (drop, graph.getLocalPoint (this, juce::Point<int> (x, y)));
    else dropCaptures (nullptr, &engine.getRoot(), -1, drop, DropMode::Replace);
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
        if (c == 'm' || c == 'M') { setModVisible (! modVisible); return true; }
        if (auto* n = engine.findNode (selectedId))
        {
            if (c == 'r') { randomise (*n, RandomSettings); return true; }
            if (c == 'R') { randomise (*n, RandomType); return true; }
        }
    }
    if (c >= '1' && c <= '4') { engine.setScene (c - '1'); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (dropPrompt != nullptr) { dropPrompt.reset(); return true; }
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
    return engine.getIntSetting (key, def);
}

void RigsmithEditor::setIntSetting (const juce::String& key, int value)
{
    engine.setIntSetting (key, value);
}

void RigsmithEditor::setModVisible (bool v)
{
    modVisible = v;
    if (modPanel.isCollapsed() == v) { modPanel.setCollapsed (! v); return; }
    header.modButton.setToggleState (v, juce::dontSendNotification);
    engine.setSetting ("modVisible", v);
    resized();
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
    if (busy != nullptr) return;
    whileBusy ("Rolling the dice", randomiser.options.usePlugins ? "Building a rig: loading amps, cabs and plug-ins..." : "Building a rig: loading amps and cabs...",
               [this] { randomiseRigNow(); });
}

void RigsmithEditor::randomiseRigNow()
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
    m.addSeparator();
    m.addItem (10, "Use VST plug-ins too (ones that have loaded quickly in Rigsmith before)", true, randomiser.options.usePlugins);
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
    if (dynamic_cast<Block*> (&n) != nullptr)
        m.addItem (3, "Random " + categoryName (Randomiser::categoryOf (n)) + " block  (Shift+R)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, id] (int r)
    {
        if (auto* node = engine.findNode (id); node != nullptr && r > 0)
            randomise (*node, r == 1 ? RandomSettings : RandomType);
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
    const auto& factory = factoryPresets (n.getType());
    if (! factory.empty())
    {
        m.addSectionHeader ("Starting points");
        for (size_t i = 0; i < factory.size(); ++i)
            m.addItem (1000 + (int) i, factory[i].name + "   -  " + factory[i].blurb);
    }
    m.addSectionHeader ("Yours");
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
        else if (r >= 1000) { engine.applyFactoryPreset (id, r - 1000); inspector.setNode (engine.findNode (id)); }
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
    else if (d.startsWith ("builtin:"))
        fresh = engine.createNode (nodeTypeFromString (d.fromFirstOccurrenceOf ("builtin:", false, false)));
    else if (CaptureDrop::isCaptureDescription (d))
    {
        dropCaptures (&target, nullptr, -1, CaptureDrop::fromDescription (d), DropMode::Replace);
        return;
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
