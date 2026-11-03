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

    // where things are saved: rig presets, block presets, TONE3000 downloads
    struct FolderRow
    {
        juce::Label title, path;
        juce::TextButton choose { "Choose..." }, reset;
    };
    juce::Label savedTitle;
    FolderRow presetRow, blockRow, t3kRow;
    juce::TextButton withModels { "Keep presets with my NAM models" };
    std::unique_ptr<juce::FileChooser> chooser;
    void setupRow (FolderRow&, const juce::String& title, const juce::String& resetText, const juce::String& key);
    void chooseFolder (const juce::String& key);
    void setFolderAndMove (const juce::String& key, const juce::File& to);
    void updateRows();
    juce::File currentFolder (const juce::String& key) const;

    void updateIniNote();
    void setupSection (Section&, const juce::String& title, const juce::String& hint);
};

} // namespace rig
