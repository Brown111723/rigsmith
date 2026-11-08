#include "Settings.h"

namespace rig
{
using namespace theme;

SettingsPanel::SettingsPanel (Engine& e, bool isStandalone) : engine (e)
{
    setupSection (models, "NAM model folders", "Searched (with sub-folders) for .nam files");
    models.list.setPath (engine.getPaths ("modelPaths"));
    setupSection (irs, "Cab IR folders", "Searched for .wav / .aif / .flac impulse responses");
    irs.list.setPath (engine.getPaths ("irPaths"));

    for (auto* f : engine.formatManager.getFormats())
    {
        if (! f->canScanForPlugins() || f->getDefaultLocationsToSearch().getNumPaths() == 0) continue;
        auto* s = pluginSections.add (new Section());
        const auto name = f->getName() == "VST" ? juce::String ("VST2") : f->getName();
        setupSection (*s, name + " plug-in folders", "Scanned for " + name + " effects");
        s->list.setPath (engine.getPluginPaths (*f));
        pluginFormats.add (f);
    }

    bool hasVst2 = false;
    for (auto* f : pluginFormats) hasVst2 |= f->getName() == "VST";
    vst2Note.setFont (font (12.0f));
    vst2Note.setColour (juce::Label::textColourId, muted);
    vst2Note.setText (hasVst2 ? juce::String() :
        "VST2 isn't in this build: Steinberg no longer licenses the VST2 SDK. Builds made with the SDK "
        "(see README) add a VST2 folder list here automatically.", juce::dontSendNotification);
    addAndMakeVisible (vst2Note);

    autoCategorise.setToggleState (engine.getSetting ("autoCategorise", true), juce::dontSendNotification);
    scanOnStartup.setToggleState (engine.getSetting ("scanOnStartup", false), juce::dontSendNotification);
    scanOnStartup.setTooltip ("Off: Rigsmith only scans the first time it runs (and whenever you click Scan). "
                              "Your plug-in list is remembered either way.");
    addAndMakeVisible (autoCategorise);
    addAndMakeVisible (scanOnStartup);
    startFullScreen.setToggleState (engine.getSetting ("fullScreen", false), juce::dontSendNotification);
    addAndMakeVisible (startFullScreen);
    scanNow.onClick = [this] { if (onScanNow) onScanNow(); };
    addAndMakeVisible (scanNow);

    t3kTitle.setText ("TONE3000", juce::dontSendNotification);
    t3kTitle.setFont (font (14.0f, true));
    t3kHint.setText ("Publishable key (t3k_pub_..., from tone3000.com > Settings > API Keys; leave empty to use the built-in one)",
                     juce::dontSendNotification);
    t3kHint.setFont (font (12.0f));
    t3kHint.setColour (juce::Label::textColourId, muted);
    t3kKey.setFont (mono (13.0f));
    t3kKey.setIndents (8, 7);
    t3kKey.setTextToShowWhenEmpty ("t3k_pub_...", dim);
    t3kKey.setText (engine.ini.get ("TONE3000", "PublishableKey"), false);
    t3kArch.addItem ("Download A2 captures (newest, recommended)", 2);
    t3kArch.addItem ("Download A1 captures", 1);
    t3kArch.setSelectedId (engine.ini.getInt ("TONE3000", "Architecture", 2) == 1 ? 1 : 2, juce::dontSendNotification);
    t3kArch.setTooltip ("Rigsmith plays both. A tone without the chosen kind falls back to the other one.");
    for (auto* c : std::initializer_list<juce::Component*> { &t3kTitle, &t3kHint, &t3kKey, &t3kArch })
        addAndMakeVisible (c);

    savedTitle.setText ("Where Rigsmith saves things", juce::dontSendNotification);
    savedTitle.setFont (font (14.0f, true));
    addAndMakeVisible (savedTitle);
    setupRow (presetRow, "Rig presets (.rig)", "Default", "presetFolder");
    setupRow (blockRow, "Block presets (.rbp)", "Default", "blockPresetFolder");
    setupRow (t3kRow, "TONE3000 downloads", "Default", "downloadFolder");
    t3kRow.reset.setTooltip ("Default: a TONE3000 folder inside your first NAM folder (models) and your first IR folder (IRs)");
    presetRow.reset.setTooltip ("Default: Documents\\Rigsmith\\Presets");
    blockRow.reset.setTooltip ("Default: Documents\\Rigsmith\\Block Presets");
    withModels.setTooltip ("Rig presets go to <your first NAM folder>\\Presets and block presets to <your first NAM folder>\\Block Presets, "
                           "so everything lives together (and your existing presets move with them)");
    withModels.onClick = [this]
    {
        const juce::File nam (models.list.getPath().getRawString (0));
        if (! nam.isDirectory()) return;
        setFolderAndMove ("presetFolder", nam.getChildFile ("Presets"));
        setFolderAndMove ("blockPresetFolder", nam.getChildFile ("Block Presets"));
    };
    addAndMakeVisible (withModels);
    updateRows();

    iniNote.setFont (font (12.0f));
    iniNote.setColour (juce::Label::textColourId, muted);
    addAndMakeVisible (iniNote);
    showIni.onClick = [this]
    {
        const auto ini = engine.ini.getFile();
        if (ini.existsAsFile()) ini.revealToUser(); else ini.getParentDirectory().startAsProcess();
    };
    portable.setVisible (isStandalone);
    portable.onClick = [this]
    {
        const bool isPortable = engine.ini.getFile() == Engine::portableSettingsFile();
        const auto target = isPortable ? Engine::appDataSettingsFile() : Engine::portableSettingsFile();
        if (! engine.moveSettingsTo (target))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save settings there",
                "Rigsmith can't write to " + target.getParentDirectory().getFullPathName()
                + ".\n\nFor a portable copy, unzip Rigsmith somewhere you can write to (a USB stick, Documents...) "
                  "rather than Program Files.");
        updateIniNote();
    };
    updateIniNote();
    addAndMakeVisible (showIni);
    addChildComponent (portable);
    portable.setVisible (isStandalone);

    setSize (720, 1040 + pluginSections.size() * 150);
}

SettingsPanel::~SettingsPanel()
{
    engine.setPaths ("modelPaths", models.list.getPath());
    engine.setPaths ("irPaths", irs.list.getPath());
    for (int i = 0; i < pluginFormats.size(); ++i)
        engine.setPluginPaths (*pluginFormats[i], pluginSections[i]->list.getPath());
    engine.ini.set ("TONE3000", "PublishableKey", t3kKey.getText().trim());
    engine.ini.setInt ("TONE3000", "Architecture", t3kArch.getSelectedId() == 1 ? 1 : 2);
    engine.setSetting ("autoCategorise", autoCategorise.getToggleState());
    engine.setSetting ("scanOnStartup", scanOnStartup.getToggleState());
    engine.setSetting ("fullScreen", startFullScreen.getToggleState());
    PluginBlock::autoCategorise = autoCategorise.getToggleState();
    if (onChanged) onChanged();
}

void SettingsPanel::updateIniNote()
{
    const auto ini = engine.ini.getFile();
    const bool isPortable = ini == Engine::portableSettingsFile();
    iniNote.setText ("Settings file: " + ini.getFullPathName() + "\n"
                     + (isPortable ? "Portable: this copy keeps its settings next to Rigsmith.exe."
                                   : "Rigsmith looks next to Rigsmith.exe first, then here."), juce::dontSendNotification);
    portable.setButtonText (isPortable ? "Keep settings in AppData instead" : "Make this copy portable");
    portable.setTooltip (isPortable ? "Moves Rigsmith.ini back to " + Engine::appDataSettingsFile().getFullPathName()
                                    : "Saves Rigsmith.ini next to Rigsmith.exe, so this folder (e.g. on a USB stick) carries its own settings");
}

void SettingsPanel::setupRow (FolderRow& row, const juce::String& title, const juce::String& resetText, const juce::String& key)
{
    row.title.setText (title, juce::dontSendNotification);
    row.title.setFont (font (13.0f, true));
    row.path.setFont (font (12.0f));
    row.path.setColour (juce::Label::textColourId, muted);
    row.path.setMinimumHorizontalScale (0.7f);
    row.reset.setButtonText (resetText);
    row.choose.onClick = [this, key] { chooseFolder (key); };
    row.reset.onClick = [this, key]
    {
        if (key == "downloadFolder") { engine.setFolder (key, {}); updateRows(); return; }
        const auto def = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rigsmith")
                            .getChildFile (key == "presetFolder" ? "Presets" : "Block Presets");
        setFolderAndMove (key, def);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &row.title, &row.path, &row.choose, &row.reset })
        addAndMakeVisible (c);
}

juce::File SettingsPanel::currentFolder (const juce::String& key) const
{
    if (key == "presetFolder") return engine.getPresetFolder();
    if (key == "blockPresetFolder") return engine.getBlockPresetRoot();
    return engine.getDownloadFolder (false);
}

void SettingsPanel::updateRows()
{
    presetRow.path.setText (engine.getPresetFolder().getFullPathName(), juce::dontSendNotification);
    blockRow.path.setText (engine.getBlockPresetRoot().getFullPathName(), juce::dontSendNotification);
    if (engine.hasCustomDownloadFolder())
        t3kRow.path.setText (engine.getDownloadFolder (false).getFullPathName(), juce::dontSendNotification);
    else
        t3kRow.path.setText (engine.getDownloadFolder (false).getFullPathName() + "  (and the same in your IR folder)",
                             juce::dontSendNotification);
    for (auto* r : { &presetRow, &blockRow, &t3kRow }) r->path.setTooltip (r->path.getText());
    if (! engine.hasCustomDownloadFolder())
        t3kRow.path.setTooltip ("Models: " + engine.getDownloadFolder (false).getFullPathName()
                                + "\nIRs: " + engine.getDownloadFolder (true).getFullPathName());
}

void SettingsPanel::chooseFolder (const juce::String& key)
{
    // start next to the NAM folders: that's where most people want everything
    auto start = currentFolder (key);
    if (! start.isDirectory()) start = juce::File (models.list.getPath().getRawString (0));
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder", start);
    juce::Component::SafePointer<SettingsPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe, key] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        if (key == "downloadFolder")
        {
            // earlier downloads stay where they are (and are still found); new ones go here
            safe->engine.setFolder (key, f);
            auto paths = safe->models.list.getPath();
            bool covered = false;
            for (int i = 0; i < paths.getNumPaths(); ++i)
                covered |= f == juce::File (paths.getRawString (i)) || f.isAChildOf (paths.getRawString (i));
            if (! covered) { paths.add (f); safe->models.list.setPath (paths); }
            auto irPaths = safe->irs.list.getPath();
            covered = false;
            for (int i = 0; i < irPaths.getNumPaths(); ++i)
                covered |= f == juce::File (irPaths.getRawString (i)) || f.isAChildOf (irPaths.getRawString (i));
            if (! covered) { irPaths.add (f); safe->irs.list.setPath (irPaths); }
            safe->updateRows();
            return;
        }
        safe->setFolderAndMove (key, f);
    });
}

void SettingsPanel::setFolderAndMove (const juce::String& key, const juce::File& to)
{
    const auto from = currentFolder (key);
    if (to == from) { updateRows(); return; }
    to.createDirectory();
    engine.setFolder (key, to);
    updateRows();

    // bring the presets that are already saved along, if the person wants that
    const auto pattern = key == "presetFolder" ? "*.rig" : "*.rbp";
    auto existing = from.isDirectory() ? from.findChildFiles (juce::File::findFiles, true, pattern) : juce::Array<juce::File>();
    if (existing.isEmpty()) return;
    const auto what = key == "presetFolder" ? juce::String ("rig preset") : juce::String ("block preset");
    juce::Component::SafePointer<SettingsPanel> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Move your presets?",
        juce::String (existing.size()) + " " + what + (existing.size() == 1 ? " is" : "s are") + " saved in\n" + from.getFullPathName()
            + "\n\nMove them to\n" + to.getFullPathName() + "?",
        "Move them", "Leave them", nullptr,
        juce::ModalCallbackFunction::create ([safe, existing, from, to] (int r)
        {
            if (r == 0 || safe == nullptr) return;
            int failed = 0;
            for (auto& f : existing)
            {
                const auto target = to.getChildFile (f.getRelativePathFrom (from));
                target.getParentDirectory().createDirectory();
                if (target.existsAsFile() || ! f.moveFileTo (target)) ++failed;
            }
            if (failed > 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Some presets stayed put",
                    juce::String (failed) + " couldn't be moved (a file with the same name is already there, or the folder is read-only).");
        }));
}

void SettingsPanel::setupSection (Section& s, const juce::String& title, const juce::String& hint)
{
    s.title.setText (title, juce::dontSendNotification);
    s.title.setFont (font (14.0f, true));
    s.hint.setText (hint, juce::dontSendNotification);
    s.hint.setFont (font (12.0f));
    s.hint.setColour (juce::Label::textColourId, muted);
    s.list.setColour (juce::ListBox::backgroundColourId, bg);
    for (auto* c : std::initializer_list<juce::Component*> { &s.title, &s.hint, &s.list })
        addAndMakeVisible (c);
}

void SettingsPanel::paint (juce::Graphics& g) { g.fillAll (panel); }

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto place = [&r] (Section& s, int h)
    {
        s.title.setBounds (r.removeFromTop (22));
        s.hint.setBounds (r.removeFromTop (18));
        s.list.setBounds (r.removeFromTop (h));
        r.removeFromTop (14);
    };
    place (models, 120);
    place (irs, 120);
    // presets and downloads, next to the NAM / IR folders
    savedTitle.setBounds (r.removeFromTop (24));
    for (auto* row : { &presetRow, &blockRow, &t3kRow })
    {
        auto line = r.removeFromTop (28);
        row->title.setBounds (line.removeFromLeft (150));
        row->reset.setBounds (line.removeFromRight (80).reduced (0, 1));
        line.removeFromRight (6);
        row->choose.setBounds (line.removeFromRight (90).reduced (0, 1));
        line.removeFromRight (8);
        row->path.setBounds (line);
        r.removeFromTop (6);
    }
    withModels.setBounds (r.removeFromTop (28).removeFromLeft (260));
    r.removeFromTop (20);
    for (auto* s : pluginSections) place (*s, 110);
    vst2Note.setBounds (r.removeFromTop (36));
    t3kTitle.setBounds (r.removeFromTop (22));
    t3kHint.setBounds (r.removeFromTop (18));
    t3kKey.setBounds (r.removeFromTop (32).removeFromLeft (440));
    r.removeFromTop (6);
    t3kArch.setBounds (r.removeFromTop (28).removeFromLeft (360));
    r.removeFromTop (20);
    autoCategorise.setBounds (r.removeFromTop (28));
    scanOnStartup.setBounds (r.removeFromTop (28));
    startFullScreen.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto buttons = r.removeFromTop (32);
    scanNow.setBounds (buttons.removeFromLeft (180));
    buttons.removeFromLeft (10);
    showIni.setBounds (buttons.removeFromLeft (170));
    buttons.removeFromLeft (10);
    portable.setBounds (buttons.removeFromLeft (230));
    r.removeFromTop (10);
    iniNote.setBounds (r.removeFromTop (40));
}

} // namespace rig
