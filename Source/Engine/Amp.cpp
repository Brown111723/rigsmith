#include "Amp.h"

namespace rig
{
using dsp::fastTanh;

static inline float peak2 (const float* l, const float* r, int n) noexcept
{
    return juce::jmax (juce::FloatVectorOperations::findMaximum (l, n), -juce::FloatVectorOperations::findMinimum (l, n),
                       juce::FloatVectorOperations::findMaximum (r, n), -juce::FloatVectorOperations::findMinimum (r, n));
}

//==============================================================================
// The tone stack (Yeh & Smith, "Discretization of the '59 Fender Bassman Tone Stack", DAFx 2006)
//==============================================================================
namespace
{
struct StackPoly { double b1, b2, b3, a0, a1, a2, a3; };

StackPoly stackPoly (const StackParts& p, double t, double m, double l) noexcept
{
    const double R1 = p.R1, R2 = p.R2, R3 = p.R3, R4 = p.R4, C1 = p.C1, C2 = p.C2, C3 = p.C3;
    StackPoly s;
    s.b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    s.b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
         + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
         + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const double c123 = C1 * C2 * C3;
    s.b3 = l * m * (c123 * R1 * R2 * R3 + c123 * R2 * R3 * R4) - m * m * (c123 * R1 * R3 * R3 + c123 * R3 * R3 * R4)
         + m * (c123 * R1 * R3 * R3 + c123 * R3 * R3 * R4) + t * c123 * R1 * R3 * R4 - t * m * c123 * R1 * R3 * R4
         + t * l * c123 * R1 * R2 * R4;
    s.a0 = 1.0;
    s.a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    s.a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
         + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    s.a3 = l * m * (c123 * R1 * R2 * R3 + c123 * R2 * R3 * R4) - m * m * (c123 * R1 * R3 * R3 + c123 * R3 * R3 * R4)
         + m * (c123 * R3 * R3 * R4 + c123 * R1 * R3 * R3 - c123 * R1 * R3 * R4) + l * c123 * R1 * R2 * R4 + c123 * R1 * R3 * R4;
    return s;
}

/** The bass pot is log taper; treble and mid are linear. Kept off the very ends, where the
    circuit's maths goes singular. */
void tapers (double treble, double mid, double bass, double& t, double& m, double& l) noexcept
{
    t = juce::jlimit (0.001, 0.999, treble);
    m = juce::jlimit (0.001, 0.999, mid);
    l = juce::jlimit (0.001, 0.999, std::exp ((bass - 1.0) * 3.4));
}
}

void ToneStack::design (const StackParts& p, double fs, double treble, double mid, double bass) noexcept
{
    double t, m, l;
    tapers (treble, mid, bass, t, m, l);
    const auto s = stackPoly (p, t, m, l);
    const double c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
    const double B0 = -s.b1 * c - s.b2 * c2 - s.b3 * c3;
    const double B1 = -s.b1 * c + s.b2 * c2 + 3.0 * s.b3 * c3;
    const double B2 = s.b1 * c + s.b2 * c2 - 3.0 * s.b3 * c3;
    const double B3 = s.b1 * c - s.b2 * c2 + s.b3 * c3;
    const double A0 = -s.a0 - s.a1 * c - s.a2 * c2 - s.a3 * c3;
    const double A1 = -3.0 * s.a0 - s.a1 * c + s.a2 * c2 + 3.0 * s.a3 * c3;
    const double A2 = -3.0 * s.a0 + s.a1 * c + s.a2 * c2 - 3.0 * s.a3 * c3;
    const double A3 = -s.a0 + s.a1 * c - s.a2 * c2 + s.a3 * c3;
    b[0] = B0 / A0; b[1] = B1 / A0; b[2] = B2 / A0; b[3] = B3 / A0;
    a[0] = 1.0; a[1] = A1 / A0; a[2] = A2 / A0; a[3] = A3 / A0;
}

double ToneStack::magnitude (const StackParts& p, double hz, double treble, double mid, double bass) noexcept
{
    double t, m, l;
    tapers (treble, mid, bass, t, m, l);
    const auto s = stackPoly (p, t, m, l);
    const std::complex<double> jw (0.0, juce::MathConstants<double>::twoPi * hz);
    const auto num = s.b1 * jw + s.b2 * jw * jw + s.b3 * jw * jw * jw;
    const auto den = s.a0 + s.a1 * jw + s.a2 * jw * jw + s.a3 * jw * jw * jw;
    return std::abs (num / den);
}

//==============================================================================
// The amps
//==============================================================================
namespace
{
// tone stack parts (treble pot, bass pot, mid pot, slope resistor; C1, C2, C3)
StackParts parts (double R1, double R2, double R3, double R4, double C1p, double C2n, double C3n)
{
    return { R1, R2, R3, R4, C1p * 1e-12, C2n * 1e-9, C3n * 1e-9 };
}
const StackParts bassmanStack = parts (250e3, 1e6, 25e3, 56e3, 250, 20, 20);
const StackParts twinStack    = parts (250e3, 250e3, 10e3, 100e3, 250, 100, 47);
const StackParts deluxeStack  = parts (250e3, 250e3, 6.8e3, 100e3, 250, 100, 47);
const StackParts plexiStack   = parts (220e3, 1e6, 22e3, 33e3, 500, 22, 22);
const StackParts jtm45Stack   = parts (250e3, 1e6, 25e3, 33e3, 270, 22, 22);
const StackParts jcm800Stack  = parts (220e3, 1e6, 22e3, 33e3, 470, 22, 22);
const StackParts voxStack     = parts (1e6, 1e6, 10e3, 100e3, 50, 22, 22);
const StackParts mesaStack    = parts (250e3, 250e3, 25e3, 100e3, 250, 100, 47);
const StackParts soldanoStack = parts (250e3, 1e6, 25e3, 47e3, 470, 20, 20);
const StackParts peaveyStack  = parts (250e3, 1e6, 25e3, 39e3, 470, 22, 22);
const StackParts hiwattStack  = parts (250e3, 1e6, 25e3, 56e3, 330, 22, 22);
const StackParts orangeStack  = parts (250e3, 1e6, 25e3, 56e3, 470, 22, 47);
const StackParts dumbleStack  = parts (250e3, 250e3, 25e3, 100e3, 500, 22, 22);
const StackParts ampegStack   = parts (1e6, 1e6, 25e3, 47e3, 500, 47, 47);

using A = AmpBlock;

AmpVoice V (const juce::String& n) { AmpVoice v; v.name = n; return v; }
AmpVoice V (const juce::String& n, float gain, float bright = 0, float tight = 1, float scoop = 0, float low = 0, float high = 0, float fbMul = 1)
{
    AmpVoice v; v.name = n; v.gainAddDb = gain; v.brightAddDb = bright; v.tightMul = tight; v.scoopDb = scoop;
    v.lowAddDb = low; v.highAddDb = high; v.feedbackMul = fbMul;
    return v;
}

std::vector<AmpModel> makeModels()
{
    std::vector<AmpModel> v;
    auto add = [&v] (const juce::String& section, const juce::String& maker, const juce::String& name, const juce::String& year,
                     const juce::String& blurb, std::vector<int> knobs, std::map<int, juce::String> labels,
                     std::vector<std::pair<int, float>> stock, AmpCircuit c, juce::String voiceName = {}, std::vector<AmpVoice> voices = {})
    {
        AmpModel m;
        m.section = section; m.maker = maker; m.name = name; m.year = year; m.blurb = blurb;
        m.bass = section == "Bass";
        m.knobs = std::move (knobs); m.labels = std::move (labels); m.stock = std::move (stock); m.c = c;
        m.voiceName = voiceName; m.voices = std::move (voices);
        v.push_back (std::move (m));
    };

    //--- Studio ---------------------------------------------------------------
    {
        AmpCircuit c;
        c.stages = 4; c.firstDb = 8; c.gainMinDb = -26; c.gainMaxDb = 30; c.laterDb = 6; c.stageHp = 60; c.stageLp = 8000;
        c.cathodeDb = -4; c.asym = 1.4f; c.blocking = 0.4f; c.parts = jcm800Stack; c.paMinDb = -14; c.paMaxDb = 10;
        c.feedback = 0.4f; c.sag = 0.6f;
        add ("Studio", "Rigsmith", "Studio", "",
             "Every control: clean to high gain on the Gain knob, the tone stack of your choice, and the power amp's Presence, Depth, Master and Sag",
             { A::pVoice, A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pDepth, A::pMaster, A::pBright, A::pTight, A::pSag }, {},
             { { A::pGain, 5 }, { A::pBass, 5 }, { A::pMid, 5 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pDepth, 4 }, { A::pMaster, 5 },
               { A::pBright, 0 }, { A::pTight, 3 }, { A::pSag, 4 }, { A::pVoice, 0 } },
             c, "Tone stack", { V ("Marshall"), V ("Fender"), V ("Vox"), V ("Mesa"), V ("Active EQ") });
    }

    //--- Clean ----------------------------------------------------------------
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 4; c.gainMinDb = -30; c.gainMaxDb = 8; c.laterDb = -2; c.stageHp = 20; c.stageLp = 12000;
        c.asym = 1.2f; c.blocking = 0.2f; c.brightDb = 0; c.stackEarly = true; c.parts = twinStack;
        c.paMinDb = -16; c.paMaxDb = 4; c.feedback = 0.6f; c.sag = 0.25f; c.otLow = 40; c.otHigh = 12000; c.loadDb = 1.5f;
        add ("Clean", "Fender", "'65 Twin Reverb", "1965",
             "85 watts of blackface headroom: the sparkling, glassy clean of a thousand studios. Stays clean almost all the way up",
             { A::pVoice, A::pGain, A::pTreble, A::pMid, A::pBass }, { { A::pGain, "Volume" }, { A::pMid, "Middle" } },
             { { A::pGain, 4 }, { A::pTreble, 6 }, { A::pMid, 5 }, { A::pBass, 4 }, { A::pMaster, 5 }, { A::pPresence, 3 }, { A::pDepth, 3 }, { A::pSag, 5 }, { A::pVoice, 1 } },
             c, "Bright", { V ("Off"), V ("On", 0, 7) });
    }
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 6; c.gainMinDb = -26; c.gainMaxDb = 16; c.laterDb = 3; c.stageHp = 20; c.stageLp = 10000;
        c.asym = 1.3f; c.blocking = 0.35f; c.stackEarly = true; c.parts = deluxeStack; c.fixedMid = 0.6f;
        c.paMinDb = -8; c.paMaxDb = 10; c.feedback = 0.4f; c.classA = 0.15f; c.sag = 0.55f; c.otLow = 50; c.otHigh = 10000; c.loadDb = 2.5f;
        add ("Clean", "Fender", "'65 Deluxe Reverb", "1965",
             "22 watts that sweeten as they break up: clean at 3, juicy edge-of-breakup by 5, and cranked rock'n'roll at 8",
             { A::pGain, A::pTreble, A::pBass }, { { A::pGain, "Volume" } },
             { { A::pGain, 4 }, { A::pTreble, 6 }, { A::pBass, 4 }, { A::pMaster, 5 }, { A::pPresence, 3 }, { A::pDepth, 3 }, { A::pSag, 5 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 8; c.gainMinDb = -26; c.gainMaxDb = 16; c.laterDb = 0; c.stageHp = 25; c.stageLp = 14000;
        c.asym = 1.45f; c.blocking = 0.3f; c.stackEarly = true; c.parts = voxStack; c.fixedMid = 0.5f;
        c.paMinDb = -4; c.paMaxDb = 12; c.feedback = 0.0f; c.classA = 0.6f; c.sag = 0.5f; c.otLow = 60; c.otHigh = 13000; c.loadDb = 4;
        add ("Clean", "Vox", "AC30 Top Boost", "1960",
             "Class A EL84 chime: bright, jangly cleans that bloom into a rich, compressed breakup (The Beatles, Brian May, The Edge)",
             { A::pVoice, A::pGain, A::pTreble, A::pBass, A::pPresence }, { { A::pGain, "Volume" }, { A::pPresence, "Cut" } },
             { { A::pGain, 5 }, { A::pTreble, 6 }, { A::pBass, 4 }, { A::pPresence, 3 }, { A::pMaster, 6 }, { A::pDepth, 3 }, { A::pSag, 5 } },
             c, "Channel", { V ("Top Boost"), V ("Normal", -4, 0, 1, 0, 1, -4) });
    }
    {
        AmpCircuit c;
        c.solidState = true; c.stages = 2; c.firstDb = 2; c.gainMinDb = -30; c.gainMaxDb = 8; c.laterDb = -6; c.stageHp = 20; c.stageLp = 16000;
        c.stack = StackKind::Active; c.paMinDb = -18; c.paMaxDb = -2; c.feedback = 0.8f; c.sag = 0.0f; c.otLow = 30; c.otHigh = 16000; c.loadDb = 0;
        add ("Clean", "Roland", "JC-120 Jazz Chorus", "1975",
             "Solid-state and spotless: the hi-fi clean of 80s pop and new wave, with no breakup at all until it clips hard",
             { A::pVoice, A::pGain, A::pTreble, A::pMid, A::pBass }, { { A::pGain, "Volume" }, { A::pMid, "Middle" } },
             { { A::pGain, 4 }, { A::pTreble, 6 }, { A::pMid, 5 }, { A::pBass, 5 }, { A::pMaster, 5 }, { A::pPresence, 5 }, { A::pDepth, 5 } },
             c, "Bright", { V ("Off"), V ("On", 0, 6) });
    }
    {
        AmpCircuit c;
        c.stages = 4; c.firstDb = 6; c.gainMinDb = -24; c.gainMaxDb = 12; c.laterDb = 1; c.stageHp = 40; c.stageLp = 9000;
        c.asym = 1.35f; c.blocking = 0.35f; c.stackEarly = true; c.parts = dumbleStack;
        c.paMinDb = -12; c.paMaxDb = 8; c.feedback = 0.55f; c.sag = 0.35f; c.otLow = 45; c.otHigh = 11000;
        add ("Clean", "Dumble", "Overdrive Special", "1980s",
             "The holy grail of boutique amps: a warm, touch-sensitive clean, and a smooth, singing overdrive channel (Robben Ford, Larry Carlton)",
             { A::pVoice, A::pGain, A::pTreble, A::pMid, A::pBass, A::pPresence, A::pMaster }, { { A::pGain, "Volume" }, { A::pGain, "Volume" } },
             { { A::pGain, 5 }, { A::pTreble, 5 }, { A::pMid, 6 }, { A::pBass, 4 }, { A::pPresence, 4 }, { A::pMaster, 5 }, { A::pDepth, 3 }, { A::pSag, 4 } },
             c, "Channel", { V ("Clean"), V ("Overdrive", 22, 0, 2.0f, 0, -1, 0) });
    }

    //--- Crunch ---------------------------------------------------------------
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 9; c.gainMinDb = -24; c.gainMaxDb = 18; c.laterDb = 2; c.stageHp = 25; c.stageLp = 9000;
        c.asym = 1.35f; c.blocking = 0.45f; c.parts = bassmanStack;
        c.paMinDb = -4; c.paMaxDb = 12; c.feedback = 0.35f; c.sag = 0.6f; c.otLow = 45; c.otHigh = 10000; c.loadDb = 2.5f;
        add ("Crunch", "Fender", "'59 Bassman", "1959",
             "The tweed that started rock: fat, raw and spongy, the blueprint for the Marshall (Buddy Holly, Mike Bloomfield)",
             { A::pVoice, A::pGain, A::pTreble, A::pMid, A::pBass, A::pPresence }, { { A::pGain, "Volume" }, { A::pMid, "Middle" } },
             { { A::pGain, 6 }, { A::pTreble, 6 }, { A::pMid, 5 }, { A::pBass, 3 }, { A::pPresence, 5 }, { A::pMaster, 6 }, { A::pDepth, 3 }, { A::pSag, 6 } },
             c, "Input", { V ("Normal"), V ("Bright", 0, 5), V ("Jumped", 3, 3) });
    }
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 10; c.gainMinDb = -22; c.gainMaxDb = 18; c.laterDb = 3; c.stageHp = 30; c.stageLp = 8500;
        c.asym = 1.4f; c.blocking = 0.45f; c.parts = jtm45Stack;
        c.paMinDb = -4; c.paMaxDb = 12; c.feedback = 0.35f; c.sag = 0.55f; c.otLow = 50; c.otHigh = 10000;
        add ("Crunch", "Marshall", "JTM45", "1963",
             "The first Marshall: a warm, loose, bluesy crunch with a tube rectifier's sag (early Clapton, Gary Moore's blues)",
             { A::pVoice, A::pGain, A::pTreble, A::pMid, A::pBass, A::pPresence }, { { A::pGain, "Volume" }, { A::pMid, "Middle" } },
             { { A::pGain, 6 }, { A::pTreble, 6 }, { A::pMid, 6 }, { A::pBass, 4 }, { A::pPresence, 5 }, { A::pMaster, 6 }, { A::pDepth, 4 }, { A::pSag, 6 } },
             c, "Input", { V ("Normal"), V ("Bright", 0, 5), V ("Jumped", 3, 3) });
    }
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 10; c.gainMinDb = -20; c.gainMaxDb = 22; c.laterDb = 4; c.stageHp = 30; c.stageLp = 9000;
        c.cathodeDb = -6; c.shelfHz = 400; c.asym = 1.45f; c.blocking = 0.5f; c.parts = plexiStack;
        c.paMinDb = -2; c.paMaxDb = 14; c.feedback = 0.3f; c.sag = 0.35f; c.otLow = 50; c.otHigh = 10000;
        add ("Crunch", "Marshall", "1959 Super Lead \"Plexi\"", "1967",
             "100 watts of classic rock roar: cranked, it's Hendrix, Page and early Van Halen. Jump the inputs for both channels at once",
             { A::pVoice, A::pGain, A::pTreble, A::pMid, A::pBass, A::pPresence }, { { A::pGain, "Volume" }, { A::pMid, "Middle" } },
             { { A::pGain, 7 }, { A::pTreble, 6 }, { A::pMid, 7 }, { A::pBass, 3 }, { A::pPresence, 6 }, { A::pMaster, 7 }, { A::pDepth, 4 }, { A::pSag, 4 }, { A::pVoice, 2 } },
             c, "Input", { V ("Normal", -3, 0, 0.6f), V ("Bright", 0, 5), V ("Jumped", 3, 3) });
    }
    {
        AmpCircuit c;
        c.stages = 4; c.firstDb = 10; c.gainMinDb = -18; c.gainMaxDb = 24; c.laterDb = 7; c.stageHp = 80; c.stageLp = 7500;
        c.cathodeDb = -8; c.shelfHz = 350; c.asym = 1.6f; c.blocking = 0.5f; c.parts = jcm800Stack;
        c.paMinDb = -14; c.paMaxDb = 10; c.feedback = 0.35f; c.sag = 0.3f; c.otLow = 55; c.otHigh = 10000;
        add ("Crunch", "Marshall", "JCM800 2203", "1981",
             "The master-volume Marshall: tight, aggressive, mid-forward rock and metal (Slash, Zakk Wylde, Kerry King). Push it with an overdrive",
             { A::pVoice, A::pGain, A::pMaster, A::pTreble, A::pMid, A::pBass, A::pPresence }, { { A::pGain, "Pre-amp" }, { A::pMid, "Middle" } },
             { { A::pGain, 6 }, { A::pMaster, 5 }, { A::pTreble, 6 }, { A::pMid, 6 }, { A::pBass, 4 }, { A::pPresence, 6 }, { A::pDepth, 4 }, { A::pSag, 3 } },
             c, "Input", { V ("High"), V ("Low", -6) });
    }
    {
        AmpCircuit c;
        c.stages = 3; c.firstDb = 6; c.gainMinDb = -20; c.gainMaxDb = 18; c.laterDb = 0; c.stageHp = 30; c.stageLp = 12000;
        c.asym = 1.25f; c.blocking = 0.3f; c.parts = hiwattStack;
        c.paMinDb = -14; c.paMaxDb = 8; c.feedback = 0.6f; c.sag = 0.15f; c.otLow = 40; c.otHigh = 12000;
        add ("Crunch", "Hiwatt", "DR103", "1970",
             "Military-grade headroom: huge, clear and punchy even loud, the sound of Pete Townshend and David Gilmour",
             { A::pGain, A::pTreble, A::pMid, A::pBass, A::pPresence, A::pMaster }, { { A::pGain, "Normal Vol" }, { A::pMid, "Middle" } },
             { { A::pGain, 5 }, { A::pTreble, 6 }, { A::pMid, 5 }, { A::pBass, 5 }, { A::pPresence, 5 }, { A::pMaster, 6 }, { A::pDepth, 4 }, { A::pSag, 3 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 4; c.firstDb = 10; c.gainMinDb = -16; c.gainMaxDb = 24; c.laterDb = 4; c.stageHp = 70; c.stageLp = 7000;
        c.asym = 1.5f; c.blocking = 0.45f; c.parts = orangeStack; c.voiceMidDb = 3; c.voiceMidHz = 600;
        c.paMinDb = -14; c.paMaxDb = 10; c.feedback = 0.25f; c.sag = 0.35f; c.otLow = 50; c.otHigh = 9000;
        add ("Crunch", "Orange", "Rockerverb 50", "2003",
             "Thick, woolly British grind with a big low-mid push: stoner rock to indie (Jim Root, Jonny Greenwood's Orange cousins)",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pMaster }, { { A::pMaster, "Volume" } },
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 6 }, { A::pTreble, 5 }, { A::pMaster, 5 }, { A::pPresence, 4 }, { A::pDepth, 4 }, { A::pSag, 4 } }, c);
    }

    //--- High gain ------------------------------------------------------------
    {
        AmpCircuit c;
        c.stages = 4; c.firstDb = 12; c.gainMinDb = -14; c.gainMaxDb = 26; c.laterDb = 5; c.stageHp = 90; c.stageLp = 7000;
        c.cathodeDb = -6; c.asym = 1.5f; c.blocking = 0.45f; c.parts = jcm800Stack;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.35f; c.sag = 0.3f; c.otLow = 55; c.otHigh = 10000;
        add ("High gain", "Friedman", "BE-100", "2010",
             "The hot-rodded Marshall perfected: huge, articulate modern rock with a tight low end (Jerry Cantrell, Steve Stevens)",
             { A::pVoice, A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pMaster }, {},
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 6 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pMaster, 5 }, { A::pDepth, 5 }, { A::pSag, 3 } },
             c, "Mode", { V ("BE"), V ("HBE", 5), V ("BE + Tight", 0, 0, 2.5f) });
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 10; c.gainMinDb = -12; c.gainMaxDb = 26; c.laterDb = 3; c.stageHp = 140; c.stageLp = 6500;
        c.cathodeDb = -6; c.asym = 1.45f; c.blocking = 0.4f; c.stackEarly = true; c.parts = mesaStack;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.45f; c.sag = 0.2f; c.otLow = 55; c.otHigh = 9500;
        add ("High gain", "Mesa/Boogie", "Mark IIC+", "1984",
             "Liquid, singing lead and the definitive thrash rhythm (Metallica's Master of Puppets, Petrucci). The V on the graphic EQ is half the sound",
             { A::pVoice, A::pGain, A::pTreble, A::pBass, A::pMid, A::pPresence, A::pMaster }, { { A::pGain, "Lead Drive" }, { A::pMid, "Middle" } },
             { { A::pGain, 7 }, { A::pTreble, 7 }, { A::pBass, 3 }, { A::pMid, 4 }, { A::pPresence, 5 }, { A::pMaster, 5 }, { A::pDepth, 4 }, { A::pSag, 2 }, { A::pVoice, 1 } },
             c, "Graphic EQ", { V ("Flat"), V ("V scoop", 0, 0, 1, -8, 3, 3) });
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -10; c.gainMaxDb = 28; c.laterDb = 4; c.stageHp = 110; c.stageLp = 6000;
        c.asym = 1.5f; c.blocking = 0.5f; c.parts = mesaStack;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.4f; c.sag = 0.55f; c.otLow = 50; c.otHigh = 9500; c.loadDb = 3;
        add ("High gain", "Mesa/Boogie", "Dual Rectifier", "1992",
             "The 90s nu-metal wall of sound: a saggy, massive low end and fizzing gain. Modern mode is the scooped one",
             { A::pVoice, A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pMaster }, {},
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 4 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pMaster, 5 }, { A::pDepth, 5 }, { A::pSag, 6 }, { A::pVoice, 2 } },
             c, "Mode", { V ("Raw", -4), V ("Vintage", -2, 0, 1, 0, 0, 0, 1.4f), V ("Modern", 2, 0, 1.3f, -4, 1, 2, 0.6f) });
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -12; c.gainMaxDb = 26; c.laterDb = 4; c.stageHp = 100; c.stageLp = 6500;
        c.cathodeDb = -6; c.asym = 1.5f; c.blocking = 0.45f; c.parts = soldanoStack;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.35f; c.sag = 0.3f; c.otLow = 55; c.otHigh = 10000;
        add ("High gain", "Soldano", "SLO-100", "1987",
             "The smooth, vocal high-gain lead that defined the late 80s (Clapton's Journeyman, Lukather, Mark Knopfler's Money for Nothing)",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pMaster }, { { A::pGain, "Preamp" } },
             { { A::pGain, 6 }, { A::pBass, 4 }, { A::pMid, 7 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pMaster, 5 }, { A::pDepth, 4 }, { A::pSag, 3 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -8; c.gainMaxDb = 30; c.laterDb = 5; c.stageHp = 150; c.stageLp = 6000;
        c.asym = 1.55f; c.blocking = 0.5f; c.parts = peaveyStack; c.voiceMidDb = 4; c.voiceMidHz = 800;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.35f; c.sag = 0.25f; c.otLow = 60; c.otHigh = 10000;
        add ("High gain", "Peavey", "5150 / 6505", "1992",
             "The metal standard: aggressive, mid-heavy and tight, from Eddie Van Halen to every metalcore record since",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pDepth, A::pPresence, A::pMaster },
             { { A::pGain, "Pre Gain" }, { A::pBass, "Low" }, { A::pTreble, "High" }, { A::pDepth, "Resonance" }, { A::pMaster, "Post Gain" } },
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 5 }, { A::pTreble, 6 }, { A::pDepth, 5 }, { A::pPresence, 5 }, { A::pMaster, 4 }, { A::pSag, 3 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -6; c.gainMaxDb = 32; c.laterDb = 5; c.stageHp = 180; c.stageLp = 6500;
        c.asym = 1.5f; c.blocking = 0.45f; c.parts = peaveyStack; c.voiceMidDb = 2; c.voiceMidHz = 900; c.voiceHighDb = 1;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.4f; c.sag = 0.25f; c.otLow = 60; c.otHigh = 10500;
        add ("High gain", "EVH", "5150III (Red)", "2007",
             "Eddie's own: tighter and clearer than the 5150, with endless gain and articulation for modern metal",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pDepth, A::pPresence, A::pMaster },
             { { A::pBass, "Low" }, { A::pTreble, "High" }, { A::pDepth, "Resonance" }, { A::pMaster, "Volume" } },
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 5 }, { A::pTreble, 6 }, { A::pDepth, 5 }, { A::pPresence, 5 }, { A::pMaster, 4 }, { A::pSag, 3 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -6; c.gainMaxDb = 32; c.laterDb = 5; c.stageHp = 110; c.stageLp = 5500;
        c.asym = 1.5f; c.blocking = 0.5f; c.parts = soldanoStack; c.voiceLowDb = 2; c.voiceMidDb = -2; c.voiceMidHz = 500;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.3f; c.sag = 0.3f; c.otLow = 50; c.otHigh = 9000;
        add ("High gain", "Bogner", "Uberschall", "2002",
             "A brutal, thick, crushing wall of gain: dark and enormous in the lows",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pDepth, A::pMaster },
             {},
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 5 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pDepth, 5 }, { A::pMaster, 4 }, { A::pSag, 3 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -8; c.gainMaxDb = 30; c.laterDb = 5; c.stageHp = 150; c.stageLp = 6500;
        c.asym = 1.45f; c.blocking = 0.4f; c.parts = soldanoStack; c.voiceMidDb = -3; c.voiceMidHz = 600; c.voiceHighDb = 2;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.5f; c.sag = 0.2f; c.otLow = 55; c.otHigh = 11000;
        add ("High gain", "ENGL", "Powerball", "1998",
             "German precision metal: modern, scooped and razor-tight, with a hi-fi clarity at full gain",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pDepth, A::pMaster }, { { A::pGain, "Lead Gain" } },
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 5 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pDepth, 4 }, { A::pMaster, 4 }, { A::pSag, 2 } }, c);
    }
    {
        AmpCircuit c;
        c.stages = 5; c.firstDb = 12; c.gainMinDb = -8; c.gainMaxDb = 30; c.laterDb = 5; c.stageHp = 130; c.stageLp = 6000;
        c.asym = 1.5f; c.blocking = 0.45f; c.parts = soldanoStack; c.voiceMidDb = 2; c.voiceMidHz = 700;
        c.paMinDb = -12; c.paMaxDb = 10; c.feedback = 0.45f; c.sag = 0.25f; c.otLow = 55; c.otHigh = 10000;
        add ("High gain", "Diezel", "VH4 (channel 3)", "1994",
             "Thick, focused, singing German high gain with a deep, controlled low end (Metallica's Load era, Adam Jones)",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence, A::pDepth, A::pMaster }, { { A::pMid, "Middle" }, { A::pDepth, "Deep" } },
             { { A::pGain, 6 }, { A::pBass, 5 }, { A::pMid, 6 }, { A::pTreble, 6 }, { A::pPresence, 5 }, { A::pDepth, 5 }, { A::pMaster, 4 }, { A::pSag, 3 } }, c);
    }

    //--- Bass -----------------------------------------------------------------
    {
        AmpCircuit c;
        c.stages = 3; c.inputHp = 12; c.firstDb = 8; c.gainMinDb = -24; c.gainMaxDb = 16; c.laterDb = 0; c.stageHp = 12; c.stageLp = 9000;
        c.asym = 1.3f; c.blocking = 0.3f; c.stackEarly = true; c.parts = ampegStack;
        c.paMinDb = -12; c.paMaxDb = 8; c.feedback = 0.4f; c.sag = 0.35f; c.otLow = 20; c.otHigh = 9000; c.depthHz = 60; c.loadDb = 1.5f;
        add ("Bass", "Ampeg", "SVT-CL", "1969",
             "300 tube watts: the rock bass sound. Round, punchy and growling when pushed (The Rolling Stones to Metallica)",
             { A::pVoice, A::pGain, A::pBass, A::pMid, A::pTreble, A::pMaster }, {},
             { { A::pGain, 5 }, { A::pBass, 6 }, { A::pMid, 5 }, { A::pTreble, 5 }, { A::pMaster, 5 }, { A::pPresence, 4 }, { A::pDepth, 4 }, { A::pSag, 4 } },
             c, "Switch", { V ("Flat"), V ("Ultra Lo", 0, 0, 1, -6, 4, 0), V ("Ultra Hi", 0, 0, 1, 0, 0, 6) });
    }
    {
        AmpCircuit c;
        c.stages = 2; c.inputHp = 15; c.firstDb = 8; c.gainMinDb = -24; c.gainMaxDb = 16; c.laterDb = 0; c.stageHp = 15; c.stageLp = 6500;
        c.asym = 1.3f; c.blocking = 0.35f; c.stackEarly = true; c.parts = bassmanStack; c.fixedMid = 0.5f;
        c.paMinDb = -6; c.paMaxDb = 10; c.feedback = 0.3f; c.sag = 0.5f; c.otLow = 30; c.otHigh = 7000; c.depthHz = 70;
        add ("Bass", "Ampeg", "B-15N Portaflex", "1960",
             "The Motown and Stax bass sound: warm, woody and round, on James Jamerson's records",
             { A::pGain, A::pBass, A::pTreble }, { { A::pGain, "Volume" } },
             { { A::pGain, 5 }, { A::pBass, 6 }, { A::pTreble, 4 }, { A::pMaster, 5 }, { A::pPresence, 3 }, { A::pDepth, 4 }, { A::pSag, 5 } }, c);
    }
    {
        AmpCircuit c;
        c.solidState = true; c.stages = 2; c.inputHp = 12; c.firstDb = 2; c.gainMinDb = -26; c.gainMaxDb = 12; c.laterDb = -4;
        c.stageHp = 12; c.stageLp = 14000; c.stack = StackKind::Active;
        c.paMinDb = -16; c.paMaxDb = 0; c.feedback = 0.8f; c.sag = 0.0f; c.otLow = 18; c.otHigh = 15000; c.loadDb = 0;
        add ("Bass", "Gallien-Krueger", "800RB", "1983",
             "Solid-state punch and growl with bite: the 80s and 90s rock and slap sound (Flea, Cliff Burton's tone)",
             { A::pVoice, A::pGain, A::pBass, A::pMid, A::pTreble, A::pPresence }, { { A::pGain, "Volume" }, { A::pPresence, "Presence" } },
             { { A::pGain, 5 }, { A::pBass, 6 }, { A::pMid, 4 }, { A::pTreble, 6 }, { A::pPresence, 6 }, { A::pMaster, 5 }, { A::pDepth, 5 } },
             c, "Contour", { V ("Flat"), V ("Contour", 0, 0, 1, -7, 2, 2) });
    }
    {
        AmpCircuit c;
        c.stages = 3; c.inputHp = 12; c.firstDb = 10; c.gainMinDb = -20; c.gainMaxDb = 20; c.laterDb = 2; c.stageHp = 15; c.stageLp = 8000;
        c.asym = 1.4f; c.blocking = 0.4f; c.parts = orangeStack;
        c.paMinDb = -10; c.paMaxDb = 10; c.feedback = 0.2f; c.sag = 0.45f; c.otLow = 25; c.otHigh = 9000; c.depthHz = 60;
        add ("Bass", "Orange", "AD200", "2000s",
             "A simple, huge all-tube bass head: fat, warm and gritty when pushed",
             { A::pGain, A::pBass, A::pMid, A::pTreble, A::pMaster }, { { A::pMaster, "Volume" } },
             { { A::pGain, 5 }, { A::pBass, 6 }, { A::pMid, 5 }, { A::pTreble, 5 }, { A::pMaster, 5 }, { A::pPresence, 4 }, { A::pDepth, 4 }, { A::pSag, 5 } }, c);
    }
    return v;
}

juce::StringArray modelChoiceNames()
{
    juce::StringArray s;
    for (auto& m : AmpBlock::models()) s.add (m.section == "Studio" ? juce::String ("Studio") : m.maker + " " + m.name);
    return s;
}

inline float tube (float u, float asym) noexcept
{
    return u >= 0.0f ? fastTanh (u) : asym * fastTanh (u / asym);
}

inline float transistor (float u) noexcept
{
    // clean until close to the rails, then flat: (1 + u^8)^(1/8)
    const float u2 = u * u, u4 = u2 * u2;
    return u / std::sqrt (std::sqrt (std::sqrt (1.0f + u4 * u4)));
}
}

const std::vector<AmpModel>& AmpBlock::models()
{
    static const auto m = makeModels();
    return m;
}

int AmpBlock::firstBassModel()
{
    const auto& ms = models();
    for (int i = 0; i < (int) ms.size(); ++i) if (ms[(size_t) i].bass) return i;
    return 0;
}

AmpBlock::AmpBlock()
    : DriveBase (NodeType::Amp, {
        Choice ("model", "Model", modelChoiceNames(), 0).fixed(),
        P ("gain", "Gain", 0.0f, 10.0f, 5.0f).rand (3.0f, 9.0f),
        P ("bass", "Bass", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("mid", "Mid", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("treble", "Treble", 0.0f, 10.0f, 6.0f).rand (4.0f, 8.0f),
        P ("presence", "Presence", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("depth", "Depth", 0.0f, 10.0f, 4.0f).rand (2.0f, 7.0f),
        P ("master", "Master", 0.0f, 10.0f, 5.0f).rand (3.0f, 7.0f),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB"),
        Choice ("voice", "Switch", { "A", "B", "C", "D", "E" }, 0),
        P ("bright", "Bright", 0.0f, 10.0f, 0.0f).rand (0.0f, 5.0f),
        P ("tight", "Tight", 0.0f, 10.0f, 3.0f).rand (0.0f, 7.0f),
        P ("sag", "Sag", 0.0f, 10.0f, 4.0f).rand (2.0f, 7.0f) })
{
    for (auto& [p, value] : model().stock) set (p, value);
    cur = circuit();
}

void AmpBlock::onChoiceChanged (int param)
{
    if (param == pModel)
    {
        set (pVoice, 0.0f);
        for (auto& [p, value] : model().stock) set (p, value);
    }
}

const AmpModel& AmpBlock::model() const
{
    const auto& ms = models();
    return ms[(size_t) juce::jlimit (0, (int) ms.size() - 1, getChoice (pModel))];
}

juce::String AmpBlock::getName() const
{
    const auto& m = model();
    return m.section == "Studio" ? juce::String ("Amp") : m.name;
}

juce::String AmpBlock::getDetail() const
{
    const auto& m = model();
    juce::String d = m.section == "Studio" ? juce::String ("Studio") : m.maker;
    if (m.voices.size() > 1) d << "  " << m.voices[(size_t) juce::jlimit (0, (int) m.voices.size() - 1, getChoice (pVoice))].name;
    return d;
}

std::vector<int> AmpBlock::panelOrder() const
{
    const auto& m = model();
    std::vector<int> o { pModel };
    for (int k : m.knobs) o.push_back (k);
    o.push_back (pLevel);
    return o;
}

juce::String AmpBlock::getParamName (int i) const
{
    const auto& m = model();
    if (i == pVoice && m.voiceName.isNotEmpty()) return m.voiceName;
    if (auto it = m.labels.find (i); it != m.labels.end()) return it->second;
    return Block::getParamName (i);
}

juce::StringArray AmpBlock::getChoiceNames (int i) const
{
    if (i == pVoice)
    {
        juce::StringArray s;
        for (auto& v : model().voices) s.add (v.name);
        return s.isEmpty() ? juce::StringArray { "-" } : s;
    }
    return Block::getChoiceNames (i);
}

std::vector<std::pair<int, juce::String>> AmpBlock::getChoiceSections (int i) const
{
    std::vector<std::pair<int, juce::String>> s;
    if (i != pModel) return s;
    juce::String last;
    const auto& ms = models();
    for (int k = 0; k < (int) ms.size(); ++k)
        if (ms[(size_t) k].section != last && ms[(size_t) k].section != "Studio") { last = ms[(size_t) k].section; s.push_back ({ k, last }); }
    return s;
}

juce::String AmpBlock::formatValue (int i, float v) const
{
    if (i == pVoice)
    {
        const auto n = getChoiceNames (i);
        return n[juce::jlimit (0, n.size() - 1, juce::roundToInt (v))];
    }
    return Block::formatValue (i, v);
}

AmpCircuit AmpBlock::circuit() const noexcept
{
    const auto& m = model();
    auto c = m.c;
    if (! m.voices.empty())
    {
        const int vi = juce::jlimit (0, (int) m.voices.size() - 1, getChoice (pVoice));
        if (m.section == "Studio")
        {
            // Studio's switch is the tone stack
            switch (vi)
            {
                case 1: c.parts = twinStack; break;
                case 2: c.parts = voxStack; c.fixedMid = -1; break;
                case 3: c.parts = mesaStack; break;
                case 4: c.stack = StackKind::Active; break;
                default: break;
            }
        }
        else
        {
            const auto& v = m.voices[(size_t) vi];
            c.gainMinDb += v.gainAddDb; c.gainMaxDb += v.gainAddDb;
            c.brightDb += v.brightAddDb;
            c.inputHp *= v.tightMul;
            c.stageHp *= v.tightMul > 1.0f ? std::sqrt (v.tightMul) : 1.0f;
            if (v.scoopDb != 0.0f) { c.voiceMidDb += v.scoopDb; c.voiceMidHz = 650.0f; }
            c.voiceLowDb += v.lowAddDb; c.voiceHighDb += v.highAddDb;
            c.feedback = juce::jlimit (0.0f, 0.85f, c.feedback * v.feedbackMul);
        }
    }
    return c;
}

int AmpBlock::getLatency() const noexcept
{
    return mono4x != nullptr ? juce::roundToInt (mono4x->getLatencyInSamples()) : 0;
}

void AmpBlock::prepareImpl (double sr, int block)
{
    mono4x = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    mono4x->initProcessing ((size_t) block);
    mono4x->reset();
    monoBuf.setSize (1, block, false, true, true);
    osRate = sr * 4.0;
    for (auto& s : st) { s.shelf.reset(); s.hp.reset(); s.lp.reset(); s.charge = 0; }
    for (auto* f : { &inHp, &brightHp, &presHp, &depthLp, &otHp, &otLp, &dcOut }) f->reset();
    for (auto* b : { &activeLow, &activeMid, &activeHigh, &voiceLow, &voiceMid, &voiceHigh, &load }) b->reset();
    stack.reset();
    sagEnv = 0; lastOut = 0;
    sagAtt = std::exp (-1.0f / (0.008f * (float) osRate));
    sagRel = std::exp (-1.0f / (0.15f * (float) osRate));
    blockRel = std::exp (-1.0f / (0.04f * (float) osRate));
    watch.invalidate();
}

void AmpBlock::design (const AmpCircuit& c) noexcept
{
    const double fs = osRate;
    const float g = get (pGain) / 10.0f;
    const float tight = get (pTight) / 10.0f;
    const bool studio = model().section == "Studio";

    // input coupling, raised by Tight (Studio's knob; a model's own switch sets tightMul)
    const float tightHz = c.inputHp * (studio ? std::pow (12.0f, tight) : 1.0f);
    inHp.setCutoff (fs, tightHz);

    for (int k = 0; k < 5; ++k)
    {
        auto& s = st[k];
        s.gain = dbToGain (k == 0 ? c.firstDb : c.laterDb);
        s.cathode = k < 2 ? dbToGain (c.cathodeDb) : 1.0f;
        s.shelf.setCutoff (fs, c.shelfHz);
        s.hp.setCutoff (fs, k == 0 ? 25.0 : (double) (studio ? c.stageHp * std::pow (3.0f, tight) : c.stageHp));
        s.lp.setCutoff (fs, k == 0 ? 15000.0 : (double) c.stageLp);
    }

    // the gain pot, and the bright cap across it (all treble at low gain, none flat out)
    const float potDb = c.gainMinDb + (c.gainMaxDb - c.gainMinDb) * g;
    gainPot = dbToGain (potDb);
    const float brightDb = c.brightDb + (studio ? get (pBright) * 1.2f : 0.0f);
    brightAmt = brightDb > 0.0f ? (dbToGain (brightDb) - 1.0f) * (1.0f - g) : 0.0f;
    brightHp.setCutoff (fs, 1800.0);

    // tone stack: passive, its loss made up at the knobs' middle settings
    if (c.stack == StackKind::Fmv)
    {
        const double mid = c.fixedMid >= 0.0f ? c.fixedMid : get (pMid) / 10.0;
        stack.design (c.parts, fs, get (pTreble) / 10.0, mid, get (pBass) / 10.0);
        stackNorm = (float) (1.0 / juce::jmax (1.0e-4, ToneStack::magnitude (c.parts, 700.0, 0.5, c.fixedMid >= 0.0f ? c.fixedMid : 0.5, 0.5)));
    }
    else
    {
        activeLow.setLowShelf (fs, 110.0, (get (pBass) - 5.0f) * 2.6f);
        activeMid.setPeak (fs, 650.0, 0.7, (get (pMid) - 5.0f) * 2.4f);
        activeHigh.setHighShelf (fs, 3200.0, (get (pTreble) - 5.0f) * 2.6f);
        stackNorm = 1.0f;
    }

    useVoiceLow = std::abs (c.voiceLowDb) > 0.05f;
    useVoiceMid = std::abs (c.voiceMidDb) > 0.05f;
    useVoiceHigh = std::abs (c.voiceHighDb) > 0.05f;
    if (useVoiceLow) voiceLow.setLowShelf (fs, 120.0, c.voiceLowDb);
    if (useVoiceMid) voiceMid.setPeak (fs, c.voiceMidHz, 0.8, c.voiceMidDb);
    if (useVoiceHigh) voiceHigh.setHighShelf (fs, 3000.0, c.voiceHighDb);

    // the power amp. Feedback lowers its gain: that's made up so it only changes the feel.
    beta = c.feedback;
    const float masterDb = c.paMinDb + (c.paMaxDb - c.paMinDb) * get (pMaster) / 10.0f;
    paDrive = dbToGain (masterDb) * (1.0f + beta);
    presAmt = get (pPresence) / 10.0f * 1.6f;
    depthAmt = get (pDepth) / 10.0f * 1.6f;
    presHp.setCutoff (fs, c.presenceHz);
    depthLp.setCutoff (fs, c.depthHz);
    sagAmt = c.sag * get (pSag) / 10.0f;
    otHp.setCutoff (fs, c.otLow);
    otLp.setCutoff (fs, c.otHigh);

    // the speaker load: the power amp follows its impedance bump, less so with more feedback
    useLoad = c.loadDb > 0.05f && beta < 0.8f;
    if (useLoad) load.setPeak (sampleRate, model().bass ? 70.0 : 110.0, 1.2, c.loadDb * (1.0f - beta));
    dcOut.setCutoff (sampleRate, 8.0);

    // level: the master and half the gain pot are made up, so the knobs change the sound more than
    // the volume; the rest is the measured trim that sits each amp at its stock settings at unity
    const float refPot = c.gainMinDb + (c.gainMaxDb - c.gainMinDb) * 0.5f;
    const int vi = juce::jlimit (0, juce::jmax (0, (int) model().voices.size() - 1), getChoice (pVoice));
    makeup = dbToGain (-masterDb - 0.5f * (potDb - refPot) + ampTrimDb (getChoice (pModel), vi));
}

void AmpBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ (float) getChoice (pModel), (float) getChoice (pVoice), get (pGain), get (pBass), get (pMid), get (pTreble),
                         get (pPresence), get (pDepth), get (pMaster), get (pBright), get (pTight), get (pSag) }))
    {
        cur = circuit();
        design (cur);
    }
    const auto& c = cur;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float inPk = peak2 (l, r, n);
    auto* m = monoBuf.getWritePointer (0);
    for (int i = 0; i < n; ++i) m[i] = 0.5f * (l[i] + r[i]);

    float* ptrs[1] = { m };
    juce::dsp::AudioBlock<float> blk (ptrs, 1, (size_t) n);
    auto up = mono4x->processSamplesUp (blk);
    auto* d = up.getChannelPointer (0);
    const int N = (int) up.getNumSamples();
    const int stages = juce::jlimit (1, 5, c.stages);
    const float asym = c.asym, blocking = c.blocking * 0.02f;
    const float classAsym = 1.0f + c.classA * 0.5f;

    auto stage = [&] (Stage& s, float x) noexcept
    {
        const float v = s.cathode < 1.0f ? x - s.shelf.lp (x) * (1.0f - s.cathode) : x;
        const float u = v * s.gain - s.charge;
        float y;
        if (c.solidState) y = transistor (u);
        else
        {
            y = tube (u, asym);
            // grid current charges the coupling cap when driven hard: the bias shifts, the stage compresses
            s.charge = juce::jlimit (0.0f, 1.5f, s.charge * blockRel + blocking * juce::jmax (0.0f, u - 0.7f));
        }
        return s.lp.lp (s.hp.hp (y));
    };
    auto toneStack = [&] (float x) noexcept
    {
        if (c.stack == StackKind::Fmv) return stack.process (x) * stackNorm;
        return activeHigh.process (activeMid.process (activeLow.process (x, 0), 0), 0);
    };

    for (int i = 0; i < N; ++i)
    {
        float y = inHp.hp (d[i]);
        y = stage (st[0], y);
        if (c.stackEarly) y = toneStack (y);
        y = y * gainPot + brightAmt * gainPot * brightHp.hp (y);
        for (int k = 1; k < stages; ++k) y = stage (st[k], y);
        if (! c.stackEarly) y = toneStack (y);
        if (useVoiceLow) y = voiceLow.process (y, 0);
        if (useVoiceMid) y = voiceMid.process (y, 0);
        if (useVoiceHigh) y = voiceHigh.process (y, 0);

        // the power amp, with its negative feedback (Presence and Depth take some of it away at the
        // top and the bottom, so those come up), and the supply sagging under load
        float u;
        if (beta > 0.0f)
        {
            const float fb = lastOut - presAmt * presHp.hp (lastOut) - depthAmt * depthLp.lp (lastOut);
            u = y * paDrive - beta * fb;
        }
        else
        {
            // no feedback (an AC30): Presence is the Cut control, a treble cut before the power tubes
            const float cut = presHp.hp (y);
            u = (y - cut * presAmt * 0.55f) * paDrive;
        }
        const float rail = 1.0f / (1.0f + sagAmt * 2.0f * sagEnv);
        float o;
        if (c.solidState) o = rail * transistor (u / rail);
        else o = u >= 0.0f ? rail * fastTanh (u / rail) : rail * classAsym * fastTanh (u / (rail * classAsym));
        const float ao = std::abs (o);
        sagEnv = ao > sagEnv ? ao + sagAtt * (sagEnv - ao) : ao + sagRel * (sagEnv - ao);
        lastOut = o;
        d[i] = otLp.lp (otHp.hp (o));
    }
    mono4x->processSamplesDown (blk);

    const float level = dbToGain (get (pLevel)) * makeup;
    for (int i = 0; i < n; ++i)
    {
        float y = dcOut.hp (m[i]);
        if (useLoad) y = load.process (y, 0);
        y *= level;
        l[i] = y; r[i] = y;
    }
    updateMeters (inPk, peak2 (l, r, n), 0.0f);
}

float AmpBlock::shape (float x) const
{
    // the preamp's static curve for a guitar played hard (peaks ~12 dB under full scale)
    const auto c = circuit();
    const float g = get (pGain) / 10.0f;
    float y = x * 0.25f * dbToGain (c.firstDb);
    y = c.solidState ? transistor (y) : tube (y, c.asym);
    y *= dbToGain (c.gainMinDb + (c.gainMaxDb - c.gainMinDb) * g);
    for (int k = 1; k < c.stages; ++k)
    {
        y *= dbToGain (c.laterDb) * (k == 1 ? 0.3f : 1.0f);
        y = c.solidState ? transistor (y) : tube (y, c.asym);
    }
    return juce::jlimit (-1.0f, 1.0f, y);
}

} // namespace rig
