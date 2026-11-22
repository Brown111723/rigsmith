#pragma once

#include "Nodes.h"
#include <memory>
#include <vector>

namespace rig
{
class Chain;
class Node;

/** Style presets: whole rigs for a sound or a genre, mostly rock, built from Rigsmith's own amps,
    cabinets, pedals and effects - no captures or IRs needed, so every one sounds the moment it
    loads. Each one has four scenes: Rhythm, Lead (a boost in front and a little more level),
    Clean (the drive pedals off) and Dry (no modulation, delay or reverb). */
struct StylePedal
{
    // 'O' overdrive, 'D' distortion, 'X' bass drive: a = drive, b = tone, c = level (dB)
    // 'F' fuzz: a = fuzz, b = tone, c = level (dB)      'B' clean boost: a = dB, b = tight (Hz), c = bright (dB)
    // 'W' wah parked at a = position (%)                 'E' envelope wah: a = sensitivity (%)
    // 'C' compressor: a = threshold (dB), b = ratio, c = blend (%)
    char kind = 0;
    const char* model = "";
    float a = 0, b = 0, c = 0;
};

struct StyleMod
{
    // 'c' chorus, 'f' flanger, 'p' phaser (rate Hz, depth %, mix %), 't' tremolo (rate, depth),
    // 'v' vibe (rate, intensity), 'r' rotary (rate >= 1 = fast)
    char kind = 0;
    float rate = 0, depth = 0, mix = 0;
    bool onInRhythm = false;   // off: it waits for the Clean scene
};

struct StylePreset
{
    const char* category;
    const char* name;
    const char* blurb;
    const char* amp;                      // part of the amp model's name
    float gain, bass, mid, treble, presence, master;
    int cab;                              // CabBlock cab index
    float gate;                           // gate threshold in dB, 0 = no gate
    StylePedal p1, p2;
    StyleMod mod;
    float delayMs, delayFb, delayMix;     // delayMs 0 = no delay
    int delayMode;                        // 0 digital, 1 tape, 2 ping-pong, 3 dual
    int verbType;                         // 0 room, 1 hall, 2 plate, 3 spring, 4 shimmer; -1 = none
    float verbDecay, verbMix;
    bool bassRig = false;   // a bass rig: the tuner and the dice follow
};

const std::vector<StylePreset>& stylePresets();
juce::StringArray styleCategories();

/** The blocks of a built style rig that its scenes switch. */
struct StyleParts
{
    std::vector<Node*> drives;      // off in Clean
    std::vector<Node*> keep;        // compressor, wah: always on
    Node* leadBoost = nullptr;
    Node* leadLevel = nullptr;
    Node* mod = nullptr;
    Node* delay = nullptr;
    Node* reverb = nullptr;
    bool modInRhythm = false;
};

std::unique_ptr<Chain> buildStyle (const StylePreset&, StyleParts&);
} // namespace rig
