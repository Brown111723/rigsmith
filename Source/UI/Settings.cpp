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

    auto* settings = engine.props.getUserSettings();
    for (auto* f : engine.formatManager.getFormats())
    {
        if (! f->canScanForPlugins() || f->getDefaultLocationsToSearch().getNumPaths() == 0) continue;
        auto* s = pluginSections.add (new Section());
        const auto name = f->getName() == "VST" ? juce::String ("VST2") : f->getName();
        setupSection (*s, name + " plug-in folders", "Scanned for " + name + " effects");
        auto path = settings != nullptr ? juce::PluginListComponent::getLastSearchPath (*settings, *f) : juce::FileSearchPath();
        if (path.getNumPaths() == 0) path = f->getDefaultLocationsToSearch();
        s->list.setPath (path);
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
    scanOnStartup.setToggleState (engine.getSetting ("scanOnStartup", true), juce::dontSendNotification);
    addAndMakeVisible (autoCategorise);
    addAndMakeVisible (scanOnStartup);
    startFullScreen.setToggleState (engine.getSetting ("fullScreen", false), juce::dontSendNotification);
    addAndMakeVisible (startFullScreen);
    scanNow.onClick = [this] { if (onScanNow) onScanNow(); };
    addAndMakeVisible (scanNow);

    setSize (720, 670 + pluginSections.size() * 150);
}

SettingsPanel::~SettingsPanel()
{
    engine.setPaths ("modelPaths", models.list.getPath());
    engine.setPaths ("irPaths", irs.list.getPath());
    if (auto* settings = engine.props.getUserSettings())
        for (int i = 0; i < pluginFormats.size(); ++i)
            juce::PluginListComponent::setLastSearchPath (*settings, *pluginFormats[i], pluginSections[i]->list.getPath());
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
    scanNow.setBounds (r.removeFromTop (32).removeFromLeft (180));
}

} // namespace rig
