#include "Header.h"

namespace rig
{
using namespace theme;

static const juce::String logoText ("RIGSMITH");
static juce::Font logoFont() { return font (15.0f, true).withExtraKerningFactor (0.14f); }

Header::Header (Engine& e) : engine (e)
{
    prev.onClick = [this] { engine.stepPreset (-1); };
    next.onClick = [this] { engine.stepPreset (1); };
    presetButton.onClick = [this] { if (onPresetMenu) onPresetMenu(); };
    presetButton.setTooltip ("Presets - Init is always first");
    save.onClick = [this] { if (onSave) onSave(); };
    menu.onClick = [this] { if (onMenu) onMenu(); };
    undo.onClick = [this] { if (onUndo) onUndo(); };
    redo.onClick = [this] { if (onRedo) onRedo(); };
    random.setHighlighted (true);
    random.onClick = [this] { if (onRandom) onRandom(); };
    random.onRightClick = [this] { if (onRandomMenu) onRandomMenu(); };
    tunerButton.onClick = [this] { if (onTuner) onTuner(); };
    modButton.onClick = [this] { if (onMod) onMod(); };
    library.onClick = [this] { if (onLibrary) onLibrary(); };
    fullScreen.onClick = [this] { if (onFullScreen) onFullScreen(); };
    tips.onClick = [this] { if (onTooltips) onTooltips(); };
    menu.setTooltip ("Menu: audio / MIDI settings, folders, plug-ins, MIDI mappings, shortcuts");
    for (auto* b : std::initializer_list<juce::Component*> { &prev, &next, &presetButton, &save, &menu, &undo, &redo,
                                                             &random, &tunerButton, &modButton, &library, &fullScreen, &tips })
        addAndMakeVisible (b);

    for (int i = 0; i < kNumScenes; ++i)
    {
        scenes[i].setButtonText (engine.sceneNames[i]);
        scenes[i].setTooltip ("Scene " + engine.sceneNames[i] + " (key " + juce::String (i + 1) + ") - double-click to rename, right-click for options");
        scenes[i].onClick = [this, i] { engine.setScene (i); };
        scenes[i].onRightClick = [this, i] { if (onSceneRightClick) onSceneRightClick (i); };
        scenes[i].onDoubleClick = [this, i] { renameScene (i); };
        addAndMakeVisible (scenes[i]);
    }
}

void Header::refresh (const Tuner::Reading& r, const juce::String& s)
{
    reading = r;
    status = s;

    juce::String name = engine.presetName + (engine.dirty ? " *" : "");
    if (presetButton.getButtonText() != name) { presetButton.setButtonText (name); presetButton.setTooltip (name + " - presets (Init is always first)"); }

    for (int i = 0; i < kNumScenes; ++i)
    {
        scenes[i].setToggleState (engine.getScene() == i, juce::dontSendNotification);
        if (scenes[i].getButtonText() != engine.sceneNames[i])
        {
            scenes[i].setButtonText (engine.sceneNames[i]);
            scenes[i].setTooltip ("Scene " + engine.sceneNames[i] + " (key " + juce::String (i + 1) + ") - double-click to rename, right-click for options");
            resized();   // named scenes ("Crunch") get wider buttons
        }
    }
    undo.setEnabled (engine.canUndo());
    redo.setEnabled (engine.canRedo());
    tunerButton.setToggleState (engine.tunerActive.load(), juce::dontSendNotification);
}

void Header::renameScene (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumScenes)) return;
    engine.setScene (index);
    auto ed = std::make_unique<juce::TextEditor>();
    ed->setFont (font (13.0f, true));
    ed->setJustification (juce::Justification::centred);
    ed->setInputRestrictions (12);
    ed->setColour (juce::TextEditor::backgroundColourId, bg);
    ed->setColour (juce::TextEditor::outlineColourId, accent);
    ed->setColour (juce::TextEditor::focusedOutlineColourId, accent);
    ed->setColour (juce::TextEditor::textColourId, text);
    ed->setText (engine.sceneNames[index], false);
    ed->setBounds (scenes[index].getBounds().withWidth (juce::jmax (scenes[index].getWidth(), 96)).withSizeKeepingCentre (juce::jmax (scenes[index].getWidth(), 96), 26));
    ed->setIndents (4, 0);

    auto finish = [this, index] (bool keep)
    {
        if (sceneEditor == nullptr) return;
        if (keep)
        {
            auto name = sceneEditor->getText().trim();
            if (name.isEmpty()) name = juce::String::charToString ((juce::juce_wchar) ('A' + index));
            if (name != engine.sceneNames[index]) { engine.sceneNames[index] = name; engine.markDirty(); }
        }
        // delete after this callback returns
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Header> (this)]
        {
            if (sp != nullptr) { sp->sceneEditor.reset(); sp->resized(); }
        });
    };
    ed->onReturnKey = [finish] { finish (true); };
    ed->onFocusLost = [finish] { finish (true); };
    ed->onEscapeKey = [finish] { finish (false); };
    sceneEditor = std::move (ed);
    addAndMakeVisible (*sceneEditor);
    sceneEditor->grabKeyboardFocus();
    sceneEditor->selectAll();
}

void Header::setStatus (double sr, int block, int lat, float load)
{
    const bool changed = std::abs (sr - sampleRate) > 1 || block != blockSize || lat != latency || std::abs (load - cpu) > 0.004f;
    sampleRate = sr; blockSize = block; latency = lat; cpu = load;
    if (changed) repaint (statusArea);
}

void Header::drawStatus (juce::Graphics& g)
{
    if (statusArea.getWidth() < 96 || sampleRate <= 0) return;
    auto r = statusArea.toFloat().reduced (0.0f, 4.0f);
    const float lineH = r.getHeight() / 2.0f;
    const auto top = r.removeFromTop (lineH), bottom = r;

    // line 1: sample rate, buffer, round-trip-ish latency
    const double ms = (blockSize + latency) / sampleRate * 1000.0;
    g.setFont (mono (11.0f));
    g.setColour (muted);
    g.drawText (juce::String (sampleRate / 1000.0, 1) + " kHz  " + juce::String (blockSize) + " smp  "
                    + juce::String (ms, 1) + " ms", top, juce::Justification::centredRight, false);

    // line 2: a little CPU bar that turns amber then red, plus plug-in delay compensation
    const int pct = juce::roundToInt (cpu * 100.0f);
    const auto c = pct >= 85 ? juce::Colour (0xffef5350) : pct >= 60 ? accent : teal;
    juce::String right = "CPU " + juce::String (pct) + "%";
    if (latency > 0) right = "PDC " + juce::String (latency) + "   " + right;
    const float textW = (float) juce::GlyphArrangement::getStringWidthInt (mono (11.0f), right) + 4.0f;
    g.setColour (pct >= 60 ? c : muted);
    g.drawText (right, bottom, juce::Justification::centredRight, false);
    auto bar = bottom.withTrimmedRight (textW + 6.0f).removeFromRight (40.0f).withSizeKeepingCentre (40.0f, 4.0f);
    if (bar.getX() > statusArea.getX())
    {
        g.setColour (border);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (c);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu))), 2.0f);
    }
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // logo: the mark, plus the name whenever its full width fits (never cut short)
    auto l = logoArea.toFloat();
    const auto mark = l.removeFromLeft (30.0f).withSizeKeepingCentre (26.0f, 26.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (mark, 6.0f, 2.0f);
    juce::Path p;
    p.startNewSubPath (mark.getX() + 6, mark.getBottom() - 8);
    p.lineTo (mark.getX() + 10, mark.getY() + 9);
    p.lineTo (mark.getX() + 14, mark.getBottom() - 11);
    p.lineTo (mark.getRight() - 6, mark.getY() + 7);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (l.getWidth() > 20.0f)
    {
        g.setColour (text);
        g.setFont (logoFont());
        g.drawText (logoText, l.withTrimmedLeft (8.0f), juce::Justification::centredLeft, false);
    }
    drawStatus (g);
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (12, 12);
    const int bh = r.getHeight();

    // right: icon buttons, evenly spaced
    const int gap = 6;
    menu.setBounds (r.removeFromRight (bh));
    r.removeFromRight (gap);
    tips.setBounds (r.removeFromRight (bh));
    r.removeFromRight (gap);
    fullScreen.setVisible (showFullScreenButton);
    if (showFullScreenButton) { fullScreen.setBounds (r.removeFromRight (bh)); r.removeFromRight (gap); }
    library.setBounds (r.removeFromRight (bh));
    r.removeFromRight (gap);
    modButton.setBounds (r.removeFromRight (bh));
    r.removeFromRight (gap);
    tunerButton.setBounds (r.removeFromRight (bh));
    r.removeFromRight (gap);
    random.setBounds (r.removeFromRight (bh));
    r.removeFromRight (16);

    // left: logo (full name only if there is room for everything else), presets, undo, scenes
    const int textW = juce::GlyphArrangement::getStringWidthInt (logoFont(), logoText) + 14;
    int sceneWidths[kNumScenes], sceneW = 0;
    for (int i = 0; i < kNumScenes; ++i)
    {
        sceneWidths[i] = juce::jlimit (44, 96, juce::GlyphArrangement::getStringWidthInt (font (13.0f, true), scenes[i].getButtonText()) + 22);
        sceneW += sceneWidths[i] + 5;
    }
    const int statusW = 190 - bh - 6;   // audio / CPU readout between the scenes and the dice
    const int fixed = bh + 4 + bh + 4 + 4 + bh + 4 + bh + 10 + bh + 4 + bh + 14 + sceneW + statusW;
    const bool showName = r.getWidth() - (30 + textW + 10) - fixed >= 150;
    logoArea = r.removeFromLeft (showName ? 30 + textW : 30);
    r.removeFromLeft (10);

    prev.setBounds (r.removeFromLeft (bh));
    r.removeFromLeft (4);
    const int presetW = juce::jlimit (110, 340, r.getWidth() - fixed + bh + 4);
    presetButton.setBounds (r.removeFromLeft (presetW));
    r.removeFromLeft (4);
    next.setBounds (r.removeFromLeft (bh));
    r.removeFromLeft (4);
    save.setBounds (r.removeFromLeft (bh));
    r.removeFromLeft (10);
    undo.setBounds (r.removeFromLeft (bh));
    r.removeFromLeft (4);
    redo.setBounds (r.removeFromLeft (bh));
    r.removeFromLeft (14);

    for (int i = 0; i < kNumScenes; ++i) { scenes[i].setBounds (r.removeFromLeft (sceneWidths[i])); r.removeFromLeft (5); }
    r.removeFromLeft (10);
    statusArea = r.removeFromRight (juce::jmin (r.getWidth(), 230));
}

void Header::mouseUp (const juce::MouseEvent&) {}

//==============================================================================
TunerOverlay::TunerOverlay (Engine& e) : engine (e)
{
    muteToggle.setToggleState (engine.tunerMute.load(), juce::dontSendNotification);
    muteToggle.onClick = [this] { engine.tunerMute = muteToggle.getToggleState(); };
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (muteToggle);
    addAndMakeVisible (close);

    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[i];
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.8f));
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i] { setMode ((Mode) i); };
        addAndMakeVisible (b);
    }
    modeButtons[0].setTooltip ("Auto: strum and see every string, or play one string for the big single-note display");
    modeButtons[1].setTooltip ("Mono: one string at a time, any note");
    modeButtons[2].setTooltip ("Poly: strum all the open strings and see how far out each one is");

    for (int i = 0; i < (int) PolyTuner::tunings().size(); ++i)
        tuningBox.addItem (PolyTuner::tunings()[(size_t) i].name, i + 1);
    const int tun = juce::jlimit (0, (int) PolyTuner::tunings().size() - 1, engine.getIntSetting ("tunerTuning", 0));
    tuningBox.setSelectedItemIndex (tun, juce::dontSendNotification);
    engine.polyTuner.tuning = tun;
    tuningBox.onChange = [this]
    {
        engine.polyTuner.tuning = tuningBox.getSelectedItemIndex();
        engine.setIntSetting ("tunerTuning", tuningBox.getSelectedItemIndex());
        repaint();
    };
    tuningBox.setTooltip ("The tuning Poly mode checks each string against");
    addAndMakeVisible (tuningBox);

    reference.setSliderStyle (juce::Slider::LinearBar);
    reference.setRange (430.0, 450.0, 1.0);
    reference.setTextValueSuffix (" Hz");
    reference.setValue (engine.getIntSetting ("tunerReference", 440), juce::dontSendNotification);
    engine.polyTuner.referenceA = (float) reference.getValue();
    engine.tuner.referenceA = (float) reference.getValue();
    reference.onValueChange = [this]
    {
        engine.polyTuner.referenceA = (float) reference.getValue();
        engine.tuner.referenceA = (float) reference.getValue();
        engine.setIntSetting ("tunerReference", (int) reference.getValue());
    };
    reference.setTooltip ("Reference pitch for A (440 Hz is standard)");
    addAndMakeVisible (reference);
    referenceLabel.setText ("A =", juce::dontSendNotification);
    referenceLabel.setJustificationType (juce::Justification::centredRight);
    referenceLabel.setColour (juce::Label::textColourId, muted);
    addAndMakeVisible (referenceLabel);

    setMode ((Mode) juce::jlimit (0, 2, engine.getIntSetting ("tunerMode", 0)));
}

void TunerOverlay::setMode (Mode m)
{
    mode = m;
    for (int i = 0; i < 3; ++i) modeButtons[i].setToggleState (i == (int) m, juce::dontSendNotification);
    engine.setIntSetting ("tunerMode", (int) m);
    repaint();
}

void TunerOverlay::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.95f));
    auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 820), juce::jmin (getHeight() - 40, 470)).withTrimmedTop (44).withTrimmedBottom (52);
    if (showingPoly()) paintPoly (g, r);
    else paintMono (g, r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 640), 300));
}

void TunerOverlay::paintMono (juce::Graphics& g, juce::Rectangle<int> r)
{
    const bool valid = reading.valid;
    const float cents = valid ? juce::jlimit (-50.0f, 50.0f, reading.cents) : 0.0f;
    const bool inTune = valid && std::abs (cents) < 3.0f;
    const auto c = ! valid ? dim : (inTune ? teal : (std::abs (cents) < 15.0f ? accent : red));

    g.setColour (c);
    g.setFont (font (120.0f, true));
    g.drawText (valid ? reading.note : "-", r.removeFromTop (140), juce::Justification::centred);

    g.setColour (muted);
    g.setFont (mono (16.0f));
    g.drawText (valid ? juce::String (reading.frequency, 1) + " Hz   " + (cents >= 0 ? "+" : "") + juce::String (cents, 1) + " cents"
                      : juce::String (mode == Auto ? "Play a string, or strum them all" : "Play a single string"),
                r.removeFromTop (30), juce::Justification::centred);

    auto meter = r.removeFromTop (70).reduced (0, 18).toFloat();
    g.setColour (card);
    g.fillRoundedRectangle (meter, 8.0f);
    for (int i = -5; i <= 5; ++i)
    {
        const float x = meter.getCentreX() + (float) i / 5.0f * (meter.getWidth() * 0.5f - 10.0f);
        g.setColour (i == 0 ? teal : borderHi);
        g.fillRect (x - 1.0f, meter.getY() + (i == 0 ? 4.0f : 10.0f), 2.0f, meter.getHeight() - (i == 0 ? 8.0f : 20.0f));
    }
    if (valid)
    {
        const float x = meter.getCentreX() + cents / 50.0f * (meter.getWidth() * 0.5f - 10.0f);
        g.setColour (c);
        g.fillRoundedRectangle (x - 5.0f, meter.getY() - 4.0f, 10.0f, meter.getHeight() + 8.0f, 5.0f);
    }
}

void TunerOverlay::paintPoly (juce::Graphics& g, juce::Rectangle<int> area)
{
    // one column per string, low string on the left: a ladder of lights, sharp above the
    // centre line and flat below it; the whole column glows when the string is in tune
    const auto& t = PolyTuner::tunings()[(size_t) juce::jlimit (0, (int) PolyTuner::tunings().size() - 1, engine.polyTuner.tuning.load())];
    const int n = (int) t.midi.size();
    auto r = area.toFloat();
    auto title = r.removeFromTop (28.0f);
    g.setColour (muted);
    g.setFont (font (13.0f, true));
    g.drawText (poly.heardCount > 0 ? "Strum all the open strings" : "Strum all the open strings  -  " + t.name, title, juce::Justification::centred);
    r.removeFromTop (8.0f);

    const float colW = juce::jmin (100.0f, r.getWidth() / (float) n);
    auto cols = r.withSizeKeepingCentre (colW * (float) n, r.getHeight());
    constexpr int segs = 21;   // 10 sharp, centre, 10 flat
    for (int s = 0; s < n; ++s)
    {
        auto col = cols.removeFromLeft (colW).reduced (10.0f, 0.0f);
        PolyTuner::StringReading sr;
        if (s < (int) poly.strings.size()) sr = poly.strings[(size_t) s];
        const bool show = sr.heard || sr.held;
        const float cents = juce::jlimit (-50.0f, 50.0f, sr.cents);
        const bool inTune = show && std::abs (cents) < 2.0f;
        const auto c = ! show ? dim : inTune ? teal : std::abs (cents) < 10.0f ? accent : red;
        const float alpha = sr.held ? 0.55f : 1.0f;

        auto label = col.removeFromBottom (44.0f);
        auto ladder = col.reduced (0.0f, 4.0f);
        g.setColour (card);
        g.fillRoundedRectangle (ladder.expanded (4.0f), 6.0f);
        if (inTune)
        {
            g.setColour (teal.withAlpha (0.18f * alpha));
            g.fillRoundedRectangle (ladder.expanded (4.0f), 6.0f);
        }
        const float segH = ladder.getHeight() / (float) segs;
        // the lit segment: 5 cents a step, nearer the centre finer (2, then 5)
        int lit = segs / 2;
        if (show && ! inTune)
        {
            const float a = std::abs (cents);
            const int steps = juce::jlimit (1, 10, a < 6.0f ? 1 : (int) std::ceil ((a - 1.0f) / 5.0f));
            lit = segs / 2 + (cents > 0 ? -steps : steps);
        }
        for (int k = 0; k < segs; ++k)
        {
            auto seg = juce::Rectangle<float> (ladder.getX(), ladder.getY() + segH * (float) k, ladder.getWidth(), segH).reduced (4.0f, 1.5f);
            const bool centre = k == segs / 2;
            juce::Colour sc = centre ? teal.withAlpha (0.25f) : borderHi.withAlpha (0.35f);
            if (show)
            {
                if (inTune && centre) sc = teal.withAlpha (alpha);
                else if (k == lit) sc = c.withAlpha (alpha);
                else if ((k < segs / 2 && lit <= k) || (k > segs / 2 && lit >= k)) sc = c.withAlpha (0.35f * alpha);   // the trail back to centre
            }
            g.setColour (sc);
            g.fillRoundedRectangle (seg, 2.0f);
        }
        g.setColour (show ? c.withAlpha (alpha) : muted);
        g.setFont (font (22.0f, true));
        g.drawText (sr.note.dropLastCharacters (sr.note.length() > 0 && juce::CharacterFunctions::isDigit (sr.note.getLastCharacter()) ? 1 : 0),
                    label.removeFromTop (26.0f), juce::Justification::centred);
        g.setFont (mono (11.0f));
        g.setColour (show ? text.withAlpha (alpha) : dim);
        g.drawText (show ? (cents >= 0 ? "+" : "") + juce::String (cents, 1) : juce::String ("-"), label, juce::Justification::centred);
    }
}

void TunerOverlay::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 820), juce::jmin (getHeight() - 40, 470));
    auto top = r.removeFromTop (32);
    for (int i = 0; i < 3; ++i) modeButtons[i].setBounds (top.removeFromLeft (64).reduced (0, 2));
    reference.setBounds (top.removeFromRight (90).reduced (0, 3));
    referenceLabel.setBounds (top.removeFromRight (40));
    top.removeFromRight (12);
    tuningBox.setBounds (top.removeFromRight (260).reduced (0, 2));
    auto row = r.removeFromBottom (36).withSizeKeepingCentre (420, 34);
    muteToggle.setBounds (row.removeFromLeft (240));
    close.setBounds (row.removeFromRight (140));
}

} // namespace rig
