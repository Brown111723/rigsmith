#include "Library.h"

namespace rig
{
using namespace theme;

Library::Library (GraphHost& h) : host (h)
{
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setClickingTogglesState (false);
        tabs[i].onClick = [this, i] { setTab ((Tab) i); };
        addAndMakeVisible (tabs[i]);
    }

    folderButton.onClick = [this]
    {
        if (tab == Plugins) { if (onScanPlugins) onScanPlugins(); }
        else if (onManageFolders) onManageFolders();
    };
    addAndMakeVisible (folderButton);

    search.setTextToShowWhenEmpty ("Search", muted);
    search.setFont (font (14.0f));
    search.setIndents (10, 7);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (42);
    list.setColour (juce::ListBox::backgroundColourId, panel);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    list.setMultipleSelectionEnabled (true);

    diceButton.setTooltip ("Which of these the dice can pick: tick or untick everything shown (use Search to narrow it down first)");
    diceButton.onClick = [this] { showDiceMenu(); };
    addAndMakeVisible (diceButton);

    footer.setFont (font (11.0f));
    footer.setColour (juce::Label::textColourId, muted);
    addAndMakeVisible (footer);

    setTab (Models);
}

void Library::setTab (Tab t)
{
    tab = t;
    for (int i = 0; i < 3; ++i)
        tabs[i].setToggleState (i == (int) t, juce::dontSendNotification);
    folderButton.setButtonText (tab == Plugins ? "Scan..." : "Folders...");
    folderButton.setTooltip (tab == Plugins ? "Scan for VST plug-ins" : "Choose which folders are searched - remembered between sessions");
    refresh();
}

void Library::refresh()
{
    files.clear();
    plugins.clear();

    if (tab == Plugins)
    {
        for (auto& d : host.getEngine().knownPlugins.getTypes())
            if (! d.isInstrument)
                plugins.add (d);
        const bool byCat = PluginBlock::autoCategorise;
        std::sort (plugins.begin(), plugins.end(), [byCat] (const auto& a, const auto& b)
        {
            if (byCat)
            {
                const auto ca = (int) categoriseDescription (a), cb = (int) categoriseDescription (b);
                if (ca != cb) return ca < cb;
                return a.name.compareIgnoreCase (b.name) < 0;
            }
            return (a.manufacturerName + a.name).compareIgnoreCase (b.manufacturerName + b.name) < 0;
        });
    }
    else
    {
        const auto paths = host.getEngine().getPaths (pathsKey());
        const auto pattern = tab == Models ? juce::String ("*.nam") : juce::String ("*.wav;*.aif;*.aiff;*.flac");
        for (int p = 0; p < paths.getNumPaths() && files.size() < 8000; ++p)
            for (const auto& entry : juce::RangedDirectoryIterator (paths.getRawString (p), true, pattern, juce::File::findFiles))
            {
                files.addIfNotAlreadyThere (entry.getFile());
                if (files.size() >= 8000) break;
            }
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });
    }
    applyFilter();
}

void Library::applyFilter()
{
    rows.clear();
    const auto words = juce::StringArray::fromTokens (search.getText(), " ", {});
    auto matches = [&words] (const juce::String& s)
    {
        for (auto& w : words)
            if (w.isNotEmpty() && ! s.containsIgnoreCase (w)) return false;
        return true;
    };

    if (tab == Plugins)
    {
        for (int i = 0; i < plugins.size(); ++i)
            if (matches (plugins[i].name + " " + plugins[i].manufacturerName + " " + plugins[i].category))
                rows.add (i);
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
            if (matches (files[i].getFileName() + " " + files[i].getParentDirectory().getFileName()))
                rows.add (i);
    }

    list.updateContent();
    list.repaint();

    juce::String f;
    if (tab == Plugins)
        f = plugins.isEmpty() ? "No VST3s yet - click Scan" : juce::String (rows.size()) + " effects - drag onto the rig";
    else
    {
        const auto paths = host.getEngine().getPaths (pathsKey());
        f = juce::String (rows.size()) + " files in " + juce::String (paths.getNumPaths()) + (paths.getNumPaths() == 1 ? " folder" : " folders");
    }
    footer.setText (f, juce::dontSendNotification);

    int included = 0;
    for (int i = 0; i < rows.size(); ++i) included += isIncluded (i) ? 1 : 0;
    diceButton.setButtonText (juce::String (included) + " / " + juce::String (rows.size()) + " in dice");
    diceButton.setColour (juce::TextButton::textColourOffId, included < rows.size() ? accent : muted);
}

int Library::getNumRows() { return rows.size(); }

void Library::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    if (selected)
    {
        g.setColour (cardHi);
        g.fillRoundedRectangle (4.0f, 2.0f, (float) w - 8.0f, (float) h - 4.0f, 6.0f);
    }

    juce::String name, sub;
    juce::Colour c = accent;
    if (tab == Plugins)
    {
        const auto& d = plugins[rows[row]];
        name = d.name;
        const auto cat = categoriseDescription (d);
        sub = (PluginBlock::autoCategorise ? categoryName (cat) + " - " : juce::String()) + d.manufacturerName
            + (d.pluginFormatName != "VST3" ? "  (" + d.pluginFormatName + ")" : juce::String());
        c = PluginBlock::autoCategorise ? categoryColour (cat)
                                        : juce::Colour::fromHSV ((float) ((juce::uint32) d.name.hashCode() % 360) / 360.0f, 0.45f, 0.95f, 1.0f);
    }
    else
    {
        const auto& f = files[rows[row]];
        name = f.getFileNameWithoutExtension();
        sub = f.getParentDirectory().getFileName();
        c = tab == Models ? categoryColour (Category::Amp) : categoryColour (Category::Cab);
    }

    const bool in = isIncluded (row);
    if (! in) sub = "Not in dice - " + sub;

    g.setColour (in ? c : c.withAlpha (0.35f));
    g.fillRoundedRectangle (12.0f, (float) h * 0.5f - 9.0f, 4.0f, 18.0f, 2.0f);
    g.setColour (in ? text : text.withAlpha (0.55f));
    g.setFont (font (13.0f, true));
    g.drawText (name, 24, h / 2 - 17, w - 66, 18, juce::Justification::centredLeft, true);
    g.setColour (muted.withAlpha (in ? 1.0f : 0.8f));
    g.setFont (font (11.0f));
    g.drawText (sub, 24, h / 2 + 2, w - 66, 15, juce::Justification::centredLeft, true);

    // the randomise tick: a little die face when included, an empty box when not
    const auto t = tickArea (w, h).toFloat();
    if (in)
    {
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (t, 5.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (t.reduced (0.5f), 5.0f, 1.3f);
        juce::Path tick;
        tick.startNewSubPath (t.getX() + 5.0f, t.getCentreY() + 0.5f);
        tick.lineTo (t.getX() + 8.5f, t.getBottom() - 5.5f);
        tick.lineTo (t.getRight() - 4.5f, t.getY() + 5.5f);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (borderHi);
        g.drawRoundedRectangle (t.reduced (0.5f), 5.0f, 1.2f);
    }
}

void Library::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (! tickArea (list.getVisibleRowWidth(), list.getRowHeight()).expanded (6).contains (e.getPosition()))
        activate (row);
}

void Library::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    if (e.mods.isPopupMenu()) { showRowMenu (row); return; }
    if (tickArea (list.getVisibleRowWidth(), list.getRowHeight()).expanded (6).contains (e.getPosition()))
        setIncluded (rowsFor (row), ! isIncluded (row));
}

bool Library::isIncluded (int row) const
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return true;
    auto& engine = host.getEngine();
    return tab == Plugins ? ! engine.isRandomExcluded (plugins[rows[row]])
                          : ! engine.isRandomExcluded (files[rows[row]]);
}

juce::Array<int> Library::rowsFor (int clicked) const
{
    juce::Array<int> out;
    if (list.isRowSelected (clicked) && list.getNumSelectedRows() > 1)
        for (int i = 0; i < list.getNumSelectedRows(); ++i) out.add (list.getSelectedRow (i));
    else
        out.add (clicked);
    return out;
}

void Library::setIncluded (const juce::Array<int>& which, bool include)
{
    auto& engine = host.getEngine();
    if (tab == Plugins)
    {
        juce::Array<juce::PluginDescription> d;
        for (int r : which) if (juce::isPositiveAndBelow (r, rows.size())) d.add (plugins[rows[r]]);
        engine.setRandomExcluded (d, ! include);
    }
    else
    {
        juce::Array<juce::File> f;
        for (int r : which) if (juce::isPositiveAndBelow (r, rows.size())) f.add (files[rows[r]]);
        engine.setRandomExcluded (f, ! include);
    }
    applyFilter();   // refreshes the counts
}

void Library::showRowMenu (int row)
{
    const auto which = rowsFor (row);
    const bool in = isIncluded (row);
    const auto what = which.size() > 1 ? juce::String (which.size()) + " selected" : juce::String ("this");

    juce::PopupMenu m;
    m.addItem (1, in ? "Leave " + what + " out of the dice" : "Let the dice pick " + what);
    juce::String folderName, maker;
    if (tab == Plugins)
    {
        maker = plugins[rows[row]].manufacturerName;
        if (maker.isNotEmpty())
        {
            m.addItem (2, "Leave everything by " + maker + " out of the dice");
            m.addItem (3, "Let the dice pick everything by " + maker);
        }
    }
    else
    {
        folderName = files[rows[row]].getParentDirectory().getFileName();
        m.addItem (2, "Leave everything in \"" + folderName + "\" out of the dice");
        m.addItem (3, "Let the dice pick everything in \"" + folderName + "\"");
        m.addSeparator();
        m.addItem (4, "Show in folder");
    }
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, which, in, row] (int r)
    {
        if (! juce::isPositiveAndBelow (row, rows.size())) return;
        if (r == 1) setIncluded (which, ! in);
        else if (r == 2 || r == 3)
        {
            juce::Array<int> same;
            for (int i = 0; i < rows.size(); ++i)
            {
                const bool match = tab == Plugins ? plugins[rows[i]].manufacturerName == plugins[rows[row]].manufacturerName
                                                  : files[rows[i]].getParentDirectory() == files[rows[row]].getParentDirectory();
                if (match) same.add (i);
            }
            setIncluded (same, r == 3);
        }
        else if (r == 4) files[rows[row]].revealToUser();
    });
}

void Library::showDiceMenu()
{
    const bool filtered = search.getText().trim().isNotEmpty();
    const auto what = filtered ? juce::String ("matching \"") + search.getText().trim() + "\"" : juce::String ("in this list");
    juce::PopupMenu m;
    m.addSectionHeader ("Ticked items can be picked by the dice");
    m.addItem (1, "Tick all " + juce::String (rows.size()) + " " + what);
    m.addItem (2, "Untick all " + juce::String (rows.size()) + " " + what);
    m.addSeparator();
    m.addItem (3, "Tip: search first (e.g. \"RX\" or a folder name) to change just those", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&diceButton), [this] (int r)
    {
        if (r != 1 && r != 2) return;
        juce::Array<int> all;
        for (int i = 0; i < rows.size(); ++i) all.add (i);
        setIncluded (all, r == 1);
    });
}

void Library::activate (int row)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    if (tab == Plugins) { if (onActivatePlugin) onActivatePlugin (plugins[rows[row]].createIdentifierString()); }
    else if (onActivateFile) onActivateFile (files[rows[row]]);
}

juce::var Library::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.isEmpty()) return {};
    const int row = selected[0];
    if (! juce::isPositiveAndBelow (row, rows.size())) return {};
    if (tab == Plugins) return "plugin:" + plugins[rows[row]].createIdentifierString();
    return "file:" + files[rows[row]].getFullPathName();
}

juce::String Library::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return {};
    const auto tip = isIncluded (row) ? "\nTicked: the dice can pick this (click the tick to leave it out)"
                                      : "\nNot ticked: the dice never pick this";
    if (tab == Plugins) return plugins[rows[row]].descriptiveName + tip;
    return files[rows[row]].getFullPathName() + tip;
}

void Library::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
    g.setColour (muted);
    g.setFont (font (11.0f, true).withExtraKerningFactor (0.1f));
    g.drawText ("LIBRARY", 16, 10, 200, 18, juce::Justification::centredLeft);
}

void Library::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    r.removeFromTop (34);
    auto tabRow = r.removeFromTop (30);
    const int tw = (tabRow.getWidth() - 8) / 3;
    for (int i = 0; i < 3; ++i) { tabs[i].setBounds (tabRow.removeFromLeft (tw)); tabRow.removeFromLeft (4); }
    r.removeFromTop (8);
    auto searchRow = r.removeFromTop (34);
    folderButton.setBounds (searchRow.removeFromRight (78));
    searchRow.removeFromRight (6);
    search.setBounds (searchRow);
    r.removeFromTop (8);
    auto foot = r.removeFromBottom (32);
    diceButton.setBounds (foot.removeFromRight (juce::jmin (130, foot.getWidth() / 2)).reduced (0, 3));
    footer.setBounds (foot);
    list.setBounds (r.withTrimmedLeft (-12).withTrimmedRight (-12));
}

} // namespace rig
