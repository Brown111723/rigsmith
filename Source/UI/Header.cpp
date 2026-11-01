#include "Header.h"

namespace rig
{
using namespace theme;

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
    random.setLabel ("Randomise");
    random.setHighlighted (true);
    random.onClick = [this] { if (onRandom) onRandom(); };
    random.onRightClick = [this] { if (onRandomMenu) onRandomMenu(); };
    randomMore.onClick = [this] { if (onRandomMenu) onRandomMenu(); };
    library.onClick = [this] { if (onLibrary) onLibrary(); };
    fullScreen.onClick = [this] { if (onFullScreen) onFullScreen(); };
    for (auto* b : std::initializer_list<juce::Component*> { &prev, &next, &presetButton, &save, &menu, &undo, &redo,
                                                             &random, &randomMore, &library, &fullScreen })
        addAndMakeVisible (b);

    for (int i = 0; i < kNumScenes; ++i)
    {
        scenes[i].setButtonText (engine.sceneNames[i]);
        scenes[i].setTooltip ("Scene " + engine.sceneNames[i] + " (key " + juce::String (i + 1) + ", right-click for options)");
        scenes[i].onClick = [this, i] { engine.setScene (i); };
        scenes[i].onRightClick = [this, i] { if (onSceneRightClick) onSceneRightClick (i); };
        addAndMakeVisible (scenes[i]);
    }

    for (auto* s : { &inGain, &outGain })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRange (-24.0, 24.0, 0.1);
        s->setDoubleClickReturnValue (true, 0.0);
        s->setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        addAndMakeVisible (s);
    }
    inGain.setColour (juce::Slider::rotarySliderFillColourId, teal);
    inGain.setTooltip ("Input gain");
    outGain.setTooltip ("Master output");
    inGain.onValueChange = [this] { engine.inputGainDb = (float) inGain.getValue(); repaint (inArea); };
    outGain.onValueChange = [this] { engine.outputGainDb = (float) outGain.getValue(); repaint (outArea); };
}

void Header::refresh (const Tuner::Reading& r, const juce::String& s)
{
    reading = r;
    status = s;

    juce::String name = engine.presetName + (engine.dirty ? " *" : "");
    if (presetButton.getButtonText() != name) presetButton.setButtonText (name);

    for (int i = 0; i < kNumScenes; ++i)
    {
        scenes[i].setToggleState (engine.getScene() == i, juce::dontSendNotification);
        if (scenes[i].getButtonText() != engine.sceneNames[i]) scenes[i].setButtonText (engine.sceneNames[i]);
    }
    undo.setEnabled (engine.canUndo());
    redo.setEnabled (engine.canRedo());
    if (! inGain.isMouseButtonDown()) inGain.setValue (engine.inputGainDb.load(), juce::dontSendNotification);
    if (! outGain.isMouseButtonDown()) outGain.setValue (engine.outputGainDb.load(), juce::dontSendNotification);

    repaint (tunerArea);
    repaint (statusArea);
    repaint (inArea);
    repaint (outArea);
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // logo
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
    if (l.getWidth() > 60.0f)
    {
        g.setColour (text);
        g.setFont (font (15.0f, true).withExtraKerningFactor (0.14f));
        g.drawText ("RIGSMITH", l.withTrimmedLeft (8.0f), juce::Justification::centredLeft);
    }

    // tuner box
    {
        const auto t = tunerArea.toFloat();
        g.setColour (bg);
        g.fillRoundedRectangle (t, 8.0f);
        g.setColour (engine.tunerActive.load() ? accent : border);
        g.drawRoundedRectangle (t.reduced (0.5f), 8.0f, 1.0f);
        auto r = tunerArea.reduced (10, 0);
        g.setColour (muted);
        g.setFont (font (10.0f, true).withExtraKerningFactor (0.08f));
        g.drawText ("TUNER", r.removeFromLeft (44), juce::Justification::centredLeft);
        if (reading.valid)
        {
            const bool inTune = std::abs (reading.cents) < 3.0f;
            g.setColour (inTune ? teal : accent);
            g.setFont (font (18.0f, true));
            g.drawText (reading.note, r.removeFromLeft (30), juce::Justification::centredLeft);
            g.setColour (muted);
            g.setFont (mono (12.0f));
            g.drawText ((reading.cents >= 0 ? "+" : "") + juce::String (reading.cents, 0) + "c", r, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (dim);
            g.setFont (mono (12.0f));
            g.drawText (engine.tunerActive.load() ? "listening" : "click", r, juce::Justification::centredLeft);
        }
    }

    auto knobLabel = [&] (juce::Rectangle<int> a, const juce::String& lbl, float db)
    {
        g.setColour (muted);
        g.setFont (font (10.0f, true));
        g.drawText (lbl, a.removeFromTop (a.getHeight() / 2), juce::Justification::bottomLeft);
        g.setFont (mono (11.0f));
        g.setColour (text);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db, 1), a, juce::Justification::topLeft);
    };
    knobLabel (inArea, "IN", engine.inputGainDb.load());
    knobLabel (outArea, "OUT", engine.outputGainDb.load());

    if (! statusArea.isEmpty())
    {
        g.setColour (muted);
        g.setFont (mono (11.0f));
        g.drawFittedText (status, statusArea, juce::Justification::centredRight, 2);
    }
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (12, 12);
    const int bh = r.getHeight();
    const bool narrow = getWidth() < 1400;
    random.setLabel (getWidth() < 1320 ? juce::String() : juce::String ("Randomise"));

    logoArea = r.removeFromLeft (narrow ? 36 : 124);
    r.removeFromLeft (6);

    // right side first, so the preset name gets whatever is left
    menu.setBounds (r.removeFromRight (bh));
    r.removeFromRight (10);
    auto knobArea = r.removeFromRight (176);
    auto ia = knobArea.removeFromLeft (88);
    inGain.setBounds (ia.removeFromLeft (40));
    inArea = ia.withTrimmedLeft (4);
    outGain.setBounds (knobArea.removeFromLeft (40));
    outArea = knobArea.withTrimmedLeft (4);
    r.removeFromRight (10);
    fullScreen.setVisible (showFullScreenButton);
    if (showFullScreenButton) { fullScreen.setBounds (r.removeFromRight (bh)); r.removeFromRight (4); }
    library.setBounds (r.removeFromRight (bh));
    r.removeFromRight (10);
    randomMore.setBounds (r.removeFromRight (22));
    r.removeFromRight (2);
    random.setBounds (r.removeFromRight (random.preferredWidth (bh) + 4));
    r.removeFromRight (10);

    prev.setBounds (r.removeFromLeft (bh));
    r.removeFromLeft (4);
    const int fixedAfterPreset = 4 + bh + 4 + bh + 10 + bh + 4 + bh + 14 + 4 * 49 + 8 + 128 + 10;
    const int presetW = juce::jlimit (130, 300, r.getWidth() - fixedAfterPreset);
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

    for (auto& s : scenes) { s.setBounds (r.removeFromLeft (44)); r.removeFromLeft (5); }
    r.removeFromLeft (8);
    tunerArea = r.removeFromLeft (128);
    statusArea = {};   // the audio status now lives in the corner of the rig view
}

void Header::mouseUp (const juce::MouseEvent& e)
{
    if (tunerArea.contains (e.getPosition()) && onTuner)
        onTuner();
}

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
