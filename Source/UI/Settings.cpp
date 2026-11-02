#include "Settings.h"

namespace rig
{
using namespace theme;

SettingsPanel::SettingsPanel (Engine& e) : engine (e)
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

    const auto ini = engine.ini.getFile();
    iniNote.setFont (font (12.0f));
    iniNote.setColour (juce::Label::textColourId, muted);
    iniNote.setText ("Saved in " + ini.getFullPathName() + " - folders, preferences and the randomise ticks. "
                     "Put a copy next to Rigsmith.exe to make a portable install.", juce::dontSendNotification);
    addAndMakeVisible (iniNote);
    showIni.onClick = [ini] { if (ini.existsAsFile()) ini.revealToUser(); else ini.getParentDirectory().startAsProcess(); };
    addAndMakeVisible (showIni);

    setSize (720, 720 + pluginSections.size() * 150);
}

SettingsPanel::~SettingsPanel()
{
    engine.setPaths ("modelPaths", models.list.getPath());
    engine.setPaths ("irPaths", irs.list.getPath());
    for (int i = 0; i < pluginFormats.size(); ++i)
        engine.setPluginPaths (*pluginFormats[i], pluginSections[i]->list.getPath());
    engine.setSetting ("autoCategorise", autoCategorise.getToggleState());
    engine.setSetting ("scanOnStartup", scanOnStartup.getToggleState());
    engine.setSetting ("fullScreen", startFullScreen.getToggleState());
    PluginBlock::autoCategorise = autoCategorise.getToggleState();
    if (onChanged) onChanged();
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
    for (auto* s : pluginSections) place (*s, 110);
    vst2Note.setBounds (r.removeFromTop (36));
    r.removeFromTop (6);
    autoCategorise.setBounds (r.removeFromTop (28));
    scanOnStartup.setBounds (r.removeFromTop (28));
    startFullScreen.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto buttons = r.removeFromTop (32);
    scanNow.setBounds (buttons.removeFromLeft (180));
    buttons.removeFromLeft (10);
    showIni.setBounds (buttons.removeFromLeft (180));
    r.removeFromTop (10);
    iniNote.setBounds (r.removeFromTop (40));
}

} // namespace rig
