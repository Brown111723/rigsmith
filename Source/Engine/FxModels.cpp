#include "Nodes.h"

namespace rig
{
namespace
{
    using Stock = std::vector<std::pair<juce::String, float>>;
    using Labels = std::vector<std::pair<juce::String, juce::String>>;

    FxModel M (const char* maker, const char* name, const char* year, const char* blurb,
               Stock stock, juce::StringArray hide = {}, Labels labels = {}, FxCharacter c = {})
    {
        FxModel m;
        m.maker = maker; m.name = name; m.year = year; m.blurb = blurb;
        m.stock = std::move (stock); m.hide = std::move (hide); m.labels = std::move (labels); m.c = c;
        return m;
    }
    FxModel studio (const char* what)
    {
        FxModel m;
        m.name = "Studio";
        m.blurb = juce::String ("Rigsmith's own ") + what + ": every control, no colouring";
        return m;
    }
    FxCharacter dark (float hz, float drive = 0.0f) { FxCharacter c; c.darkHz = hz; c.drive = drive; return c; }

    std::map<NodeType, std::vector<FxModel>> build()
    {
        std::map<NodeType, std::vector<FxModel>> t;

        //--- Dynamics --------------------------------------------------------------
        t[NodeType::Gate] = {
            studio ("noise gate"),
            M ("Boss", "NS-2 Noise Suppressor", "1987", "The pedalboard standard: threshold and decay, smooth enough to leave sustain alone",
               { { "view", 0 }, { "threshold", -56 }, { "release", 140 }, { "hold", 10 }, { "range", -70 }, { "hysteresis", 4 }, { "attack", 1 } },
               { "view", "range" }, { { "release", "Decay" } }),
            M ("ISP", "Decimator II", "2000s", "Time Vector Processing: one knob, and it tracks the note's decay so it closes fast without chopping",
               { { "view", 0 }, { "threshold", -50 }, { "release", 45 }, { "hold", 0 }, { "range", -90 }, { "hysteresis", 3 }, { "attack", 0.1f } },
               { "view", "release", "range" }),
            M ("ISP", "Decimator G String (keyed)", "2000s", "Keyed from the guitar before the amp: the high-gain metal standard",
               { { "view", 1 }, { "key", 1 }, { "threshold", -48 }, { "release", 35 }, { "hold", 0 }, { "range", -90 }, { "hysteresis", 3 }, { "attack", 0.1f } },
               { "view", "attack", "hold", "hysteresis", "lookahead", "keyhpf", "keylpf", "bands", "crossover", "highrange", "listen", "range", "release" }),
            M ("MXR", "Smart Gate", "1980s", "Three modes in one: a gentle hiss gate or a full, hard gate",
               { { "view", 0 }, { "threshold", -54 }, { "release", 70 }, { "hold", 15 }, { "range", -80 }, { "keyhpf", 300 } },
               { "view" }),
            M ("Fortin", "Zuul", "2010s", "Djent-tight: keyed from your guitar, opens instantly, slams shut",
               { { "view", 1 }, { "key", 1 }, { "threshold", -46 }, { "attack", 0.05f }, { "hold", 5 }, { "release", 25 }, { "range", -90 }, { "hysteresis", 5 } },
               { "view", "attack", "hold", "lookahead", "keyhpf", "keylpf", "bands", "crossover", "highrange", "listen" }),
            M ("Boz Digital", "Gatey Watey", "2010s", "Dual-band gating: close the hiss in the highs further than the lows",
               { { "view", 1 }, { "bands", 1 }, { "threshold", -56 }, { "range", -10 }, { "highrange", -65 }, { "crossover", 1800 }, { "release", 120 } }),
        };
        t[NodeType::Comp] = {
            studio ("compressor"),
            M ("MXR", "Dyna Comp", "1972", "The squishy orange box: country snap and funk sustain",
               { { "ratio", 12 }, { "attack", 2 }, { "release", 300 }, { "knee", 4 }, { "auto", 0 }, { "makeup", 12 }, { "threshold", -36 }, { "mix", 100 } },
               { "ratio", "attack", "release", "knee", "auto", "mix" }, { { "threshold", "Sensitivity" }, { "makeup", "Output" } }, dark (4800, 0.2f)),
            M ("Boss", "CS-3 Compression Sustainer", "1986", "Sustain, attack, tone, level: bright, clean and practical",
               { { "ratio", 8 }, { "attack", 6 }, { "release", 200 }, { "knee", 6 }, { "auto", 0 }, { "makeup", 5 }, { "threshold", -30 }, { "mix", 100 } },
               { "ratio", "release", "knee", "auto", "mix" }, { { "threshold", "Sustain" }, { "makeup", "Level" } }, dark (7500)),
            M ("Keeley", "Compressor Plus", "2010s", "Studio-quality pedal compression with a blend for parallel squash",
               { { "ratio", 6 }, { "attack", 8 }, { "release", 250 }, { "knee", 8 }, { "auto", 0 }, { "makeup", 4 }, { "threshold", -30 }, { "mix", 70 } },
               { "ratio", "attack", "release", "knee", "auto" }, { { "threshold", "Sustain" }, { "makeup", "Level" }, { "mix", "Blend" } }, dark (10000, 0.05f)),
            M ("Wampler", "Ego Compressor", "2010s", "Transparent sustain with a blend knob: compression you feel more than hear",
               { { "ratio", 5 }, { "attack", 12 }, { "release", 220 }, { "knee", 10 }, { "auto", 0 }, { "makeup", 4 }, { "threshold", -28 }, { "mix", 60 } },
               { "ratio", "release", "knee", "auto" }, { { "threshold", "Sustain" }, { "makeup", "Volume" }, { "mix", "Blend" } }),
            M ("Origin Effects", "Cali76 (1176-style)", "2010s", "The 1176 FET compressor in a pedal: fast, punchy, a little grit",
               { { "ratio", 8 }, { "attack", 0.5f }, { "release", 120 }, { "knee", 2 }, { "auto", 0 }, { "makeup", 4 }, { "threshold", -26 }, { "mix", 100 } },
               { "knee", "auto" }, { { "threshold", "Input" }, { "makeup", "Output" } }, dark (0, 0.3f)),
            M ("Teletronix", "LA-2A", "1962", "The optical classic: smooth, slow, program-dependent, two knobs",
               { { "ratio", 3 }, { "attack", 10 }, { "release", 600 }, { "knee", 14 }, { "auto", 0 }, { "makeup", 5 }, { "threshold", -28 }, { "mix", 100 } },
               { "ratio", "attack", "release", "knee", "auto", "mix" }, { { "threshold", "Peak Reduction" }, { "makeup", "Gain" } }, dark (15000, 0.15f)),
            M ("SSL", "G-Bus Compressor", "1980s", "Mix-bus glue: 4:1, fast release, everything sits together",
               { { "ratio", 4 }, { "attack", 10 }, { "release", 100 }, { "knee", 3 }, { "auto", 1 }, { "threshold", -20 }, { "mix", 100 } },
               { "knee" }),
            M ("Xotic", "SP Compressor", "2010s", "A tiny Ross-style compressor with a blend: keeps the pick attack",
               { { "ratio", 6 }, { "attack", 15 }, { "release", 250 }, { "knee", 6 }, { "auto", 0 }, { "makeup", 4 }, { "threshold", -30 }, { "mix", 65 } },
               { "ratio", "attack", "release", "knee", "auto" }, { { "threshold", "Comp" }, { "makeup", "Volume" }, { "mix", "Blend" } }, dark (9000)),
            M ("Diamond", "Comp", "2000s", "Optical pedal compression with a tilt EQ: the Nashville favourite",
               { { "ratio", 4 }, { "attack", 20 }, { "release", 400 }, { "knee", 12 }, { "auto", 0 }, { "makeup", 4 }, { "threshold", -26 }, { "mix", 100 } },
               { "ratio", "attack", "release", "knee", "auto", "mix" }, { { "threshold", "Comp" }, { "makeup", "Volume" } }, dark (12000, 0.05f)),
        };
        t[NodeType::Limiter] = {
            studio ("limiter"),
            M ("UREI", "1176 (all buttons in)", "1967", "The 'British mode' trick: every ratio button in, aggressive and full of attitude",
               { { "drive", 8 }, { "ceiling", -1 }, { "release", 50 } }, {}, { { "drive", "Input" } }, dark (0, 0.45f)),
            M ("Teletronix", "LA-3A", "1969", "Solid-state opto levelling: smooth and even",
               { { "drive", 4 }, { "ceiling", -1 }, { "release", 300 } }, {}, { { "drive", "Gain" } }, dark (16000, 0.1f)),
            M ("Fairchild", "670", "1959", "The tube limiter on countless classic records: thick and warm",
               { { "drive", 5 }, { "ceiling", -1.5f }, { "release", 400 } }, {}, { { "drive", "Input Gain" }, { "release", "Time Constant" } }, dark (14000, 0.3f)),
            M ("Rigsmith", "Brickwall", "", "A clean, fast ceiling for live rigs: nothing gets past",
               { { "drive", 2 }, { "ceiling", -1 }, { "release", 30 } }),
        };
        t[NodeType::Swell] = {
            studio ("auto swell"),
            M ("Boss", "SG-1 Slow Gear", "1979", "The original violin pedal: each note fades in",
               { { "threshold", -45 }, { "rise", 500 }, { "rearm", 150 } }, { "rearm" }, { { "threshold", "Sens" }, { "rise", "Attack" } }),
            M ("Line 6", "Auto-Volume", "2000s", "Quicker swells that still feel playable",
               { { "threshold", -50 }, { "rise", 250 }, { "rearm", 100 } }),
            M ("Rigsmith", "Reverse", "", "Very slow swells that sound like a tape played backwards",
               { { "threshold", -42 }, { "rise", 2200 }, { "rearm", 400 } }),
        };

        //--- Drive -------------------------------------------------------------------
        t[NodeType::Fuzz] = {
            studio ("fuzz"),
            M ("Dallas Arbiter", "Fuzz Face (germanium)", "1966", "Hendrix's fuzz: warm, woolly, cleans up when you roll the guitar's volume back",
               { { "fuzz", 7 }, { "tone", 6 }, { "gate", -90 }, { "octave", 0 }, { "level", -6 } }, { "tone", "gate", "octave" }, { { "level", "Volume" } }, dark (5000)),
            M ("Dallas Arbiter", "Fuzz Face (silicon)", "1969", "The BC108 Fuzz Face: brighter and more aggressive (Gilmour, later Hendrix)",
               { { "fuzz", 7.5f }, { "tone", 7.5f }, { "gate", -90 }, { "octave", 0 }, { "level", -6 } }, { "tone", "gate", "octave" }, { { "level", "Volume" } }, dark (7500)),
            M ("Sola Sound", "Tone Bender MkII", "1966", "The three-transistor British fuzz: raspy, saturated, Page and Beck",
               { { "fuzz", 8 }, { "tone", 7 }, { "gate", -85 }, { "octave", 0 }, { "level", -6 } }, { "tone", "gate", "octave" }, { { "fuzz", "Attack" } }, dark (6500)),
            M ("Maestro", "FZ-1 Fuzz-Tone", "1962", "The first fuzz pedal: the buzzing, sputtery Satisfaction sound",
               { { "fuzz", 8 }, { "tone", 8 }, { "gate", -58 }, { "octave", 0 }, { "level", -6 } }, { "tone", "gate", "octave" }, { { "fuzz", "Attack" }, { "level", "Volume" } }, dark (4200)),
            M ("Univox", "Super-Fuzz", "1968", "Octave-up fuzz with a scooped tone: thick and buzzy (Pete Townshend, Mudhoney)",
               { { "fuzz", 8 }, { "tone", 4 }, { "gate", -80 }, { "octave", 1 }, { "level", -7 } }, { "gate", "octave" }, { { "fuzz", "Expander" }, { "level", "Balance" } }, dark (6000)),
            M ("Roger Mayer", "Octavia", "1967", "Hendrix's octave fuzz: the ring-modulated upper octave on Purple Haze",
               { { "fuzz", 7 }, { "tone", 7 }, { "gate", -85 }, { "octave", 1 }, { "level", -6 } }, { "tone", "gate", "octave" }, { { "level", "Volume" } }, dark (8000)),
            M ("Z.Vex", "Fuzz Factory", "1995", "Unstable, gated, sputtering: turn the gate up for velcro fuzz",
               { { "fuzz", 9 }, { "tone", 6 }, { "gate", -48 }, { "octave", 0 }, { "level", -7 } }, { "octave" }, { { "fuzz", "Drive" }, { "gate", "Gate" }, { "level", "Volume" } }),
            M ("Fulltone", "'69", "1990s", "A boutique Fuzz Face with more range: warm to wild",
               { { "fuzz", 7 }, { "tone", 6.5f }, { "gate", -88 }, { "octave", 0 }, { "level", -6 } }, { "gate", "octave" }, { { "fuzz", "Fuzz" }, { "tone", "Contour" } }, dark (5500)),
            M ("Death By Audio", "Fuzz War", "2010s", "A wall of brutal, saturated fuzz",
               { { "fuzz", 10 }, { "tone", 5 }, { "gate", -75 }, { "octave", 0 }, { "level", -8 } }, { "octave" }),
        };
        t[NodeType::Boost] = {
            studio ("clean boost"),
            M ("Dallas", "Rangemaster", "1965", "The treble booster that drove Brian May's and Clapton's amps: bright and cutting",
               { { "gain", 14 }, { "tight", 350 }, { "bright", 6 } }, { "tight", "bright" }, { { "gain", "Boost" } }, dark (9000, 0.3f)),
            M ("Xotic", "EP Booster", "2010s", "The Echoplex preamp: always-on sparkle and a fuller, warmer tone",
               { { "gain", 6 }, { "tight", 30 }, { "bright", 1.5f } }, { "tight", "bright" }, { { "gain", "Boost" } }, dark (0, 0.15f)),
            M ("MXR", "Micro Amp", "1978", "Up to 26 dB of clean gain: as flat as a boost gets",
               { { "gain", 8 }, { "tight", 25 }, { "bright", 0 } }, { "tight", "bright" }),
            M ("Xotic", "RC Booster", "2000s", "A clean boost with active treble and bass",
               { { "gain", 8 }, { "tight", 40 }, { "bright", 0 } }, {}, { { "bright", "Treble" }, { "tight", "Bass Cut" } }),
            M ("Keeley", "Katana", "2000s", "A clean boost that pushes into gentle grit when it's turned up",
               { { "gain", 10 }, { "tight", 60 }, { "bright", 2 } }, { "tight" }, { { "gain", "Boost" }, { "bright", "Bright" } }, dark (0, 0.1f)),
            M ("Fulltone", "Fat Boost", "2000s", "A fat, warm boost with a little drive in it",
               { { "gain", 9 }, { "tight", 20 }, { "bright", -1 } }, { "tight" }, { { "gain", "Volume" }, { "bright", "Tone" } }, dark (12000, 0.25f)),
        };
        t[NodeType::Crusher] = {
            studio ("bit crusher"),
            M ("E-mu", "SP-1200", "1987", "12-bit, 26 kHz: the gritty boom-bap sampler",
               { { "bits", 12 }, { "down", 2 }, { "tone", 13000 }, { "mix", 100 } }, {}, {}, dark (0, 0.2f)),
            M ("Akai", "S950", "1988", "12-bit with a warm, filtered top end",
               { { "bits", 12 }, { "down", 2 }, { "tone", 10000 }, { "mix", 100 } }),
            M ("Fairlight", "CMI", "1979", "8-bit early digital sampling: grainy and dark",
               { { "bits", 8 }, { "down", 3 }, { "tone", 9000 }, { "mix", 100 } }),
            M ("Nintendo", "NES (2A03)", "1983", "4-bit chiptune crunch",
               { { "bits", 4 }, { "down", 4 }, { "tone", 12000 }, { "mix", 100 } }),
            M ("Rigsmith", "Telephone", "", "Band-limited and crunchy, like a phone line",
               { { "bits", 8 }, { "down", 6 }, { "tone", 3400 }, { "mix", 100 } }),
        };

        //--- Modulation --------------------------------------------------------------
        t[NodeType::Chorus] = {
            studio ("chorus"),
            M ("Boss", "CE-1 Chorus Ensemble", "1976", "The first chorus pedal: lush, with a warm preamp that's famous on its own",
               { { "rate", 0.7f }, { "depth", 60 }, { "delay", 10 }, { "width", 70 }, { "mix", 50 } }, { "delay", "width", "mix" }, { { "depth", "Intensity" } }, dark (6000, 0.25f)),
            M ("Boss", "CE-2", "1979", "The 80s chorus: warm, mono, bucket-brigade",
               { { "rate", 0.8f }, { "depth", 55 }, { "delay", 12 }, { "width", 0 }, { "mix", 50 } }, { "delay", "width", "mix" }, {}, dark (5500)),
            M ("Roland", "Dimension D (SDD-320)", "1979", "Subtle, wide, almost invisible: makes anything sound bigger",
               { { "rate", 0.25f }, { "depth", 20 }, { "delay", 14 }, { "width", 100 }, { "mix", 45 } }, { "rate", "delay" }, { { "depth", "Mode" } }, dark (12000)),
            M ("Electro-Harmonix", "Small Clone", "1983", "Kurt Cobain's watery, seasick chorus",
               { { "rate", 0.9f }, { "depth", 90 }, { "delay", 15 }, { "width", 40 }, { "mix", 55 } }, { "delay", "width", "mix" }, {}, dark (6000)),
            M ("TC Electronic", "Stereo Chorus+ (SCF)", "1976", "The bright, hi-fi stereo chorus of 80s rock",
               { { "rate", 0.6f }, { "depth", 50 }, { "delay", 9 }, { "width", 100 }, { "mix", 50 } }, { "delay" }, { { "rate", "Speed" }, { "depth", "Width" }, { "width", "Stereo" } }, dark (14000)),
            M ("MXR", "M134 Stereo Chorus", "1980s", "Analog stereo chorus with real depth",
               { { "rate", 0.7f }, { "depth", 60 }, { "delay", 13 }, { "width", 90 }, { "mix", 50 } }, { "delay" }, {}, dark (7500)),
            M ("Roland", "JC-120 Chorus", "1975", "The Jazz Chorus amp's stereo chorus: sparkling clean",
               { { "rate", 3.5f }, { "depth", 35 }, { "delay", 6 }, { "width", 100 }, { "mix", 50 } }, { "delay", "width" }, { { "rate", "Speed" } }, dark (16000)),
        };
        t[NodeType::Flanger] = {
            studio ("flanger"),
            M ("MXR", "M117R Flanger", "1979", "Eddie Van Halen's jet: deep and resonant",
               { { "rate", 0.2f }, { "depth", 85 }, { "manual", 2.2f }, { "feedback", 65 }, { "mix", 50 } }, { "mix" }, { { "rate", "Speed" }, { "depth", "Width" }, { "feedback", "Regen" } }, dark (6000)),
            M ("Electro-Harmonix", "Electric Mistress", "1976", "The silky, chorus-like flanger (Andy Summers, Gilmour)",
               { { "rate", 0.3f }, { "depth", 75 }, { "manual", 1.5f }, { "feedback", 45 }, { "mix", 50 } }, { "mix" }, { { "manual", "Range" }, { "feedback", "Color" } }, dark (5000)),
            M ("A/DA", "Flanger", "1977", "The widest-sweeping analogue flanger: dramatic, metallic jet sounds",
               { { "rate", 0.25f }, { "depth", 95 }, { "manual", 1.2f }, { "feedback", 80 }, { "mix", 50 } }, { "mix" }, { { "depth", "Range" }, { "feedback", "Enhance" }, { "rate", "Speed" } }, dark (7000)),
            M ("Boss", "BF-2 Flanger", "1980", "Manual, depth, rate, resonance: the classic compact flanger",
               { { "rate", 0.4f }, { "depth", 70 }, { "manual", 1.8f }, { "feedback", 55 }, { "mix", 50 } }, { "mix" }, { { "feedback", "Res" } }, dark (6500)),
            M ("TC Electronic", "Vortex", "2010s", "Clean, modern flanging, including through-zero",
               { { "rate", 0.3f }, { "depth", 70 }, { "manual", 1.5f }, { "feedback", 40 }, { "mix", 50 } }, {}, {}, dark (14000)),
            M ("Studio", "Tape flange", "1960s", "Two tape machines, a thumb on the reel: the through-zero swoosh of Itchycoo Park",
               { { "rate", 0.08f }, { "depth", 90 }, { "manual", 0.8f }, { "feedback", -40 }, { "mix", 50 } }, {}, {}, dark (12000)),
        };
        t[NodeType::Phaser] = {
            studio ("phaser"),
            M ("MXR", "Phase 90 (script)", "1974", "One knob, four stages, little feedback: Eruption and Van Halen I",
               { { "rate", 0.45f }, { "depth", 85 }, { "centre", 700 }, { "feedback", 10 }, { "stages", 0 }, { "mix", 50 } },
               { "depth", "centre", "feedback", "stages", "mix" }, { { "rate", "Speed" } }, dark (9000)),
            M ("MXR", "Phase 90 (block logo)", "1977", "The later Phase 90: more feedback, a more pronounced swirl",
               { { "rate", 0.5f }, { "depth", 85 }, { "centre", 700 }, { "feedback", 40 }, { "stages", 0 }, { "mix", 50 } },
               { "depth", "centre", "feedback", "stages", "mix" }, { { "rate", "Speed" } }, dark (9000)),
            M ("Electro-Harmonix", "Small Stone", "1974", "Deep, liquid and vocal, with the Color switch for more resonance",
               { { "rate", 0.3f }, { "depth", 100 }, { "centre", 900 }, { "feedback", 60 }, { "stages", 0 }, { "mix", 50 } },
               { "depth", "centre", "stages", "mix" }, { { "feedback", "Color" } }, dark (8000)),
            M ("Maestro", "PS-1 Phase Shifter", "1971", "The first phaser pedal: slow, deep, six stages",
               { { "rate", 0.25f }, { "depth", 90 }, { "centre", 800 }, { "feedback", 25 }, { "stages", 1 }, { "mix", 50 } },
               { "depth", "centre", "feedback", "stages", "mix" }, {}, dark (7000)),
            M ("Mu-Tron", "Bi-Phase", "1975", "Twelve stages of enormous, sweeping phase",
               { { "rate", 0.2f }, { "depth", 100 }, { "centre", 800 }, { "feedback", 55 }, { "stages", 3 }, { "mix", 50 } },
               { "mix" }, { { "feedback", "Feedback" } }, dark (10000)),
            M ("Boss", "PH-3 Phase Shifter", "2000s", "Rate, depth, resonance and selectable stages",
               { { "rate", 0.6f }, { "depth", 80 }, { "centre", 1000 }, { "feedback", 45 }, { "stages", 2 }, { "mix", 50 } }, { "mix" }, { { "feedback", "Res" } }),
            M ("MXR", "Phase 100", "1970s", "Ten stages and an intensity switch: richer than the Phase 90",
               { { "rate", 0.4f }, { "depth", 90 }, { "centre", 850 }, { "feedback", 35 }, { "stages", 2 }, { "mix", 50 } },
               { "centre", "stages", "mix" }, { { "rate", "Speed" }, { "depth", "Intensity" } }, dark (9000)),
        };
        t[NodeType::Tremolo] = {
            studio ("tremolo"),
            M ("Fender", "Deluxe Reverb (opto)", "1963", "The blackface amp's photocell tremolo: smooth and lush",
               { { "rate", 4.5f }, { "depth", 60 }, { "shape", 0 }, { "stereo", 0 } }, { "shape", "stereo" }, { { "rate", "Speed" }, { "depth", "Intensity" } }),
            M ("Vox", "AC30 (bias)", "1960s", "Bias tremolo: choppier and a little gritty",
               { { "rate", 5.5f }, { "depth", 65 }, { "shape", 1 }, { "stereo", 0 } }, { "shape", "stereo" }, { { "rate", "Speed" }, { "depth", "Depth" } }, dark (0, 0.15f)),
            M ("Fender", "Brownface harmonic", "1960", "Lows and highs pulse against each other: phasey, hypnotic",
               { { "rate", 4 }, { "depth", 60 }, { "shape", 3 }, { "stereo", 0 } }, { "shape", "stereo" }, { { "rate", "Speed" }, { "depth", "Intensity" } }),
            M ("Boss", "TR-2", "1997", "Rate, wave and depth: triangle to square",
               { { "rate", 5 }, { "depth", 70 }, { "shape", 1 }, { "stereo", 0 } }, { "stereo" }, { { "shape", "Wave" } }),
            M ("Fulltone", "Supa-Trem", "2000s", "Hard, choppy square-wave tremolo: the helicopter",
               { { "rate", 6 }, { "depth", 100 }, { "shape", 2 }, { "stereo", 0 } }, { "shape", "stereo" }, { { "rate", "Speed" } }),
            M ("Demeter", "Tremulator", "1980s", "Optical studio tremolo: deep, smooth, very musical",
               { { "rate", 5 }, { "depth", 75 }, { "shape", 0 }, { "stereo", 0 } }, { "shape", "stereo" }, { { "rate", "Speed" } }),
            M ("Magnatone", "Stereo vibrato", "1957", "The panning 'vibrato' of Buddy Holly's amp: lush and spacious",
               { { "rate", 3.5f }, { "depth", 80 }, { "shape", 0 }, { "stereo", 1 } }, { "shape", "stereo" }, { { "rate", "Speed" } }),
        };
        t[NodeType::Vibe] = {
            studio ("vibe"),
            M ("Shin-ei", "Uni-Vibe", "1968", "The original: Machine Gun, Bridge of Sighs, the pulsing throb",
               { { "rate", 2.5f }, { "intensity", 80 }, { "mode", 0 }, { "level", 0 } }, {}, { { "rate", "Speed" } }, dark (6500, 0.2f)),
            M ("Fulltone", "Mini DejaVibe", "1990s", "A faithful, more compact Uni-Vibe",
               { { "rate", 2.2f }, { "intensity", 75 }, { "mode", 0 }, { "level", 0 } }, {}, { { "rate", "Speed" } }, dark (8000, 0.1f)),
            M ("Dunlop", "UV-1 Uni-Vibe", "1990s", "Dunlop's full-size reissue with a speed pedal",
               { { "rate", 2.6f }, { "intensity", 80 }, { "mode", 0 }, { "level", 0 } }, {}, { { "rate", "Speed" } }, dark (7000, 0.15f)),
            M ("MXR", "M68 Uni-Vibe", "2010s", "The pedalboard Uni-Vibe: rich, with a vibrato mode",
               { { "rate", 2.4f }, { "intensity", 75 }, { "mode", 0 }, { "level", 0 } }, {}, { { "rate", "Speed" } }, dark (9000)),
            M ("Voodoo Lab", "Micro Vibe", "1990s", "Small, simple, chorus-only vibe",
               { { "rate", 2 }, { "intensity", 70 }, { "mode", 0 }, { "level", 0 } }, { "mode", "level" }, { { "rate", "Speed" } }, dark (10000)),
        };
        t[NodeType::Rotary] = {
            studio ("rotary speaker"),
            M ("Leslie", "122", "1960s", "The Hammond organ's partner: the definitive rotary sound",
               { { "ramp", 1.2f }, { "balance", 60 }, { "width", 80 }, { "drive", 25 } }, {}, { { "balance", "Horn / Drum" } }),
            M ("Leslie", "147", "1960s", "The 122's twin for other organs: a touch smoother",
               { { "ramp", 1.3f }, { "balance", 55 }, { "width", 75 }, { "drive", 15 } }),
            M ("Fender", "Vibratone (Leslie 16)", "1967", "A single rotor for guitar: Stevie Ray Vaughan's Cold Shot",
               { { "ramp", 1.5f }, { "balance", 20 }, { "width", 60 }, { "drive", 10 } }, { "balance" }, {}, dark (4500)),
            M ("Neo Instruments", "Ventilator", "2000s", "The pedal that convinced organists: fast ramps and real grit",
               { { "ramp", 1.0f }, { "balance", 60 }, { "width", 85 }, { "drive", 40 } }),
            M ("Strymon", "Lex", "2010s", "Modern rotary pedal with horn level and preamp drive",
               { { "ramp", 1.1f }, { "balance", 65 }, { "width", 90 }, { "drive", 20 } }, {}, { { "balance", "Horn Level" }, { "drive", "Preamp Drive" } }),
        };

        //--- Time ------------------------------------------------------------------
        t[NodeType::Delay] = {
            studio ("delay"),
            M ("Maestro", "Echoplex EP-3", "1970", "The tape echo of Page, Van Halen and Brian May: warm repeats, the preamp everyone wants",
               { { "mode", 1 }, { "time", 320 }, { "feedback", 35 }, { "mix", 30 }, { "lowcut", 150 }, { "highcut", 4500 }, { "mod", 25 }, { "duck", 0 }, { "spread", 30 } },
               { "mode", "lowcut", "highcut", "duck", "spread", "mod" }, { { "time", "Echo Delay" }, { "feedback", "Repeats" }, { "mix", "Echo Volume" } }, dark (0, 0.2f)),
            M ("Roland", "RE-201 Space Echo", "1974", "Three tape heads, wow and flutter, dark repeats that run away into oscillation",
               { { "mode", 1 }, { "time", 330 }, { "feedback", 50 }, { "mix", 35 }, { "lowcut", 200 }, { "highcut", 3200 }, { "mod", 35 }, { "duck", 0 }, { "spread", 30 } },
               { "mode", "lowcut", "duck", "spread" }, { { "time", "Repeat Rate" }, { "feedback", "Intensity" }, { "mix", "Echo Volume" }, { "highcut", "Treble" }, { "mod", "Tape Wear" } }, dark (0, 0.15f)),
            M ("Boss", "DM-2 Delay", "1981", "Bucket-brigade analogue: dark, warm repeats up to 300 ms",
               { { "mode", 0 }, { "time", 250 }, { "feedback", 40 }, { "mix", 35 }, { "lowcut", 120 }, { "highcut", 2800 }, { "mod", 15 }, { "duck", 0 }, { "spread", 20 } },
               { "mode", "lowcut", "highcut", "mod", "duck", "spread" }, { { "time", "Repeat Rate" }, { "feedback", "Intensity" }, { "mix", "Echo" } }),
            M ("Electro-Harmonix", "Deluxe Memory Man", "1978", "Analogue delay with chorus and vibrato on the repeats (The Edge's early sound)",
               { { "mode", 0 }, { "time", 450 }, { "feedback", 45 }, { "mix", 40 }, { "lowcut", 120 }, { "highcut", 3500 }, { "mod", 40 }, { "duck", 0 }, { "spread", 40 } },
               { "mode", "lowcut", "highcut", "duck" }, { { "mix", "Blend" }, { "mod", "Chorus / Vibrato" } }, dark (0, 0.1f)),
            M ("MXR", "Carbon Copy", "2008", "The modern analogue favourite: warm repeats with optional modulation",
               { { "mode", 0 }, { "time", 400 }, { "feedback", 40 }, { "mix", 35 }, { "lowcut", 100 }, { "highcut", 3000 }, { "mod", 20 }, { "duck", 0 }, { "spread", 30 } },
               { "mode", "lowcut", "highcut", "duck", "spread" }, { { "feedback", "Regen" }, { "mod", "Mod" } }),
            M ("TC Electronic", "2290", "1985", "The 80s studio digital delay: pristine, stereo, ducking (Satriani, Vai)",
               { { "mode", 2 }, { "time", 420 }, { "feedback", 35 }, { "mix", 30 }, { "lowcut", 60 }, { "highcut", 12000 }, { "mod", 10 }, { "duck", 40 }, { "spread", 80 } }),
            M ("Boss", "DD-3 Digital Delay", "1986", "The compact digital standard: clean, clear repeats",
               { { "mode", 0 }, { "time", 400 }, { "feedback", 35 }, { "mix", 30 }, { "lowcut", 40 }, { "highcut", 9000 }, { "mod", 0 }, { "duck", 0 }, { "spread", 30 } },
               { "mode", "lowcut", "highcut", "mod", "duck", "spread" }, { { "time", "D.Time" }, { "feedback", "F.Back" }, { "mix", "E.Level" } }),
            M ("Line 6", "DL4 Tube Echo", "1999", "The green box's Echoplex model: tape warmth with digital convenience",
               { { "mode", 1 }, { "time", 380 }, { "feedback", 40 }, { "mix", 35 }, { "lowcut", 120 }, { "highcut", 5000 }, { "mod", 20 }, { "duck", 0 }, { "spread", 30 } },
               { "mode", "lowcut", "duck", "spread" }, { { "highcut", "Tweez" }, { "mod", "Tweak" } }),
        };
        t[NodeType::Reverb] = {
            studio ("reverb"),
            M ("Fender", "'63 Spring Reverb", "1961", "The outboard tube spring tank: drippy, splashy surf",
               { { "type", 3 }, { "decay", 2.4f }, { "predelay", 0 }, { "size", 55 }, { "damping", 30 }, { "lowcut", 150 }, { "mod", 10 }, { "shimmer", 0 }, { "mix", 40 } },
               { "type", "predelay", "size", "lowcut", "mod", "shimmer" }, { { "decay", "Dwell" }, { "damping", "Tone" } }, dark (0, 0.15f)),
            M ("Fender", "Deluxe Reverb (amp spring)", "1963", "The amp's built-in spring: one knob, never too much",
               { { "type", 3 }, { "decay", 1.8f }, { "predelay", 0 }, { "size", 45 }, { "damping", 40 }, { "lowcut", 200 }, { "mod", 5 }, { "shimmer", 0 }, { "mix", 25 } },
               { "type", "predelay", "size", "damping", "lowcut", "mod", "shimmer", "decay" }, { { "mix", "Reverb" } }),
            M ("EMT", "140 Plate", "1957", "The steel plate of every 60s and 70s studio: bright, dense, smooth",
               { { "type", 2 }, { "decay", 2.5f }, { "predelay", 10 }, { "size", 60 }, { "damping", 35 }, { "lowcut", 200 }, { "mod", 15 }, { "shimmer", 0 }, { "mix", 25 } },
               { "type", "size", "shimmer" }),
            M ("Lexicon", "224 Hall", "1978", "The first great digital hall: lush, modulated, endlessly smooth",
               { { "type", 1 }, { "decay", 3.2f }, { "predelay", 30 }, { "size", 80 }, { "damping", 45 }, { "lowcut", 150 }, { "mod", 35 }, { "shimmer", 0 }, { "mix", 25 } },
               { "type", "shimmer" }),
            M ("Lexicon", "480L Room", "1986", "The studio room: natural space around a dry guitar",
               { { "type", 0 }, { "decay", 1.1f }, { "predelay", 8 }, { "size", 45 }, { "damping", 50 }, { "lowcut", 150 }, { "mod", 15 }, { "shimmer", 0 }, { "mix", 20 } },
               { "type", "shimmer" }),
            M ("Electro-Harmonix", "Holy Grail", "2000s", "The simple, lovely hall pedal: one knob of ambience",
               { { "type", 1 }, { "decay", 2.6f }, { "predelay", 15 }, { "size", 70 }, { "damping", 45 }, { "lowcut", 150 }, { "mod", 25 }, { "shimmer", 0 }, { "mix", 30 } },
               { "type", "predelay", "size", "damping", "lowcut", "mod", "shimmer" }, { { "mix", "Reverb" } }),
            M ("Strymon", "BigSky Cloud / Shimmer", "2013", "Huge, octave-shimmering ambience for pads and swells",
               { { "type", 4 }, { "decay", 8 }, { "predelay", 40 }, { "size", 100 }, { "damping", 40 }, { "lowcut", 250 }, { "mod", 50 }, { "shimmer", 60 }, { "mix", 40 } }),
            M ("Eventide", "Blackhole", "2000s", "Otherworldly, near-infinite space",
               { { "type", 1 }, { "decay", 18 }, { "predelay", 50 }, { "size", 100 }, { "damping", 45 }, { "lowcut", 250 }, { "mod", 60 }, { "shimmer", 0 }, { "mix", 45 } },
               { "type", "shimmer" }, { { "size", "Gravity" } }),
        };

        //--- Pitch -------------------------------------------------------------------
        t[NodeType::Pitch] = {
            studio ("pitch shifter"),
            M ("DigiTech", "Whammy (harmony)", "1989", "The Whammy's fixed intervals: fully wet, octave-up leads and dive bombs",
               { { "mode", 2 }, { "semis", 12 }, { "detune", 0 }, { "mix", 100 }, { "tone", 12000 } }, { "mode", "detune", "tone" }, { { "semis", "Interval" } }),
            M ("Electro-Harmonix", "POG", "2005", "Polyphonic octaves: organ-like octave up blended with the dry",
               { { "mode", 1 }, { "semis", 12 }, { "detune", 0 }, { "mix", 50 }, { "tone", 7000 } }, { "mode", "semis", "detune" }, { { "tone", "Filter" } }),
            M ("Boss", "OC-2 Octave", "1982", "Analogue octave down: dark, thick, monophonic bass under your riff",
               { { "mode", 0 }, { "semis", -12 }, { "detune", 0 }, { "mix", 50 }, { "tone", 3000 } }, { "mode", "semis", "detune", "tone" }, { { "mix", "Oct 1" } }),
            M ("Electro-Harmonix", "Micro POG", "2008", "Sub octave and octave up, clean and tracking well",
               { { "mode", 0 }, { "semis", -12 }, { "detune", 0 }, { "mix", 50 }, { "tone", 8000 } }, { "semis", "detune" }),
            M ("Eventide", "H910 Harmonizer", "1975", "The original doubler: a few cents either side for huge, wide guitars",
               { { "mode", 3 }, { "semis", 0 }, { "detune", 14 }, { "mix", 50 }, { "tone", 9000 } }, { "mode", "semis" }),
            M ("Boss", "PS-6 Harmonist", "2000s", "Fixed-interval harmony: a fifth or fourth above for twin leads",
               { { "mode", 2 }, { "semis", 7 }, { "detune", 0 }, { "mix", 45 }, { "tone", 10000 } }, { "mode", "detune" }, { { "semis", "Harmony" } }),
            M ("DigiTech", "Drop", "2015", "Drop-tune without touching the tuners: fully wet, half step to an octave down",
               { { "mode", 2 }, { "semis", -2 }, { "detune", 0 }, { "mix", 100 }, { "tone", 14000 } }, { "mode", "detune", "tone", "mix" }, { { "semis", "Drop" } }),
        };
        //--- Acoustic & vocal ------------------------------------------------------
        t[NodeType::AcousticPre] = {
            studio ("acoustic preamp"),
            M ("LR Baggs", "Venue DI", "2010s", "The gigging acoustic player's preamp: five-band EQ, a sweepable notch and a clean boost",
               { { "gain", 0 }, { "lowcut", 60 }, { "bass", 1 }, { "mid", -2 }, { "midfreq", 700 }, { "treble", 1 }, { "presence", 1 }, { "notch", 160 }, { "notchdepth", -8 }, { "bodyimage", 0 }, { "comp", 0 } },
               { "bodyimage", "comp" }, { { "presence", "Presence" }, { "gain", "Volume" } }),
            M ("Fishman", "Platinum Pro EQ", "2000s", "Studio-style acoustic EQ with an optical compressor and a brilliance control",
               { { "gain", 0 }, { "lowcut", 80 }, { "bass", 0 }, { "mid", -3 }, { "midfreq", 900 }, { "treble", 2 }, { "presence", 2 }, { "notch", 120 }, { "notchdepth", -6 }, { "bodyimage", 0 }, { "comp", 3 } },
               { "bodyimage" }, { { "presence", "Brilliance" }, { "comp", "Compressor" } }),
            M ("Fishman", "Aura Spectrum DI", "2008", "Images of studio-miked guitars blended in: the body and air a piezo never hears",
               { { "gain", 0 }, { "lowcut", 70 }, { "bass", 0 }, { "mid", -1 }, { "midfreq", 800 }, { "treble", 1 }, { "presence", 0 }, { "notch", 140 }, { "notchdepth", -6 }, { "bodyimage", 80 }, { "comp", 2 } },
               { "presence", "midfreq" }, { { "bodyimage", "Image Blend" } }),
            M ("Grace Design", "Felix", "2014", "Boutique, transparent, quiet: what a high-end studio would use for a pickup",
               { { "gain", 3 }, { "lowcut", 50 }, { "bass", 0 }, { "mid", 0 }, { "midfreq", 1000 }, { "treble", 0 }, { "presence", 0 }, { "notch", 200 }, { "notchdepth", 0 }, { "bodyimage", 0 }, { "comp", 0 } },
               { "bodyimage", "comp", "presence" }),
            M ("Radial", "PZ-Pre", "2010s", "A workhorse acoustic preamp with a notch, a high-pass and a gentle compressor",
               { { "gain", 2 }, { "lowcut", 90 }, { "bass", 2 }, { "mid", -2 }, { "midfreq", 600 }, { "treble", 2 }, { "presence", 0 }, { "notch", 100 }, { "notchdepth", -10 }, { "bodyimage", 0 }, { "comp", 2 } },
               { "bodyimage", "presence" }),
        };
        t[NodeType::VoiceStrip] = {
            studio ("vocal channel strip"),
            M ("Neve", "1073", "1970", "The console channel on countless classic vocals: thick, forward and warm through its transformers",
               { { "gain", 0 }, { "lowcut", 80 }, { "warmth", 5 }, { "comp", 3 }, { "deess", 3 }, { "body", 1 }, { "presence", 3 }, { "air", 4 }, { "gate", -90 } },
               {}, { { "presence", "High Mid (4.8k)" }, { "air", "High (12k)" }, { "body", "Low (220)" } }, dark (0, 0.15f)),
            M ("API", "512c + 550A", "1970s", "Punchy American console sound: forward, bright and confident",
               { { "gain", 0 }, { "lowcut", 90 }, { "warmth", 3 }, { "comp", 4 }, { "deess", 3 }, { "body", 0 }, { "presence", 4 }, { "air", 3 }, { "gate", -90 } },
               {}, {}, dark (0, 0.1f)),
            M ("SSL", "4000 E Channel", "1979", "The 80s and 90s pop vocal: clean, tight, bright, with that channel compressor",
               { { "gain", 0 }, { "lowcut", 100 }, { "warmth", 1 }, { "comp", 6 }, { "deess", 4 }, { "body", -1 }, { "presence", 3 }, { "air", 4 }, { "gate", -70 } }),
            M ("Avalon", "VT-737sp", "1990s", "Tube preamp, opto compressor and EQ: the silky, polished modern vocal",
               { { "gain", 0 }, { "lowcut", 80 }, { "warmth", 6 }, { "comp", 5 }, { "deess", 4 }, { "body", 1 }, { "presence", 2 }, { "air", 6 }, { "gate", -90 } },
               {}, {}, dark (0, 0.2f)),
            M ("Universal Audio", "610", "1960s", "The vintage tube console channel: dark, round and saturated (Sinatra to the Beach Boys)",
               { { "gain", 0 }, { "lowcut", 70 }, { "warmth", 8 }, { "comp", 3 }, { "deess", 2 }, { "body", 2 }, { "presence", 1 }, { "air", 1 }, { "gate", -90 } },
               {}, {}, dark (13000, 0.3f)),
        };
        return t;
    }
}

const std::vector<FxModel>& fxModelsFor (NodeType t)
{
    static const auto all = build();
    static const std::vector<FxModel> none;
    const auto it = all.find (t);
    return it != all.end() ? it->second : none;
}

} // namespace rig
