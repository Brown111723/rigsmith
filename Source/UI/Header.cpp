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
    library.onClick = [this] { if (onLibrary) onLibrary(); };
    fullScreen.onClick = [this] { if (onFullScreen) onFullScreen(); };
    for (auto* b : std::initializer_list<juce::Component*> { &prev, &next, &presetButton, &save, &menu, &undo, &redo,
                                                             &random, &tunerButton, &library, &fullScreen })
        addAndMakeVisible (b);

    for (int i = 0; i < kNumScenes; ++i)
    {
        scenes[i].setButtonText (engine.sceneNames[i]);
        scenes[i].setTooltip ("Scene " + engine.sceneNames[i] + " (key " + juce::String (i + 1) + ", right-click for options)");
        scenes[i].onClick = [this, i] { engine.setScene (i); };
        scenes[i].onRightClick = [this, i] { if (onSceneRightClick) onSceneRightClick (i); };
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
            scenes[i].setTooltip ("Scene " + engine.sceneNames[i] + " (key " + juce::String (i + 1) + ", right-click for options)");
            resized();   // named scenes ("Crunch") get wider buttons
        }
    }
    undo.setEnabled (engine.canUndo());
    redo.setEnabled (engine.canRedo());
    tunerButton.setToggleState (engine.tunerActive.load(), juce::dontSendNotification);
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
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (12, 12);
    const int bh = r.getHeight();

    // right: icon buttons
    menu.setBounds (r.removeFromRight (bh));
    r.removeFromRight (4);
    fullScreen.setVisible (showFullScreenButton);
    if (showFullScreenButton) { fullScreen.setBounds (r.removeFromRight (bh)); r.removeFromRight (4); }
    library.setBounds (r.removeFromRight (bh));
    r.removeFromRight (12);
    tunerButton.setBounds (r.removeFromRight (bh));
    r.removeFromRight (4);
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
    const int fixed = bh + 4 + 4 + bh + 4 + bh + 10 + bh + 4 + bh + 14 + sceneW;
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
}

void TunerOverlay::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.94f));
    auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 640), 300);

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
                      : juce::String ("Play a single string"),
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

void TunerOverlay::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 640), 300);
    auto row = r.removeFromBottom (36).withSizeKeepingCentre (420, 34);
    muteToggle.setBounds (row.removeFromLeft (240));
    close.setBounds (row.removeFromRight (140));
}

} // namespace rig
