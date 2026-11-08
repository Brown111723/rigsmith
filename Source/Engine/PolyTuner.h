#pragma once

#include "Common.h"

namespace rig
{
/** A polyphonic tuner, after TC Electronic's PolyTune: strum every open string and see how far
    each one is out at once. The audio thread captures; the message thread looks at a long,
    finely-resolved spectrum around where each string should be (its fundamental and first
    harmonics), so six strings ringing together each get their own reading. */
class PolyTuner
{
public:
    PolyTuner();

    void prepare (double sampleRate);
    void push (const float* data, int n) noexcept;   // audio thread

    struct Tuning
    {
        juce::String name;
        std::vector<int> midi;     // low string first
    };
    static const std::vector<Tuning>& tunings();

    struct StringReading
    {
        bool heard = false;        // ringing now
        bool held = false;         // showing the last reading for a moment after it stopped
        float cents = 0.0f;        // + sharp, - flat
        float frequency = 0.0f;
        juce::String note;         // "E2"
        float level = 0.0f;        // 0..1, relative to the loudest string
    };

    struct Reading
    {
        std::vector<StringReading> strings;
        int heardCount = 0;
        bool strummed = false;     // several strings at once: show them all
    };

    /** Message thread, ~10 times a second. */
    Reading analyse();

    std::atomic<float> referenceA { 440.0f };
    std::atomic<int> tuning { 0 };

    static juce::String noteName (int midi);

private:
    static constexpr int kFftOrder = 15, kFftSize = 1 << kFftOrder;   // zero-padded: fine frequency steps
    static constexpr int kWindow = 8192;                               // ~0.68 s at 12 kHz
    juce::dsp::FFT fft { kFftOrder };
    juce::AbstractFifo fifo { 32768 };
    std::vector<float> ring, history, work, window, mag;
    int decimation = 4, decimCounter = 0;
    float decimAcc = 0, lpState = 0;
    double rate = 48000.0, effectiveRate = 12000.0;
    std::vector<StringReading> last;
    std::vector<int> holdFrames;

    float magAt (float hz) const noexcept;
};

} // namespace rig
