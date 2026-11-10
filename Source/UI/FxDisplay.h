#pragma once

#include "Theme.h"

namespace rig
{
/** FabFilter-style live picture of what a block's knobs are doing:
    filter curves over a real-time spectrum, compressor transfer curves with the
    signal riding on them, waveshaper curves, LFO shapes, echo taps, reverb decay. */
class FxDisplay : public juce::Component,
                  private juce::Timer
{
public:
    FxDisplay (Engine&, Block&);
    ~FxDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override {}

    /** Shown large in the corner while a knob is hovered or dragged. */
    void setHighlight (const juce::String& text) { highlight = text; repaint(); }

    // interactive EQ handles
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void()> onEdited;

private:
    void timerCallback() override;
    void pullSpectrum();

    // coordinate helpers
    juce::Rectangle<float> plot() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db, float range) const;
    float dbForY (float y, float range) const;

    void drawGrid (juce::Graphics&, bool freqAxis, float dbRange);
    void drawSpectrum (juce::Graphics&);
    void drawResponse (juce::Graphics&);
    void drawDynamics (juce::Graphics&);
    void drawShaper (juce::Graphics&);
    void drawLfo (juce::Graphics&);
    void drawDelay (juce::Graphics&);
    void drawReverb (juce::Graphics&);
    void drawEnvelope (juce::Graphics&);
    void drawTuner (juce::Graphics&);
    void drawGate (juce::Graphics&);
    void drawCab (juce::Graphics&);
    /** The speaker picture on a Cabinet's display: its square, the cone's centre and radius. */
    juce::Rectangle<float> cabArea() const;
    bool draggingMic = false;
    void moveMic (juce::Point<float>);
    float gateY (float db) const;
    bool draggingThreshold = false;
    std::vector<char> gateOpen;   // per history column

    struct Handle { int freqParam, gainParam; juce::Point<float> pos; };
    std::vector<Handle> eqHandles() const;
    int hitHandle (juce::Point<float>) const;

    Engine& engine;
    Block& block;
    const Visual visual;
    const juce::Colour colour;
    juce::String highlight;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> preHist, postHist, fftData, scratch;
    std::vector<float> preDisp, postDisp;   // per-pixel smoothed dB

    std::vector<float> envIn, envGain;      // scrolling history for Envelope / Dynamics
    int dragHandle = -1, hoverHandle = -1;

    Tuner::Reading tunerReading;
    float strobePhase = 0, needle = 0;
    std::vector<float> centsHistory;
};

} // namespace rig
