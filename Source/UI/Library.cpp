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
        else chooseFolder();
    };
    addAndMakeVisible (folderButton);

    search.setTextToShowWhenEmpty ("Search", muted);
    search.setFont (font (14.0f));
    search.setIndents (10, 7);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, panel);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

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
    folderButton.setButtonText (tab == Plugins ? "Scan..." : "Folder...");
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
        std::sort (plugins.begin(), plugins.end(), [] (const auto& a, const auto& b)
        {
            return (a.manufacturerName + a.name).compareIgnoreCase (b.manufacturerName + b.name) < 0;
        });
    }
    else
    {
        const auto folder = host.getEngine().getFolder (folderKey());
        const auto pattern = tab == Models ? juce::String ("*.nam") : juce::String ("*.wav;*.aif;*.aiff;*.flac");
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, pattern, juce::File::findFiles))
        {
            files.add (entry.getFile());
            if (files.size() >= 5000) break;
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
            if (matches (files[i].getRelativePathFrom (host.getEngine().getFolder (folderKey()))))
                rows.add (i);
    }

    list.updateContent();
    list.repaint();

    juce::String f;
    if (tab == Plugins)
        f = plugins.isEmpty() ? "No VST3s yet - click Scan" : juce::String (rows.size()) + " effects - drag onto the rig";
    else
        f = juce::String (rows.size()) + " files in " + host.getEngine().getFolder (folderKey()).getFileName();
    footer.setText (f, juce::dontSendNotification);
}

void Library::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (tab == Models ? "Choose your NAM model folder" : "Choose your IR folder",
                                                   host.getEngine().getFolder (folderKey()));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto r = fc.getResult();
        if (r.isDirectory())
        {
            host.getEngine().setFolder (folderKey(), r);
            refresh();
        }
    });
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
        sub = d.manufacturerName + (d.category.isNotEmpty() ? " - " + d.category : juce::String());
        c = juce::Colour::fromHSV ((float) ((juce::uint32) d.name.hashCode() % 360) / 360.0f, 0.45f, 0.95f, 1.0f);
    }
    else
    {
        const auto& f = files[rows[row]];
        name = f.getFileNameWithoutExtension();
        sub = f.getParentDirectory().getFileName();
        c = tab == Models ? accent : teal;
    }

    g.setColour (c);
    g.fillRoundedRectangle (12.0f, (float) h * 0.5f - 9.0f, 4.0f, 18.0f, 2.0f);
    g.setColour (text);
    g.setFont (font (13.0f, true));
    g.drawText (name, 24, 4, w - 30, h / 2, juce::Justification::bottomLeft, true);
    g.setColour (muted);
    g.setFont (font (11.0f));
    g.drawText (sub, 24, h / 2, w - 30, h / 2 - 4, juce::Justification::topLeft, true);
}

void Library::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { activate (row); }

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
    if (tab == Plugins) return plugins[rows[row]].descriptiveName;
    return files[rows[row]].getFullPathName();
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
    footer.setBounds (r.removeFromBottom (28));
    list.setBounds (r.withTrimmedLeft (-12).withTrimmedRight (-12));
}

} // namespace rig
