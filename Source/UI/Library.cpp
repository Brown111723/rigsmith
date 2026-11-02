#include "Library.h"

namespace rig
{
using namespace theme;

Library::Library (GraphHost& h) : host (h)
{
    for (int i = 0; i < numTabs; ++i)
    {
        tabs[i].setClickingTogglesState (false);
        tabs[i].onClick = [this, i] { setTab ((Tab) i); };
        if (i == Tone3000) addChildComponent (tabs[i]);   // shown once the editor connects a client
        else addAndMakeVisible (tabs[i]);
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

    gearButton.setTooltip ("Show one kind: amp heads, amp + cab, pedals... (from TONE3000, the model's own details, or folder names)");
    gearButton.onClick = [this] { showGearMenu(); };
    addChildComponent (gearButton);

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
    for (int i = 0; i < numTabs; ++i)
        tabs[i].setToggleState (i == (int) t, juce::dontSendNotification);
    const bool t3k = tab == Tone3000;
    if (t3kPanel != nullptr) t3kPanel->setVisible (t3k);
    for (auto* c : std::initializer_list<juce::Component*> { &search, &list, &footer, &diceButton })
        c->setVisible (! t3k);
    if (t3k) { folderButton.setVisible (false); gearButton.setVisible (false); resized(); return; }
    folderButton.setVisible (tab != Effects);
    gearButton.setVisible (tab == Models || tab == Irs);
    gearFilter = -1;
    resized();
    folderButton.setButtonText (tab == Plugins ? "Scan..." : "Folders...");
    folderButton.setTooltip (tab == Plugins ? "Scan for VST plug-ins" : "Choose which folders are searched - remembered between sessions");
    refresh();
}

void Library::setTone3000 (t3k::Client& client)
{
    t3kPanel = std::make_unique<Tone3000Panel> (host.getEngine(), client);
    t3kPanel->onLibraryChanged = [this] { if (tab == Models || tab == Irs) refresh(); };
    addChildComponent (*t3kPanel);
    tabs[Tone3000].setVisible (true);
    resized();
}

void Library::refresh()
{
    if (tab == Tone3000) return;
    gears.clear();
    files.clear();
    plugins.clear();
    effects.clear();

    if (tab == Effects)
    {
        for (auto& [group, types] : builtInGroups())
            for (auto t : types) effects.add (t);
    }
    else if (tab == Plugins)
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
        // grouped by what they are (amp heads, amp + cab, pedals... / cabinets, spaces...), then by name
        gears.clear();
        for (auto& f : files) gears.add ((int) classifyFile (f));
        std::vector<int> order ((size_t) files.size());
        for (int i = 0; i < files.size(); ++i) order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [this] (int a, int b)
        {
            if (gears[a] != gears[b]) return gearRank ((Gear) gears[a]) < gearRank ((Gear) gears[b]);
            return files[a].getFileName().compareNatural (files[b].getFileName()) < 0;
        });
        juce::Array<juce::File> sortedFiles;
        juce::Array<int> sortedGears;
        for (int i : order) { sortedFiles.add (files[i]); sortedGears.add (gears[i]); }
        files.swapWith (sortedFiles);
        gears.swapWith (sortedGears);
    }
    updateGearButton();
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

    if (tab == Effects)
    {
        for (int i = 0; i < effects.size(); ++i)
            if (matches (builtInMenuName (effects[i]) + " " + builtInBlurb (effects[i]) + " " + categoryName (categoryForBuiltIn (effects[i]))))
                rows.add (i);
    }
    else if (tab == Plugins)
    {
        for (int i = 0; i < plugins.size(); ++i)
            if (matches (plugins[i].name + " " + plugins[i].manufacturerName + " " + plugins[i].category))
                rows.add (i);
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
            if ((gearFilter < 0 || gears[i] == gearFilter)
                && matches (files[i].getFileName() + " " + files[i].getParentDirectory().getFileName() + " " + gearName ((Gear) gears[i])))
                rows.add (i);
    }

    list.updateContent();
    list.repaint();

    juce::String f;
    if (tab == Effects)
        f = juce::String (rows.size()) + " built-in effects";
    else if (tab == Plugins)
        f = plugins.isEmpty() ? "No VST3s yet - click Scan" : juce::String (rows.size()) + " effects - drag onto the rig";
    else
    {
        const auto paths = host.getEngine().getPaths (pathsKey());
        f = juce::String (rows.size()) + " files in " + juce::String (paths.getNumPaths()) + (paths.getNumPaths() == 1 ? " folder" : " folders");
    }
    footer.setText (f, juce::dontSendNotification);

    int included = 0, tickable = 0;
    for (int i = 0; i < rows.size(); ++i)
        if (hasTick (i)) { ++tickable; included += isIncluded (i) ? 1 : 0; }
    diceButton.setButtonText (juce::String (included) + " / " + juce::String (tickable) + " in dice");
    diceButton.setColour (juce::TextButton::textColourOffId, included < tickable ? accent : muted);
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
    juce::Colour c = accent, secondColour;
    if (tab == Effects)
    {
        const auto t = effects[rows[row]];
        name = builtInShortName (t);
        sub = categoryName (categoryForBuiltIn (t)) + " - " + builtInBlurb (t);
        c = categoryColour (categoryForBuiltIn (t));
    }
    else if (tab == Plugins)
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
        const auto gear = (Gear) gears[rows[row]];
        name = f.getFileNameWithoutExtension();
        sub = gearName (gear) + " - " + f.getParentDirectory().getFileName();
        if (auto tone = ToneInfo::forFile (f); tone.valid)
            sub = gearName (gear) + " - " + tone.title + (tone.creator.isNotEmpty() ? " by " + tone.creator : juce::String());
        c = gearColour (gear);
        secondColour = gearIncludesCab (gear) ? gearColour (Gear::Cab) : juce::Colour();
    }

    const bool ticked = hasTick (row);
    const bool in = ! ticked || isIncluded (row);
    if (! in) sub = "Not in dice - " + sub;

    g.setColour (in ? c : c.withAlpha (0.35f));
    g.fillRoundedRectangle (12.0f, (float) h * 0.5f - 9.0f, 4.0f, 18.0f, 2.0f);
    if (! secondColour.isTransparent())   // amp + cab: two-tone stripe
    {
        g.setColour (in ? secondColour : secondColour.withAlpha (0.35f));
        g.fillRoundedRectangle (12.0f, (float) h * 0.5f + 1.0f, 4.0f, 8.0f, 2.0f);
    }
    g.setColour (in ? text : text.withAlpha (0.55f));
    g.setFont (font (13.0f, true));
    g.drawText (name, 24, h / 2 - 17, w - 66, 18, juce::Justification::centredLeft, true);
    g.setColour (muted.withAlpha (in ? 1.0f : 0.8f));
    g.setFont (font (11.0f));
    g.drawText (sub, 24, h / 2 + 2, w - 66, 15, juce::Justification::centredLeft, true);

    // the randomise tick: a little die face when included, an empty box when not
    const auto t = tickArea (w, h).toFloat();
    if (! ticked) {}
    else if (in)
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
    if (hasTick (row) && tickArea (list.getVisibleRowWidth(), list.getRowHeight()).expanded (6).contains (e.getPosition()))
        setIncluded (rowsFor (row), ! isIncluded (row));
}

bool Library::hasTick (int row) const
{
    return ! (tab == Effects && juce::isPositiveAndBelow (row, rows.size()) && effects[rows[row]] == NodeType::Tuner);
}

bool Library::isIncluded (int row) const
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return true;
    auto& engine = host.getEngine();
    if (tab == Effects) return ! engine.isRandomExcluded (effects[rows[row]]);
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
    if (tab == Effects)
    {
        juce::Array<NodeType> t;
        for (int r : which) if (juce::isPositiveAndBelow (r, rows.size())) t.add (effects[rows[r]]);
        engine.setRandomExcluded (t, ! include);
    }
    else if (tab == Plugins)
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
    if (hasTick (row))
        m.addItem (1, in ? "Leave " + what + " out of the dice" : "Let the dice pick " + what);
    juce::String folderName, maker;
    if (tab == Effects)
    {
        const auto cat = categoryName (categoryForBuiltIn (effects[rows[row]]));
        m.addItem (2, "Leave every " + cat + " effect out of the dice");
        m.addItem (3, "Let the dice pick every " + cat + " effect");
        m.addSeparator();
        m.addItem (5, "Add to the rig");
    }
    else if (tab == Plugins)
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
                const bool match = tab == Effects ? categoryForBuiltIn (effects[rows[i]]) == categoryForBuiltIn (effects[rows[row]])
                                 : tab == Plugins ? plugins[rows[i]].manufacturerName == plugins[rows[row]].manufacturerName
                                                  : files[rows[i]].getParentDirectory() == files[rows[row]].getParentDirectory();
                if (match) same.add (i);
            }
            setIncluded (same, r == 3);
        }
        else if (r == 4) files[rows[row]].revealToUser();
        else if (r == 5) activate (row);
    });
}

int Library::gearRank (Gear g)
{
    switch (g)
    {
        case Gear::AmpHead: return 0;  case Gear::AmpCab: return 1;   case Gear::Pedal: return 2;
        case Gear::Outboard: return 3; case Gear::Cab: return 4;      case Gear::Space: return 5;
        case Gear::Experimental: return 6; case Gear::Unknown: break;
    }
    return 7;
}

void Library::updateGearButton()
{
    gearButton.setButtonText (gearFilter < 0 ? juce::String ("All types") : gearName ((Gear) gearFilter));
    gearButton.setColour (juce::TextButton::textColourOffId, gearFilter < 0 ? text : gearColour ((Gear) gearFilter));
}

void Library::showGearMenu()
{
    std::map<int, int> counts;
    for (int g : gears) ++counts[g];
    juce::PopupMenu m;
    m.addItem (1000, "All types  (" + juce::String (gears.size()) + ")", true, gearFilter < 0);
    m.addSeparator();
    std::vector<int> kinds;
    for (auto& [g, n] : counts) kinds.push_back (g);
    std::sort (kinds.begin(), kinds.end(), [] (int a, int b) { return gearRank ((Gear) a) < gearRank ((Gear) b); });
    for (int g : kinds)
    {
        juce::PopupMenu::Item item (gearName ((Gear) g) + "  (" + juce::String (counts[g]) + ")");
        item.itemID = 1 + g;
        item.colour = gearColour ((Gear) g);
        item.isTicked = gearFilter == g;
        m.addItem (item);
    }
    m.addSeparator();
    m.addItem (-1, tab == Models ? "Types come from TONE3000, the model's own details, or folder names"
                                 : "Types come from TONE3000 or folder names (Cabs, Rooms, Halls...)", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearButton), [this] (int r)
    {
        if (r == 0) return;
        gearFilter = r == 1000 ? -1 : r - 1;
        updateGearButton();
        applyFilter();
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
    if (tab == Effects) { if (onActivateBuiltIn) onActivateBuiltIn (effects[rows[row]]); }
    else if (tab == Plugins) { if (onActivatePlugin) onActivatePlugin (plugins[rows[row]].createIdentifierString()); }
    else if (onActivateFile) onActivateFile (files[rows[row]]);
}

juce::var Library::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.isEmpty()) return {};
    const int row = selected[0];
    if (! juce::isPositiveAndBelow (row, rows.size())) return {};
    if (tab == Effects) return "builtin:" + nodeTypeToString (effects[rows[row]]);
    if (tab == Plugins) return "plugin:" + plugins[rows[row]].createIdentifierString();
    return "file:" + files[rows[row]].getFullPathName();
}

juce::String Library::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return {};
    if (tab == Effects)
        return builtInMenuName (effects[rows[row]]) + "\nDrag onto the rig, or double-click to add it after the selected block"
             + (! hasTick (row) ? juce::String() : isIncluded (row) ? "\nTicked: the dice can pick this" : "\nNot ticked: the dice never pick this");
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
    const int tw = (tabRow.getWidth() - 4 * 3) / 4;
    for (int i = 0; i < 4; ++i) { tabs[i].setBounds (tabRow.removeFromLeft (tw)); tabRow.removeFromLeft (4); }
    if (tabs[Tone3000].isVisible())
    {
        // TONE3000 gets its own full-width tab under the others
        r.removeFromTop (4);
        tabs[Tone3000].setBounds (r.removeFromTop (28));
    }
    r.removeFromTop (8);
    if (t3kPanel != nullptr && tab == Tone3000)
    {
        t3kPanel->setBounds (r.withTrimmedBottom (4));
        return;
    }
    auto searchRow = r.removeFromTop (34);
    if (folderButton.isVisible())
    {
        folderButton.setBounds (searchRow.removeFromRight (78));
        searchRow.removeFromRight (6);
    }
    if (gearButton.isVisible())
    {
        gearButton.setBounds (searchRow.removeFromRight (86));
        searchRow.removeFromRight (6);
    }
    search.setBounds (searchRow);
    r.removeFromTop (8);
    auto foot = r.removeFromBottom (32);
    diceButton.setBounds (foot.removeFromRight (juce::jmin (130, foot.getWidth() / 2)).reduced (0, 3));
    footer.setBounds (foot);
    list.setBounds (r.withTrimmedLeft (-12).withTrimmedRight (-12));
}

} // namespace rig
