#include "Pedals.h"

namespace rig
{
using PB = PedalBlock;

namespace
{
    PedalModel makeModel (const char* name, const char* maker, const char* year, const char* blurb,
                      std::vector<int> knobs, std::map<int, juce::String> labels, PedalCircuit c,
                      const char* voiceName = "", juce::StringArray voices = {})
    {
        PedalModel m;
        m.name = name; m.maker = maker; m.year = year; m.blurb = blurb;
        m.knobs = std::move (knobs); m.labels = std::move (labels); m.c = c;
        m.voiceName = voiceName; m.voices = std::move (voices);
        return m;
    }

    // a few circuits that turn up in both blocks
    PedalCircuit tubeScreamer()
    {
        PedalCircuit c;
        c.inHp = 20; c.clipHp = 720; c.dryThru = 1.0f;          // the famous mid hump: only the mids are clipped
        c.gainMinDb = 21; c.gainMaxDb = 41;
        c.clip = ClipKind::OpAmpSoft; c.threshold = 0.55f;
        c.bandwidth = 6000; c.tone = ToneKind::LowPassSweep; c.toneA = 720; c.toneB = 5500;
        c.makeupDb = -3.8f;
        return c;
    }
    PedalCircuit ocd()
    {
        PedalCircuit c;
        c.inHp = 45; c.gainMinDb = 10; c.gainMaxDb = 42;
        c.clip = ClipKind::Mosfet; c.threshold = 0.7f; c.asym = 1.25f;
        c.bandwidth = 9000; c.tone = ToneKind::LowPassSweep; c.toneA = 1200; c.toneB = 7000;
        c.makeupDb = -15.5f;
        return c;
    }
    PedalCircuit bluesbreaker()
    {
        PedalCircuit c;
        c.inHp = 25; c.clipHp = 160; c.dryThru = 0.6f;          // a gentler hump than the TS: more lows kept
        c.gainMinDb = 8; c.gainMaxDb = 32;
        c.clip = ClipKind::OpAmpSoft; c.threshold = 0.6f;
        c.bandwidth = 11000; c.tone = ToneKind::LowPassSweep; c.toneA = 1500; c.toneB = 11000;
        c.makeupDb = -8.3f;
        return c;
    }

    const std::map<int, juce::String> dtl { { PB::pDrive, "Drive" }, { PB::pTone, "Tone" }, { PB::pLevel, "Level" } };
}

const std::vector<PedalModel>& PedalBlock::overdriveModels()
{
    static const std::vector<PedalModel> m = []
    {
        std::vector<PedalModel> v;
        v.push_back (makeModel ("Tube Screamer (TS808 / TS9)", "Ibanez", "1979", "The king of overdrives: a mid hump that cuts through any mix and pushes an amp into singing sustain (SRV)",
                            { pDrive, pTone }, dtl, tubeScreamer(), "Version", { "TS808", "TS9" }));
        {
            PedalCircuit c;
            c.inHp = 30; c.clipHp = 400; c.gainMinDb = 4; c.gainMaxDb = 38;
            c.clip = ClipKind::HardGermanium; c.threshold = 0.35f;
            c.bandwidth = 9000; c.tone = ToneKind::ShelfTreble; c.toneA = 1200;
            c.cleanDrop = 0.75f; c.midPeakDb = 2.5f; c.midPeakHz = 1000; c.makeupDb = -2.8f;   // germanium drive blended with a clean boost that fades as the gain rises
            v.push_back (makeModel ("Klon Centaur", "Klon", "1994", "The mythical transparent overdrive: a clean boost blended with germanium clipping, so your amp still sounds like itself",
                                { pDrive, pTone }, { { pDrive, "Gain" }, { pTone, "Treble" }, { pLevel, "Output" } }, c));
        }
        {
            auto c = tubeScreamer();
            c.clipHp = 600; c.gainMinDb = 18; c.gainMaxDb = 44; c.asym = 1.9f; c.bandwidth = 7000; c.toneA = 900; c.toneB = 7000; c.makeupDb = -7.0f;
            v.push_back (makeModel ("Super OverDrive SD-1", "Boss", "1981", "Boss's answer to the Tube Screamer: asymmetric clipping for a more harmonic, rock crunch (EVH, Zakk Wylde)",
                                { pDrive, pTone }, dtl, c));
        }
        v.push_back (makeModel ("OCD (Obsessive Compulsive Drive)", "Fulltone", "2005", "Open, amp-like and touch-sensitive: cleans up beautifully from the guitar's volume knob",
                            { pDrive, pTone }, { { pDrive, "Drive" }, { pTone, "Tone" }, { pLevel, "Volume" } }, ocd(), "Mode", { "LP", "HP" }));
        {
            PedalCircuit c;
            c.inHp = 30; c.dryThru = 1.0f; c.gainMinDb = 6; c.gainMaxDb = 40;
            c.clip = ClipKind::OpAmpSoft; c.threshold = 0.55f;
            c.bandwidth = 14000; c.tone = ToneKind::Timmy; c.makeupDb = -12.0f;
            v.push_back (makeModel ("Timmy", "Paul Cochrane", "2000s", "The flat, transparent benchmark: bass cut before the gain and treble cut after, so it adds grit without changing your sound",
                                { pDrive, pBass, pTreble }, { { pDrive, "Gain" }, { pBass, "Bass" }, { pTreble, "Treble" }, { pLevel, "Volume" } },
                                c, "Clipping", { "Symmetric", "Asymmetric", "Open" }));
        }
        {
            PedalCircuit c;
            c.inHp = 30; c.clipHp = 300; c.gainMinDb = 6; c.gainMaxDb = 36;
            c.clip = ClipKind::HardSilicon; c.threshold = 0.45f;
            c.bandwidth = 10000; c.tone = ToneKind::ShelfTreble; c.toneA = 1500;
            c.cleanDrop = 0.6f; c.midPeakDb = 1.5f; c.midPeakHz = 1100; c.makeupDb = -5.4f;
            v.push_back (makeModel ("Soul Food", "Electro-Harmonix", "2014", "The Klon circuit for everyone: lots of clean headroom and sweet, touch-sensitive drive",
                                { pDrive, pTone }, { { pDrive, "Drive" }, { pTone, "Treble" }, { pLevel, "Volume" } }, c));
        }
        v.push_back (makeModel ("Morning Glory", "JHS", "2010s", "A Marshall Bluesbreaker-style drive with extra chime and high-end clarity for clean and edge-of-breakup tones",
                            { pDrive, pTone }, { { pDrive, "Drive" }, { pTone, "Tone" }, { pLevel, "Volume" } }, bluesbreaker(), "Gain", { "Lo", "Hi" }));
        {
            auto c = tubeScreamer();
            c.clipHp = 500; c.dryThru = 0.8f; c.gainMinDb = 20; c.gainMaxDb = 38; c.asym = 1.7f;
            c.tone = ToneKind::None; c.bandwidth = 3800; c.makeupDb = -7.5f;
            v.push_back (makeModel ("OD-1 OverDrive", "Boss", "1977", "The pedal that started it all: the first compact overdrive, smooth asymmetric clipping, no tone knob",
                                { pDrive }, { { pDrive, "Over Drive" }, { pLevel, "Level" } }, c));
        }
        v.push_back (makeModel ("King of Tone", "Analogman", "2003", "A refined Bluesbreaker: warm, open drive (Derek Trucks). Switch each side between overdrive, distortion and boost",
                            { pDrive, pTone }, { { pDrive, "Drive" }, { pTone, "Tone" }, { pLevel, "Volume" } }, bluesbreaker(), "Mode", { "Overdrive", "Distortion", "Boost" }));
        {
            PedalCircuit c;
            c.inHp = 20; c.clipHp = 100; c.dryThru = 0.8f; c.gainMinDb = 6; c.gainMaxDb = 32;
            c.clip = ClipKind::OpAmpSoft; c.threshold = 0.6f; c.asym = 1.2f;
            c.bandwidth = 13000; c.tone = ToneKind::Tilt; c.toneA = 900; c.makeupDb = -10.9f;
            v.push_back (makeModel ("Lightspeed", "Greer Amps", "2010s", "Organic, amp-like overdrive: rich, mild clipping that adds harmonics without changing your EQ",
                                { pDrive, pTone }, dtl, c));
        }
        return v;
    }();
    return m;
}

const std::vector<PedalModel>& PedalBlock::distortionModels()
{
    static const std::vector<PedalModel> m = []
    {
        std::vector<PedalModel> v;
        {
            PedalCircuit c;
            c.inHp = 30; c.gainMinDb = 28; c.gainMaxDb = 56;          // a transistor booster into an op-amp, diodes to ground
            c.clip = ClipKind::HardSilicon; c.threshold = 0.5f;
            c.bandwidth = 8000; c.tone = ToneKind::MuffBlend; c.toneA = 450; c.toneB = 1500; c.makeupDb = -13.0f;
            v.push_back (makeModel ("DS-1 Distortion", "Boss", "1978", "The famous orange box: abrasive hard clipping and tight sustain (Kurt Cobain, Joe Satriani)",
                                { pDrive, pTone }, { { pDrive, "Dist" }, { pTone, "Tone" }, { pLevel, "Level" } }, c, "Mode", { "Standard", "Ultra (DS-1W)" }));
        }
        {
            PedalCircuit c;
            c.inHp = 30; c.clipHp = 560; c.dryThru = 0.0f; c.gainMinDb = 6; c.gainMaxDb = 67;
            c.clip = ClipKind::HardSilicon; c.threshold = 0.55f;
            c.bandwidth = 18000; c.tone = ToneKind::RatFilter; c.toneA = 475; c.toneB = 32000; c.makeupDb = -6.7f;
            v.push_back (makeModel ("RAT 2", "Pro Co", "1978", "The shape-shifting fuzzstortion: from dirty boost to massive crunch. Filter turns the highs down (Jeff Beck, Dave Grohl)",
                                { pDrive, pTone }, { { pDrive, "Distortion" }, { pTone, "Filter" }, { pLevel, "Volume" } }, c,
                                "Version", { "RAT 2", "Turbo RAT", "You Dirty RAT" }));
        }
        v.push_back (makeModel ("Tube Screamer (TS808 / TS9)", "Ibanez", "1979", "The most-copied circuit in history: a mid hump that pushes an amp into a singing lead (SRV)",
                            { pDrive, pTone }, dtl, tubeScreamer(), "Version", { "TS808", "TS9" }));
        {
            PedalCircuit c;
            c.inHp = 40; c.gainMinDb = 30; c.gainMaxDb = 70;
            c.clip = ClipKind::Muff; c.threshold = 0.45f;
            c.bandwidth = 7000; c.tone = ToneKind::MuffBlend; c.toneA = 700; c.toneB = 1600; c.makeupDb = -12.1f;
            v.push_back (makeModel ("Big Muff Pi", "Electro-Harmonix", "1969", "Violin-like sustain and a wall of fuzz-distortion (David Gilmour, Billy Corgan, J Mascis)",
                                { pDrive, pTone }, { { pDrive, "Sustain" }, { pTone, "Tone" }, { pLevel, "Volume" } }, c,
                                "Version", { "Ram's Head", "NYC", "Triangle" }));
        }
        {
            PedalCircuit c;
            c.inHp = 60; c.gainMinDb = 34; c.gainMaxDb = 74;
            c.clip = ClipKind::HardSilicon; c.threshold = 0.5f;
            c.bandwidth = 9000; c.tone = ToneKind::ThreeBand; c.toneA = 100; c.toneB = 4000; c.eqStyle = 0; c.makeupDb = -14.7f;
            v.push_back (makeModel ("MT-2 Metal Zone", "Boss", "1991", "Extreme gain with a parametric mid: from scooped death metal to honking leads",
                                { pDrive, pBass, pMid, pMidFreq, pTreble },
                                { { pDrive, "Dist" }, { pBass, "Low" }, { pMid, "Middle" }, { pMidFreq, "Mid Freq" }, { pTreble, "High" }, { pLevel, "Level" } }, c));
        }
        {
            PedalCircuit c;
            c.inHp = 40; c.gainMinDb = 26; c.gainMaxDb = 56;
            c.clip = ClipKind::HardSilicon; c.threshold = 0.5f;
            c.bandwidth = 7500; c.tone = ToneKind::ColorMix; c.toneA = 100; c.toneB = 1100; c.makeupDb = -22.5f;
            v.push_back (makeModel ("HM-2 Heavy Metal", "Boss", "1983", "The chainsaw: everything on 10 is the Swedish death metal sound (Entombed, Dismember)",
                                { pDrive, pBass, pTreble }, { { pDrive, "Dist" }, { pBass, "Color Low" }, { pTreble, "Color High" }, { pLevel, "Level" } }, c,
                                "Version", { "HM-2", "HM-2W Custom" }));
        }
        {
            PedalCircuit c;
            c.inHp = 40; c.gainMinDb = 26; c.gainMaxDb = 60;
            c.clip = ClipKind::HardSilicon; c.threshold = 0.5f;
            c.bandwidth = 8500; c.tone = ToneKind::ThreeBand; c.toneA = 120; c.toneB = 2500; c.eqStyle = 1; c.makeupDb = -13.8f;
            v.push_back (makeModel ("Shredmaster", "Marshall", "1991", "Thick 90s alternative and thrash high gain (Jonny Greenwood on Creep). Contour scoops the mids",
                                { pDrive, pBass, pMid, pTreble }, { { pDrive, "Gain" }, { pBass, "Bass" }, { pMid, "Contour" }, { pTreble, "Treble" }, { pLevel, "Volume" } }, c));
        }
        v.push_back (makeModel ("OCD (Obsessive Compulsive Drive)", "Fulltone", "2005", "Amp-like overdrive into distortion that reacts to your pick and volume knob",
                            { pDrive, pTone }, { { pDrive, "Drive" }, { pTone, "Tone" }, { pLevel, "Volume" } }, ocd(), "Mode", { "LP", "HP" }));
        {
            PedalCircuit c;
            c.inHp = 30; c.gainMinDb = 0; c.gainMaxDb = 47;
            c.clip = ClipKind::HardGermanium; c.threshold = 0.3f;
            c.bandwidth = 4500; c.tone = ToneKind::None; c.makeupDb = -9.0f;
            v.push_back (makeModel ("Distortion+", "MXR", "1974", "The little yellow box: op-amp gain into germanium diodes, 70s hard rock and punk (Randy Rhoads)",
                                { pDrive }, { { pDrive, "Distortion" }, { pLevel, "Output" } }, c));
        }
        {
            PedalCircuit c;
            c.inHp = 40; c.gainMinDb = 26; c.gainMaxDb = 60;
            c.clip = ClipKind::Mosfet; c.threshold = 0.5f; c.asym = 1.1f;
            c.bandwidth = 9000; c.tone = ToneKind::ThreeBand; c.toneA = 100; c.toneB = 2500; c.eqStyle = 2;
            c.midPeakDb = 4.0f; c.midPeakHz = 800; c.makeupDb = -13.0f;   // the Plexi mids
            v.push_back (makeModel ("BE-OD", "Friedman", "2010s", "Plexi-in-a-box: turns a clean channel into a roaring, modded Marshall stack",
                                { pDrive, pTight, pBass, pTreble, pPresence },
                                { { pDrive, "Gain" }, { pTight, "Tight" }, { pBass, "Bass" }, { pTreble, "Treble" }, { pPresence, "Presence" }, { pLevel, "Volume" } }, c));
        }
        return v;
    }();
    return m;
}

//==============================================================================
static juce::StringArray modelNames (NodeType t)
{
    juce::StringArray s;
    for (auto& m : t == NodeType::Distortion ? PedalBlock::distortionModels() : PedalBlock::overdriveModels())
        s.add (m.maker + " " + m.name);
    return s;
}

PedalBlock::PedalBlock (NodeType t)
    : DriveBase (t, {
        P ("drive", "Drive", 0.0f, 10.0f, 5.0f).rand (2.0f, 9.0f),
        P ("tone", "Tone", 0.0f, 10.0f, 5.0f).rand (3.0f, 8.0f),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB"),
        P ("blend", "Clean Blend", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 30.0f),
        Choice ("model", "Model", modelNames (t), 0),
        P ("bass", "Bass", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("mid", "Mid", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("treble", "Treble", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("midfreq", "Mid Freq", 200.0f, 5000.0f, 800.0f, "Hz").mid (900.0f).rand (400.0f, 2000.0f),
        Choice ("voice", "Switch", { "A", "B", "C" }, 0),
        P ("tight", "Tight", 0.0f, 10.0f, 5.0f).rand (3.0f, 8.0f),
        P ("presence", "Presence", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f) })
{
    cur = circuit();
}

const PedalModel& PedalBlock::model() const
{
    const auto& ms = models();
    return ms[(size_t) juce::jlimit (0, (int) ms.size() - 1, getChoice (pModel))];
}

juce::String PedalBlock::getName() const { return getType() == NodeType::Distortion ? "Distortion" : "Overdrive"; }

juce::String PedalBlock::getDetail() const
{
    const auto& m = model();
    auto d = m.name.upToFirstOccurrenceOf (" (", false, false);
    const int v = getChoice (pVoice);
    if (m.name.startsWith ("Tube")) d = m.voices[juce::jlimit (0, 1, v)];
    else if (m.voices.size() > 1 && v > 0) d << "  " << m.voices[juce::jlimit (0, m.voices.size() - 1, v)];
    return d;
}

std::vector<int> PedalBlock::panelOrder() const
{
    const auto& m = model();
    std::vector<int> o { pModel };
    if (m.voices.size() > 1) o.push_back (pVoice);
    for (int k : m.knobs) o.push_back (k);
    o.push_back (pBlend);
    o.push_back (pLevel);
    return o;
}

juce::String PedalBlock::getParamName (int i) const
{
    const auto& m = model();
    if (i == pVoice && m.voiceName.isNotEmpty()) return m.voiceName;
    if (auto it = m.labels.find (i); it != m.labels.end()) return it->second;
    return Block::getParamName (i);
}

juce::StringArray PedalBlock::getChoiceNames (int i) const
{
    if (i == pVoice)
    {
        const auto& m = model();
        return m.voices.isEmpty() ? juce::StringArray { "-" } : m.voices;
    }
    return Block::getChoiceNames (i);
}

juce::String PedalBlock::formatValue (int i, float v) const
{
    if (i == pVoice)
    {
        const auto n = getChoiceNames (i);
        return n[juce::jlimit (0, n.size() - 1, juce::roundToInt (v))];
    }
    return Block::formatValue (i, v);
}

PedalCircuit PedalBlock::circuit() const noexcept
{
    const auto& m = model();
    auto c = m.c;
    const int v = juce::jlimit (0, juce::jmax (0, m.voices.size() - 1), getChoice (pVoice));
    const auto& n = m.name;
    if (n.startsWith ("Tube Screamer") && v == 1)   { c.bandwidth = 7500; c.toneB = 6500; c.midPeakDb = -1.0f; c.makeupDb -= 0.7f; }      // TS9: brighter, a touch thinner
    else if (n.startsWith ("OCD") && v == 1)        { c.inHp = 25; c.gainMaxDb += 6; c.toneB = 9500; c.makeupDb -= 1.0f; }               // HP: more bass, gain, output
    else if (n.startsWith ("Timmy"))
    {
        if (v == 1) { c.asym = 1.7f; c.makeupDb -= 1.5f; }
        if (v == 2) { c.threshold = 1.1f; c.dryThru = 1.0f; c.makeupDb -= 3.0f; }
    }
    else if (n.startsWith ("Morning") && v == 1)    { c.gainMaxDb += 8; c.gainMinDb += 4; c.makeupDb -= 2.7f; }
    else if (n.startsWith ("King"))
    {
        if (v == 1) { c.clip = ClipKind::HardSilicon; c.dryThru = 0.0f; c.gainMinDb += 6; c.gainMaxDb += 8; c.threshold = 0.5f; c.makeupDb -= 1.8f; }
        if (v == 2) { c.gainMaxDb = 18; c.threshold = 1.3f; c.makeupDb += 1.7f; }
    }
    else if (n.startsWith ("DS-1") && v == 1)       { c.lowShelfDb = 4.0f; c.toneA = 320; c.inHp = 20; c.makeupDb -= 1.4f; }
    else if (n.startsWith ("RAT"))
    {
        if (v == 1) { c.clip = ClipKind::HardLed; c.threshold = 1.4f; c.makeupDb -= 6.2f; }
        if (v == 2) { c.clip = ClipKind::HardGermanium; c.threshold = 0.32f; c.asym = 1.4f; c.makeupDb += 3.8f; }
    }
    else if (n.startsWith ("Big Muff"))
    {
        if (v == 1) { c.toneA = 550; c.toneB = 2100; }
        if (v == 2) { c.toneA = 950; c.toneB = 1200; c.gainMaxDb -= 4; }
    }
    else if (n.startsWith ("HM-2") && v == 1)       { c.gainMaxDb += 6; c.toneA = 80; c.toneB = 1400; c.makeupDb += 1.8f; }
    return c;
}

//==============================================================================
float PedalBlock::clipSample (float v, const PedalCircuit& c) noexcept
{
    const float th = c.threshold;
    auto knee = [] (float x, float t, float n)   // x / (1 + |x/t|^n)^(1/n): a diode's rounded corner
    {
        return x / std::pow (1.0f + std::pow (std::abs (x / t), n), 1.0f / n);
    };
    switch (c.clip)
    {
        case ClipKind::OpAmpSoft:
        case ClipKind::Mosfet:
        {
            const float t = v >= 0.0f ? th : th * c.asym;
            return c.clip == ClipKind::Mosfet ? knee (v, t, 1.4f) : t * dsp::fastTanh (v / t);
        }
        case ClipKind::HardSilicon:   return knee (v, v >= 0.0f ? th : th * c.asym, 2.5f);
        case ClipKind::HardGermanium: return knee (v, v >= 0.0f ? th : th * c.asym, 1.6f);
        case ClipKind::HardLed:       return knee (v, th, 3.0f);
        case ClipKind::Muff:          return knee (v, th, 2.0f);
    }
    return v;
}

void PedalBlock::design (const PedalCircuit& c) noexcept
{
    const double fs = osRate;
    const float t = get (pTone) / 10.0f;
    const float bass = get (pBass) / 10.0f, mid = get (pMid) / 10.0f, treble = get (pTreble) / 10.0f;
    gainLin = dbToGain (c.gainMinDb + (c.gainMaxDb - c.gainMinDb) * get (pDrive) / 10.0f);
    makeup = dbToGain (c.makeupDb);

    double inHz = c.inHp;
    if (c.tone == ToneKind::Timmy) inHz = 750.0 * std::pow (30.0 / 750.0, (double) bass);          // the Timmy's bass knob cuts before the gain
    if (model().knobs.end() != std::find (model().knobs.begin(), model().knobs.end(), (int) pTight))
        inHz = 30.0 + 270.0 * get (pTight) / 10.0f;                                                // BE-OD's Tight
    inHp.setHighPass (fs, inHz, 0.707);

    useClipHp = c.clipHp > 0.0f;
    if (useClipHp) clipHp.setHighPass (fs, c.clipHp, 0.5);
    bw.setLowPass (fs, c.bandwidth, 0.6);
    // a RAT's op-amp runs out of bandwidth as the gain goes up
    useBw2 = c.tone == ToneKind::RatFilter;
    if (useBw2) bw2.setLowPass (fs, juce::jlimit (1600.0, 20000.0, 350000.0 / juce::jmax (1.0f, gainLin)), 0.707);

    usePre = c.tone == ToneKind::ColorMix;
    switch (c.tone)
    {
        case ToneKind::LowPassSweep:
            for (auto& o : toneLp) o.setCutoff (fs, c.toneA * std::pow (c.toneB / c.toneA, t));
            break;
        case ToneKind::Tilt:
            for (auto& o : toneLp) o.setCutoff (fs, c.toneA);
            toneMix = t;
            break;
        case ToneKind::ShelfTreble:
            eqHigh.setHighShelf (fs, c.toneA, (t - 0.5f) * 20.0f);
            break;
        case ToneKind::MuffBlend:
            for (auto& o : toneLp)  o.setCutoff (fs, c.toneA);
            for (auto& o : toneLp2) o.setCutoff (fs, c.toneB);
            toneMix = t;
            break;
        case ToneKind::RatFilter:
            for (auto& o : toneLp) o.setCutoff (fs, c.toneB * std::pow (c.toneA / c.toneB, t));
            break;
        case ToneKind::ThreeBand:
            if (c.eqStyle == 0)   // MT-2: everything +-15 dB, a sweepable mid
            {
                eqLow.setLowShelf (fs, c.toneA, (bass - 0.5f) * 30.0f);
                eqMid.setPeak (fs, get (pMidFreq), 0.9, (mid - 0.5f) * 30.0f);
                eqHigh.setHighShelf (fs, c.toneB, (treble - 0.5f) * 30.0f);
                eqPres.setIdentity();
            }
            else if (c.eqStyle == 1)   // Shredmaster: the contour only scoops
            {
                eqLow.setLowShelf (fs, c.toneA, (bass - 0.5f) * 24.0f);
                eqMid.setPeak (fs, 650.0, 0.8, -mid * 16.0f);
                eqHigh.setHighShelf (fs, c.toneB, (treble - 0.5f) * 24.0f);
                eqPres.setIdentity();
            }
            else   // BE-OD: bass, treble, presence
            {
                eqLow.setLowShelf (fs, c.toneA, (bass - 0.5f) * 24.0f);
                eqMid.setIdentity();
                eqHigh.setHighShelf (fs, c.toneB, (treble - 0.5f) * 24.0f);
                eqPres.setHighShelf (fs, 5000.0, (get (pPresence) / 10.0f - 0.5f) * 20.0f);
            }
            break;
        case ToneKind::ColorMix:   // HM-2: two big resonant boosts, before and after the clipping
            eqLow.setPeak (fs, c.toneA, 0.9, bass * 18.0f);
            eqHigh.setPeak (fs, c.toneB, 1.3, treble * 18.0f);
            preLow.setPeak (fs, c.toneA, 0.9, bass * 9.0f);
            preHigh.setPeak (fs, c.toneB, 1.3, treble * 9.0f);
            break;
        case ToneKind::Timmy:
            for (auto& o : toneLp) o.setCutoff (fs, 1500.0 * std::pow (16000.0 / 1500.0, (double) treble));
            break;
        case ToneKind::None: break;
    }
    useFixLow = std::abs (c.lowShelfDb) > 0.05f;
    if (useFixLow) fixLow.setLowShelf (fs, 100.0, c.lowShelfDb);
    useFixMid = std::abs (c.midPeakDb) > 0.05f;
    if (useFixMid) fixMid.setPeak (fs, c.midPeakHz, 0.8, c.midPeakDb);
}

float PedalBlock::shape (float x) const
{
    const auto c = circuit();
    const float g = dbToGain (c.gainMinDb + (c.gainMaxDb - c.gainMinDb) * get (pDrive) / 10.0f) * 0.05f;
    float y = c.clip == ClipKind::Muff ? clipSample (clipSample (x * std::sqrt (g * 20.0f), c) * std::sqrt (g * 20.0f), c)
                                       : clipSample (x * g * 20.0f, c);
    y += c.dryThru * x;
    return juce::jlimit (-1.0f, 1.0f, y / juce::jmax (0.2f, c.threshold * 1.2f));
}

void PedalBlock::prepareImpl (double sr, int block)
{
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing ((size_t) block);
    os->reset();
    osRate = sr * 4.0;
    for (auto* b : { &inHp, &clipHp, &bw, &bw2, &eqLow, &eqMid, &eqHigh, &eqPres, &preLow, &preHigh, &fixLow, &fixMid }) b->reset();
    for (int ch = 0; ch < 2; ++ch) { toneLp[ch].reset(); toneLp2[ch].reset(); muffHp[ch].reset(); muffHp[ch].setCutoff (osRate, 100.0); }
    dry.setSize (2, block, false, true, true);
    watch.invalidate();
}

void PedalBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ (float) getChoice (pModel), (float) getChoice (pVoice), get (pDrive), get (pTone), get (pBass), get (pMid),
                         get (pTreble), get (pMidFreq), get (pTight), get (pPresence) }))
    {
        cur = circuit();
        design (cur);
    }
    const auto& c = cur;
    const float level = dbToGain (get (pLevel)) * makeup;
    const float blend = get (pBlend) * 0.01f;
    const float muffStage = std::sqrt (gainLin);
    const float klonClean = c.cleanDrop > 0.0f ? 1.0f - c.cleanDrop * get (pDrive) / 10.0f : 0.0f;
    float inPk = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        dry.copyFrom (ch, 0, b, ch, 0, n);
        inPk = juce::jmax (inPk, b.getMagnitude (ch, 0, n));
    }

    float* ptrs[2] = { b.getWritePointer (0), b.getWritePointer (1) };
    juce::dsp::AudioBlock<float> blk (ptrs, 2, (size_t) n);
    auto up = os->processSamplesUp (blk);
    for (size_t chs = 0; chs < 2; ++chs)
    {
        const int ch = (int) chs;
        auto* d = up.getChannelPointer (chs);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            float x = inHp.process (d[i], ch);
            if (usePre) x = preHigh.process (preLow.process (x, ch), ch);
            float y;
            if (c.clip == ClipKind::Muff)
            {
                // two transistor clipping stages with a coupling filter between them
                const float s1 = clipSample (x * muffStage, c);
                y = clipSample (muffHp[ch].hp (s1) * muffStage, c);
            }
            else
            {
                const float v = (useClipHp ? clipHp.process (x, ch) : x) * gainLin;
                y = clipSample (v, c) + c.dryThru * x;
            }
            y = bw.process (y, ch);
            if (useBw2) y = bw2.process (y, ch);

            switch (c.tone)
            {
                case ToneKind::LowPassSweep:
                case ToneKind::RatFilter:
                case ToneKind::Timmy:        y = toneLp[ch].lp (y); break;
                case ToneKind::Tilt:         { const float lp = toneLp[ch].lp (y); y = lp * (1.4f - 0.8f * toneMix) + (y - lp) * (2.0f * toneMix); break; }
                case ToneKind::ShelfTreble:  y = eqHigh.process (y, ch); break;
                case ToneKind::MuffBlend:    { const float lo = toneLp[ch].lp (y); const float hi = y - toneLp2[ch].lp (y); y = (lo * (1.0f - toneMix) + hi * toneMix) * 1.8f; break; }
                case ToneKind::ThreeBand:    y = eqPres.process (eqHigh.process (eqMid.process (eqLow.process (y, ch), ch), ch), ch); break;
                case ToneKind::ColorMix:     y = eqHigh.process (eqLow.process (y, ch), ch); break;
                case ToneKind::None:         break;
            }
            if (useFixLow) y = fixLow.process (y, ch);
            if (useFixMid) y = fixMid.process (y, ch);
            d[i] = y;
        }
    }
    os->processSamplesDown (blk);

    // the latency of the oversampling is small (a few samples); the clean paths line up closely enough
    float outPk = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        auto* cl = dry.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float y = d[i] + cl[i] * klonClean * 1.5f;
            y = (y * (1.0f - blend) + cl[i] * blend * 2.0f) * level;
            d[i] = y;
            outPk = juce::jmax (outPk, std::abs (y));
        }
    }
    updateMeters (inPk, outPk, 0.0f);
}

} // namespace rig
