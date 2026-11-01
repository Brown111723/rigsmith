#include "Header.h"

namespace rig
{
using namespace theme;

Header::Header (Engine& e) : engine (e)
{
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    prev.onClick = [this] { engine.stepPreset (-1); };
    next.onClick = [this] { engine.stepPreset (1); };
    presetButton.onClick = [this] { if (onPresetMenu) onPresetMenu(); };
    presetButton.setTooltip ("Presets");
    save.onClick = [this] { if (onSave) onSave(); };
    save.setTooltip ("Save preset (Ctrl+S)");
    menu.setButtonText ("Menu");
    menu.onClick = [this] { if (onMenu) onMenu(); };
    for (auto* b : { &prev, &next, &presetButton, &save, &menu }) addAndMakeVisible (b);

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
    g.setColour (text);
    g.setFont (font (15.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("RIGSMITH", l.withTrimmedLeft (8.0f), juce::Justification::centredLeft);

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

    g.setColour (muted);
    g.setFont (mono (11.0f));
    g.drawFittedText (status, statusArea, juce::Justification::centredRight, 2);
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    logoArea = r.removeFromLeft (130);
    r.removeFromLeft (8);

    prev.setBounds (r.removeFromLeft (40));
    r.removeFromLeft (4);
    presetButton.setBounds (r.removeFromLeft (230));
    r.removeFromLeft (4);
    next.setBounds (r.removeFromLeft (40));
    r.removeFromLeft (6);
    save.setBounds (r.removeFromLeft (60));
    r.removeFromLeft (16);

    for (auto& s : scenes) { s.setBounds (r.removeFromLeft (44)); r.removeFromLeft (5); }
    r.removeFromLeft (12);

    tunerArea = r.removeFromLeft (150);
    r.removeFromLeft (14);

    menu.setBounds (r.removeFromRight (64));
    r.removeFromRight (12);

    auto knobArea = r.removeFromRight (190);
    auto ia = knobArea.removeFromLeft (95);
    inGain.setBounds (ia.removeFromLeft (44));
    inArea = ia.withTrimmedLeft (4);
    outGain.setBounds (knobArea.removeFromLeft (44));
    outArea = knobArea.withTrimmedLeft (4);
    r.removeFromRight (8);
    statusArea = r;
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
