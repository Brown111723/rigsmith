#include "StylePresets.h"
#include "Amp.h"
#include "Blocks.h"
#include "Cab.h"
#include "Fx.h"
#include "Pedals.h"

namespace rig
{
namespace
{
using Pd = StylePedal;
using Md = StyleMod;
const Pd none {};
const Md noMod {};

// cabinets
constexpr int blackface = 0, tweed12 = 1, blue = 2, jc = 3, tweed410 = 4, greenback = 5, v30 = 6, t75 = 7, creamback = 8, evm = 9;
constexpr int fridge = 10, modern410 = 11, flip15 = 12, vintage215 = 13;
// delay modes and reverb types
constexpr int digital = 0, tape = 1, pingpong = 2, dual = 3;
constexpr int room = 0, hall = 1, plate = 2, spring = 3, shimmer = 4, noVerb = -1;
}

const std::vector<StylePreset>& stylePresets()
{
    //  category, name, blurb,
    //  amp, gain, bass, mid, treble, presence, master,  cab, gate,
    //  pedal 1, pedal 2, modulation,  delay ms, feedback, mix, mode,  reverb type, decay, mix
    static const std::vector<StylePreset> all
    {
        //== Classic rock ===================================================================
        { "Classic Rock", "Stack Crunch", "A cranked late-60s British stack with no pedals: chords that bite and bloom",
          "Plexi", 7.0f, 5.0f, 7.0f, 6.0f, 6.0f, 7.0f, greenback, 0,
          none, none, noMod, 340, 18, 10, tape, plate, 1.6f, 14 },
        { "Classic Rock", "Boosted Plexi", "A Screamer with the drive down, pushing a Plexi into singing sustain",
          "Plexi", 6.0f, 4.5f, 7.0f, 6.5f, 6.0f, 7.0f, greenback, -62,
          Pd { 'O', "Tube Screamer", 1.5f, 6.0f, 6.0f }, none, noMod, 380, 22, 14, tape, plate, 1.8f, 15 },
        { "Classic Rock", "Arena Classic", "Big 70s arena rhythm: a stack, a slow phaser and a wide echo",
          "Plexi", 6.5f, 5.0f, 6.5f, 6.0f, 6.0f, 7.0f, greenback, 0,
          none, none, Md { 'p', 0.4f, 55, 40, true }, 380, 25, 16, pingpong, hall, 2.2f, 16 },
        { "Classic Rock", "Phased Crunch", "A small phaser always on in front of a cooking amp: the late-70s rhythm sound",
          "JTM45", 7.5f, 5.0f, 6.0f, 6.5f, 6.0f, 7.0f, greenback, 0,
          none, none, Md { 'p', 0.6f, 60, 50, true }, 0, 0, 0, digital, room, 0.9f, 12 },
        { "Classic Rock", "Brown Crunch", "Hot, saggy late-70s hard rock: a pushed Plexi with flanger for the choruses",
          "Plexi", 8.0f, 6.0f, 6.0f, 7.0f, 6.5f, 8.0f, greenback, -64,
          none, none, Md { 'f', 0.25f, 60, 40, false }, 300, 15, 10, digital, plate, 1.4f, 12 },
        { "Classic Rock", "Hiwatt Power Chords", "Loud, clean-headroom British power: huge open chords that ring",
          "DR103", 6.0f, 5.0f, 6.5f, 6.0f, 5.0f, 8.0f, greenback, 0,
          none, none, noMod, 0, 0, 0, digital, room, 1.2f, 15 },
        { "Classic Rock", "Tweed Rock", "A '59 Bassman turned up: fat, raw and compressed, with spring",
          "Bassman", 7.0f, 5.0f, 6.0f, 6.0f, 5.0f, 7.0f, tweed410, 0,
          none, none, noMod, 0, 0, 0, digital, spring, 1.5f, 15 },
        { "Classic Rock", "Classic Rock Lead", "A Klon into an 800: thick singing leads with tape echo",
          "JCM800", 6.5f, 5.0f, 6.5f, 6.0f, 6.0f, 6.0f, greenback, -62,
          Pd { 'O', "Klon", 4.0f, 5.5f, 4.0f }, none, noMod, 420, 30, 22, tape, plate, 2.0f, 16 },
        { "Classic Rock", "Woman Tone", "A dark, singing neck-pickup lead: treble rolled off, mids up",
          "JTM45", 8.0f, 6.0f, 7.5f, 3.0f, 4.0f, 7.0f, greenback, -64,
          Pd { 'O', "SD-1", 4.0f, 3.5f, 3.0f }, none, noMod, 400, 25, 18, tape, hall, 2.2f, 16 },

        //== Hard rock ======================================================================
        { "Hard Rock", "Hard Rock Rhythm", "A hot-rodded 800 tightened by a Screamer: the classic hard rock riff",
          "JCM800", 7.5f, 5.0f, 6.5f, 6.5f, 6.0f, 6.0f, v30, -60,
          Pd { 'O', "Tube Screamer", 1.5f, 6.0f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.9f, 10 },
        { "Hard Rock", "Hair Metal Lead", "80s LA lead: high gain, chorus, big ping-pong echo and hall",
          "JCM800", 8.5f, 5.5f, 6.0f, 7.0f, 7.0f, 6.0f, t75, -58,
          Pd { 'O', "Tube Screamer", 1.0f, 7.0f, 6.0f }, none, Md { 'c', 0.6f, 40, 35, true }, 450, 35, 25, pingpong, hall, 2.6f, 20 },
        { "Hard Rock", "SLO Lead", "Smooth, fat high-gain lead with endless sustain",
          "SLO", 7.0f, 5.0f, 6.0f, 6.0f, 6.0f, 5.0f, v30, -58,
          none, none, noMod, 400, 30, 20, digital, plate, 2.0f, 15 },
        { "Hard Rock", "Friedman Crunch", "Modern British crunch: tight lows, aggressive mids, great for riffs",
          "BE-100", 6.5f, 5.0f, 6.0f, 6.5f, 6.0f, 6.0f, v30, -60,
          none, none, noMod, 0, 0, 0, digital, room, 1.0f, 10 },
        { "Hard Rock", "RAT Riff Machine", "A RAT in front of a stack: thick, grinding riffs",
          "Plexi", 5.5f, 5.0f, 6.0f, 6.0f, 6.0f, 7.0f, greenback, -60,
          Pd { 'D', "RAT", 4.0f, 5.0f, 0.0f }, none, noMod, 0, 0, 0, digital, room, 0.9f, 10 },
        { "Hard Rock", "Wah Lead", "A wah parked half-way into a cranked stack: a vocal, honking lead",
          "Plexi", 7.5f, 5.0f, 6.5f, 6.0f, 6.0f, 7.0f, greenback, -62,
          Pd { 'W', "", 62.0f }, none, noMod, 420, 28, 20, tape, plate, 1.9f, 15 },
        { "Hard Rock", "Rockerverb Grind", "Thick British grind with big low mids",
          "Rockerverb", 7.0f, 6.5f, 6.0f, 5.5f, 5.0f, 6.0f, creamback, -60,
          none, none, noMod, 0, 0, 0, digital, room, 1.0f, 12 },
        { "Hard Rock", "Stadium Lead", "A Klon into a Friedman, with wide ping-pong echo and a long hall",
          "BE-100", 7.0f, 5.0f, 6.5f, 6.0f, 6.0f, 6.0f, v30, -58,
          Pd { 'O', "Klon", 4.0f, 5.5f, 4.0f }, none, noMod, 480, 35, 25, pingpong, hall, 3.0f, 22 },

        //== Blues rock =====================================================================
        { "Blues Rock", "Texas Blues", "A Screamer into a cranked Fender: fat, mid-heavy blues",
          "Deluxe", 5.5f, 5.0f, 6.0f, 6.5f, 6.0f, 7.0f, blackface, 0,
          Pd { 'O', "Tube Screamer", 5.0f, 6.0f, 3.0f }, none, noMod, 0, 0, 0, digital, spring, 2.0f, 18 },
        { "Blues Rock", "Klon Blues", "A Klon into a Bassman: transparent, touch-sensitive drive",
          "Bassman", 5.0f, 5.0f, 6.0f, 6.0f, 5.0f, 7.0f, tweed410, 0,
          Pd { 'O', "Klon", 5.0f, 5.0f, 3.0f }, none, noMod, 360, 20, 12, tape, spring, 1.8f, 16 },
        { "Blues Rock", "Dumble Smooth", "Liquid, vocal overdrive with a hi-fi speaker",
          "Overdrive Special", 6.0f, 5.0f, 6.0f, 6.0f, 5.0f, 6.0f, evm, 0,
          none, none, noMod, 380, 25, 15, digital, plate, 2.0f, 16 },
        { "Blues Rock", "Bluesbreaker Burst", "A small British combo on the edge: sweet, singing blues rock",
          "JTM45", 7.0f, 5.0f, 6.5f, 6.0f, 6.0f, 7.0f, blue, 0,
          Pd { 'O', "Timmy", 4.0f, 5.0f, 2.0f }, none, noMod, 380, 22, 15, tape, plate, 1.8f, 14 },
        { "Blues Rock", "Slide Grit", "Compressed, gritty slide: long sustain with spring",
          "Bassman", 6.5f, 5.0f, 6.5f, 5.5f, 5.0f, 7.0f, tweed410, 0,
          Pd { 'C', "", -26.0f, 4.0f, 80.0f }, none, noMod, 300, 15, 10, tape, spring, 2.0f, 18 },
        { "Blues Rock", "Vintage Blues Drive", "Edge-of-breakup tweed for players who dig in",
          "Bassman", 5.5f, 5.0f, 6.0f, 6.0f, 5.0f, 7.0f, tweed12, 0,
          Pd { 'O', "King of Tone", 4.0f, 5.0f, 2.0f }, none, noMod, 0, 0, 0, digital, room, 1.2f, 14 },

        //== Punk and grunge ================================================================
        { "Punk & Grunge", "Punk Buzzsaw", "A dimed 800 with no frills: bright, fast and loud",
          "JCM800", 8.0f, 6.0f, 5.0f, 7.0f, 6.5f, 7.0f, greenback, -60,
          none, none, noMod, 0, 0, 0, digital, room, 0.6f, 8 },
        { "Punk & Grunge", "Pop Punk", "Tight, scooped Rectifier power chords with a gate",
          "Dual Rectifier", 6.0f, 6.0f, 4.0f, 6.5f, 6.0f, 6.0f, v30, -56,
          none, none, noMod, 0, 0, 0, digital, room, 0.6f, 8 },
        { "Punk & Grunge", "Grunge Muff", "A Big Muff into a clean Fender: a woolly wall of fuzz",
          "Twin", 4.0f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, blackface, -64,
          Pd { 'D', "Big Muff", 7.0f, 4.5f, 0.0f }, none, Md { 'c', 0.8f, 30, 35, false }, 0, 0, 0, digital, room, 1.0f, 12 },
        { "Punk & Grunge", "Grunge DS-1", "A DS-1 cranked into a low-gain stack: raw early-90s grunge",
          "JCM800", 4.5f, 6.0f, 5.0f, 6.0f, 6.0f, 7.0f, greenback, -60,
          Pd { 'D', "DS-1", 7.0f, 5.0f, 0.0f }, none, noMod, 0, 0, 0, digital, room, 0.8f, 10 },
        { "Punk & Grunge", "Seattle Sludge", "A dark RAT into a Rectifier: heavy, detuned sludge",
          "Dual Rectifier", 6.0f, 6.5f, 5.0f, 5.0f, 5.0f, 6.0f, v30, -56,
          Pd { 'D', "RAT", 5.0f, 3.5f, -2.0f }, none, noMod, 0, 0, 0, digital, room, 0.9f, 10 },
        { "Punk & Grunge", "Skate Punk", "Mid-forward high gain with a Screamer for tightness",
          "5150III", 5.5f, 5.0f, 6.0f, 6.5f, 6.0f, 6.0f, v30, -54,
          Pd { 'O', "Tube Screamer", 0.5f, 6.0f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.6f, 8 },

        //== Alternative and indie ==========================================================
        { "Alternative & Indie", "Indie Jangle", "A chiming AC30 with a compressor and a little chorus",
          "AC30", 3.5f, 4.5f, 6.0f, 7.0f, 6.0f, 6.0f, blue, 0,
          Pd { 'C', "", -24.0f, 3.0f, 70.0f }, none, Md { 'c', 0.5f, 25, 25, true }, 420, 25, 14, tape, plate, 1.8f, 16 },
        { "Alternative & Indie", "Britpop Crunch", "An AC30 pushed by a RAT: swaggering 90s British crunch",
          "AC30", 6.0f, 5.0f, 6.0f, 6.5f, 6.0f, 7.0f, blue, 0,
          Pd { 'D', "RAT", 3.0f, 6.0f, 0.0f }, none, noMod, 360, 20, 12, tape, room, 1.2f, 14 },
        { "Alternative & Indie", "Garage Fuzz", "A fuzz into a small British combo: trashy, wild and fun",
          "AC30", 5.5f, 5.0f, 6.0f, 6.0f, 6.0f, 7.0f, blue, 0,
          Pd { 'F', "", 8.5f, 6.0f, -6.0f }, none, noMod, 0, 0, 0, digital, spring, 1.4f, 14 },
        { "Alternative & Indie", "Alt Rock Wall", "A Muff into a Rectifier: a huge, saturated wall for choruses",
          "Dual Rectifier", 5.0f, 6.0f, 5.0f, 6.0f, 5.5f, 6.0f, v30, -56,
          Pd { 'D', "Big Muff", 6.0f, 5.0f, -2.0f }, none, noMod, 0, 0, 0, digital, hall, 1.8f, 14 },
        { "Alternative & Indie", "Post-Punk Chorus", "A clean JC-120 with lush chorus and dotted echoes",
          "JC-120", 3.0f, 4.5f, 5.5f, 6.5f, 5.0f, 6.0f, jc, 0,
          none, none, Md { 'c', 0.7f, 50, 45, true }, 375, 35, 22, pingpong, hall, 2.2f, 18 },
        { "Alternative & Indie", "Math Rock Clean", "Crisp compressed clean for tapping and odd meters",
          "Twin", 3.0f, 4.5f, 5.5f, 6.5f, 5.0f, 6.0f, blackface, 0,
          Pd { 'C', "", -28.0f, 4.0f, 70.0f }, none, noMod, 0, 0, 0, digital, plate, 1.4f, 12 },
        { "Alternative & Indie", "Dream Pop", "A soft clean in a wash of chorus, echo and hall",
          "Deluxe", 3.0f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, blackface, 0,
          none, none, Md { 'c', 0.4f, 55, 45, true }, 520, 45, 35, pingpong, hall, 4.5f, 35 },
        { "Alternative & Indie", "Fuzz Pop", "A tight fuzz into a Fender: big, bright and catchy",
          "Deluxe", 4.0f, 5.0f, 5.5f, 6.5f, 6.0f, 6.0f, blackface, -62,
          Pd { 'F', "", 7.0f, 6.5f, -6.0f }, none, noMod, 0, 0, 0, digital, room, 1.0f, 12 },

        //== Metal ==========================================================================
        { "Metal", "Thrash Rhythm", "A Mark IIC+ tightened by a Screamer: fast, precise 80s thrash",
          "Mark IIC+", 6.5f, 4.0f, 5.0f, 6.5f, 6.0f, 5.0f, v30, -50,
          Pd { 'O', "Tube Screamer", 0.0f, 6.5f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.5f, 6 },
        { "Metal", "80s Metal", "A dimed 800 with a boost: classic British metal",
          "JCM800", 9.0f, 5.0f, 6.0f, 6.5f, 6.5f, 6.0f, greenback, -54,
          Pd { 'O', "Tube Screamer", 0.5f, 6.0f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.7f, 8 },
        { "Metal", "Modern Metal Chug", "A 5150III Red with a boost and a tight gate: punchy palm mutes",
          "5150III", 6.5f, 5.5f, 5.0f, 6.5f, 6.0f, 5.0f, v30, -48,
          Pd { 'O', "Tube Screamer", 0.0f, 6.5f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.5f, 6 },
        { "Metal", "Djent Tight", "Very tight, bright and gated: built for extended-range riffs",
          "VH4", 6.0f, 5.0f, 5.5f, 6.5f, 6.5f, 5.0f, v30, -44,
          Pd { 'O', "Tube Screamer", 0.0f, 7.5f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.4f, 5 },
        { "Metal", "Swedish Death", "An HM-2 with everything up into a stack: the buzzsaw",
          "JCM800", 4.0f, 6.0f, 5.0f, 6.0f, 6.0f, 7.0f, v30, -50,
          Pd { 'D', "HM-2", 8.0f, 8.0f, -2.0f }, none, noMod, 0, 0, 0, digital, room, 0.7f, 8 },
        { "Metal", "Nu-Metal Scoop", "A scooped, detuned Rectifier: huge lows and bite",
          "Dual Rectifier", 7.0f, 7.0f, 2.5f, 6.5f, 6.0f, 6.0f, v30, -48,
          none, none, noMod, 0, 0, 0, digital, room, 0.5f, 6 },
        { "Metal", "Doom Uberschall", "Low, slow and enormous: a dark Bogner with a long room",
          "Uberschall", 7.0f, 6.5f, 5.0f, 5.0f, 5.0f, 6.0f, v30, -54,
          none, none, noMod, 0, 0, 0, digital, hall, 2.5f, 14 },
        { "Metal", "Metal Lead", "A boosted 6505 with delay and hall for screaming leads",
          "6505", 7.0f, 5.0f, 6.0f, 6.0f, 6.0f, 5.0f, v30, -52,
          Pd { 'O', "Tube Screamer", 1.0f, 6.0f, 6.0f }, none, noMod, 420, 30, 20, digital, hall, 2.4f, 18 },
        { "Metal", "Shred Lead", "A smooth Mark lead with ping-pong echo for fast runs",
          "Mark IIC+", 7.5f, 4.5f, 6.0f, 6.0f, 6.0f, 5.0f, v30, -52,
          none, none, noMod, 380, 28, 18, pingpong, hall, 2.2f, 16 },
        { "Metal", "Power Metal", "A bright, saturated ENGL with a big hall",
          "Powerball", 7.0f, 5.0f, 5.5f, 6.5f, 6.5f, 5.0f, v30, -50,
          none, none, noMod, 0, 0, 0, digital, hall, 2.4f, 14 },
        { "Metal", "Groove Metal", "Scooped and gated solid-state-style crunch for groove riffs",
          "6505", 7.5f, 6.0f, 3.0f, 7.0f, 6.5f, 5.0f, v30, -46,
          Pd { 'O', "Tube Screamer", 0.0f, 7.0f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.4f, 5 },

        //== Modern rock ====================================================================
        { "Modern Rock", "Modern Rock Rhythm", "A Rectifier with a gate: thick, polished radio rock",
          "Dual Rectifier", 6.0f, 5.5f, 5.5f, 6.0f, 6.0f, 6.0f, v30, -56,
          none, none, noMod, 0, 0, 0, digital, room, 0.8f, 10 },
        { "Modern Rock", "Radio Rock", "A Friedman with a touch of delay: big but clear",
          "BE-100", 6.0f, 5.5f, 6.0f, 6.0f, 6.0f, 6.0f, v30, -58,
          none, none, noMod, 360, 20, 10, digital, plate, 1.6f, 12 },
        { "Modern Rock", "Arena Anthem", "A boosted stack with dotted-eighth echoes",
          "JCM800", 6.5f, 5.0f, 6.0f, 6.5f, 6.0f, 6.0f, v30, -60,
          Pd { 'O', "Klon", 3.0f, 5.5f, 4.0f }, none, noMod, 375, 40, 22, pingpong, hall, 2.6f, 18 },
        { "Modern Rock", "Rock Ballad Clean", "A warm, chorused clean for the quiet verse",
          "Twin", 3.0f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, blackface, 0,
          Pd { 'C', "", -24.0f, 3.0f, 70.0f }, none, Md { 'c', 0.5f, 40, 35, true }, 450, 30, 20, pingpong, hall, 2.8f, 22 },
        { "Modern Rock", "Two-Amp Wall", "Two cabinets' worth of Creamback and V30 weight",
          "Rockerverb", 6.5f, 6.0f, 6.0f, 6.0f, 5.5f, 6.0f, creamback, -58,
          Pd { 'O', "Tube Screamer", 1.0f, 6.0f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 1.0f, 12 },
        { "Modern Rock", "Alt Metal Drop", "Drop-tuned weight with clarity: a tight Uberschall",
          "Uberschall", 6.0f, 6.0f, 5.5f, 6.0f, 6.0f, 5.0f, v30, -52,
          Pd { 'O', "Tube Screamer", 0.0f, 6.0f, 6.0f }, none, noMod, 0, 0, 0, digital, room, 0.6f, 8 },

        //== Southern and country rock ======================================================
        { "Southern & Country", "Southern Rock", "Bright, open crunch with spring: twin guitar harmonies",
          "Plexi", 5.5f, 4.5f, 6.0f, 7.0f, 6.0f, 7.0f, greenback, 0,
          none, none, noMod, 0, 0, 0, digital, spring, 1.8f, 15 },
        { "Southern & Country", "Chicken Pickin'", "Squashed, snappy clean with a slapback",
          "Twin", 3.5f, 4.5f, 5.5f, 7.0f, 6.0f, 6.0f, blackface, 0,
          Pd { 'C', "", -30.0f, 6.0f, 90.0f }, none, noMod, 110, 5, 22, tape, spring, 1.4f, 14 },
        { "Southern & Country", "Country Rock Drive", "A Timmy into a Deluxe: twangy, open overdrive",
          "Deluxe", 5.0f, 4.5f, 5.5f, 6.5f, 6.0f, 7.0f, blackface, 0,
          Pd { 'O', "Timmy", 3.5f, 5.5f, 2.0f }, none, noMod, 140, 10, 15, tape, spring, 1.6f, 14 },
        { "Southern & Country", "Twang Machine", "Bright, glassy, compressed clean",
          "Twin", 3.0f, 4.0f, 5.0f, 7.5f, 6.5f, 6.0f, blackface, 0,
          Pd { 'C', "", -26.0f, 4.0f, 80.0f }, none, noMod, 0, 0, 0, digital, spring, 1.8f, 18 },

        //== Clean ==========================================================================
        { "Clean & Jazz", "Sparkle Clean", "A big Fender clean, compressed and glassy",
          "Twin", 3.0f, 5.0f, 5.0f, 6.5f, 5.5f, 6.0f, blackface, 0,
          Pd { 'C', "", -24.0f, 3.0f, 60.0f }, none, noMod, 0, 0, 0, digital, plate, 1.8f, 16 },
        { "Clean & Jazz", "Jazz Box", "A warm, dark clean for archtop chords",
          "Twin", 3.0f, 6.0f, 5.5f, 3.5f, 4.0f, 5.0f, blackface, 0,
          none, none, noMod, 0, 0, 0, digital, room, 1.4f, 14 },
        { "Clean & Jazz", "JC Chorus Clean", "The solid-state clean with its built-in chorus",
          "JC-120", 3.0f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, jc, 0,
          none, none, Md { 'c', 0.8f, 45, 50, true }, 0, 0, 0, digital, hall, 2.0f, 14 },
        { "Clean & Jazz", "Vintage Tremolo", "A small Fender with tremolo and drippy spring",
          "Deluxe", 3.5f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, blackface, 0,
          none, none, Md { 't', 4.5f, 55, 0, true }, 0, 0, 0, digital, spring, 2.5f, 25 },

        //== Funk and pop ===================================================================
        { "Funk & Pop", "Funk Scratch", "Tight, compressed, bright: built for sixteenth-note scratching",
          "Twin", 3.0f, 4.0f, 6.0f, 7.0f, 6.0f, 6.0f, blackface, 0,
          Pd { 'C', "", -30.0f, 6.0f, 85.0f }, none, noMod, 0, 0, 0, digital, room, 0.6f, 8 },
        { "Funk & Pop", "Auto-Wah Funk", "An envelope filter on a clean Fender: quacking funk",
          "Twin", 3.0f, 4.5f, 5.5f, 6.5f, 5.5f, 6.0f, blackface, 0,
          Pd { 'C', "", -28.0f, 4.0f, 80.0f }, Pd { 'E', "", 70.0f }, noMod, 0, 0, 0, digital, room, 0.8f, 10 },
        { "Funk & Pop", "Pop Clean Chorus", "Glossy clean with chorus and a short echo",
          "JC-120", 3.0f, 4.5f, 5.0f, 6.5f, 5.5f, 6.0f, jc, 0,
          Pd { 'C', "", -24.0f, 3.0f, 70.0f }, none, Md { 'c', 0.6f, 35, 35, true }, 280, 20, 14, pingpong, plate, 1.6f, 15 },

        //== Ambient and shoegaze ===========================================================
        { "Ambient & Shoegaze", "Shoegaze Wash", "A Muff, a flanger, long echoes and shimmer: a wall of haze",
          "AC30", 5.0f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, blue, 0,
          Pd { 'D', "Big Muff", 6.0f, 5.0f, -3.0f }, none, Md { 'f', 0.15f, 70, 45, true }, 550, 50, 35, pingpong, shimmer, 6.0f, 40 },
        { "Ambient & Shoegaze", "Ambient Swells", "A clean amp, long echoes and a shimmering reverb for swells",
          "Deluxe", 3.0f, 5.0f, 5.0f, 6.0f, 5.0f, 6.0f, blackface, 0,
          none, none, Md { 'c', 0.3f, 40, 30, true }, 620, 55, 40, pingpong, shimmer, 8.0f, 50 },
        { "Ambient & Shoegaze", "Post-Rock Build", "A Klon into an AC30, with echoes for the crescendo",
          "AC30", 5.5f, 5.0f, 6.0f, 6.5f, 6.0f, 7.0f, blue, 0,
          Pd { 'O', "Klon", 4.0f, 5.5f, 3.0f }, none, noMod, 500, 45, 30, pingpong, hall, 4.0f, 30 },

        //== Psychedelic and surf ===========================================================
        { "Psych & Surf", "60s Fuzz", "A fuzz into a tweed: raw, buzzy late-60s psych",
          "Bassman", 5.0f, 5.0f, 6.0f, 6.0f, 5.0f, 7.0f, tweed410, 0,
          Pd { 'F', "", 9.0f, 5.5f, -6.0f }, none, noMod, 0, 0, 0, digital, spring, 1.6f, 16 },
        { "Psych & Surf", "Vibe Haze", "A fuzz into a stack with a swirling vibe",
          "Plexi", 6.0f, 5.0f, 6.0f, 6.0f, 6.0f, 7.0f, greenback, 0,
          Pd { 'F', "", 7.5f, 5.0f, -6.0f }, none, Md { 'v', 1.5f, 70, 0, true }, 0, 0, 0, digital, room, 1.4f, 14 },
        { "Psych & Surf", "Rotary Psych", "A clean amp through a spinning rotary speaker",
          "Twin", 4.0f, 5.0f, 5.5f, 6.0f, 5.0f, 6.0f, blackface, 0,
          none, none, Md { 'r', 0.5f, 0, 0, true }, 0, 0, 0, digital, room, 1.4f, 14 },
        { "Psych & Surf", "Surf Spring", "Drippy spring reverb and tape echo on a bright Fender",
          "Twin", 3.5f, 4.5f, 5.0f, 7.0f, 6.0f, 6.0f, blackface, 0,
          none, none, noMod, 300, 30, 22, tape, spring, 3.0f, 40 },
        { "Psych & Surf", "Rockabilly Slap", "A tweed with slapback: snappy and vintage",
          "Bassman", 5.0f, 5.0f, 6.0f, 6.0f, 5.0f, 7.0f, tweed410, 0,
          none, none, noMod, 120, 10, 30, tape, spring, 1.4f, 12 },

        //== Bass ===========================================================================
        { "Bass", "Rock Bass", "A compressed SVT through an 8x10: the rock bass sound",
          "SVT", 5.5f, 6.0f, 5.5f, 5.0f, 5.0f, 6.0f, fridge, 0,
          Pd { 'C', "", -22.0f, 4.0f, 100.0f }, none, noMod, 0, 0, 0, digital, noVerb, 0, 0, true },
        { "Bass", "Grit Bass", "A B7K into an SVT: growling, modern rock bass",
          "SVT", 5.0f, 6.0f, 5.0f, 5.0f, 5.0f, 6.0f, fridge, 0,
          Pd { 'C', "", -22.0f, 4.0f, 100.0f }, Pd { 'X', "B7K", 5.0f, 5.0f, 0.0f }, noMod, 0, 0, 0, digital, noVerb, 0, 0, true },
        { "Bass", "Motown Bass", "A B-15 through its 1x15: round, warm and soft on top",
          "B-15", 4.0f, 6.0f, 5.0f, 4.0f, 4.0f, 6.0f, flip15, 0,
          none, none, noMod, 0, 0, 0, digital, noVerb, 0, 0, true },
        { "Bass", "Punk Pick Bass", "A bright, aggressive pick tone for driving eighths",
          "800RB", 6.0f, 6.0f, 5.5f, 6.5f, 6.0f, 6.0f, modern410, 0,
          Pd { 'X', "SansAmp", 4.0f, 5.0f, 0.0f }, none, noMod, 0, 0, 0, digital, noVerb, 0, 0, true },
        { "Bass", "Fuzz Bass", "A fuzzed Orange with a clean blend under it",
          "AD200", 6.0f, 6.5f, 5.0f, 5.0f, 4.5f, 6.0f, fridge, 0,
          Pd { 'X', "Muff", 6.0f, 5.0f, 0.0f }, none, noMod, 0, 0, 0, digital, noVerb, 0, 0, true },
        { "Bass", "Metal Bass", "Compressed and grinding: a B3K over a tight 4x10",
          "800RB", 5.5f, 6.0f, 5.0f, 6.0f, 6.0f, 6.0f, modern410, 0,
          Pd { 'C', "", -24.0f, 5.0f, 100.0f }, Pd { 'X', "B3K", 6.0f, 6.0f, 0.0f }, noMod, 0, 0, 0, digital, noVerb, 0, 0, true },
        { "Bass", "Chorus Bass", "A clean SVT with chorus for melodic lines",
          "SVT", 4.5f, 6.0f, 5.0f, 5.5f, 5.0f, 6.0f, modern410, 0,
          Pd { 'C', "", -22.0f, 3.0f, 100.0f }, none, Md { 'c', 0.5f, 30, 30, true }, 0, 0, 0, digital, noVerb, 0, 0, true },
    };
    return all;
}

juce::StringArray styleCategories()
{
    juce::StringArray s;
    for (auto& p : stylePresets()) s.addIfNotAlreadyThere (p.category);
    return s;
}

namespace
{
template <typename Models>
int findModel (const Models& ms, const juce::String& want)
{
    if (want.isEmpty()) return -1;
    for (int i = 0; i < (int) ms.size(); ++i)
        if (ms[(size_t) i].name.containsIgnoreCase (want) || ms[(size_t) i].aka.containsIgnoreCase (want)) return i;
    return -1;
}

std::unique_ptr<Node> makePedal (const StylePedal& p)
{
    switch (p.kind)
    {
        case 'O': case 'D': case 'X':
        {
            std::unique_ptr<PedalBlock> b;
            if (p.kind == 'O') b = std::make_unique<OverdriveBlock>();
            else if (p.kind == 'D') b = std::make_unique<DistortionBlock>();
            else b = std::make_unique<BassDriveBlock>();
            if (const int m = findModel (b->models(), p.model); m >= 0) { b->set (PedalBlock::pModel, (float) m); b->onChoiceChanged (PedalBlock::pModel); }
            b->set (PedalBlock::pDrive, p.a);
            b->set (PedalBlock::pTone, p.b);
            b->set (PedalBlock::pLevel, p.c);
            return b;
        }
        case 'F':
        {
            auto b = std::make_unique<FuzzBlock>();
            b->set (FuzzBlock::pFuzz, p.a); b->set (FuzzBlock::pTone, p.b); b->set (FuzzBlock::pLevel, p.c);
            return b;
        }
        case 'B':
        {
            auto b = std::make_unique<BoostBlock>();
            b->set (BoostBlock::pGain, p.a); b->set (BoostBlock::pTight, p.b); b->set (BoostBlock::pBright, p.c);
            return b;
        }
        case 'W': case 'E':
        {
            auto b = std::make_unique<WahBlock>();
            if (p.kind == 'W') { b->set (WahBlock::pMode, 0.0f); b->set (WahBlock::pPos, p.a); }
            else { b->set (WahBlock::pMode, 1.0f); b->set (WahBlock::pSens, p.a); }
            return b;
        }
        case 'C':
        {
            auto b = std::make_unique<CompBlock>();
            b->set (CompBlock::pThr, p.a); b->set (CompBlock::pRatio, p.b); b->set (CompBlock::pMix, p.c);
            b->set (CompBlock::pAtt, 12.0f); b->set (CompBlock::pRel, 180.0f);
            return b;
        }
        default: return {};
    }
}

std::unique_ptr<Node> makeMod (const StyleMod& m)
{
    switch (m.kind)
    {
        case 'c': { auto b = std::make_unique<ChorusBlock>();  b->set (ChorusBlock::pRate, m.rate);  b->set (ChorusBlock::pDepth, m.depth);  b->set (ChorusBlock::pMix, m.mix);  return b; }
        case 'f': { auto b = std::make_unique<FlangerBlock>(); b->set (FlangerBlock::pRate, m.rate); b->set (FlangerBlock::pDepth, m.depth); b->set (FlangerBlock::pMix, m.mix); return b; }
        case 'p': { auto b = std::make_unique<PhaserBlock>();  b->set (PhaserBlock::pRate, m.rate);  b->set (PhaserBlock::pDepth, m.depth);  b->set (PhaserBlock::pMix, m.mix);  return b; }
        case 't': { auto b = std::make_unique<TremoloBlock>(); b->set (TremoloBlock::pRate, m.rate); b->set (TremoloBlock::pDepth, m.depth); return b; }
        case 'v': { auto b = std::make_unique<VibeBlock>();    b->set (VibeBlock::pRate, m.rate);    b->set (VibeBlock::pIntensity, m.depth); return b; }
        case 'r': { auto b = std::make_unique<RotaryBlock>();  b->set (RotaryBlock::pSpeed, m.rate >= 1.0f ? 1.0f : 0.0f); return b; }
        default: return {};
    }
}
} // namespace

std::unique_ptr<Chain> buildStyle (const StylePreset& s, StyleParts& parts)
{
    auto r = std::make_unique<Chain>();
    parts = {};
    parts.modInRhythm = s.mod.onInRhythm;

    if (s.gate < 0.0f)
    {
        auto g = std::make_unique<GateBlock>();
        g->set (GateBlock::pThreshold, s.gate);
        r->nodes.push_back (std::move (g));
    }
    for (auto* p : { &s.p1, &s.p2 })
        if (auto n = makePedal (*p))
        {
            const bool drive = p->kind == 'O' || p->kind == 'D' || p->kind == 'X' || p->kind == 'F' || p->kind == 'B';
            (drive ? parts.drives : parts.keep).push_back (n.get());
            r->nodes.push_back (std::move (n));
        }

    // the Lead scene: a clean boost in front of the amp, and a little more level after the cab
    auto boost = std::make_unique<BoostBlock>();
    boost->set (BoostBlock::pGain, s.bassRig ? 3.0f : 5.0f);
    boost->set (BoostBlock::pTight, s.bassRig ? 40.0f : 110.0f);
    boost->set (BoostBlock::pBright, 1.0f);
    parts.leadBoost = boost.get();
    r->nodes.push_back (std::move (boost));

    auto amp = std::make_unique<AmpBlock>();
    if (const int m = findModel (AmpBlock::models(), s.amp); m >= 0) { amp->set (AmpBlock::pModel, (float) m); amp->onChoiceChanged (AmpBlock::pModel); }
    amp->set (AmpBlock::pGain, s.gain);
    amp->set (AmpBlock::pBass, s.bass);
    amp->set (AmpBlock::pMid, s.mid);
    amp->set (AmpBlock::pTreble, s.treble);
    amp->set (AmpBlock::pPresence, s.presence);
    amp->set (AmpBlock::pMaster, s.master);
    r->nodes.push_back (std::move (amp));

    auto cab = std::make_unique<CabBlock>();
    cab->set (CabBlock::pCab, (float) juce::jlimit (0, (int) CabBlock::cabs().size() - 1, s.cab));
    r->nodes.push_back (std::move (cab));

    auto level = std::make_unique<GainBlock>();
    level->set (0, 2.5f);
    parts.leadLevel = level.get();
    r->nodes.push_back (std::move (level));

    if (auto m = makeMod (s.mod)) { parts.mod = m.get(); r->nodes.push_back (std::move (m)); }

    if (s.delayMs > 0.0f)
    {
        auto d = std::make_unique<DelayBlock>();
        d->set (DelayBlock::pMode, (float) s.delayMode);
        d->set (DelayBlock::pTime, s.delayMs);
        d->set (DelayBlock::pFeedback, s.delayFb);
        d->set (DelayBlock::pMix, s.delayMix);
        d->set (DelayBlock::pHighCut, 5000.0f);
        parts.delay = d.get();
        r->nodes.push_back (std::move (d));
    }
    if (s.verbType >= 0)
    {
        auto v = std::make_unique<ReverbBlock>();
        v->set (ReverbBlock::pType, (float) s.verbType);
        v->set (ReverbBlock::pDecay, s.verbDecay);
        v->set (ReverbBlock::pMix, s.verbMix);
        parts.reverb = v.get();
        r->nodes.push_back (std::move (v));
    }
    return r;
}
} // namespace rig
