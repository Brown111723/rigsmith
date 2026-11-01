#include "Tuner.h"

namespace rig
{
Tuner::Tuner()
    : ring ((size_t) fifo.getTotalSize(), 0.0f),
      history ((size_t) kWindow, 0.0f),
      diff ((size_t) kWindow / 2, 0.0f)
{
}

void Tuner::prepare (double sr)
{
    rate = sr;
    decimation = juce::jmax (1, (int) std::round (sr / 48000.0));
    effectiveRate = sr / decimation;
    decimCounter = 0;
    decimAcc = 0;
    fifo.reset();
}

void Tuner::push (const float* data, int n) noexcept
{
    // Average-decimate to ~48 kHz so the analysis window always reaches low B (~31 Hz needs > 1500 samples).
    float tmp[512];
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        decimAcc += data[i];
        if (++decimCounter >= decimation)
        {
            tmp[count++] = decimAcc / (float) decimation;
            decimAcc = 0; decimCounter = 0;
            if (count == 512)
            {
                if (fifo.getFreeSpace() >= count)
                    { int k = 0; fifo.write (count).forEach ([&] (int idx) { ring[(size_t) idx] = tmp[k++]; }); }
                count = 0;
            }
        }
    }
    if (count > 0 && fifo.getFreeSpace() >= count)
        { int k = 0; fifo.write (count).forEach ([&] (int idx) { ring[(size_t) idx] = tmp[k++]; }); }
}

static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

Tuner::Reading Tuner::analyse()
{
    // Drain everything new, keeping only the latest window.
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        std::vector<float> incoming ((size_t) ready);
        { size_t k = 0; fifo.read (ready).forEach ([&] (int idx) { incoming[k++] = ring[(size_t) idx]; }); }
        if (ready >= kWindow)
            std::copy (incoming.end() - kWindow, incoming.end(), history.begin());
        else
        {
            std::move (history.begin() + ready, history.end(), history.begin());
            std::copy (incoming.begin(), incoming.end(), history.end() - ready);
        }
    }

    Reading r;
    float rms = 0;
    for (auto x : history) rms += x * x;
    rms = std::sqrt (rms / kWindow);
    if (rms < 0.003f)
    {
        smoothedFreq = 0;
        return r;
    }

    // YIN difference + cumulative mean normalised difference
    const int W = kWindow / 2;
    const int tauMin = juce::jmax (2, (int) (effectiveRate / 1400.0));
    const int tauMax = W - 1;
    diff[0] = 1.0f;
    float running = 0;
    for (int tau = 1; tau < W; ++tau)
    {
        float s = 0;
        for (int i = 0; i < W; ++i)
        {
            const float d = history[(size_t) i] - history[(size_t) (i + tau)];
            s += d * d;
        }
        running += s;
        diff[(size_t) tau] = running > 0 ? s * (float) tau / running : 1.0f;
    }

    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (diff[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
        return r;

    // parabolic interpolation
    float t = (float) best;
    if (best > 1 && best < tauMax - 1)
    {
        const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const float denom = a + c - 2 * b;
        if (std::abs (denom) > 1e-9f) t += 0.5f * (a - c) / denom;
    }

    const float freq = (float) effectiveRate / t;
    smoothedFreq = smoothedFreq > 0 && std::abs (freq / smoothedFreq - 1.0f) < 0.03f
                     ? smoothedFreq * 0.7f + freq * 0.3f : freq;

    const float midi = 69.0f + 12.0f * std::log2 (smoothedFreq / referenceA.load());
    const int nearest = (int) std::round (midi);
    r.valid = true;
    r.frequency = smoothedFreq;
    r.cents = (midi - (float) nearest) * 100.0f;
    r.note = noteNames[((nearest % 12) + 12) % 12];
    r.octave = nearest / 12 - 1;
    return r;
}

} // namespace rig
