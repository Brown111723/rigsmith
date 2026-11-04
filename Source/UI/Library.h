#pragma once

#include "GraphView.h"
#include "Tone3000Panel.h"

namespace rig
{
/** Right-hand browser: NAM models, cab IRs, scanned VST3s and the built-in effects.
    Drag onto the rig or double-click.

    Models and IRs are listed by gear, under a heading per type (amp heads, amp + cab,
    pedals...): every capture of one amp is one row ("Marshall JCM800 - 12 captures"),
    which opens up to show each capture. The die on each row says whether the dice may pick
    it (saved in Rigsmith.ini); it never switches anything off. */
class Library : public juce::Component,
                private juce::ListBoxModel
{
public:
    explicit Library (GraphHost&);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::File&)> onActivateFile;
    /** Double-clicked: a gear row (all its captures) or a capture. Exactly these get loaded. */
    std::function<void (const juce::Array<juce::File>&)> onActivateFiles;
    std::function<void (const juce::String& pluginIdentifier)> onActivatePlugin;
    std::function<void (NodeType)> onActivateBuiltIn;
    std::function<void()> onScanPlugins;
    std::function<void()> onManageFolders;

    enum Tab { Models = 0, Irs, Plugins, Effects, Tone3000 };
    void setTab (Tab);
    /** Adds the TONE3000 page (the editor owns the client). */
    void setTone3000 (t3k::Client&);
    Tone3000Panel* getTone3000Panel() const { return t3kPanel.get(); }

    /** For tests and screenshots. */
    void setSearchText (const juce::String& s) { search.setText (s, true); }
    void setExpanded (const juce::String& groupTitle, bool);
    int getNumListRows() const { return rows.size(); }

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

    // the dice icons: may the dice pick this?
    enum class Dice { None, Some, All };
    Dice diceState (int row) const;
    bool isIncluded (int row) const { return diceState (row) == Dice::All; }
    void setIncluded (const juce::Array<int>& rowsToChange, bool include);
    juce::Array<int> rowsFor (int clickedRow) const;   // the clicked row, or every selected row if it's one of them
    void showRowMenu (int row);
    void showDiceMenu();
    static juce::Rectangle<int> diceArea (int w, int h) { return { w - 34, h / 2 - 10, 20, 20 }; }
    static void drawDice (juce::Graphics&, juce::Rectangle<int>, Dice, int in, int of);
    bool hasDice (int row) const;   // headers and the tuner have none

    void applyFilter();
    std::unique_ptr<Tone3000Panel> t3kPanel;
    juce::String pathsKey() const { return tab == Models ? "modelPaths" : "irPaths"; }
    bool isFileTab() const noexcept { return tab == Models || tab == Irs; }

    GraphHost& host;
    Tab tab = Models;
    juce::TextButton tabs[numTabs] { juce::TextButton ("Models"), juce::TextButton ("IRs"), juce::TextButton ("VST3"), juce::TextButton ("Built-in"),
                                    juce::TextButton ("TONE3000  -  amps, pedals & IRs from the community") };
    juce::TextButton folderButton;
    juce::TextEditor search;
    /** The list, with a compact "3 captures" tag as its drag image. */
    struct List : public juce::ListBox
    {
        std::function<juce::String (const juce::SparseSet<int>&)> describeDrag;
        juce::ScaledImage createSnapshotOfRows (const juce::SparseSet<int>& rows, int& x, int& y) override;
    } list;
    juce::Label footer;
    juce::TextButton diceButton;

    // models and IRs, by gear
    struct Entry
    {
        enum Kind { Header, Group, Capture } kind = Group;
        Gear gear = Gear::Unknown;
        int group = -1, capture = -1;   // into groups / that group's files
        int groupsInSection = 0, filesInSection = 0;   // headers
    };
    std::vector<CaptureGroupPtr> groups;
    std::vector<Entry> entries;
    std::set<juce::String> expanded;      // group keys opened to show their captures
    std::set<int> collapsedSections;      // gear types folded away
    juce::Array<juce::File> filesFor (int row) const;
    juce::File fileFor (int row) const;   // the one to load / drag
    static juce::Rectangle<int> chevronArea (int h) { return { 16, h / 2 - 8, 16, 16 }; }

    juce::Array<juce::PluginDescription> plugins;
    juce::Array<NodeType> effects;
    int gearFilter = -1;          // -1 = all
    juce::String toneFilter;      // "" = any
    juce::TextButton gearButton, toneButton;
    void showGearMenu();
    void showToneMenu();
    void updateFilterButtons();
    juce::Array<int> rows;   // indices into entries / plugins / effects after filtering
};

} // namespace rig
