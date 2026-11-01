#pragma once

#include "Common.h"

namespace rig
{
/** Lock-free capture on the audio thread, YIN pitch detection on the message thread. */
class Tuner
{
public:
    Tuner();

    void prepare (double sampleRate);
    void push (const float* data, int n) noexcept;   // audio thread

    struct Reading
    {
        bool valid = false;
        float frequency = 0, cents = 0;
        juce::String note;
        int octave = 0;
    };

    Reading analyse();                                // message thread
    std::atomic<float> referenceA { 440.0f };

private:
    static constexpr int kWindow = 2048;
    juce::AbstractFifo fifo { 16384 };
    std::vector<float> ring;
    std::vector<float> history, diff;
    int decimation = 1, decimCounter = 0;
    float decimAcc = 0;
    double rate = 48000.0, effectiveRate = 48000.0;
    float smoothedFreq = 0;
};

} // namespace rig
