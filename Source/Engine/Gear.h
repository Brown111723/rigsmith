#pragma once

#include "Nodes.h"

namespace rig
{
/** What a NAM capture or IR actually is: TONE3000's gear types, which also match the
    "gear_type" a NAM model stores in its own metadata. */
enum class Gear { Unknown = 0, AmpHead, AmpCab, Pedal, Outboard, Cab, Space, Experimental };

juce::String gearName (Gear);                  // "Amp + Cab"
juce::String gearShortName (Gear);             // "AMP+CAB" (card label)
juce::Colour gearColour (Gear);
/** Category for colours, the randomiser and the card label: a pedal capture is a Drive,
    a space IR is a Reverb, and so on. */
Category gearCategory (Gear, bool isIr);
bool gearIncludesCab (Gear g) noexcept;

Gear gearFromTone3000 (const juce::String& wire);       // "amp-cab" -> AmpCab
juce::String gearToTone3000 (Gear);
Gear gearFromNamMetadata (const juce::String& gearType); // "amp_cab" -> AmpCab

/** Details of a tone downloaded from TONE3000, kept as tone3000.json next to its files. */
struct ToneInfo
{
    bool valid = false;
    int id = 0;
    juce::String title, gear, format, creator, url, license, image;

    juce::var toVar() const;
    static ToneInfo fromVar (const juce::var&);
    static constexpr const char* sidecarName = "tone3000.json";
    /** The sidecar for a model or IR file (cached per folder). */
    static ToneInfo forFile (const juce::File&);
    bool write (const juce::File& folder) const;
};

/** Works out what a .nam / IR file is, best source first:
      1. a TONE3000 sidecar next to it,
      2. the model's own metadata ("gear_type"; .nam only, read from the file header),
      3. words in the file and folder names ("Amp + Cab", "Pedals", "Rooms"...).
    Cached by path and modification time, so library refreshes stay quick. */
Gear classifyFile (const juce::File&, juce::String metadataGearType = {});
void clearGearCache();

} // namespace rig
