#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <memory>
#include <vector>
#include <functional>
#include <array>
#include <map>
#include <complex>

namespace rig
{
inline constexpr int kNumScenes = 4;
inline constexpr int kMaxBranches = 6;

inline float dbToGain (float db) { return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (g); }

/** RT-safe biquad (RBJ cookbook), transposed direct form II, stereo state. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    inline float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    /** Magnitude response in dB at a frequency (for drawing curves). */
    double magnitudeDb (double f, double sr) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * f / sr;
        const std::complex<double> e1 = std::polar (1.0, -w), e2 = e1 * e1;
        const auto num = (double) b0 + (double) b1 * e1 + (double) b2 * e2;
        const auto den = 1.0 + (double) a1 * e1 + (double) a2 * e2;
        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (num / den)));
    }

    void setBandPass (double sr, double f, double q) noexcept
    {
        const double w = omega (sr, f), al = std::sin (w) / (2 * q), c = std::cos (w);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }

    void setPeak (double sr, double f, double q, double db) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = omega (sr, f), al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void setLowShelf (double sr, double f, double db, double s = 0.8) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = omega (sr, f), c = std::cos (w);
        const double al = std::sin (w) / 2.0 * std::sqrt ((A + 1 / A) * (1 / s - 1) + 2), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }

    void setHighShelf (double sr, double f, double db, double s = 0.8) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = omega (sr, f), c = std::cos (w);
        const double al = std::sin (w) / 2.0 * std::sqrt ((A + 1 / A) * (1 / s - 1) + 2), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }

    void setHighPass (double sr, double f, double q = 0.7071) noexcept
    {
        const double w = omega (sr, f), al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void setLowPass (double sr, double f, double q = 0.7071) noexcept
    {
        const double w = omega (sr, f), al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

private:
    static double omega (double sr, double f) noexcept
    {
        return juce::MathConstants<double>::twoPi * juce::jlimit (5.0, sr * 0.49, f) / sr;
    }

    void set (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
};

/** Coefficients are only recomputed when a watched value actually changes. */
struct ChangeWatch
{
    float last[8] { -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f };
    bool changed (std::initializer_list<float> v) noexcept
    {
        bool c = false; int i = 0;
        for (auto x : v) { if (last[i] != x) { last[i] = x; c = true; } ++i; }
        return c;
    }
    void invalidate() noexcept { for (auto& l : last) l = -1e9f; }
};
} // namespace rig
