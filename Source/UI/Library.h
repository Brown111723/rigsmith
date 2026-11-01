#pragma once

#include "GraphView.h"

namespace rig
{
/** Right-hand browser: NAM models, cab IRs and scanned VST3s. Drag onto the rig or double-click. */
class Library : public juce::Component,
                private juce::ListBoxModel
{
public:
    explicit Library (GraphHost&);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::File&)> onActivateFile;
    std::function<void (const juce::String& pluginIdentifier)> onActivatePlugin;
    std::function<void()> onScanPlugins;

private:
    enum Tab { Models = 0, Irs, Plugins };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { activate (row); }
    void activate (int row);
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getTooltipForRow (int row) override;

    void setTab (Tab);
    void applyFilter();
    void chooseFolder();
    juce::String folderKey() const { return tab == Models ? "modelFolder" : "irFolder"; }

    GraphHost& host;
    Tab tab = Models;
    juce::TextButton tabs[3] { juce::TextButton ("Models"), juce::TextButton ("IRs"), juce::TextButton ("VST3") };
    juce::TextButton folderButton;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Label footer;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Array<juce::File> files;
    juce::Array<juce::PluginDescription> plugins;
    juce::Array<int> rows;   // indices into files/plugins after filtering
};

} // namespace rig
