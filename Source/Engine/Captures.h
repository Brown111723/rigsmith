#pragma once

#include "Gear.h"

namespace rig
{
/** What a .nam file says about itself: the "metadata" block the NAM trainer and
    TONE3000 write ("gear_make", "gear_model", "gear_type", "tone_type", "name",
    "modeled_by", "t3k_id", "t3k_gear"). */
struct CaptureInfo
{
    juce::String name, gearMake, gearModel, gearType, toneType, modeledBy, t3kGear;
    int t3kId = 0;
    bool hasMetadata = false;

    /** "Marshall JCM800 Zakk Wylde Signature": make and model, without saying the make twice. */
    juce::String gearTitle() const;
    juce::var toVar() const;
    static CaptureInfo fromVar (const juce::var&);
};

/** Reads a .nam's metadata. It may sit at the start (A1) or after the weights (A2), so this
    looks at both ends of the file without parsing the weights. Cached by size and date,
    in memory and in Rigsmith.captures next to the ini, so big libraries open quickly. */
CaptureInfo readCaptureInfo (const juce::File&);
void setCaptureCacheFile (const juce::File&);
void saveCaptureCache();

/** "hi_gain" -> "Hi-gain", "metal" -> "Metal" */
juce::String toneTypeName (const juce::String&);

/** Library order for gear types: amp heads, amp + cab, pedals, outboard, cabinets, spaces... */
int gearRank (Gear);

//==============================================================================
/** One piece of gear and all its captures: e.g. a single "Darkglass Alpha Omega Ultra" pedal
    with its twelve .nam files (different settings), however many folders they're spread over. */
struct CaptureGroup
{
    juce::String key, title;
    Gear gear = Gear::Unknown;
    juce::StringArray creators;            // modeled_by / TONE3000 creator, unique
    juce::StringArray toneTypes;           // unique, as written ("metal", "clean"...)
    juce::Array<juce::File> files;         // in a sensible order (by capture name, naturally)
    juce::StringArray labels;              // per file: the capture's name ("Omega - Omega-BITE G3")
    juce::StringArray shortLabels;         // per file: just what differs ("BITE G3")
    juce::StringArray fileToneTypes;       // per file

    int size() const noexcept { return files.size(); }
    int indexOf (const juce::File& f) const { return files.indexOf (f); }
    juce::String creatorText() const;      // "leenykeany", "leenykeany + 2 more"
    juce::String toneText() const;         // "Metal, 2000s"
};
using CaptureGroupPtr = std::shared_ptr<const CaptureGroup>;

/** Which group a file belongs to: the gear (make + model + type) when the file says,
    else its TONE3000 tone, else the file alone. */
juce::String captureGroupKey (const juce::File&);

/** What's left of each name after the part they all share ("Omega - Omega-BITE G3" -> "G3"). */
juce::StringArray shortenCaptureLabels (const juce::StringArray&);

/** Groups files by gear; groups come out sorted by type then title. */
std::vector<CaptureGroupPtr> groupCaptures (const juce::Array<juce::File>&);

//==============================================================================
/** A block that holds several captures (NAM models or IRs) and sweeps between them:
    neighbours are blended, so the sound morphs from one capture to the next. */
struct Sweepable
{
    virtual ~Sweepable() = default;
    /** These captures, in this order; position 0..n-1 (fractions blend). */
    virtual bool loadCaptures (const juce::Array<juce::File>&, float position, juce::String& error) = 0;
    virtual juce::Array<juce::File> getCaptureFiles() const = 0;
    virtual int getNumCaptures() const noexcept = 0;
    virtual juce::StringArray getCaptureNames() const = 0;
    virtual juce::String getCaptureToneType (int) const { return {}; }
    virtual float getCapturePosition() const noexcept = 0;
    virtual void setCapturePosition (float) noexcept = 0;
    virtual int getNearestCapture() const noexcept = 0;
    /** The parameter that holds the position (for MIDI learn and scenes). */
    virtual int getCaptureParam() const noexcept = 0;
    /** Which files it takes: "nam" or IRs. */
    virtual bool takesFile (const juce::File&) const = 0;
    virtual Gear getSweepGear() const = 0;

    static bool isNamFile (const juce::File& f) { return f.hasFileExtension ("nam"); }
    static bool isIrFile (const juce::File& f)  { return f.hasFileExtension ("wav;aif;aiff;flac"); }
    static constexpr int maxCaptures = 24;
};

//==============================================================================
/** Every NAM capture and IR in the library folders, grouped by gear. One scan, shared by
    the library, the blocks' capture knobs and the dice. */
class CaptureLibrary
{
public:
    struct Set
    {
        juce::Array<juce::File> files;
        std::vector<CaptureGroupPtr> groups;
        std::map<juce::String, CaptureGroupPtr> byFile;   // full path -> group
        juce::uint32 scannedAt = 0;
        juce::String pathsKey;
    };

    /** Folders to scan (set by the engine). */
    std::function<juce::FileSearchPath (bool irs)> getPaths;

    /** The models (irs = false) or IRs. Scans when needed: the first time, after rescan(),
        when the folders change, or when the last scan is older than maxAgeMs (0 = any age). */
    const Set& get (bool irs, juce::uint32 maxAgeMs = 0);
    /** Look again next time (files added, folders changed, a TONE3000 download...). */
    void rescan();

    /** The gear a file belongs to, with its other captures. Files outside the library
        folders are grouped with their own folder. Never null for an existing file. */
    CaptureGroupPtr groupFor (const juce::File&);

    /** Short names for a sweep's captures: each one's distinctive part within its own gear
        ("Crunch G4"), so a sweep mixing gear still reads well. */
    juce::StringArray shortNames (const juce::Array<juce::File>& files, const juce::StringArray& fullNames);

    /** Bumped on every scan (UI caches compare it). */
    int version = 0;

    static constexpr int maxFiles = 8000;

private:
    Set sets[2];
    bool dirty[2] { true, true };
    std::map<juce::String, std::vector<CaptureGroupPtr>> folderGroups;   // outside the library
};

} // namespace rig
