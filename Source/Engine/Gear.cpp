#include "Gear.h"
#include "Captures.h"

namespace rig
{
juce::String gearName (Gear g)
{
    switch (g)
    {
        case Gear::AmpHead:      return "Amp head";
        case Gear::AmpCab:       return "Amp + Cab";
        case Gear::Pedal:        return "Pedal";
        case Gear::Outboard:     return "Outboard";
        case Gear::Cab:          return "Cabinet";
        case Gear::Space:        return "Space";
        case Gear::Experimental: return "Experimental";
        case Gear::Unknown:      break;
    }
    return "Uncategorised";
}

juce::String gearShortName (Gear g)
{
    switch (g)
    {
        case Gear::AmpHead:      return "AMP HEAD";
        case Gear::AmpCab:       return "AMP + CAB";
        case Gear::Pedal:        return "PEDAL";
        case Gear::Outboard:     return "OUTBOARD";
        case Gear::Cab:          return "CAB IR";
        case Gear::Space:        return "SPACE IR";
        case Gear::Experimental: return "EXPERIMENTAL";
        case Gear::Unknown:      break;
    }
    return {};
}

juce::Colour gearColour (Gear g)
{
    switch (g)
    {
        case Gear::AmpHead:      return categoryColour (Category::Amp);
        case Gear::AmpCab:       return juce::Colour (0xfff2c14e);   // gold: amp and cab in one
        case Gear::Pedal:        return categoryColour (Category::Drive);
        case Gear::Outboard:     return categoryColour (Category::Eq);
        case Gear::Cab:          return categoryColour (Category::Cab);
        case Gear::Space:        return categoryColour (Category::Reverb);
        case Gear::Experimental: return juce::Colour (0xffd77be6);
        case Gear::Unknown:      break;
    }
    return categoryColour (Category::Amp).withSaturation (0.35f);
}

Category gearCategory (Gear g, bool isIr)
{
    switch (g)
    {
        case Gear::Pedal:        return Category::Drive;
        case Gear::Outboard:     return Category::Eq;
        case Gear::Space:        return Category::Reverb;
        case Gear::Experimental: return Category::Other;
        case Gear::Cab:          return Category::Cab;
        case Gear::AmpHead:
        case Gear::AmpCab:       return Category::Amp;
        case Gear::Unknown:      break;
    }
    return isIr ? Category::Cab : Category::Amp;
}

bool gearIncludesCab (Gear g) noexcept { return g == Gear::AmpCab; }

Gear gearFromTone3000 (const juce::String& w)
{
    const auto s = w.trim().toLowerCase();
    if (s == "amp")                        return Gear::AmpHead;
    if (s == "amp-cab" || s == "full-rig") return Gear::AmpCab;
    if (s == "pedal")                      return Gear::Pedal;
    if (s == "outboard")                   return Gear::Outboard;
    if (s == "cab" || s == "ir")           return Gear::Cab;
    if (s == "space")                      return Gear::Space;
    if (s == "experimental")               return Gear::Experimental;
    return Gear::Unknown;
}

juce::String gearToTone3000 (Gear g)
{
    switch (g)
    {
        case Gear::AmpHead:      return "amp";
        case Gear::AmpCab:       return "amp-cab";
        case Gear::Pedal:        return "pedal";
        case Gear::Outboard:     return "outboard";
        case Gear::Cab:          return "cab";
        case Gear::Space:        return "space";
        case Gear::Experimental: return "experimental";
        case Gear::Unknown:      break;
    }
    return {};
}

Gear gearFromNamMetadata (const juce::String& t)
{
    // the NAM trainer's gear types
    const auto s = t.trim().toLowerCase();
    if (s == "amp" || s == "preamp" || s == "pedal_amp") return Gear::AmpHead;
    if (s == "amp_cab" || s == "amp_pedal_cab")          return Gear::AmpCab;
    if (s == "pedal")                                    return Gear::Pedal;
    if (s == "studio")                                   return Gear::Outboard;
    return gearFromTone3000 (s.replaceCharacter ('_', '-'));   // "outboard", "cab", "amp-cab"...
}

//==============================================================================
juce::var ToneInfo::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("source", "TONE3000");
    o->setProperty ("id", id);
    o->setProperty ("title", title);
    o->setProperty ("gear", gear);
    o->setProperty ("format", format);
    o->setProperty ("creator", creator);
    o->setProperty ("url", url);
    o->setProperty ("license", license);
    o->setProperty ("image", image);
    return juce::var (o);
}

ToneInfo ToneInfo::fromVar (const juce::var& v)
{
    ToneInfo t;
    if (! v.isObject()) return t;
    t.id = (int) v.getProperty ("id", 0);
    t.title = v.getProperty ("title", "").toString();
    t.gear = v.getProperty ("gear", "").toString();
    t.format = v.getProperty ("format", "").toString();
    t.creator = v.getProperty ("creator", "").toString();
    t.url = v.getProperty ("url", "").toString();
    t.license = v.getProperty ("license", "").toString();
    t.image = v.getProperty ("image", "").toString();
    t.valid = t.id != 0 || t.title.isNotEmpty();
    return t;
}

bool ToneInfo::write (const juce::File& folder) const
{
    folder.createDirectory();
    const bool ok = folder.getChildFile (sidecarName).replaceWithText (juce::JSON::toString (toVar(), false));
    clearGearCache();
    return ok;
}

namespace
{
    juce::CriticalSection cacheLock;
    struct Entry { juce::Time modified; Gear gear; };
    std::map<juce::String, Entry> fileCache;
    std::map<juce::String, std::pair<juce::Time, ToneInfo>> sidecarCache;

    bool containsAny (const juce::String& s, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (s.contains (w)) return true;
        return false;
    }

    Gear fromNames (const juce::File& f, bool isIr)
    {
        // the file name and its two parent folders, e.g. "TONE3000/Amp + Cab/Plexi crunch.nam"
        const auto s = (f.getParentDirectory().getParentDirectory().getFileName() + " / "
                        + f.getParentDirectory().getFileName() + " / " + f.getFileNameWithoutExtension()).toLowerCase();
        if (containsAny (s, { "experimental" })) return Gear::Experimental;
        if (isIr)
        {
            if (containsAny (s, { "space", "room", "hall", "reverb", "plate", "chamber", "church", "cathedral", "ambien" }))
                return Gear::Space;
            if (containsAny (s, { "pedal" })) return Gear::Pedal;
            return Gear::Cab;
        }
        if (containsAny (s, { "amp + cab", "amp+cab", "amp-cab", "amp cab", "ampcab", "full rig", "full-rig", "fullrig",
                              "with cab", "w cab", "w/ cab", "+ cab", "+cab" }))
            return Gear::AmpCab;
        if (containsAny (s, { "pedal", "fuzz", "boost", "tube screamer", "ts808", "ts9", "klon", "stomp" }))
            return Gear::Pedal;
        if (containsAny (s, { "outboard", "studio", "compressor", "1176", "la-2a", "la2a", "channel strip", "console", "tape" }))
            return Gear::Outboard;
        // whole words only here ("example" isn't an amp)
        const auto words = juce::StringArray::fromTokens (s.replaceCharacters ("_-.()[]/+", "         "), " ", {});
        for (auto* w : { "amp", "amps", "head", "heads", "preamp", "di", "nocab" })
            if (words.contains (w)) return Gear::AmpHead;
        if (s.contains ("no cab")) return Gear::AmpHead;
        return Gear::Unknown;
    }
}

ToneInfo ToneInfo::forFile (const juce::File& file)
{
    const auto sidecar = file.getParentDirectory().getChildFile (sidecarName);
    const juce::ScopedLock sl (cacheLock);
    const auto key = sidecar.getFullPathName();
    const auto modified = sidecar.getLastModificationTime();
    if (auto it = sidecarCache.find (key); it != sidecarCache.end() && it->second.first == modified)
        return it->second.second;
    ToneInfo t;
    if (sidecar.existsAsFile()) t = fromVar (juce::JSON::parse (sidecar.loadFileAsString()));
    sidecarCache[key] = { modified, t };
    return t;
}

Gear classifyFile (const juce::File& f, juce::String metadataGearType)
{
    const bool isIr = ! f.hasFileExtension ("nam");
    const auto modified = f.getLastModificationTime();
    {
        const juce::ScopedLock sl (cacheLock);
        if (auto it = fileCache.find (f.getFullPathName()); it != fileCache.end() && it->second.modified == modified)
            return it->second.gear;
    }

    Gear g = Gear::Unknown;
    if (auto tone = ToneInfo::forFile (f); tone.valid)
        g = gearFromTone3000 (tone.gear);
    if (g == Gear::Unknown && ! isIr)
    {
        const auto info = readCaptureInfo (f);   // start or end of the file (A1 / A2)
        if (metadataGearType.isEmpty()) metadataGearType = info.gearType;
        g = gearFromNamMetadata (metadataGearType);
        if (g == Gear::Unknown) g = gearFromTone3000 (info.t3kGear);
    }
    if (g == Gear::Unknown)
        g = fromNames (f, isIr);

    const juce::ScopedLock sl (cacheLock);
    fileCache[f.getFullPathName()] = { modified, g };
    return g;
}

void clearGearCache()
{
    const juce::ScopedLock sl (cacheLock);
    fileCache.clear();
    sidecarCache.clear();
}

} // namespace rig
