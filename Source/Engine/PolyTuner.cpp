#include "PolyTuner.h"

namespace rig
{
PolyTuner::PolyTuner()
    : ring ((size_t) fifo.getTotalSize(), 0.0f),
      history ((size_t) kWindow, 0.0f),
      work ((size_t) kFftSize * 2, 0.0f),
      window ((size_t) kWindow, 0.0f),
      mag ((size_t) kFftSize / 2, 0.0f)
{
    for (int i = 0; i < kWindow; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kWindow - 1));
}

const std::vector<PolyTuner::Tuning>& PolyTuner::tunings()
{
    static const std::vector<Tuning> t {
        { "Standard (E A D G B E)",         { 40, 45, 50, 55, 59, 64 } },
        { "Half step down (Eb)",            { 39, 44, 49, 54, 58, 63 } },
        { "Whole step down (D)",            { 38, 43, 48, 53, 57, 62 } },
        { "Drop D (D A D G B E)",           { 38, 45, 50, 55, 59, 64 } },
        { "Drop C# (C# G# C# F# A# D#)",    { 37, 44, 49, 54, 58, 63 } },
        { "Drop C (C G C F A D)",           { 36, 43, 48, 53, 57, 62 } },
        { "Drop B (B F# B E G# C#)",        { 35, 42, 47, 52, 56, 61 } },
        { "DADGAD",                         { 38, 45, 50, 55, 57, 62 } },
        { "Open G (D G D G B D)",           { 38, 43, 50, 55, 59, 62 } },
        { "Open D (D A D F# A D)",          { 38, 45, 50, 54, 57, 62 } },
        { "Open E (E B E G# B E)",          { 40, 47, 52, 56, 59, 64 } },
        { "7-string standard (B E A D G B E)", { 35, 40, 45, 50, 55, 59, 64 } },
        { "Bass (E A D G)",                 { 28, 33, 38, 43 } },
        { "Bass drop D (D A D G)",          { 26, 33, 38, 43 } },
        { "Bass half step down (Eb)",       { 27, 32, 37, 42 } },
        { "Bass whole step down (D G C F)", { 26, 31, 36, 41 } },
        { "5-string bass (B E A D G)",      { 23, 28, 33, 38, 43 } },
        { "6-string bass (B E A D G C)",    { 23, 28, 33, 38, 43, 48 } },
    };
    return t;
}

juce::String PolyTuner::noteName (int midi)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

void PolyTuner::prepare (double sr)
{
    rate = sr;
    decimation = juce::jmax (1, (int) std::round (sr / 12000.0));
    effectiveRate = sr / decimation;
    decimCounter = 0;
    decimAcc = 0;
    lpState = 0;
    fifo.reset();
}

void PolyTuner::push (const float* data, int n) noexcept
{
    // gently low-passed and decimated to ~12 kHz: the strings' first harmonics are all well below 3 kHz
    const float a = std::exp (-juce::MathConstants<float>::twoPi * 3000.0f / (float) rate);
    float tmp[512];
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        lpState = data[i] + a * (lpState - data[i]);
        decimAcc += lpState;
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

float PolyTuner::magAt (float hz) const noexcept
{
    const float bin = hz * (float) kFftSize / (float) effectiveRate;
    const int i = (int) bin;
    if (i < 1 || i + 2 >= (int) mag.size()) return 0.0f;
    // quadratic through the three nearest bins: smooth between the fine, zero-padded steps
    const float f = bin - (float) i;
    const float y0 = mag[(size_t) i - 1], y1 = mag[(size_t) i], y2 = mag[(size_t) i + 1];
    return juce::jmax (0.0f, y1 + 0.5f * f * (y2 - y0 + f * (y2 - 2.0f * y1 + y0)));
}

PolyTuner::Reading PolyTuner::analyse()
{
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

    const auto& t = tunings()[(size_t) juce::jlimit (0, (int) tunings().size() - 1, tuning.load())];
    const size_t numStrings = t.midi.size();
    if (last.size() != numStrings) { last.assign (numStrings, {}); holdFrames.assign (numStrings, 0); }

    Reading r;
    r.strings.resize (numStrings);

    float rms = 0;
    for (auto x : history) rms += x * x;
    rms = std::sqrt (rms / kWindow);

    if (rms > 0.002f)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < kWindow; ++i) work[(size_t) i] = history[(size_t) i] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        std::copy (work.begin(), work.begin() + (long) mag.size(), mag.begin());

        // a noise floor: the median of the useful range
        std::vector<float> band (mag.begin() + 10, mag.begin() + (long) std::min (mag.size(), (size_t) (kFftSize * 1500.0 / effectiveRate)));
        std::nth_element (band.begin(), band.begin() + (long) band.size() / 2, band.end());
        const float floorMag = band[band.size() / 2] + 1.0e-9f;

        const float refA = referenceA.load();
        std::vector<float> strength (numStrings, 0.0f);
        for (size_t s = 0; s < numStrings; ++s)
        {
            const float target = refA * std::pow (2.0f, (float) (t.midi[s] - 69) / 12.0f);
            // the fundamental and its first harmonics together: low strings are placed by their harmonics too
            // ...but not a harmonic that lands on another string or on one of its harmonics: the low E's 3rd
            // and 4th are B and high E, and strings a fourth apart share one (4 x E = 3 x A), which
            // would drag each reading towards its neighbour's
            float weights[] { 1.0f, 0.7f, 0.45f, 0.3f };
            for (int h = 1; h < 4; ++h)
                for (size_t j = 0; j < numStrings; ++j)
                    for (int k = 1; k <= 4 && j != s; ++k)
                        if (std::abs (12.0f * std::log2 ((float) (h + 1) * target
                                                          / ((float) k * refA * std::pow (2.0f, (float) (t.midi[j] - 69) / 12.0f)))) < 0.6f)
                            weights[h] = 0.0f;
            auto score = [&] (float f)
            {
                float sc = 0.0f;
                for (int h = 0; h < 4; ++h)
                    if (weights[h] > 0.0f && f * (float) (h + 1) < effectiveRate * 0.45f) sc += weights[h] * magAt (f * (float) (h + 1));
                return sc;
            };
            float bestC = 0.0f, best = -1.0f;
            for (float c = -60.0f; c <= 60.0f; c += 0.5f)
            {
                const float sc = score (target * std::pow (2.0f, c / 1200.0f));
                if (sc > best) { best = sc; bestC = c; }
            }
            // refine: a parabola through the best step and its neighbours
            const float a = score (target * std::pow (2.0f, (bestC - 0.5f) / 1200.0f));
            const float c2 = score (target * std::pow (2.0f, (bestC + 0.5f) / 1200.0f));
            const float denom = a - 2.0f * best + c2;
            if (std::abs (denom) > 1.0e-12f) bestC += 0.5f * 0.5f * (a - c2) / denom;

            auto& sr = r.strings[s];
            sr.cents = bestC;
            sr.frequency = target * std::pow (2.0f, bestC / 1200.0f);
            sr.note = noteName (t.midi[s]);
            // heard: a clear peak where the string's fundamental (or its octave) should be
            const float fund = juce::jmax (magAt (sr.frequency), 0.7f * magAt (sr.frequency * 2.0f));
            strength[s] = fund / floorMag;
            sr.heard = strength[s] > 12.0f && std::abs (bestC) < 59.0f;
        }
        const float strongest = *std::max_element (strength.begin(), strength.end());
        for (size_t s = 0; s < numStrings; ++s)
        {
            auto& sr = r.strings[s];
            sr.level = strongest > 0 ? juce::jlimit (0.0f, 1.0f, strength[s] / strongest) : 0.0f;
            sr.heard = sr.heard && sr.level > 0.06f;
        }
    }

    // smooth, and hold each string's last reading for a moment after it stops ringing
    for (size_t s = 0; s < numStrings; ++s)
    {
        auto& sr = r.strings[s];
        if (sr.note.isEmpty()) sr.note = noteName (t.midi[s]);
        if (sr.heard)
        {
            if (last[s].heard || last[s].held) sr.cents = last[s].cents + 0.5f * (sr.cents - last[s].cents);
            holdFrames[s] = 15;
            ++r.heardCount;
        }
        else if (holdFrames[s] > 0)
        {
            --holdFrames[s];
            const auto note = sr.note;
            sr = last[s];
            sr.heard = false;
            sr.held = true;
            sr.note = note;
        }
        last[s] = sr;
    }
    r.strummed = r.heardCount >= 3;
    return r;
}

} // namespace rig
