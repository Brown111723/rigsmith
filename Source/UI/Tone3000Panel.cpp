#include "Tone3000Panel.h"

namespace rig
{
using namespace theme;

static const char* listEndpoints[] = { "favorited", "downloaded", "created", "trending" };

Tone3000Panel::Tone3000Panel (Engine& e, t3k::Client& c) : engine (e), client (c)
{
    client.addChangeListener (this);

    keyHelp.setFont (font (12.5f));
    keyHelp.setColour (juce::Label::textColourId, muted);
    keyHelp.setJustificationType (juce::Justification::topLeft);
    keyHelp.setText ("TONE3000 lets apps like Rigsmith load its tones with a free \"publishable key\".\n\n"
                     "1. Sign in at tone3000.com and open Settings > API Keys\n"
                     "2. Create a key and paste the t3k_pub_... key below\n\n"
                     "It's saved in Rigsmith.ini. (Builds made with a key already have one.)", juce::dontSendNotification);
    keyEditor.setTextToShowWhenEmpty ("t3k_pub_...", dim);
    keyEditor.setFont (mono (13.0f));
    keyEditor.setIndents (8, 8);
    saveKey.onClick = [this]
    {
        const auto k = keyEditor.getText().trim();
        if (! k.startsWith ("t3k_pub_")) { setStatus ("That doesn't look like a publishable key (t3k_pub_...)"); return; }
        client.setKey (k);
        setStatus ("Key saved - now sign in or browse");
        updateState();
    };
    getKey.onClick = [] { juce::URL (t3k::Client::keysPage).launchInDefaultBrowser(); };

    signIn.onClick = [this]
    {
        setStatus ("Sign in in your browser, then come back here...", true);
        client.startFlow ({}, {}, {}, [this, a = alive] (int, juce::String err)
        {
            if (! *a) return;
            setStatus (err.isNotEmpty() ? err : client.isConnected() ? "Signed in as " + client.userName : juce::String());
            updateState();
        });
    };
    browseNam.setColour (juce::TextButton::buttonColourId, accent);
    browseNam.setColour (juce::TextButton::textColourOffId, bg);
    browseNam.onClick = [this] { browse ("nam"); };
    browseIr.onClick = [this] { browse ("ir"); };
    browseNam.setTooltip ("Opens the TONE3000 catalog in your browser: pick an amp, pedal or full rig and it loads into Rigsmith");
    browseIr.setTooltip ("Browse TONE3000 for cabinet and space IRs");
    signOut.onClick = [this] { client.signOut(); tones.clear(); list.updateContent(); updateState(); setStatus ("Signed out"); };
    openSite.setButtonText ("tone3000.com");
    openSite.onClick = [] { juce::URL (t3k::Client::siteUrl).launchInDefaultBrowser(); };
    openSite.setTooltip ("Open TONE3000 in your browser");

    for (int i = 0; i < 4; ++i)
    {
        lists[i].setClickingTogglesState (false);
        lists[i].onClick = [this, i] { listIndex = i; reloadList(); };
    }

    list.setModel (this);
    list.setRowHeight (58);
    list.setColour (juce::ListBox::backgroundColourId, panel);
    list.setOutlineThickness (0);

    status.setFont (font (11.5f));
    status.setColour (juce::Label::textColourId, muted);
    status.setJustificationType (juce::Justification::centredLeft);

    for (auto* comp : std::initializer_list<juce::Component*> { &keyHelp, &keyEditor, &saveKey, &getKey, &signIn, &browseNam, &browseIr,
                                                                &signOut, &openSite, &list, &status })
        addChildComponent (comp);
    for (auto& b : lists) addChildComponent (b);
    updateState();
}

Tone3000Panel::~Tone3000Panel()
{
    *alive = false;
    client.removeChangeListener (this);
}

Tone3000Panel::State Tone3000Panel::state() const
{
    if (! client.hasKey()) return State::NeedsKey;
    return client.isConnected() ? State::SignedIn : State::SignedOut;
}

void Tone3000Panel::changeListenerCallback (juce::ChangeBroadcaster*) { updateState(); }

void Tone3000Panel::visibilityChanged()
{
    if (isVisible() && state() == State::SignedIn && tones.isEmpty()) reloadList();
}

void Tone3000Panel::updateState()
{
    const auto s = state();
    keyHelp.setVisible (s == State::NeedsKey);
    keyEditor.setVisible (s == State::NeedsKey);
    saveKey.setVisible (s == State::NeedsKey);
    getKey.setVisible (s == State::NeedsKey);
    signIn.setVisible (s == State::SignedOut);
    browseNam.setVisible (s != State::NeedsKey);
    browseIr.setVisible (s != State::NeedsKey);
    openSite.setVisible (s != State::NeedsKey);
    signOut.setVisible (s == State::SignedIn);
    list.setVisible (s == State::SignedIn);
    for (int i = 0; i < 4; ++i)
    {
        lists[i].setVisible (s == State::SignedIn);
        lists[i].setToggleState (i == listIndex, juce::dontSendNotification);
    }
    if (s == State::SignedIn)
    {
        if (client.userName.isEmpty()) client.fetchUser ([] (bool) {});
        if (client.avatarUrl.isNotEmpty() && ! avatar.isValid())
            client.fetchImage (client.avatarUrl, [this, a = alive] (juce::Image img) { if (*a) { avatar = img; repaint(); } });
        if (isVisible() && tones.isEmpty()) reloadList();
    }
    status.setVisible (true);
    resized();
    repaint();
}

void Tone3000Panel::setStatus (const juce::String& s, bool isBusy)
{
    busy = isBusy;
    status.setText (s, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, isBusy ? accent : muted);
}

void Tone3000Panel::reloadList()
{
    for (int i = 0; i < 4; ++i) lists[i].setToggleState (i == listIndex, juce::dontSendNotification);
    if (state() != State::SignedIn) return;
    const int req = ++listRequest;
    setStatus ("Loading " + lists[listIndex].getButtonText().toLowerCase() + "...", true);
    client.listTones (listEndpoints[listIndex], 1, [this, a = alive, req] (juce::Array<t3k::Tone> result, juce::String err)
    {
        if (! *a || req != listRequest) return;
        tones.clear();
        for (auto& t : result) if (t.canLoad()) tones.add (t);   // only NAM and IR tones load in Rigsmith
        list.updateContent();
        list.repaint();
        if (err.isNotEmpty()) setStatus (err);
        else if (tones.isEmpty())
            setStatus (listIndex == 0 ? "No favourites yet - heart tones on TONE3000 and they show up here"
                     : listIndex == 1 ? "Nothing downloaded yet - Browse TONE3000 to find tones"
                     : listIndex == 2 ? "You haven't published any tones" : "Nothing trending right now");
        else setStatus (juce::String (tones.size()) + " tones - click one to load it");

        for (auto& t : tones)
            if (t.image.isNotEmpty() && thumbs.find (t.image) == thumbs.end())
                client.fetchImage (t.image, [this, a, url = t.image] (juce::Image img) { if (*a) { thumbs[url] = img; list.repaint(); } });
    });
}

//==============================================================================
void Tone3000Panel::browse (const juce::String& format, std::function<void (const juce::File&)> into)
{
    if (! client.hasKey()) { setStatus ("Add your TONE3000 key first"); return; }
    setStatus ("Pick a tone in your browser - it loads here when you choose it", true);
    client.startFlow ("select_tone", format, {}, [this, a = alive, into] (int toneId, juce::String err)
    {
        if (! *a) return;
        updateState();
        if (err.isNotEmpty()) { setStatus (err); return; }
        if (toneId <= 0) { setStatus (client.isConnected() ? "Signed in as " + client.userName : juce::String()); return; }
        loadToneById (toneId, into);
    });
}

void Tone3000Panel::loadToneById (int id, std::function<void (const juce::File&)> into)
{
    setStatus ("Getting the tone...", true);
    client.getTone (id, [this, a = alive, into] (t3k::Tone t, juce::String err)
    {
        if (! *a) return;
        if (err.isNotEmpty()) { setStatus (err); return; }
        loadTone (t, into);
    });
}

void Tone3000Panel::loadTone (const t3k::Tone& tone, std::function<void (const juce::File&)> into)
{
    if (! tone.canLoad())
    {
        setStatus (tone.title + " is a " + tone.format.toUpperCase() + " tone - Rigsmith loads NAM captures and IRs");
        return;
    }
    setStatus ("Opening " + tone.title + "...", true);
    client.listModels (tone.id, [this, a = alive, tone, into] (juce::Array<t3k::Model> models, juce::String err)
    {
        if (! *a) return;
        if (err.isNotEmpty() || models.isEmpty()) { setStatus (err.isNotEmpty() ? err : "No files in " + tone.title); return; }

        auto fetch = [this, a, tone, into] (juce::Array<t3k::Model> chosen, int loadIndex)
        {
            auto remaining = std::make_shared<int> (chosen.size());
            auto toLoad = std::make_shared<juce::File>();
            setStatus ("Downloading " + tone.title + (chosen.size() > 1 ? " (" + juce::String (chosen.size()) + " files)" : juce::String()) + "...", true);
            for (int i = 0; i < chosen.size(); ++i)
                client.download (tone, chosen[i], [this, a, tone, into, remaining, toLoad, i, loadIndex] (juce::File f, juce::String derr)
                {
                    if (! *a) return;
                    if (derr.isNotEmpty()) setStatus (derr);
                    if (i == loadIndex && f.existsAsFile()) *toLoad = f;
                    if (--*remaining > 0) return;
                    clearGearCache();
                    if (onLibraryChanged) onLibraryChanged();
                    if (toLoad->existsAsFile())
                    {
                        setStatus ("Loaded " + tone.title + (tone.creator.isNotEmpty() ? " by " + tone.creator : juce::String()));
                        if (into) into (*toLoad);
                        else if (onFileReady) onFileReady (*toLoad);
                    }
                    list.repaint();
                });
        };

        if (models.size() == 1) { fetch (models, 0); return; }

        // several captures (knob settings, mic positions...): pick one, or take them all
        juce::PopupMenu m;
        m.addSectionHeader (tone.title + " - " + juce::String (models.size()) + (tone.format == "ir" ? " IRs" : " captures"));
        for (int i = 0; i < juce::jmin (models.size(), 60); ++i)
            m.addItem (1 + i, models[i].name.isNotEmpty() ? models[i].name : "Model " + juce::String (i + 1));
        m.addSeparator();
        m.addItem (1000, "Download all " + juce::String (models.size()) + " and load the first");
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, a, models, fetch] (int r)
        {
            if (! *a) return;
            if (r == 1000) fetch (models, 0);
            else if (r > 0 && r - 1 < models.size()) fetch (juce::Array<t3k::Model> { models[r - 1] }, 0);
            else setStatus ({});
        });
    });
}

//==============================================================================
void Tone3000Panel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, tones.size())) return;
    const auto& t = tones.getReference (row);
    if (selected)
    {
        g.setColour (cardHi);
        g.fillRoundedRectangle (4.0f, 2.0f, (float) w - 8.0f, (float) h - 4.0f, 6.0f);
    }

    // tone image (or a gear-coloured tile)
    const juce::Rectangle<float> img (12.0f, 7.0f, (float) h - 14.0f, (float) h - 14.0f);
    const auto gc = gearColour (t.getGear());
    if (auto it = thumbs.find (t.image); it != thumbs.end() && it->second.isValid())
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip; clip.addRoundedRectangle (img, 6.0f);
        g.reduceClipRegion (clip);
        g.drawImage (it->second, img, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setColour (gc.withAlpha (0.18f));
        g.fillRoundedRectangle (img, 6.0f);
        g.setColour (gc);
        g.setFont (font (11.0f, true));
        g.drawText (t.format.toUpperCase(), img, juce::Justification::centred);
    }
    g.setColour (gc);
    g.fillRoundedRectangle (img.getX(), img.getBottom() - 3.0f, img.getWidth(), 3.0f, 1.5f);

    const int x = (int) img.getRight() + 10;
    const bool have = client.isInLibrary (t);
    g.setColour (text);
    g.setFont (font (13.0f, true));
    g.drawText (t.title, x, 8, w - x - (have ? 30 : 10), 18, juce::Justification::centredLeft, true);
    g.setFont (font (11.0f));
    g.setColour (gc);
    const auto gearText = gearName (t.getGear()) + "  " + t.format.toUpperCase();
    g.drawText (gearText, x, 27, w - x - 10, 14, juce::Justification::centredLeft, true);
    g.setColour (muted);
    g.drawText ("by " + t.creator, x, 41, w - x - 10, 14, juce::Justification::centredLeft, true);
    if (have)
    {
        // already in your library
        juce::Path tick;
        const float cx = (float) w - 20.0f, cy = 17.0f;
        tick.startNewSubPath (cx - 5.0f, cy);
        tick.lineTo (cx - 1.5f, cy + 3.5f);
        tick.lineTo (cx + 5.0f, cy - 4.0f);
        g.setColour (teal);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void Tone3000Panel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, tones.size())) return;
    const auto t = tones[row];
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Load into the rig");
        m.addItem (2, "Open on TONE3000");
        m.addItem (3, "Show in folder", client.isInLibrary (t));
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, t] (int r)
        {
            if (r == 1) loadTone (t);
            else if (r == 2 && t.url.isNotEmpty()) juce::URL (t.url).launchInDefaultBrowser();
            else if (r == 3) client.folderFor (t).revealToUser();
        });
        return;
    }
    loadTone (t);
}

void Tone3000Panel::listBoxItemDoubleClicked (int, const juce::MouseEvent&) {}

juce::String Tone3000Panel::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, tones.size())) return {};
    const auto& t = tones.getReference (row);
    auto tip = t.title + " by " + t.creator + "\n" + gearName (t.getGear()) + " - " + t.format.toUpperCase();
    if (t.description.isNotEmpty()) tip << "\n\n" << t.description.substring (0, 300);
    tip << "\n\nClick to load it into the rig" << (client.isInLibrary (t) ? " (already downloaded)" : "");
    return tip;
}

//==============================================================================
void Tone3000Panel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    const auto s = state();

    // wordmark (text only: TONE3000's own logo files can be dropped in later)
    auto top = getLocalBounds().reduced (4, 0).removeFromTop (30);
    g.setColour (text);
    g.setFont (font (17.0f, true).withExtraKerningFactor (0.06f));
    g.drawText ("TONE3000", top.removeFromLeft (120), juce::Justification::centredLeft);

    if (s == State::SignedIn && ! userArea.isEmpty())
    {
        auto u = userArea.toFloat();
        const auto circle = u.removeFromLeft (u.getHeight()).reduced (2.0f);
        if (avatar.isValid())
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path p; p.addEllipse (circle);
            g.reduceClipRegion (p);
            g.drawImage (avatar, circle, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (circle);
            g.setColour (accent);
            g.setFont (font (13.0f, true));
            g.drawText (client.userName.substring (0, 1).toUpperCase(), circle, juce::Justification::centred);
        }
        g.setColour (text);
        g.setFont (font (12.5f, true));
        g.drawText (client.userName.isNotEmpty() ? client.userName : "Signed in", u.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
    }

    if (s == State::SignedOut && ! splashArea.isEmpty())
    {
        auto r = splashArea;
        g.setColour (accent);
        g.setFont (font (15.0f, true));
        g.drawText ("Rigsmith  x  TONE3000", r.removeFromTop (24), juce::Justification::centredLeft);
        r.removeFromTop (4);
        g.setColour (muted);
        g.setFont (font (12.5f));
        g.drawFittedText ("Thousands of amps, pedals, full rigs, cabinets and spaces, captured by the TONE3000 "
                          "community. Sign in with your free TONE3000 account and load any of them straight "
                          "into your rig - they're saved to your library, sorted by type, with credit to "
                          "their creators.", r, juce::Justification::topLeft, 9, 1.0f);
    }
}

void Tone3000Panel::resized()
{
    auto r = getLocalBounds().reduced (4, 0);
    const auto s = state();
    auto top = r.removeFromTop (30);
    if (s != State::NeedsKey) openSite.setBounds (top.removeFromRight (92).reduced (0, 3));
    r.removeFromTop (8);
    status.setBounds (r.removeFromBottom (36));

    if (s == State::NeedsKey)
    {
        keyHelp.setBounds (r.removeFromTop (130));
        keyEditor.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        auto row = r.removeFromTop (32);
        saveKey.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (6);
        getKey.setBounds (row);
        return;
    }

    if (s == State::SignedIn)
    {
        auto u = r.removeFromTop (30);
        signOut.setBounds (u.removeFromRight (74).reduced (0, 2));
        userArea = u;
        r.removeFromTop (8);
    }
    else userArea = {};

    auto b = r.removeFromTop (38);
    browseIr.setBounds (b.removeFromRight (52));
    b.removeFromRight (6);
    browseNam.setBounds (b);
    r.removeFromTop (10);

    if (s == State::SignedOut)
    {
        splashArea = r.removeFromTop (110);
        signIn.setBounds (r.removeFromTop (32).removeFromLeft (110));
        return;
    }
    splashArea = {};

    auto tabRow = r.removeFromTop (28);
    const int tw = (tabRow.getWidth() - 9) / 4;
    for (auto& t : lists) { t.setBounds (tabRow.removeFromLeft (tw)); tabRow.removeFromLeft (3); }
    r.removeFromTop (6);
    list.setBounds (r.withTrimmedLeft (-4).withTrimmedRight (-4));
}

} // namespace rig
