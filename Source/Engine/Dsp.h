#pragma once

#include "Common.h"

/** Small, allocation-free building blocks for the built-in effects. */
namespace rig::dsp
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

inline float fastTanh (float x) noexcept
{
    x = juce::jlimit (-4.5f, 4.5f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** One-pole low-pass / high-pass. */
struct OnePole
{
    float a = 0, z = 0;
    void setCutoff (double sr, double hz) noexcept { a = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (1.0, sr * 0.49, hz) / sr); }
    void setTime (double sr, double ms) noexcept { a = ms <= 0 ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * sr)); }
    float lp (float x) noexcept { z = x + a * (z - x); return z; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset (float v = 0) noexcept { z = v; }
};

/** Envelope follower with separate attack and release. */
struct Follower
{
    float env = 0, att = 0, rel = 0;
    void set (double sr, double attMs, double relMs) noexcept
    {
        att = (float) std::exp (-1.0 / (juce::jmax (0.01, attMs) * 0.001 * sr));
        rel = (float) std::exp (-1.0 / (juce::jmax (0.01, relMs) * 0.001 * sr));
    }
    float process (float x) noexcept
    {
        x = std::abs (x);
        env = x + (x > env ? att : rel) * (env - x);
        return env;
    }
};

/** LFO shapes: 0 sine, 1 triangle, 2 soft square, 3 saw-up. Phase in [0,1). Output -1..1. */
inline float wave (double ph, int shape) noexcept
{
    const float p = (float) (ph - std::floor (ph));
    switch (shape)
    {
        case 1:  return 1.0f - 4.0f * std::abs (p - 0.5f);
        case 2:  return fastTanh (std::sin (twoPi * p) * 6.0f);
        case 3:  return 2.0f * p - 1.0f;
        default: return std::sin (twoPi * p);
    }
}

struct Lfo
{
    double phase = 0, inc = 0;
    void setRate (double sr, double hz) noexcept { inc = hz / sr; }
    double tick() noexcept { phase += inc; if (phase >= 1.0) phase -= 1.0; return phase; }
};

/** Power-of-two circular buffer with fractional reads. */
struct DelayBuffer
{
    std::vector<float> data;
    int mask = 0, write = 0;

    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        data.assign ((size_t) n, 0.0f);
        mask = n - 1;
        write = 0;
    }
    void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); }
    void push (float x) noexcept { data[(size_t) write] = x; write = (write + 1) & mask; }

    /** delay measured from the most recently pushed sample (delay 0 = that sample) */
    float read (float delay) const noexcept
    {
        delay = juce::jlimit (0.0f, (float) (mask - 3), delay);
        const float pos = (float) (write - 1) - delay;
        const int i = (int) std::floor (pos);
        const float f = pos - (float) i;
        // 4-point Hermite
        const float xm1 = data[(size_t) ((i - 1) & mask)], x0 = data[(size_t) (i & mask)];
        const float x1 = data[(size_t) ((i + 1) & mask)], x2 = data[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    float readInt (int delay) const noexcept { return data[(size_t) ((write - 1 - delay) & mask)]; }
};

/** First-order allpass (phasers, vibes). */
struct AllPass1
{
    float a = 0, z = 0;
    static float coef (double sr, double hz) noexcept
    {
        const double t = std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, hz) / sr);
        return (float) ((t - 1.0) / (t + 1.0));
    }
    float process (float x) noexcept { const float y = a * x + z; z = x - a * y; return y; }
};

/** Schroeder allpass diffuser on a fixed delay. */
struct Diffuser
{
    DelayBuffer buf;
    int length = 1;
    float g = 0.6f;
    void prepare (int maxLen) { buf.prepare (maxLen); }
    float process (float x) noexcept
    {
        const float d = buf.readInt (length - 1);
        const float v = x + g * d;
        buf.push (v);
        return d - g * v;
    }
};

/** Topology-preserving state variable filter (per-sample cutoff changes are fine). */
struct Svf
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set (double sr, double hz, double q) noexcept
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, hz) / sr);
        k = (float) (1.0 / juce::jmax (0.1, q));
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void process (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0; }
};

/** Two-grain delay-line pitch shifter with correlation-aligned grain starts
    (a light WSOLA), so overlapping grains reinforce instead of cancelling.
    ratio = 2 for an octave up, 0.5 for an octave down. */
struct PitchShifter
{
    static constexpr int M = 96;   // samples compared when aligning a new grain
    DelayBuffer buf;
    float ratio = 1.0f;
    int L = 1920, S = 576;
    float d[2] { 0, 0 };
    int age[2] { 0, 0 };
    float ref[M] {};

    void prepare (double sr, float windowMs)
    {
        buf.prepare ((int) (sr * 0.3));
        S = (int) (sr * 0.012);
        setWindow (sr, windowMs);
    }

    void setWindow (double sr, float windowMs) noexcept
    {
        L = juce::jmax (64, (int) (sr * windowMs * 0.001));
        age[0] = 0; age[1] = L / 2;
        d[0] = d[1] = (float) base();
    }

    int base() const noexcept
    {
        return ratio > 1.0f ? (int) ((float) L * (ratio - 1.0f)) + M * 2 + 4 : (int) (M * ratio) + 4;
    }

    void restart (int h) noexcept
    {
        age[h] = 0;
        const int o = 1 - h;
        for (int k = 0; k < M; ++k) ref[k] = buf.read (d[o] - (float) k * ratio);
        const int b = base();
        int best = b;
        float bestScore = -1.0e30f;
        for (int c = b; c < b + S; c += 2)
        {
            float dot = 0, e = 1.0e-9f;
            for (int k = 0; k < M; ++k)
            {
                const float s = buf.read ((float) c - (float) k * ratio);
                dot += s * ref[k];
                e += s * s;
            }
            const float score = dot / std::sqrt (e);
            if (score > bestScore) { bestScore = score; best = c; }
        }
        d[h] = (float) best;
    }

    float process (float x) noexcept
    {
        buf.push (x);
        float y = 0;
        for (int h = 0; h < 2; ++h)
        {
            if (age[h] >= L) restart (h);
            const float w = std::sin (juce::MathConstants<float>::pi * (float) age[h] / (float) L);
            y += w * w * buf.read (d[h]);
            d[h] += 1.0f - ratio;
            ++age[h];
        }
        return y;
    }
};

} // namespace rig::dsp
