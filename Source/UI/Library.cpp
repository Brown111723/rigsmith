#include "Library.h"
#include "Icons.h"

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

    tabs[Models].setTooltip ("Your NAM captures (amps, pedals...), grouped by gear");
    tabs[Irs].setTooltip ("Your impulse responses: cabinets, rooms and halls");
    tabs[Plugins].setTooltip ("Your scanned VST3 effects");
    tabs[Effects].setTooltip ("Rigsmith's own effects");
    tabs[Tone3000].setTooltip ("Browse and download captures and IRs from TONE3000");
    search.setTooltip ("Search names, makers, tone types, creators and folders");
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

    gearButton.setTooltip ("Show one kind: amp heads, amp + cab, pedals... (from TONE3000, the capture's own details, or folder names)");
    gearButton.onClick = [this] { showGearMenu(); };
    addChildComponent (gearButton);
    toneButton.setTooltip ("Show one tone: clean, crunch, hi-gain, metal... (the tone type each capture was given when it was made)");
    toneButton.onClick = [this] { showToneMenu(); };
    addChildComponent (toneButton);

    diceButton.setTooltip ("Which of these the dice can pick: let them pick, or leave out, everything shown (use Search to narrow it down first)");
    diceButton.onClick = [this] { showDiceMenu(); };
    addAndMakeVisible (diceButton);

    footer.setFont (font (11.0f));
    footer.setColour (juce::Label::textColourId, muted);
    footer.setMinimumHorizontalScale (0.8f);
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
    if (t3k) { folderButton.setVisible (false); gearButton.setVisible (false); toneButton.setVisible (false); resized(); return; }
    folderButton.setVisible (tab != Effects);
    gearButton.setVisible (isFileTab());
    toneButton.setVisible (tab == Models);
    gearFilter = -1;
    toneFilter = {};
    resized();
    folderButton.setButtonText (tab == Plugins ? "Scan..." : "Folders...");
    folderButton.setTooltip (tab == Plugins ? "Scan for VST plug-ins" : "Choose which folders are searched - remembered between sessions");
    refresh();
}

void Library::setTone3000 (t3k::Client& client)
{
    t3kPanel = std::make_unique<Tone3000Panel> (host.getEngine(), client);
    t3kPanel->onLibraryChanged = [this] { if (isFileTab()) refresh(); };
    addChildComponent (*t3kPanel);
    tabs[Tone3000].setVisible (true);
    resized();
}

void Library::setExpanded (const juce::String& title, bool open)
{
    for (auto& g : groups)
        if (g->title == title) { if (open) expanded.insert (g->key); else expanded.erase (g->key); }
    applyFilter();
}

void Library::refresh()
{
    if (tab == Tone3000) return;
    groups.clear();
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
        // every capture, grouped by gear and sorted by type (amp heads, amp + cab, pedals... / cabinets, spaces...)
        auto& lib = host.getEngine().captures;
        lib.rescan();
        groups = lib.get (tab == Irs).groups;
    }
    updateFilterButtons();
    applyFilter();
}

void Library::applyFilter()
{
    rows.clear();
    entries.clear();
    const auto words = juce::StringArray::fromTokens (search.getText(), " ", {});
    const bool searching = search.getText().trim().isNotEmpty();
    auto matches = [&words] (const juce::String& s)
    {
        for (auto& w : words)
            if (w.isNotEmpty() && ! s.containsIgnoreCase (w)) return false;
        return true;
    };

    int shownFiles = 0, shownGroups = 0;
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
        // which gear (and which of its captures) the search and filters leave
        struct Shown { int group; std::vector<int> captures; bool open; };
        std::vector<Shown> shown;
        for (int gi = 0; gi < (int) groups.size(); ++gi)
        {
            const auto& g = *groups[(size_t) gi];
            if (gearFilter >= 0 && (int) g.gear != gearFilter) continue;
            std::vector<int> caps;
            for (int c = 0; c < g.size(); ++c)
                if (toneFilter.isEmpty() || g.fileToneTypes[c].equalsIgnoreCase (toneFilter))
                    caps.push_back (c);
            if (caps.empty()) continue;

            const auto groupText = g.title + " " + gearName (g.gear) + " " + g.toneText() + " " + g.creators.joinIntoString (" ")
                                 + " " + g.files[0].getParentDirectory().getFileName();
            bool open = expanded.count (g.key) > 0;
            if (! matches (groupText))
            {
                // maybe some captures match ("G3", a folder, a file name): show just those, opened up
                std::vector<int> hit;
                for (int c : caps)
                    if (matches (groupText + " " + g.labels[c] + " " + toneTypeName (g.fileToneTypes[c]) + " "
                                 + g.files[c].getFileName() + " " + g.files[c].getParentDirectory().getFileName()))
                        hit.push_back (c);
                if (hit.empty()) continue;
                caps = hit;
                open = g.size() > 1;
            }
            shown.push_back ({ gi, caps, open });
        }

        for (size_t i = 0; i < shown.size();)
        {
            // a heading for each type, with how much is under it
            const auto gear = groups[(size_t) shown[i].group]->gear;
            size_t j = i;
            Entry header;
            header.kind = Entry::Header;
            header.gear = gear;
            while (j < shown.size() && groups[(size_t) shown[j].group]->gear == gear)
            {
                ++header.groupsInSection;
                header.filesInSection += (int) shown[j].captures.size();
                ++j;
            }
            entries.push_back (header);
            shownGroups += header.groupsInSection;
            shownFiles += header.filesInSection;
            const bool folded = collapsedSections.count ((int) gear) > 0 && ! searching;
            for (; i < j; ++i)
            {
                if (folded) continue;
                const auto& s = shown[i];
                const auto& g = *groups[(size_t) s.group];
                Entry e;
                e.kind = Entry::Group;
                e.gear = gear;
                e.group = s.group;
                // the search found just one of its captures: list that capture on its own
                if (g.size() > 1 && s.captures.size() == 1 && searching) { e.kind = Entry::Capture; e.capture = s.captures[0]; }
                entries.push_back (e);
                if (e.kind == Entry::Group && s.open && g.size() > 1)
                    for (int c : s.captures)
                    {
                        Entry ce;
                        ce.kind = Entry::Capture;
                        ce.gear = gear;
                        ce.group = s.group;
                        ce.capture = c;
                        entries.push_back (ce);
                    }
            }
        }
        for (int i = 0; i < (int) entries.size(); ++i) rows.add (i);
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
        const auto where = " in " + juce::String (paths.getNumPaths()) + (paths.getNumPaths() == 1 ? " folder" : " folders");
        if (shownGroups == shownFiles)
            f = juce::String (shownFiles) + (tab == Models ? " captures" : " IRs") + where;
        else
            f = juce::String (shownGroups) + (tab == Models ? " gear, " : " packs, ") + juce::String (shownFiles)
              + (tab == Models ? " captures" : " IRs") + where;
    }
    footer.setText (f, juce::dontSendNotification);

    // dice count: files (not rows) for models and IRs
    int included = 0, total = 0;
    if (isFileTab())
    {
        std::set<juce::String> counted;
        for (int r = 0; r < rows.size(); ++r)
            if (entries[(size_t) r].kind == Entry::Header)
                for (auto& file : filesFor (r))
                    if (counted.insert (file.getFullPathName()).second)
                    {
                        ++total;
                        included += host.getEngine().isRandomExcluded (file) ? 0 : 1;
                    }
    }
    else
        for (int i = 0; i < rows.size(); ++i)
            if (hasDice (i)) { ++total; included += isIncluded (i) ? 1 : 0; }
    diceButton.setButtonText (juce::String (included) + " / " + juce::String (total) + " in dice");
    diceButton.setColour (juce::TextButton::textColourOffId, included < total ? accent : muted);
}

int Library::getNumRows() { return rows.size(); }

juce::Array<juce::File> Library::filesFor (int row) const
{
    juce::Array<juce::File> out;
    if (! isFileTab() || ! juce::isPositiveAndBelow (row, rows.size())) return out;
    const auto& e = entries[(size_t) rows[row]];
    if (e.kind == Entry::Header)
    {
        // everything under this heading that the filters show (folded or not)
        for (auto& g : groups)
            if (g->gear == e.gear && (gearFilter < 0 || (int) g->gear == gearFilter))
                for (int c = 0; c < g->size(); ++c)
                    if (toneFilter.isEmpty() || g->fileToneTypes[c].equalsIgnoreCase (toneFilter))
                        out.add (g->files[c]);
        // with a search on, only what's listed under it
        if (search.getText().trim().isNotEmpty())
        {
            juce::Array<juce::File> listed;
            std::set<juce::String> seen;
            for (int r = row + 1; r < rows.size() && entries[(size_t) rows[r]].kind != Entry::Header; ++r)
                for (auto& f : filesFor (r))
                    if (seen.insert (f.getFullPathName()).second) listed.add (f);
            return listed;
        }
        return out;
    }
    const auto& g = *groups[(size_t) e.group];
    if (e.kind == Entry::Capture) { out.add (g.files[e.capture]); return out; }
    for (int c = 0; c < g.size(); ++c)
        if (toneFilter.isEmpty() || g.fileToneTypes[c].equalsIgnoreCase (toneFilter))
            out.add (g.files[c]);
    return out;
}

juce::File Library::fileFor (int row) const
{
    if (! isFileTab() || ! juce::isPositiveAndBelow (row, rows.size())) return {};
    const auto& e = entries[(size_t) rows[row]];
    if (e.kind == Entry::Header) return {};
    const auto files = filesFor (row);
    // prefer one the dice may pick (usually the favourite captures)
    for (auto& f : files)
        if (! host.getEngine().isRandomExcluded (f)) return f;
    return files[0];
}

void Library::drawDice (juce::Graphics& g, juce::Rectangle<int> area, Dice state, int in, int of)
{
    // the die is lit when the dice may pick this, dark when they never will. Nothing else changes:
    // a row with a dark die still loads and plays like any other.
    const auto r = area.toFloat();
    if (state == Dice::All)
        drawIcon (g, Icon::Dice, r, accent);
    else if (state == Dice::None)
        drawIcon (g, Icon::Dice, r, dim.withAlpha (0.45f));
    else
    {
        drawIcon (g, Icon::Dice, r, accent.withAlpha (0.55f));
        g.setColour (muted);
        g.setFont (mono (9.0f));
        g.drawText (juce::String (in) + "/" + juce::String (of), area.translated (-30, 0).withWidth (28), juce::Justification::centredRight);
    }
}

void Library::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    const Entry* entry = isFileTab() ? &entries[(size_t) rows[row]] : nullptr;

    if (entry != nullptr && entry->kind == Entry::Header)
    {
        // TYPE heading: "AMP HEADS   4 gear - 37 captures", click to fold
        const bool folded = collapsedSections.count ((int) entry->gear) > 0 && search.getText().trim().isEmpty();
        const auto c = gearColour (entry->gear);
        const int y = h - 24;
        juce::Path tri;
        const float cx = 20.0f, cy = (float) y + 10.0f;
        if (folded) tri.addTriangle (cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy);
        else        tri.addTriangle (cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 4);
        g.setColour (c);
        g.fillPath (tri);
        auto title = (gearName (entry->gear) + (entry->gear == Gear::Unknown ? "" : "s")).toUpperCase();
        if (entry->gear == Gear::AmpCab) title = "AMP + CAB";
        if (entry->gear == Gear::Unknown) title = "UNCATEGORISED";
        g.setFont (font (11.0f, true).withExtraKerningFactor (0.08f));
        g.drawText (title, 32, y, w - 40, 20, juce::Justification::centredLeft);
        const int tw = juce::GlyphArrangement::getStringWidthInt (font (11.0f, true).withExtraKerningFactor (0.08f), title);
        g.setColour (muted);
        g.setFont (font (11.0f));
        auto count = entry->groupsInSection == entry->filesInSection
                   ? juce::String (entry->filesInSection)
                   : juce::String (entry->groupsInSection) + "  -  " + juce::String (entry->filesInSection) + (tab == Models ? " captures" : " IRs");
        g.drawText (count, 32 + tw + 10, y, w - tw - 60, 20, juce::Justification::centredLeft);
        g.setColour (c.withAlpha (0.35f));
        g.fillRect (12, h - 2, w - 24, 1);
        return;
    }

    if (selected)
    {
        g.setColour (cardHi);
        g.fillRoundedRectangle (4.0f, 2.0f, (float) w - 8.0f, (float) h - 4.0f, 6.0f);
    }

    juce::String name, sub;
    juce::Colour c = accent, secondColour;
    int textX = 24;
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
        const auto& grp = *groups[(size_t) entry->group];
        c = gearColour (grp.gear);
        secondColour = gearIncludesCab (grp.gear) ? gearColour (Gear::Cab) : juce::Colour();
        const bool child = entry->kind == Entry::Capture && row > 0 && entries[(size_t) rows[row - 1]].group == entry->group;
        if (entry->kind == Entry::Capture)
        {
            const int i = entry->capture;
            const auto tone = toneTypeName (grp.fileToneTypes[i]);
            if (child)
            {
                // one capture, under its gear
                textX = 46;
                name = grp.labels[i];
                sub = (tone.isNotEmpty() ? tone + " - " : juce::String()) + grp.files[i].getFileName();
            }
            else
            {
                // the one capture of this gear that matched the search
                name = grp.title;
                sub = grp.labels[i] + (tone.isNotEmpty() ? " - " + tone : juce::String()) + "  (1 of " + juce::String (grp.size()) + ")";
            }
        }
        else
        {
            name = grp.title;
            const bool many = grp.size() > 1;
            if (many)
            {
                textX = 38;
                // a disclosure arrow: open up to see every capture
                const bool open = row + 1 < rows.size() && entries[(size_t) rows[row + 1]].kind == Entry::Capture
                               && entries[(size_t) rows[row + 1]].group == entry->group;
                const auto a = chevronArea (h).toFloat();
                juce::Path tri;
                const auto ctr = a.getCentre().translated (6.0f, 0.0f);
                if (open) tri.addTriangle (ctr.x - 4, ctr.y - 2.5f, ctr.x + 4, ctr.y - 2.5f, ctr.x, ctr.y + 3.5f);
                else      tri.addTriangle (ctr.x - 2.5f, ctr.y - 4, ctr.x - 2.5f, ctr.y + 4, ctr.x + 3.5f, ctr.y);
                g.setColour (muted);
                g.fillPath (tri);
                sub = juce::String (filesFor (row).size()) + (tab == Models ? " captures" : " IRs");
            }
            else
            {
                sub = grp.labels[0] != grp.title ? grp.labels[0] : gearName (grp.gear);
                if (tab == Irs && ! many) sub = gearName (grp.gear) + " - " + grp.files[0].getParentDirectory().getFileName();
            }
            if (const auto tones = grp.toneText(); tones.isNotEmpty()) sub << " - " << tones;
            if (const auto by = grp.creatorText(); by.isNotEmpty()) sub << " - by " << by;
            else if (tab == Models && ! many) sub << " - " << grp.files[0].getParentDirectory().getFileName();
        }
    }

    // colour stripe (amp + cab: two-tone)
    const bool childRow = textX == 46;
    const float sx = childRow ? 36.0f : 12.0f, sw = childRow ? 3.0f : 4.0f;
    g.setColour (childRow ? c.withAlpha (0.7f) : c);
    g.fillRoundedRectangle (sx, (float) h * 0.5f - (childRow ? 7.0f : 9.0f), sw, childRow ? 14.0f : 18.0f, 2.0f);
    if (! secondColour.isTransparent() && ! childRow)
    {
        g.setColour (secondColour);
        g.fillRoundedRectangle (sx, (float) h * 0.5f + 1.0f, sw, 8.0f, 2.0f);
    }

    // the name is never dimmed: the die only says whether the dice may pick it
    const int right = diceState (row) == Dice::Some ? 70 : 42;   // room for "3/12" by the die
    g.setColour (text);
    g.setFont (font (childRow ? 12.5f : 13.0f, ! childRow));
    g.drawText (name, textX, h / 2 - 17, w - textX - right, 18, juce::Justification::centredLeft, true);
    g.setColour (muted);
    g.setFont (font (11.0f));
    g.drawText (sub, textX, h / 2 + 2, w - textX - right, 15, juce::Justification::centredLeft, true);

    if (hasDice (row))
    {
        int in = 0, of = 0;
        if (isFileTab())
            for (auto& f : filesFor (row)) { ++of; in += host.getEngine().isRandomExcluded (f) ? 0 : 1; }
        drawDice (g, diceArea (w, h), diceState (row), in, of);
    }
}

void Library::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (diceArea (list.getVisibleRowWidth(), list.getRowHeight()).expanded (6).contains (e.getPosition())) return;
    if (isFileTab() && juce::isPositiveAndBelow (row, rows.size()) && entries[(size_t) rows[row]].kind == Entry::Header) return;
    activate (row);
}

void Library::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    if (e.mods.isPopupMenu()) { showRowMenu (row); return; }
    if (hasDice (row) && diceArea (list.getVisibleRowWidth(), list.getRowHeight()).expanded (6).contains (e.getPosition()))
    {
        setIncluded (rowsFor (row), ! isIncluded (row));
        return;
    }
    if (! isFileTab()) return;
    const auto& en = entries[(size_t) rows[row]];
    if (en.kind == Entry::Header)
    {
        // fold / unfold a whole type
        const int key = (int) en.gear;
        if (collapsedSections.count (key)) collapsedSections.erase (key); else collapsedSections.insert (key);
        list.deselectAllRows();
        applyFilter();
        return;
    }
    if (en.kind == Entry::Group && groups[(size_t) en.group]->size() > 1
        && chevronArea (list.getRowHeight()).expanded (6, 12).contains (e.getPosition()))
    {
        const auto& key = groups[(size_t) en.group]->key;
        if (expanded.count (key)) expanded.erase (key); else expanded.insert (key);
        applyFilter();
    }
}

bool Library::hasDice (int row) const
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return false;
    if (isFileTab()) return entries[(size_t) rows[row]].kind != Entry::Header;
    return ! (tab == Effects && effects[rows[row]] == NodeType::Tuner);
}

Library::Dice Library::diceState (int row) const
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return Dice::All;
    auto& engine = host.getEngine();
    if (tab == Effects) return engine.isRandomExcluded (effects[rows[row]]) ? Dice::None : Dice::All;
    if (tab == Plugins) return engine.isRandomExcluded (plugins[rows[row]]) ? Dice::None : Dice::All;
    int in = 0, of = 0;
    for (auto& f : filesFor (row)) { ++of; in += engine.isRandomExcluded (f) ? 0 : 1; }
    return in == of ? Dice::All : in == 0 ? Dice::None : Dice::Some;
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
        for (int r : which)
            for (auto& file : filesFor (r)) f.addIfNotAlreadyThere (file);
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
    const Entry* en = isFileTab() ? &entries[(size_t) rows[row]] : nullptr;
    if (en != nullptr && en->kind == Entry::Header)
    {
        const auto kind = gearName (en->gear).toLowerCase();
        m.addItem (11, "Let the dice pick every " + kind);
        m.addItem (12, "Leave every " + kind + " out of the dice");
        m.addSeparator();
        m.addItem (13, collapsedSections.count ((int) en->gear) ? "Show" : "Fold away");
    }
    else
    {
        if (hasDice (row))
        {
            const bool many = en != nullptr && en->kind == Entry::Group && groups[(size_t) en->group]->size() > 1;
            const auto thing = many && which.size() == 1 ? juce::String ("these ") + juce::String (filesFor (row).size()) + " captures" : what;
            m.addItem (1, in ? "Leave " + thing + " out of the dice" : "Let the dice pick " + thing);
        }
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
            const auto maker = plugins[rows[row]].manufacturerName;
            if (maker.isNotEmpty())
            {
                m.addItem (2, "Leave everything by " + maker + " out of the dice");
                m.addItem (3, "Let the dice pick everything by " + maker);
            }
        }
        else
        {
            const auto& g = *groups[(size_t) en->group];
            if (en->kind == Entry::Group && g.size() > 1)
                m.addItem (6, expanded.count (g.key) ? "Hide the captures" : "Show all " + juce::String (g.size()) + " captures");
            const auto folderName = fileFor (row).getParentDirectory().getFileName();
            m.addItem (2, "Leave everything in \"" + folderName + "\" out of the dice");
            m.addItem (3, "Let the dice pick everything in \"" + folderName + "\"");
            m.addSeparator();
            m.addItem (5, "Add to the rig");
            m.addItem (4, "Show in folder");
        }
    }
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, which, in, row] (int r)
    {
        if (! juce::isPositiveAndBelow (row, rows.size())) return;
        if (r == 1) setIncluded (which, ! in);
        else if (r == 2 || r == 3)
        {
            if (isFileTab())
            {
                // everything in that folder, whatever the filters show
                const auto folder = fileFor (row).getParentDirectory();
                juce::Array<juce::File> same;
                for (auto& g : groups)
                    for (auto& f : g->files)
                        if (f.getParentDirectory() == folder) same.add (f);
                host.getEngine().setRandomExcluded (same, r == 2);
                applyFilter();
                return;
            }
            juce::Array<int> same;
            for (int i = 0; i < rows.size(); ++i)
            {
                const bool match = tab == Effects ? categoryForBuiltIn (effects[rows[i]]) == categoryForBuiltIn (effects[rows[row]])
                                                  : plugins[rows[i]].manufacturerName == plugins[rows[row]].manufacturerName;
                if (match) same.add (i);
            }
            setIncluded (same, r == 3);
        }
        else if (r == 4) fileFor (row).revealToUser();
        else if (r == 5) activate (row);
        else if (r == 6)
        {
            const auto& key = groups[(size_t) entries[(size_t) rows[row]].group]->key;
            if (expanded.count (key)) expanded.erase (key); else expanded.insert (key);
            applyFilter();
        }
        else if (r == 11 || r == 12)
        {
            host.getEngine().setRandomExcluded (filesFor (row), r == 12);
            applyFilter();
        }
        else if (r == 13)
        {
            const int key = (int) entries[(size_t) rows[row]].gear;
            if (collapsedSections.count (key)) collapsedSections.erase (key); else collapsedSections.insert (key);
            applyFilter();
        }
    });
}

void Library::updateFilterButtons()
{
    gearButton.setButtonText (gearFilter < 0 ? juce::String ("All types") : gearName ((Gear) gearFilter));
    gearButton.setColour (juce::TextButton::textColourOffId, gearFilter < 0 ? text : gearColour ((Gear) gearFilter));
    toneButton.setButtonText (toneFilter.isEmpty() ? juce::String ("Any tone") : toneTypeName (toneFilter));
    toneButton.setColour (juce::TextButton::textColourOffId, toneFilter.isEmpty() ? text : accent);
}

void Library::showGearMenu()
{
    std::map<int, std::pair<int, int>> counts;   // gear -> (gear count, files)
    int all = 0;
    for (auto& g : groups) { auto& c = counts[(int) g->gear]; ++c.first; c.second += g->size(); ++all; }
    juce::PopupMenu m;
    m.addItem (1000, "All types  (" + juce::String (all) + ")", true, gearFilter < 0);
    m.addSeparator();
    std::vector<int> kinds;
    for (auto& [g, n] : counts) kinds.push_back (g);
    std::sort (kinds.begin(), kinds.end(), [] (int a, int b) { return gearRank ((Gear) a) < gearRank ((Gear) b); });
    for (int g : kinds)
    {
        const auto [n, files] = counts[g];
        juce::PopupMenu::Item item (gearName ((Gear) g) + "  (" + juce::String (n)
                                    + (files != n ? ", " + juce::String (files) + (tab == Models ? " captures" : " IRs") : juce::String()) + ")");
        item.itemID = 1 + g;
        item.colour = gearColour ((Gear) g);
        item.isTicked = gearFilter == g;
        m.addItem (item);
    }
    m.addSeparator();
    m.addItem (-1, tab == Models ? "Types come from TONE3000, the capture's own details, or folder names"
                                 : "Types come from TONE3000 or folder names (Cabs, Rooms, Halls...)", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearButton), [this] (int r)
    {
        if (r == 0) return;
        gearFilter = r == 1000 ? -1 : r - 1;
        updateFilterButtons();
        applyFilter();
    });
}

void Library::showToneMenu()
{
    // every tone type in the (type-filtered) library, most used first
    std::map<juce::String, int> counts;
    std::map<juce::String, juce::String> spelling;
    for (auto& g : groups)
    {
        if (gearFilter >= 0 && (int) g->gear != gearFilter) continue;
        for (auto& t : g->toneTypes) { ++counts[t.toLowerCase()]; spelling[t.toLowerCase()] = t; }
    }
    std::vector<std::pair<juce::String, int>> tones (counts.begin(), counts.end());
    std::sort (tones.begin(), tones.end(), [] (const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });

    juce::PopupMenu m;
    m.addItem (1, "Any tone", true, toneFilter.isEmpty());
    m.addSeparator();
    for (size_t i = 0; i < tones.size(); ++i)
        m.addItem (100 + (int) i, toneTypeName (spelling[tones[i].first]) + "  (" + juce::String (tones[i].second) + ")",
                   true, toneFilter.equalsIgnoreCase (tones[i].first));
    if (tones.empty())
        m.addItem (-1, "None of these captures say what tone they are", false);
    m.addSeparator();
    m.addItem (-2, "The tone type is set when a capture is made (NAM trainer, TONE3000)", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toneButton), [this, tones, spelling] (int r)
    {
        if (r == 1) toneFilter = {};
        else if (r >= 100 && r - 100 < (int) tones.size()) toneFilter = spelling.at (tones[(size_t) (r - 100)].first);
        else return;
        updateFilterButtons();
        applyFilter();
    });
}

void Library::showDiceMenu()
{
    const bool filtered = search.getText().trim().isNotEmpty() || gearFilter >= 0 || toneFilter.isNotEmpty();
    const auto what = filtered ? juce::String ("shown") : juce::String ("in this list");
    int count = 0;
    if (isFileTab())
    {
        for (int r = 0; r < rows.size(); ++r)
            if (entries[(size_t) rows[r]].kind == Entry::Header) count += filesFor (r).size();
    }
    else count = rows.size();
    juce::PopupMenu m;
    m.addSectionHeader ("The dice only pick things whose die is lit");
    m.addItem (1, "Let the dice pick all " + juce::String (count) + " " + what);
    m.addItem (2, "Leave all " + juce::String (count) + " " + what + " out of the dice");
    m.addSeparator();
    m.addItem (3, "Tip: search first (e.g. \"RX\" or a folder name) to change just those", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&diceButton), [this] (int r)
    {
        if (r != 1 && r != 2) return;
        juce::Array<int> all;
        for (int i = 0; i < rows.size(); ++i)
            if (! isFileTab() || entries[(size_t) rows[i]].kind == Entry::Header) all.add (i);
        setIncluded (all, r == 1);
    });
}

void Library::activate (int row)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return;
    if (tab == Effects) { if (onActivateBuiltIn) onActivateBuiltIn (effects[rows[row]]); }
    else if (tab == Plugins) { if (onActivatePlugin) onActivatePlugin (plugins[rows[row]].createIdentifierString()); }
    else if (const auto f = fileFor (row); f != juce::File() && onActivateFile) onActivateFile (f);
}

juce::var Library::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.isEmpty()) return {};
    const int row = selected[0];
    if (! juce::isPositiveAndBelow (row, rows.size())) return {};
    if (tab == Effects) return "builtin:" + nodeTypeToString (effects[rows[row]]);
    if (tab == Plugins) return "plugin:" + plugins[rows[row]].createIdentifierString();
    const auto f = fileFor (row);
    return f == juce::File() ? juce::var() : juce::var ("file:" + f.getFullPathName());
}

juce::String Library::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, rows.size())) return {};
    const auto state = diceState (row);
    const juce::String diceTip = ! hasDice (row) ? juce::String()
                               : state == Dice::All  ? "\nDie lit: the dice can pick this (click the die to leave it out)"
                               : state == Dice::None ? "\nDie dark: the dice never pick this (it still loads and plays as normal)"
                                                     : "\nThe dice can pick some of these captures";
    if (tab == Effects)
        return builtInMenuName (effects[rows[row]]) + "\nDrag onto the rig, or double-click to add it after the selected block" + diceTip;
    if (tab == Plugins) return plugins[rows[row]].descriptiveName + diceTip;

    const auto& e = entries[(size_t) rows[row]];
    if (e.kind == Entry::Header)
        return "Click to fold or show every " + gearName (e.gear).toLowerCase() + ". Right-click to let the dice pick them, or leave them out";
    const auto& g = *groups[(size_t) e.group];
    if (e.kind == Entry::Capture)
        return g.title + "\n" + g.labels[e.capture] + "\n" + g.files[e.capture].getFullPathName() + diceTip;
    if (g.size() == 1) return g.files[0].getFullPathName() + diceTip;
    return g.title + " - " + juce::String (g.size()) + " captures\nDrag or double-click to load it; then the Capture knob switches between them.\n"
         + "Click the arrow to see each capture" + diceTip;
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
    search.setBounds (searchRow);
    r.removeFromTop (6);
    if (gearButton.isVisible())
    {
        // filters: what kind of gear, and what tone
        auto filterRow = r.removeFromTop (28);
        if (toneButton.isVisible())
        {
            toneButton.setBounds (filterRow.removeFromRight ((filterRow.getWidth() - 6) / 2));
            filterRow.removeFromRight (6);
        }
        gearButton.setBounds (filterRow);
        r.removeFromTop (6);
    }
    else r.removeFromTop (2);
    auto foot = r.removeFromBottom (32);
    diceButton.setBounds (foot.removeFromRight (juce::jmin (130, foot.getWidth() / 2)).reduced (0, 3));
    footer.setBounds (foot);
    list.setBounds (r.withTrimmedLeft (-12).withTrimmedRight (-12));
}

} // namespace rig
