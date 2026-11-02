#pragma once

#include "GraphView.h"
#include "Tone3000Panel.h"

namespace rig
{
/** Right-hand browser: NAM models, cab IRs, scanned VST3s and the built-in effects.
    Drag onto the rig or double-click.
    The tick on each row says whether the dice may pick it (saved in Rigsmith.ini). */
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
    std::function<void (NodeType)> onActivateBuiltIn;
    std::function<void()> onScanPlugins;
    std::function<void()> onManageFolders;

private:
public:
    enum Tab { Models = 0, Irs, Plugins, Effects, Tone3000 };
    void setTab (Tab);
    /** Adds the TONE3000 page (the editor owns the client). */
    void setTone3000 (t3k::Client&);
    Tone3000Panel* getTone3000Panel() const { return t3kPanel.get(); }
private:
    static constexpr int numTabs = 5;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { activate (row); }
    void activate (int row);
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getTooltipForRow (int row) override;

    // randomise ticks
    bool isIncluded (int row) const;
    void setIncluded (const juce::Array<int>& rowsToChange, bool include);
    juce::Array<int> rowsFor (int clickedRow) const;   // the clicked row, or every selected row if it's one of them
    void showRowMenu (int row);
    void showDiceMenu();
    static juce::Rectangle<int> tickArea (int w, int h) { return { w - 34, h / 2 - 10, 20, 20 }; }

    void applyFilter();
    std::unique_ptr<Tone3000Panel> t3kPanel;
    juce::String pathsKey() const { return tab == Models ? "modelPaths" : "irPaths"; }

    GraphHost& host;
    Tab tab = Models;
    juce::TextButton tabs[numTabs] { juce::TextButton ("Models"), juce::TextButton ("IRs"), juce::TextButton ("VST3"), juce::TextButton ("Built-in"),
                                    juce::TextButton ("TONE3000  -  amps, pedals & IRs from the community") };
    juce::TextButton folderButton;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Label footer;
    juce::TextButton diceButton;

    juce::Array<juce::File> files;
    juce::Array<juce::PluginDescription> plugins;
    juce::Array<NodeType> effects;
    juce::Array<int> gears;       // Gear of each file
    int gearFilter = -1;          // -1 = all
    juce::TextButton gearButton;
    void showGearMenu();
    void updateGearButton();
    static int gearRank (Gear);
    bool hasTick (int row) const;   // the tuner has none: it's never randomised
    juce::Array<int> rows;   // indices into files/plugins after filtering
};

} // namespace rig
