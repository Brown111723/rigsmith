#include "FactoryPresets.h"

namespace rig
{
namespace
{
    using V = std::vector<std::pair<juce::String, float>>;
    FactoryPreset fp (const char* name, const char* blurb, V v) { return { name, blurb, std::move (v) }; }

    std::map<NodeType, std::vector<FactoryPreset>> build()
    {
        std::map<NodeType, std::vector<FactoryPreset>> m;

        //--- Dynamics -----------------------------------------------------------
        m[NodeType::Gate] = {
            fp ("Transparent", "Just below the noise floor: kills hum between notes, never chops a note", { { "threshold", -66 }, { "release", 150 }, { "hold", 30 }, { "range", -40 } }),
            fp ("High-gain tight", "Stops hiss dead between palm-mutes", { { "threshold", -52 }, { "release", 40 }, { "hold", 10 }, { "range", -80 } }),
            fp ("Djent chug", "Fast, hard, keyed from the clean input like a Decimator X", { { "view", 1 }, { "key", 1 }, { "threshold", -45 }, { "attack", 0.1f }, { "hold", 5 }, { "release", 25 }, { "range", -90 }, { "hysteresis", 4 } }),
            fp ("Metal rhythm (keyed)", "Gate after the amp, listening to your guitar", { { "view", 1 }, { "key", 1 }, { "threshold", -50 }, { "attack", 0.3f }, { "hold", 15 }, { "release", 60 }, { "range", -80 }, { "hysteresis", 6 } }),
            fp ("Lead sustain", "Long tails survive; silence stays silent", { { "threshold", -62 }, { "release", 350 }, { "hold", 80 }, { "range", -60 } }),
            fp ("Hiss only (dual band)", "Leaves the lows alone, closes the highs where the hiss lives", { { "view", 1 }, { "bands", 1 }, { "threshold", -58 }, { "range", -6 }, { "highrange", -60 }, { "crossover", 2500 }, { "release", 120 } }),
            fp ("Soft downward", "A gentle 12 dB dip, more natural than a hard gate", { { "threshold", -55 }, { "release", 200 }, { "hold", 40 }, { "range", -12 } }),
            fp ("Palm-mute friendly", "Key ignores the low thump so chugs don't hold it open", { { "view", 1 }, { "threshold", -50 }, { "keyhpf", 250 }, { "release", 50 }, { "hold", 10 }, { "range", -80 } }),
            fp ("Pick attack (lookahead)", "Opens 2 ms early: no clipped transients on fast picking", { { "view", 1 }, { "threshold", -50 }, { "lookahead", 2 }, { "attack", 0.5f }, { "release", 40 }, { "hold", 10 }, { "range", -80 } }),
        };
        m[NodeType::Comp] = {
            fp ("Country squash", "The classic chicken-pickin' snap: fast attack, high ratio", { { "threshold", -30 }, { "ratio", 8 }, { "attack", 1 }, { "release", 120 }, { "knee", 3 }, { "mix", 100 } }),
            fp ("Clean sustain", "Evens out arpeggios, keeps the pick", { { "threshold", -24 }, { "ratio", 4 }, { "attack", 10 }, { "release", 200 }, { "knee", 8 }, { "mix", 100 } }),
            fp ("Parallel (NY)", "Heavy squash blended under the dry signal: punch with dynamics", { { "threshold", -36 }, { "ratio", 10 }, { "attack", 3 }, { "release", 150 }, { "knee", 6 }, { "mix", 40 } }),
            fp ("Dyna Comp", "The orange pedal: squishy, funky, lots of sustain", { { "threshold", -38 }, { "ratio", 12 }, { "attack", 2 }, { "release", 300 }, { "knee", 4 }, { "mix", 100 } }),
            fp ("Studio glue", "2:1, slow attack: barely there, just tidier", { { "threshold", -20 }, { "ratio", 2 }, { "attack", 30 }, { "release", 250 }, { "knee", 12 }, { "mix", 100 } }),
            fp ("Funk clean", "Tight and percussive for 16th-note strumming", { { "threshold", -28 }, { "ratio", 6 }, { "attack", 5 }, { "release", 80 }, { "knee", 4 }, { "mix", 85 } }),
            fp ("Lead before drive", "More sustain into the amp without more gain", { { "threshold", -26 }, { "ratio", 5 }, { "attack", 15 }, { "release", 250 }, { "knee", 6 }, { "mix", 100 } }),
            fp ("Slap guard", "Only catches the loudest peaks", { { "threshold", -12 }, { "ratio", 6 }, { "attack", 0.5f }, { "release", 60 }, { "knee", 2 }, { "mix", 100 } }),
            fp ("Bass: even fingers", "Levels out fingerstyle without squashing the note", { { "threshold", -22 }, { "ratio", 4 }, { "attack", 18 }, { "release", 180 }, { "knee", 8 }, { "mix", 100 } }),
            fp ("Bass: slap", "Fast and firm: catches pops, lets thumbs punch", { { "threshold", -18 }, { "ratio", 8 }, { "attack", 2 }, { "release", 90 }, { "knee", 3 }, { "mix", 100 } }),
            fp ("Bass: pick rock", "Slow attack keeps the click, long release holds the note", { { "threshold", -26 }, { "ratio", 5 }, { "attack", 30 }, { "release", 300 }, { "knee", 6 }, { "mix", 100 } }),
            fp ("Bass: parallel", "Crushed and blended under the clean bass", { { "threshold", -38 }, { "ratio", 12 }, { "attack", 5 }, { "release", 150 }, { "knee", 4 }, { "mix", 45 } }),
            fp ("Ambient swell sustain", "Long release for pads into reverb", { { "threshold", -32 }, { "ratio", 6 }, { "attack", 40 }, { "release", 800 }, { "knee", 10 }, { "mix", 100 } }),
        };
        m[NodeType::Limiter] = {
            fp ("Safety", "Catches stray peaks only", { { "drive", 0 }, { "ceiling", -1 }, { "release", 80 } }),
            fp ("Loud and even", "Pushes the level up 6 dB, flattens peaks", { { "drive", 6 }, { "ceiling", -1 }, { "release", 50 } }),
            fp ("Brickwall", "Hard ceiling for live rigs", { { "drive", 3 }, { "ceiling", -3 }, { "release", 30 } }),
            fp ("Smash", "Obvious, pumping limiting as an effect", { { "drive", 14 }, { "ceiling", -8 }, { "release", 15 } }),
            fp ("Transparent glue", "Slow release, a dB or two of catch", { { "drive", 2 }, { "ceiling", -0.5f }, { "release", 250 } }),
        };
        m[NodeType::Expander] = {
            fp ("Gentle noise reduction", "Lowers the hiss rather than chopping it", { { "threshold", -50 }, { "ratio", 2 }, { "range", -20 }, { "attack", 2 }, { "release", 150 } }),
            fp ("Tighten rhythm", "Shorter tails between chords", { { "threshold", -40 }, { "ratio", 4 }, { "range", -40 }, { "attack", 1 }, { "release", 80 } }),
            fp ("Clean-up after fuzz", "Fuzz hiss falls away as notes decay", { { "threshold", -45 }, { "ratio", 3 }, { "range", -35 }, { "attack", 1 }, { "release", 200 } }),
            fp ("Dynamic punch", "Soft notes softer, loud notes stay", { { "threshold", -30 }, { "ratio", 1.8f }, { "range", -12 }, { "attack", 5 }, { "release", 120 } }),
            fp ("Room taming", "For mic'd or reverb-heavy signals", { { "threshold", -42 }, { "ratio", 2.5f }, { "range", -24 }, { "attack", 3 }, { "release", 300 } }),
        };
        m[NodeType::Transient] = {
            fp ("More pick", "Sharper attack on every note", { { "attack", 50 }, { "sustain", 0 } }),
            fp ("Tight chug", "Punchy palm-mutes with short tails", { { "attack", 40 }, { "sustain", -40 } }),
            fp ("Soften picking", "Rounds off harsh attacks", { { "attack", -50 }, { "sustain", 10 } }),
            fp ("Bloom", "Notes swell into sustain", { { "attack", -30 }, { "sustain", 50 } }),
            fp ("Percussive clean", "Snappy funk and country", { { "attack", 70 }, { "sustain", -25 } }),
            fp ("Ambient wash", "Attack down, sustain up, into reverb", { { "attack", -70 }, { "sustain", 60 } }),
        };
        m[NodeType::Swell] = {
            fp ("Violin", "The classic volume-knob swell", { { "threshold", -45 }, { "rise", 400 }, { "rearm", 200 } }),
            fp ("Slow pad", "Long fade-ins for ambient chords", { { "threshold", -45 }, { "rise", 1500 }, { "rearm", 300 } }),
            fp ("Quick bow", "Short swell, still playable", { { "threshold", -48 }, { "rise", 150 }, { "rearm", 120 } }),
            fp ("Reverse-ish", "Very slow rise, sounds like a reversed note", { { "threshold", -42 }, { "rise", 2800 }, { "rearm", 500 } }),
            fp ("Sensitive", "Re-triggers on light picking", { { "threshold", -58 }, { "rise", 500 }, { "rearm", 80 } }),
        };

        //--- Drive ---------------------------------------------------------------
        // model numbers: see PedalBlock::overdriveModels()
        m[NodeType::Overdrive] = {
            fp ("TS808 boost (tighten)", "The metal trick: drive low, level up, into a dirty amp", { { "model", 0 }, { "voice", 0 }, { "drive", 1 }, { "tone", 6 }, { "level", 6 } }),
            fp ("TS808 at noon", "The classic mid-hump overdrive", { { "model", 0 }, { "voice", 0 }, { "drive", 5 }, { "tone", 5 } }),
            fp ("TS9 Texas lead", "SRV: drive low, tone up, into a loud clean amp", { { "model", 0 }, { "voice", 1 }, { "drive", 3 }, { "tone", 6.5f }, { "level", 4 } }),
            fp ("Klon: clean boost", "Gain low, output up: the 'always on' Klon", { { "model", 1 }, { "drive", 1.5f }, { "tone", 5.5f }, { "level", 6 } }),
            fp ("Klon: edge of breakup", "Gain at 10 o'clock", { { "model", 1 }, { "drive", 4 }, { "tone", 6 }, { "level", 3 } }),
            fp ("Klon: lead", "Gain up, treble up", { { "model", 1 }, { "drive", 8 }, { "tone", 6.5f }, { "level", -4 } }),
            fp ("SD-1 into a Marshall", "Zakk / EVH: drive low, level up", { { "model", 2 }, { "drive", 2.5f }, { "tone", 6 }, { "level", 6 } }),
            fp ("SD-1 crunch", "Rhythm crunch on a clean amp", { { "model", 2 }, { "drive", 6.5f }, { "tone", 5.5f } }),
            fp ("OCD LP: classic rock", "Drive at noon, vintage voice", { { "model", 3 }, { "voice", 0 }, { "drive", 5 }, { "tone", 5 } }),
            fp ("OCD HP: big crunch", "More bass, gain and output", { { "model", 3 }, { "voice", 1 }, { "drive", 6.5f }, { "tone", 5.5f }, { "level", -2 } }),
            fp ("Timmy: transparent", "Flat EQ, a little grit", { { "model", 4 }, { "voice", 0 }, { "drive", 3.5f }, { "bass", 7 }, { "treble", 6 }, { "level", 2 } }),
            fp ("Timmy: tightened", "Bass cut before the gain for high-gain amps", { { "model", 4 }, { "voice", 1 }, { "drive", 5 }, { "bass", 3 }, { "treble", 6 } }),
            fp ("Soul Food: sparkle", "Clean boost with a little hair", { { "model", 5 }, { "drive", 2.5f }, { "tone", 6 }, { "level", 5 } }),
            fp ("Morning Glory: chime", "Edge of breakup with chime", { { "model", 6 }, { "voice", 0 }, { "drive", 4 }, { "tone", 6 }, { "level", 2 } }),
            fp ("Morning Glory: hi gain", "Bluesbreaker pushed hard", { { "model", 6 }, { "voice", 1 }, { "drive", 7 }, { "tone", 5.5f } }),
            fp ("OD-1: vintage", "The first compact overdrive", { { "model", 7 }, { "drive", 6 } }),
            fp ("King of Tone: Trucks", "Warm, open slide tone", { { "model", 8 }, { "voice", 0 }, { "drive", 4.5f }, { "tone", 5 }, { "level", 2 } }),
            fp ("King of Tone: distortion side", "More gain, harder clipping", { { "model", 8 }, { "voice", 1 }, { "drive", 6 }, { "tone", 5.5f } }),
            fp ("King of Tone: boost side", "Barely clipping, just push", { { "model", 8 }, { "voice", 2 }, { "drive", 6 }, { "tone", 6 }, { "level", 2 } }),
            fp ("Lightspeed: organic", "Amp-like, flat, low gain", { { "model", 9 }, { "drive", 4 }, { "tone", 5 }, { "level", 2 } }),
            fp ("Bass-friendly (blend)", "Half clean, keeps the low end", { { "model", 0 }, { "drive", 6 }, { "tone", 5 }, { "blend", 50 } }),
        };
        m[NodeType::Distortion] = {
            fp ("DS-1: Satriani", "Dist and tone around 2 o'clock", { { "model", 0 }, { "voice", 0 }, { "drive", 7 }, { "tone", 6 } }),
            fp ("DS-1: Bleach", "Cobain: dist up, tone down", { { "model", 0 }, { "voice", 0 }, { "drive", 8.5f }, { "tone", 3.5f } }),
            fp ("DS-1W Ultra", "The Waza Craft's thicker mode", { { "model", 0 }, { "voice", 1 }, { "drive", 7 }, { "tone", 5 } }),
            fp ("RAT: dirty boost", "Distortion low: grit and push", { { "model", 1 }, { "voice", 0 }, { "drive", 3 }, { "tone", 3 }, { "level", 2 } }),
            fp ("RAT: classic", "Distortion at 2 o'clock, filter at 10", { { "model", 1 }, { "voice", 0 }, { "drive", 7 }, { "tone", 3.5f } }),
            fp ("RAT: fuzz", "Everything up, filter down: the RAT's fuzz", { { "model", 1 }, { "voice", 0 }, { "drive", 10 }, { "tone", 2 }, { "level", -2 } }),
            fp ("Turbo RAT", "LED clipping: louder and more open", { { "model", 1 }, { "voice", 1 }, { "drive", 7 }, { "tone", 4 } }),
            fp ("TS808 into a hot amp", "Drive 0, level up", { { "model", 2 }, { "drive", 0.5f }, { "tone", 6 }, { "level", 6 } }),
            fp ("Big Muff: Gilmour", "Comfortably Numb: sustain up, tone at noon", { { "model", 3 }, { "voice", 0 }, { "drive", 7.5f }, { "tone", 5.5f } }),
            fp ("Big Muff: Corgan", "Siamese Dream wall of fuzz", { { "model", 3 }, { "voice", 1 }, { "drive", 9 }, { "tone", 4 } }),
            fp ("Big Muff: Triangle mids", "Less scoop, more cut", { { "model", 3 }, { "voice", 2 }, { "drive", 8 }, { "tone", 6 } }),
            fp ("Metal Zone: scooped", "Lows and highs up, mids down", { { "model", 4 }, { "drive", 7 }, { "bass", 7 }, { "mid", 3 }, { "midfreq", 700 }, { "treble", 7 } }),
            fp ("Metal Zone: done right", "Mids up around 1 kHz, lows tamed", { { "model", 4 }, { "drive", 6 }, { "bass", 4 }, { "mid", 7 }, { "midfreq", 1000 }, { "treble", 5 } }),
            fp ("HM-2: chainsaw", "Everything on 10: Swedish death metal", { { "model", 5 }, { "voice", 0 }, { "drive", 10 }, { "bass", 10 }, { "treble", 10 }, { "level", -4 } }),
            fp ("HM-2: tamed", "Big lows and mids, not maxed", { { "model", 5 }, { "voice", 0 }, { "drive", 7 }, { "bass", 6 }, { "treble", 5 } }),
            fp ("Shredmaster: Creep", "Gain up, contour at noon", { { "model", 6 }, { "drive", 8 }, { "bass", 5 }, { "mid", 5 }, { "treble", 6 } }),
            fp ("Shredmaster: thrash", "Scooped and tight", { { "model", 6 }, { "drive", 7 }, { "bass", 6 }, { "mid", 8 }, { "treble", 6.5f } }),
            fp ("OCD HP: high gain", "Driven hard, HP mode", { { "model", 7 }, { "voice", 1 }, { "drive", 8.5f }, { "tone", 5.5f }, { "level", -3 } }),
            fp ("Distortion+: Rhoads", "Distortion up, into a Marshall", { { "model", 8 }, { "drive", 7 } }),
            fp ("BE-OD: plexi roar", "Gain at noon, tight up a bit", { { "model", 9 }, { "drive", 5 }, { "tight", 6 }, { "bass", 5 }, { "treble", 5.5f }, { "presence", 5 } }),
            fp ("BE-OD: modern metal", "Gain up, tight up, presence up", { { "model", 9 }, { "drive", 8 }, { "tight", 8 }, { "bass", 5.5f }, { "treble", 6 }, { "presence", 6.5f } }),
        };
        m[NodeType::Fuzz] = {
            fp ("Big Muff", "Thick, sustaining wall of fuzz", { { "fuzz", 8 }, { "tone", 5 }, { "level", -6 }, { "gate", -75 } }),
            fp ("Fuzz Face", "Vintage, clean-up with the guitar volume", { { "fuzz", 6 }, { "tone", 6 }, { "level", -4 }, { "gate", -85 } }),
            fp ("Tone Bender", "Raspy 60s British fuzz", { { "fuzz", 7 }, { "tone", 7 }, { "level", -5 }, { "gate", -80 } }),
            fp ("Octavia", "Octave-up fuzz, best on the neck pickup", { { "fuzz", 7.5f }, { "tone", 6 }, { "level", -6 }, { "octave", 1 } }),
            fp ("Doom", "Dark, gated, enormous", { { "fuzz", 10 }, { "tone", 3 }, { "level", -8 }, { "gate", -55 } }),
            fp ("Velcro", "Sputtering, gated, broken-sounding", { { "fuzz", 9 }, { "tone", 5 }, { "level", -6 }, { "gate", -45 } }),
            fp ("Mild fuzz", "Low fuzz, more grit than wall", { { "fuzz", 3.5f }, { "tone", 6 }, { "level", -2 }, { "gate", -85 } }),
            fp ("Bright lead", "Cutting fuzz lead", { { "fuzz", 8 }, { "tone", 8 }, { "level", -6 }, { "gate", -70 } }),
        };
        m[NodeType::Boost] = {
            fp ("Clean boost", "Solo boost, flat", { { "gain", 6 }, { "tight", 40 }, { "bright", 0 } }),
            fp ("Treble booster", "Rangemaster-style into a dirty amp", { { "gain", 12 }, { "tight", 250 }, { "bright", 5 } }),
            fp ("Tight metal boost", "Cuts the flub before a high-gain amp", { { "gain", 10 }, { "tight", 180 }, { "bright", 2 } }),
            fp ("Mid push", "More presence in the mix", { { "gain", 4 }, { "tight", 120 }, { "bright", 1 } }),
            fp ("Fat boost", "Full-range level up", { { "gain", 8 }, { "tight", 20 }, { "bright", -2 } }),
            fp ("EP-3 sparkle", "Always-on preamp sweetener", { { "gain", 3 }, { "tight", 30 }, { "bright", 2 } }),
            fp ("Max smash", "Drives the amp as hard as it goes", { { "gain", 16 }, { "tight", 150 }, { "bright", 3 } }),
        };
        m[NodeType::Crusher] = {
            fp ("8-bit", "Chiptune grit", { { "bits", 8 }, { "down", 4 }, { "tone", 10000 }, { "mix", 100 } }),
            fp ("Lo-fi blend", "A little dirt under the dry tone", { { "bits", 10 }, { "down", 3 }, { "tone", 8000 }, { "mix", 30 } }),
            fp ("Radio", "Narrow, crunchy, AM-radio", { { "bits", 6 }, { "down", 6 }, { "tone", 3500 }, { "mix", 100 } }),
            fp ("Destroyed", "Total digital ruin", { { "bits", 3 }, { "down", 16 }, { "tone", 6000 }, { "mix", 100 } }),
            fp ("Sample-rate alias", "Metallic aliasing, full bit depth", { { "bits", 16 }, { "down", 12 }, { "tone", 20000 }, { "mix", 80 } }),
            fp ("Subtle texture", "Barely-there grain", { { "bits", 12 }, { "down", 2 }, { "tone", 12000 }, { "mix", 20 } }),
        };

        //--- Filter ---------------------------------------------------------------
        m[NodeType::Wah] = {
            fp ("Bass: envelope funk", "Opens when you dig in: Bootsy-style auto-wah that keeps the lows", { { "model", 0 }, { "mode", 1 }, { "sens", 65 }, { "low", 200 }, { "high", 1500 }, { "q", 5.5f }, { "mix", 75 } }),
            fp ("Cry Baby GCB95", "The stock Cry Baby sweep", { { "model", 0 }, { "mode", 0 }, { "low", 350 }, { "high", 2200 }, { "q", 6.5f }, { "mix", 100 } }),
            fp ("Vox V847A", "Smoother, vocal: Clapton in Cream", { { "model", 1 }, { "mode", 0 }, { "low", 450 }, { "high", 1800 }, { "q", 4.2f }, { "mix", 100 } }),
            fp ("535Q: deep and boosted", "Range 2, high Q, +6 dB for solos", { { "model", 2 }, { "mode", 0 }, { "range", 1 }, { "q", 9 }, { "boost", 6 }, { "mix", 100 } }),
            fp ("535Q: bright funk", "Range 5, medium Q", { { "model", 2 }, { "mode", 0 }, { "range", 4 }, { "q", 5.5f }, { "mix", 100 } }),
            fp ("Bad Horsie (switchless)", "Rock it to turn it on, rest heel-down to turn it off", { { "model", 3 }, { "mode", 0 }, { "low", 330 }, { "high", 2700 }, { "q", 7.5f }, { "auto", 1 }, { "mix", 100 } }),
            fp ("XW-1: vintage Italian", "Low Q, bias up, a touch of treble", { { "model", 4 }, { "mode", 0 }, { "q", 4 }, { "bias", 7 }, { "treble", 2 }, { "low", 400 }, { "high", 2000 }, { "mix", 100 } }),
            fp ("XW-1: modern funk", "High Q, more bass", { { "model", 4 }, { "mode", 0 }, { "q", 8 }, { "bias", 4 }, { "bass", 3 }, { "low", 350 }, { "high", 2500 }, { "mix", 100 } }),
            fp ("Parked wah", "Fixed half-cocked: the Money for Nothing honk", { { "model", 0 }, { "mode", 0 }, { "position", 55 }, { "low", 380 }, { "high", 2200 }, { "q", 7 }, { "mix", 100 } }),
            fp ("Auto-wah funk", "Follows your picking: dig in to open it", { { "model", 0 }, { "mode", 1 }, { "sens", 70 }, { "low", 350 }, { "high", 2400 }, { "q", 7 }, { "mix", 100 } }),
            fp ("Mu-Tron-style envelope", "Quacky and deep", { { "model", 3 }, { "mode", 1 }, { "sens", 85 }, { "low", 250 }, { "high", 3000 }, { "q", 10 }, { "mix", 100 } }),
            fp ("Slow LFO sweep", "Hands-free wah", { { "model", 1 }, { "mode", 2 }, { "rate", 0.5f }, { "low", 400 }, { "high", 2000 }, { "q", 5 }, { "mix", 100 } }),
            fp ("Fast wobble", "Rhythmic LFO wah", { { "model", 0 }, { "mode", 2 }, { "rate", 4 }, { "low", 400 }, { "high", 2000 }, { "q", 6 }, { "mix", 100 } }),
        };

        //--- Modulation ------------------------------------------------------------
        m[NodeType::Chorus] = {
            fp ("Bass chorus", "Slow and gentle: movement without losing the low end", { { "rate", 0.5f }, { "depth", 30 }, { "delay", 9 }, { "width", 100 }, { "mix", 28 } }),
            fp ("CE-2", "Warm 80s chorus", { { "rate", 0.8f }, { "depth", 55 }, { "delay", 12 }, { "width", 80 }, { "mix", 50 } }),
            fp ("Subtle shimmer", "Slow and shallow, always-on", { { "rate", 0.35f }, { "depth", 25 }, { "delay", 10 }, { "width", 100 }, { "mix", 35 } }),
            fp ("80s clean", "Wide, lush, Andy Summers", { { "rate", 0.6f }, { "depth", 70 }, { "delay", 15 }, { "width", 100 }, { "mix", 50 } }),
            fp ("Seasick", "Deep and slow, Nirvana-style", { { "rate", 0.9f }, { "depth", 100 }, { "delay", 18 }, { "width", 60 }, { "mix", 55 } }),
            fp ("Doubler", "Short delay, little movement: two guitars", { { "rate", 0.2f }, { "depth", 15 }, { "delay", 22 }, { "width", 100 }, { "mix", 50 } }),
            fp ("Leslie-ish", "Fast, wide wobble", { { "rate", 4.5f }, { "depth", 40 }, { "delay", 8 }, { "width", 100 }, { "mix", 50 } }),
            fp ("Dimension D", "Very subtle stereo widening", { { "rate", 0.25f }, { "depth", 20 }, { "delay", 14 }, { "width", 100 }, { "mix", 45 } }),
            fp ("Vibrato", "Fully wet: pitch wobble only", { { "rate", 5 }, { "depth", 35 }, { "delay", 6 }, { "width", 0 }, { "mix", 100 } }),
        };
        m[NodeType::Flanger] = {
            fp ("Jet", "Slow sweep, lots of feedback", { { "rate", 0.15f }, { "depth", 85 }, { "manual", 2 }, { "feedback", 75 }, { "mix", 50 } }),
            fp ("Electric Mistress", "Classic, silky", { { "rate", 0.3f }, { "depth", 70 }, { "manual", 1.5f }, { "feedback", 45 }, { "mix", 50 } }),
            fp ("Van Halen", "Unchained: slow, deep, resonant", { { "rate", 0.2f }, { "depth", 90 }, { "manual", 2.5f }, { "feedback", 65 }, { "mix", 50 } }),
            fp ("Through-zero feel", "Negative feedback, hollow", { { "rate", 0.25f }, { "depth", 80 }, { "manual", 1 }, { "feedback", -60 }, { "mix", 50 } }),
            fp ("Subtle", "Gentle, chorus-like movement", { { "rate", 0.4f }, { "depth", 40 }, { "manual", 3 }, { "feedback", 20 }, { "mix", 35 } }),
            fp ("Metallic", "Static comb filter, robotic", { { "rate", 0.02f }, { "depth", 5 }, { "manual", 0.8f }, { "feedback", 85 }, { "mix", 50 } }),
            fp ("Fast whoosh", "Quick, rhythmic", { { "rate", 2.5f }, { "depth", 60 }, { "manual", 1.5f }, { "feedback", 50 }, { "mix", 50 } }),
        };
        m[NodeType::Phaser] = {
            fp ("Phase 90 (script)", "Vintage, no feedback, Eruption", { { "rate", 0.45f }, { "depth", 80 }, { "centre", 700 }, { "feedback", 15 }, { "stages", 0 }, { "mix", 50 } }),
            fp ("Phase 90 (block)", "More resonant, swirlier", { { "rate", 0.5f }, { "depth", 85 }, { "centre", 700 }, { "feedback", 45 }, { "stages", 0 }, { "mix", 50 } }),
            fp ("Small Stone", "Deep, liquid, 70s", { { "rate", 0.3f }, { "depth", 100 }, { "centre", 900 }, { "feedback", 60 }, { "stages", 1 }, { "mix", 50 } }),
            fp ("Uni-Vibe-ish", "Throbbing 4-stage", { { "rate", 1.5f }, { "depth", 90 }, { "centre", 600 }, { "feedback", 20 }, { "stages", 0 }, { "mix", 55 } }),
            fp ("Slow ambient", "Very slow 12-stage sweep", { { "rate", 0.1f }, { "depth", 100 }, { "centre", 800 }, { "feedback", 50 }, { "stages", 3 }, { "mix", 50 } }),
            fp ("Funk", "Fast, shallow, for clean rhythm", { { "rate", 2.5f }, { "depth", 60 }, { "centre", 1000 }, { "feedback", 30 }, { "stages", 1 }, { "mix", 50 } }),
            fp ("Resonant sweep", "Vocal, high feedback", { { "rate", 0.6f }, { "depth", 90 }, { "centre", 1200 }, { "feedback", 80 }, { "stages", 2 }, { "mix", 50 } }),
        };
        m[NodeType::Tremolo] = {
            fp ("Fender opto", "Smooth, medium, amp tremolo", { { "rate", 5 }, { "depth", 55 }, { "shape", 0 } }),
            fp ("Boulder Canyon", "Slow and deep", { { "rate", 2.5f }, { "depth", 80 }, { "shape", 0 } }),
            fp ("Surf", "Fast, choppy, with reverb", { { "rate", 7 }, { "depth", 70 }, { "shape", 1 } }),
            fp ("Helicopter", "Hard square chop", { { "rate", 6 }, { "depth", 100 }, { "shape", 2 } }),
            fp ("Brownface harmonic", "Phasey 60s harmonic vibrato", { { "rate", 4 }, { "depth", 60 }, { "shape", 3 } }),
            fp ("Stereo panner", "Ping-pongs between speakers", { { "rate", 1.5f }, { "depth", 90 }, { "shape", 0 }, { "stereo", 1 } }),
            fp ("Bo Diddley", "Fast and strong", { { "rate", 8 }, { "depth", 85 }, { "shape", 0 } }),
            fp ("Gentle pulse", "Subtle movement under chords", { { "rate", 3.5f }, { "depth", 30 }, { "shape", 0 } }),
        };
        m[NodeType::Vibe] = {
            fp ("Machine Gun", "Hendrix at the Fillmore", { { "rate", 2.5f }, { "intensity", 80 }, { "mode", 0 } }),
            fp ("Slow throb", "Robin Trower, Bridge of Sighs", { { "rate", 0.8f }, { "intensity", 90 }, { "mode", 0 } }),
            fp ("Fast swirl", "Rotating-speaker feel", { { "rate", 6 }, { "intensity", 70 }, { "mode", 0 } }),
            fp ("Vibrato", "Pitch wobble only", { { "rate", 4.5f }, { "intensity", 60 }, { "mode", 1 } }),
            fp ("Subtle", "Gentle movement for clean chords", { { "rate", 1.5f }, { "intensity", 45 }, { "mode", 0 } }),
        };
        m[NodeType::Rotary] = {
            fp ("Leslie slow", "Chorale: the classic organ sound", { { "speed", 0 }, { "ramp", 1.2f }, { "balance", 60 }, { "width", 80 }, { "drive", 20 } }),
            fp ("Leslie fast", "Tremolo speed", { { "speed", 1 }, { "ramp", 1.2f }, { "balance", 60 }, { "width", 80 }, { "drive", 20 } }),
            fp ("Gritty fast", "Overdriven cabinet, fast", { { "speed", 1 }, { "ramp", 0.8f }, { "balance", 55 }, { "width", 90 }, { "drive", 65 } }),
            fp ("Drum heavy", "More low rotor, warmer", { { "speed", 0 }, { "ramp", 2 }, { "balance", 30 }, { "width", 70 }, { "drive", 25 } }),
            fp ("Horn heavy", "More shimmer, brighter", { { "speed", 0 }, { "ramp", 1 }, { "balance", 85 }, { "width", 90 }, { "drive", 15 } }),
            fp ("Brake", "Stopped rotors: a fixed, voiced tone", { { "speed", 2 }, { "ramp", 1.5f }, { "balance", 60 }, { "width", 70 }, { "drive", 20 } }),
        };

        //--- Delay -----------------------------------------------------------------
        m[NodeType::Delay] = {
            fp ("Slapback", "Rockabilly, country, 50s: one quick repeat", { { "mode", 1 }, { "time", 110 }, { "feedback", 5 }, { "mix", 35 }, { "lowcut", 150 }, { "highcut", 4500 }, { "mod", 10 } }),
            fp ("Dotted eighth (120 BPM)", "The Edge: 375 ms, three or four repeats", { { "mode", 0 }, { "time", 375 }, { "feedback", 40 }, { "mix", 35 }, { "lowcut", 200 }, { "highcut", 6000 }, { "mod", 5 } }),
            fp ("Quarter note (120 BPM)", "Rhythmic repeats in time", { { "mode", 0 }, { "time", 500 }, { "feedback", 35 }, { "mix", 30 }, { "lowcut", 150 }, { "highcut", 6000 } }),
            fp ("Gilmour lead", "Long, warm tape echo behind solos", { { "mode", 1 }, { "time", 480 }, { "feedback", 45 }, { "mix", 30 }, { "lowcut", 150 }, { "highcut", 3500 }, { "mod", 25 } }),
            fp ("Space Echo", "Dark, wobbly tape with long tails", { { "mode", 1 }, { "time", 330 }, { "feedback", 60 }, { "mix", 35 }, { "lowcut", 200 }, { "highcut", 2800 }, { "mod", 40 } }),
            fp ("Ambient wash", "Long ping-pong, ducked under your playing", { { "mode", 2 }, { "time", 700 }, { "feedback", 70 }, { "mix", 45 }, { "lowcut", 300 }, { "highcut", 4000 }, { "mod", 30 }, { "duck", 50 } }),
            fp ("Lead (ducked)", "Repeats only between phrases", { { "mode", 0 }, { "time", 420 }, { "feedback", 40 }, { "mix", 40 }, { "lowcut", 150 }, { "highcut", 5000 }, { "duck", 70 } }),
            fp ("Doubler", "Very short: thickens like two guitars", { { "mode", 3 }, { "time", 30 }, { "feedback", 0 }, { "mix", 40 }, { "spread", 100 }, { "mod", 20 } }),
            fp ("Ping-pong wide", "Bounces left to right", { { "mode", 2 }, { "time", 400 }, { "feedback", 45 }, { "mix", 35 }, { "spread", 100 } }),
            fp ("Analog BBD", "Murky, Carbon Copy-style", { { "mode", 1 }, { "time", 350 }, { "feedback", 45 }, { "mix", 30 }, { "lowcut", 120 }, { "highcut", 2500 }, { "mod", 35 } }),
            fp ("Self-oscillate", "Runaway feedback for effects (careful!)", { { "mode", 1 }, { "time", 250 }, { "feedback", 98 }, { "mix", 40 }, { "lowcut", 300 }, { "highcut", 3000 }, { "mod", 30 } }),
            fp ("Dual rhythmic", "Two taps in the stereo field", { { "mode", 3 }, { "time", 450 }, { "feedback", 35 }, { "mix", 35 }, { "spread", 80 } }),
            fp ("Clean digital", "Pristine, full-range repeats", { { "mode", 0 }, { "time", 400 }, { "feedback", 30 }, { "mix", 25 }, { "lowcut", 20 }, { "highcut", 16000 }, { "mod", 0 } }),
        };

        //--- Reverb ----------------------------------------------------------------
        m[NodeType::Reverb] = {
            fp ("Small room", "Natural, a touch of space", { { "type", 0 }, { "decay", 0.6f }, { "predelay", 5 }, { "size", 30 }, { "damping", 50 }, { "lowcut", 150 }, { "mix", 20 } }),
            fp ("Studio room", "Tight live room", { { "type", 0 }, { "decay", 1.0f }, { "predelay", 10 }, { "size", 45 }, { "damping", 45 }, { "lowcut", 150 }, { "mix", 22 } }),
            fp ("Concert hall", "Big and smooth", { { "type", 1 }, { "decay", 2.8f }, { "predelay", 30 }, { "size", 75 }, { "damping", 40 }, { "lowcut", 180 }, { "mix", 25 } }),
            fp ("Plate", "Bright, dense, the studio classic", { { "type", 2 }, { "decay", 1.8f }, { "predelay", 15 }, { "size", 55 }, { "damping", 30 }, { "lowcut", 200 }, { "mix", 25 } }),
            fp ("Spring (Fender '65)", "Amp spring tank, dial it up for surf", { { "type", 3 }, { "decay", 2 }, { "predelay", 0 }, { "size", 50 }, { "damping", 35 }, { "lowcut", 200 }, { "mix", 30 } }),
            fp ("Surf drip", "Wet, splashy spring", { { "type", 3 }, { "decay", 3 }, { "predelay", 0 }, { "size", 60 }, { "damping", 25 }, { "lowcut", 150 }, { "mix", 50 } }),
            fp ("Shimmer", "Octave-up ambience, Eno / U2", { { "type", 4 }, { "decay", 7 }, { "predelay", 40 }, { "size", 85 }, { "damping", 40 }, { "lowcut", 250 }, { "shimmer", 60 }, { "mix", 40 } }),
            fp ("Ambient cathedral", "Huge, long, modulated", { { "type", 1 }, { "decay", 9 }, { "predelay", 60 }, { "size", 100 }, { "damping", 50 }, { "lowcut", 250 }, { "mod", 50 }, { "mix", 40 } }),
            fp ("Lead plate", "Pre-delay keeps the solo clear", { { "type", 2 }, { "decay", 2.2f }, { "predelay", 80 }, { "size", 60 }, { "damping", 35 }, { "lowcut", 250 }, { "mix", 25 } }),
            fp ("Always-on", "Just enough to not sound dry", { { "type", 0 }, { "decay", 0.8f }, { "predelay", 8 }, { "size", 35 }, { "damping", 55 }, { "lowcut", 200 }, { "mix", 12 } }),
            fp ("Dark hall", "Warm, damped tail", { { "type", 1 }, { "decay", 3.5f }, { "predelay", 25 }, { "size", 80 }, { "damping", 75 }, { "lowcut", 150 }, { "mix", 25 } }),
            fp ("Infinite pad", "Near-endless sustain", { { "type", 1 }, { "decay", 20 }, { "predelay", 20 }, { "size", 100 }, { "damping", 40 }, { "lowcut", 300 }, { "mod", 60 }, { "mix", 50 } }),
        };

        //--- Pitch -------------------------------------------------------------------
        m[NodeType::Pitch] = {
            fp ("Bass: sub octave", "A dark octave under the bass, OC-2 style synth lines", { { "mode", 0 }, { "mix", 45 }, { "tone", 1800 } }),
            fp ("Octave down (bass)", "Fake bass under your riffs", { { "mode", 0 }, { "mix", 50 }, { "tone", 6000 } }),
            fp ("Octave up", "Bright, 12-string-ish", { { "mode", 1 }, { "mix", 40 }, { "tone", 9000 } }),
            fp ("Harmony: fifth", "Power-chord from single notes", { { "mode", 2 }, { "semis", 7 }, { "mix", 45 } }),
            fp ("Harmony: fourth", "Classic dual-guitar interval", { { "mode", 2 }, { "semis", 5 }, { "mix", 45 } }),
            fp ("Harmony: major third", "Sweet harmony (in major keys)", { { "mode", 2 }, { "semis", 4 }, { "mix", 40 } }),
            fp ("Harmony: minor third", "Moody harmony", { { "mode", 2 }, { "semis", 3 }, { "mix", 40 } }),
            fp ("Drop tune -1", "Half step down without retuning", { { "mode", 2 }, { "semis", -1 }, { "mix", 100 } }),
            fp ("Drop tune -2", "Whole step down without retuning", { { "mode", 2 }, { "semis", -2 }, { "mix", 100 } }),
            fp ("Doubler (wide)", "Two slightly detuned takes", { { "mode", 3 }, { "detune", 12 }, { "mix", 50 } }),
            fp ("Doubler (subtle)", "Light thickening", { { "mode", 3 }, { "detune", 6 }, { "mix", 35 } }),
            fp ("Whammy +12 (wet)", "Octave up, fully wet", { { "mode", 1 }, { "mix", 100 }, { "tone", 12000 } }),
        };

        //--- EQ, utility -----------------------------------------------------------------
        m[NodeType::Eq] = {
            fp ("Tighten (pre-amp)", "Cut lows before a high-gain amp", { { "hpf", 120 }, { "low", -3 }, { "lowfreq", 150 }, { "mid", 0 }, { "high", 0 }, { "lpf", 20000 } }),
            fp ("Scooped metal", "The classic V curve", { { "low", 4 }, { "lowfreq", 100 }, { "mid", -6 }, { "midfreq", 600 }, { "midq", 0.8f }, { "high", 3 }, { "highfreq", 4000 } }),
            fp ("Mid push (cut through)", "More presence in a busy mix", { { "mid", 4 }, { "midfreq", 900 }, { "midq", 0.9f }, { "hpf", 80 } }),
            fp ("Post-cab cleanup", "Low cut 80 Hz, high cut 7.5 kHz: the mix-ready recipe", { { "hpf", 80 }, { "lpf", 7500 } }),
            fp ("Fizz remover", "Tames 3-6 kHz harshness on high gain", { { "high", -4 }, { "highfreq", 5000 }, { "lpf", 9000 } }),
            fp ("Mud cut", "Clears 250-400 Hz boom", { { "mid", -4 }, { "midfreq", 300 }, { "midq", 1.2f }, { "hpf", 70 } }),
            fp ("Bright clean", "Sparkle for clean tones", { { "high", 4 }, { "highfreq", 6000 }, { "low", -2 }, { "lowfreq", 120 } }),
            fp ("Warm jazz", "Rolled-off top, round lows", { { "high", -5 }, { "highfreq", 3000 }, { "low", 2 }, { "lowfreq", 150 }, { "lpf", 6000 } }),
            fp ("Telephone", "Band-limited lo-fi", { { "hpf", 400 }, { "lpf", 3000 }, { "mid", 6 }, { "midfreq", 1500 } }),
            fp ("Solo boost (mids)", "Lifts a lead without more gain", { { "mid", 5 }, { "midfreq", 1200 }, { "midq", 0.7f } }),
            fp ("Bass guitar", "Lows and growl", { { "low", 3 }, { "lowfreq", 80 }, { "mid", 3 }, { "midfreq", 800 }, { "high", -2 }, { "highfreq", 5000 } }),
        };
        m[NodeType::Gain] = {
            fp ("-6 dB pad", "Tame a hot block", { { "gain", -6 }, { "pan", 0 } }),
            fp ("-12 dB pad", "Big level drop", { { "gain", -12 }, { "pan", 0 } }),
            fp ("+3 dB", "A small lift", { { "gain", 3 }, { "pan", 0 } }),
            fp ("+6 dB solo", "Solo level", { { "gain", 6 }, { "pan", 0 } }),
            fp ("Pan left", "Hard left (use in a split)", { { "gain", 0 }, { "pan", -1 } }),
            fp ("Pan right", "Hard right (use in a split)", { { "gain", 0 }, { "pan", 1 } }),
        };
        m[NodeType::Cab] = {
            fp ("Modern metal", "V30 4x12, a 57 just off the cap, tight low cut", { { "cab", 6 }, { "mic", 0 }, { "position", 22 }, { "distance", 5 }, { "room", 0 }, { "lowcut", 90 }, { "highcut", 9500 } }),
            fp ("Classic rock stack", "Greenbacks with a 421, a little further out", { { "cab", 5 }, { "mic", 1 }, { "position", 35 }, { "distance", 12 }, { "room", 8 }, { "lowcut", 70 }, { "highcut", 20000 } }),
            fp ("Glassy clean", "Blackface 1x12, condenser, some air", { { "cab", 0 }, { "mic", 3 }, { "position", 30 }, { "distance", 35 }, { "room", 15 }, { "lowcut", 60 }, { "highcut", 20000 } }),
            fp ("British chime", "Alnico Blues with a 57 on the cap", { { "cab", 2 }, { "mic", 0 }, { "position", 15 }, { "distance", 8 }, { "room", 5 }, { "lowcut", 80 }, { "highcut", 20000 } }),
            fp ("Smooth lead (ribbon)", "Creambacks and a ribbon: thick, no fizz", { { "cab", 8 }, { "mic", 2 }, { "position", 25 }, { "distance", 15 }, { "room", 6 }, { "lowcut", 80 }, { "highcut", 20000 } }),
            fp ("Scooped 80s", "T75s, mic on the cap", { { "cab", 7 }, { "mic", 0 }, { "position", 10 }, { "distance", 5 }, { "room", 0 }, { "lowcut", 85 }, { "highcut", 11000 } }),
            fp ("Tweed breakup", "Small tweed combo, mic at the edge", { { "cab", 1 }, { "mic", 1 }, { "position", 60 }, { "distance", 20 }, { "room", 12 }, { "lowcut", 70 }, { "highcut", 20000 } }),
            fp ("Pedal platform", "Jazz clean 2x12: flat and wide", { { "cab", 3 }, { "mic", 3 }, { "position", 30 }, { "distance", 25 }, { "room", 10 }, { "lowcut", 60 }, { "highcut", 20000 } }),
            fp ("Dark & woolly", "Mic at the edge of the cone, pulled back", { { "cab", 5 }, { "mic", 2 }, { "position", 85 }, { "distance", 40 }, { "room", 10 }, { "lowcut", 60 }, { "highcut", 6500 } }),
            fp ("Room mic", "Far back: the cab in a space", { { "cab", 6 }, { "mic", 3 }, { "position", 40 }, { "distance", 100 }, { "room", 45 }, { "lowcut", 80 }, { "highcut", 20000 } }),
            fp ("Bass: rock 8x10", "The fridge with a bass dynamic", { { "cab", 10 }, { "mic", 4 }, { "position", 30 }, { "distance", 15 }, { "room", 0 }, { "lowcut", 20 }, { "highcut", 20000 } }),
            fp ("Bass: modern slap", "4x10 with a horn, condenser", { { "cab", 11 }, { "mic", 3 }, { "position", 25 }, { "distance", 20 }, { "room", 5 }, { "lowcut", 20 }, { "highcut", 20000 } }),
            fp ("Bass: Motown", "The flip-top 1x15: round and soft", { { "cab", 12 }, { "mic", 2 }, { "position", 45 }, { "distance", 20 }, { "room", 8 }, { "lowcut", 20 }, { "highcut", 20000 } }),
            fp ("Bass: dub", "2x15s, everything below", { { "cab", 13 }, { "mic", 4 }, { "position", 60 }, { "distance", 25 }, { "room", 0 }, { "lowcut", 20 }, { "highcut", 4000 } }),
            fp ("Bass: driven", "Neo 2x12 and a 421: takes distortion without fizz", { { "cab", 14 }, { "mic", 1 }, { "position", 30 }, { "distance", 10 }, { "room", 0 }, { "lowcut", 30 }, { "highcut", 9000 } }),
        };
        m[NodeType::BassDrive] = {
            fp ("SansAmp: rock DI", "The Bass Driver's classic: drive at 11, presence up", { { "model", 0 }, { "voice", 0 }, { "drive", 5 }, { "bass", 6 }, { "treble", 5.5f }, { "presence", 6 }, { "blend", 10 } }),
            fp ("SansAmp: fat tube", "Rounder: less presence, more bass", { { "model", 0 }, { "voice", 0 }, { "drive", 3.5f }, { "bass", 7 }, { "treble", 4.5f }, { "presence", 3.5f }, { "blend", 20 } }),
            fp ("B3K: djent grind", "Bright and tight over a clean low end", { { "model", 1 }, { "voice", 1 }, { "drive", 6.5f }, { "tone", 7 }, { "blend", 40 } }),
            fp ("B3K: fat growl", "Grunt on Fat, less top", { { "model", 1 }, { "voice", 0 }, { "drive", 5 }, { "tone", 4.5f }, { "blend", 30 } }),
            fp ("B7K: modern metal", "Scooped mids, clanky top, half clean", { { "model", 2 }, { "voice", 1 }, { "drive", 6 }, { "bass", 6 }, { "mid", 4 }, { "midfreq", 500 }, { "treble", 7 }, { "blend", 45 } }),
            fp ("B7K: prog punch", "Mids forward at 1 kHz", { { "model", 2 }, { "voice", 0 }, { "drive", 4.5f }, { "bass", 5.5f }, { "mid", 6.5f }, { "midfreq", 1000 }, { "treble", 5.5f }, { "blend", 40 } }),
            fp ("Alpha: tight", "Punchy and defined", { { "model", 3 }, { "voice", 0 }, { "drive", 5.5f }, { "bass", 5.5f }, { "treble", 5.5f }, { "blend", 30 } }),
            fp ("Omega: brutal", "Raw, wide-open distortion", { { "model", 3 }, { "voice", 1 }, { "drive", 7 }, { "bass", 6 }, { "treble", 6 }, { "blend", 25 } }),
            fp ("SVT: clean and huge", "Gain low: the big warm clean", { { "model", 4 }, { "voice", 0 }, { "drive", 3 }, { "bass", 6 }, { "mid", 5.5f }, { "midfreq", 800 }, { "treble", 5 }, { "blend", 0 } }),
            fp ("SVT: dug in", "Gain up until it growls", { { "model", 4 }, { "voice", 0 }, { "drive", 7 }, { "bass", 5.5f }, { "mid", 6 }, { "midfreq", 800 }, { "treble", 5.5f }, { "blend", 0 } }),
            fp ("SVT: Ultra Lo", "The scooped, sub-heavy switch", { { "model", 4 }, { "voice", 1 }, { "drive", 4 }, { "bass", 5.5f }, { "mid", 5 }, { "midfreq", 800 }, { "treble", 5 }, { "blend", 0 } }),
            fp ("M80: slap (Color)", "Mid scoop in, a touch of gain", { { "model", 5 }, { "voice", 1 }, { "drive", 2.5f }, { "bass", 5.5f }, { "mid", 5 }, { "treble", 6 }, { "blend", 60 } }),
            fp ("M80: pick grit", "Distortion channel blended half and half", { { "model", 5 }, { "voice", 0 }, { "drive", 6 }, { "bass", 5.5f }, { "mid", 5.5f }, { "treble", 5.5f }, { "blend", 45 } }),
            fp ("Bass Muff: wall of fuzz", "Sustain up, bass boost in", { { "model", 6 }, { "voice", 1 }, { "drive", 7.5f }, { "tone", 4.5f }, { "blend", 0 } }),
            fp ("Bass Muff: fuzz + dry", "Fuzz on top of your clean bass", { { "model", 6 }, { "voice", 2 }, { "drive", 6.5f }, { "tone", 5.5f }, { "blend", 0 } }),
            fp ("ODB-3: punk buzz", "Gain up, highs up", { { "model", 7 }, { "drive", 7 }, { "bass", 6 }, { "treble", 6.5f }, { "blend", 30 } }),
            fp ("Tone Hammer: warm DI", "Flat and round, no grit", { { "model", 8 }, { "voice", 0 }, { "drive", 3 }, { "bass", 5.5f }, { "mid", 5 }, { "midfreq", 500 }, { "treble", 5 }, { "blend", 0 } }),
            fp ("Tone Hammer: AGS on", "The saturated second gain stage", { { "model", 8 }, { "voice", 1 }, { "drive", 5.5f }, { "bass", 5.5f }, { "mid", 6 }, { "midfreq", 650 }, { "treble", 4.5f }, { "blend", 0 } }),
            fp ("Brassmaster: synth bass", "Brassy fuzz against a clean low end", { { "model", 9 }, { "voice", 0 }, { "drive", 7 }, { "tone", 6 }, { "blend", 40 } }),
        };
        m[NodeType::Ir] = {
            fp ("Mix-ready", "80 Hz - 7.5 kHz: the standard cab cleanup", { { "lowcut", 80 }, { "highcut", 7500 }, { "mix", 100 }, { "level", 0 } }),
            fp ("Tight metal", "Low cut higher, fizz cut", { { "lowcut", 110 }, { "highcut", 6500 }, { "mix", 100 }, { "level", 0 } }),
            fp ("Full range", "No filtering", { { "lowcut", 20 }, { "highcut", 20000 }, { "mix", 100 }, { "level", 0 } }),
            fp ("Dark & warm", "Rolled-off top", { { "lowcut", 70 }, { "highcut", 5000 }, { "mix", 100 }, { "level", 0 } }),
            fp ("Bright clean", "Open top end for cleans", { { "lowcut", 90 }, { "highcut", 11000 }, { "mix", 100 }, { "level", 0 } }),
        };
        m[NodeType::Nam] = {
            fp ("Flat (as captured)", "Tone stack at noon, gain as captured", { { "gain", 5 }, { "bass", 5 }, { "mid", 5 }, { "treble", 5 }, { "presence", 5 } }),
            fp ("Scooped", "Metal V: mids down", { { "bass", 6.5f }, { "mid", 3 }, { "treble", 6.5f }, { "presence", 6 } }),
            fp ("Mid-forward lead", "Cuts through the band", { { "gain", 6 }, { "bass", 4.5f }, { "mid", 7 }, { "treble", 5.5f }, { "presence", 5.5f } }),
            fp ("Tight rhythm", "Less bass, a little more gain", { { "gain", 6 }, { "bass", 3.5f }, { "mid", 5.5f }, { "treble", 6 }, { "presence", 6 } }),
            fp ("Warm", "Smoother top end", { { "bass", 6 }, { "mid", 5.5f }, { "treble", 3.5f }, { "presence", 3.5f } }),
            fp ("Bright", "More sparkle and bite", { { "bass", 4.5f }, { "mid", 5 }, { "treble", 7 }, { "presence", 7 } }),
            fp ("Cleaner", "Gain down for a cleaner take on the capture", { { "gain", 3 }, { "bass", 5 }, { "mid", 5 }, { "treble", 5.5f }, { "presence", 5 } }),
            fp ("Hotter", "Gain up for more saturation", { { "gain", 7.5f }, { "bass", 4.5f }, { "mid", 5.5f }, { "treble", 5 }, { "presence", 5 } }),
        };
        return m;
    }
}

const std::vector<FactoryPreset>& factoryPresets (NodeType t)
{
    static const auto all = build();
    static const std::vector<FactoryPreset> none;
    const auto it = all.find (t);
    return it != all.end() ? it->second : none;
}

} // namespace rig
