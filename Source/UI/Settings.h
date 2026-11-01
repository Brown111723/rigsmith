#pragma once

#include "Theme.h"

namespace rig
{
/** Library and plug-in folders plus a few preferences. Everything is saved when the
    window closes and read back automatically next time Rigsmith opens. */
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (Engine&);
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
    juce::ToggleButton startFullScreen { "Open Rigsmith full screen (standalone; F11 toggles)" }, scanOnStartup { "Look for new plug-ins every time Rigsmith opens" };
    juce::TextButton scanNow { "Scan plug-ins now" };
    juce::Label vst2Note;

    void setupSection (Section&, const juce::String& title, const juce::String& hint);
};

} // namespace rig
