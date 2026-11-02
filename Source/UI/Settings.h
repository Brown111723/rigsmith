#pragma once

#include "Theme.h"

namespace rig
{
/** Library and plug-in folders plus a few preferences. Everything is saved to Rigsmith.ini
    when the window closes and read back automatically next time Rigsmith opens. */
class SettingsPanel : public juce::Component
{
public:
    SettingsPanel (Engine&, bool isStandalone);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onChanged;
    std::function<void()> onScanNow;

private:
    struct Section
    {
        juce::Label title, hint;
        juce::FileSearchPathListComponent list;
    };

    Engine& engine;
    Section models, irs;
    juce::OwnedArray<Section> pluginSections;
    juce::Array<juce::AudioPluginFormat*> pluginFormats;
    juce::ToggleButton autoCategorise { "Auto-categorise and colour plug-ins (Drive, Delay, Reverb...)" };
    juce::ToggleButton startFullScreen { "Open Rigsmith full screen (standalone; F11 toggles)" }, scanOnStartup { "Look for new plug-ins every time Rigsmith opens (otherwise only the first time)" };
    juce::TextButton scanNow { "Scan plug-ins now" }, showIni { "Show Rigsmith.ini" }, portable;
    juce::Label vst2Note, iniNote;
    // TONE3000
    juce::Label t3kTitle, t3kHint;
    juce::TextEditor t3kKey;
    juce::ComboBox t3kArch;

    void updateIniNote();
    void setupSection (Section&, const juce::String& title, const juce::String& hint);
};

} // namespace rig
