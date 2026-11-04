#include "Captures.h"

#include <string_view>

namespace rig
{
//==============================================================================
juce::String CaptureInfo::gearTitle() const
{
    const auto make = gearMake.trim(), model = gearModel.trim();
    if (make.isEmpty()) return model;
    if (model.isEmpty()) return make;
    if (model.containsIgnoreCase (make)) return model;
    if (make.containsIgnoreCase (model)) return make;
    return make + " " + model;
}

juce::var CaptureInfo::toVar() const
{
    auto* o = new juce::DynamicObject();
    auto put = [o] (const char* k, const juce::String& v) { if (v.isNotEmpty()) o->setProperty (k, v); };
    put ("name", name); put ("gear_make", gearMake); put ("gear_model", gearModel); put ("gear_type", gearType);
    put ("tone_type", toneType); put ("modeled_by", modeledBy); put ("t3k_gear", t3kGear);
    if (t3kId != 0) o->setProperty ("t3k_id", t3kId);
    return juce::var (o);
}

CaptureInfo CaptureInfo::fromVar (const juce::var& v)
{
    CaptureInfo c;
    if (! v.isObject()) return c;
    auto str = [&v] (const char* k)
    {
        const auto p = v.getProperty (k, {});
        return p.isString() ? p.toString().trim() : juce::String();
    };
    c.name = str ("name");
    c.gearMake = str ("gear_make");
    c.gearModel = str ("gear_model");
    c.gearType = str ("gear_type");
    c.toneType = str ("tone_type");
    c.modeledBy = str ("modeled_by");
    c.t3kGear = str ("t3k_gear");
    const auto id = v.getProperty ("t3k_id", {});
    c.t3kId = id.isString() ? id.toString().getIntValue() : (int) id;
    c.hasMetadata = c.name.isNotEmpty() || c.gearMake.isNotEmpty() || c.gearModel.isNotEmpty()
                 || c.gearType.isNotEmpty() || c.toneType.isNotEmpty() || c.t3kId != 0;
    return c;
}

juce::String toneTypeName (const juce::String& t)
{
    auto s = t.trim().replaceCharacter ('_', ' ');
    if (s.equalsIgnoreCase ("hi gain") || s.equalsIgnoreCase ("high gain")) return "Hi-gain";
    if (s.isEmpty()) return {};
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}

int gearRank (Gear g)
{
    switch (g)
    {
        case Gear::AmpHead: return 0;  case Gear::AmpCab: return 1;   case Gear::Pedal: return 2;
        case Gear::Outboard: return 3; case Gear::Cab: return 4;      case Gear::Space: return 5;
        case Gear::Experimental: return 6; case Gear::Unknown: break;
    }
    return 7;
}

//==============================================================================
namespace
{
    struct CacheEntry { juce::int64 size = 0, modified = 0; CaptureInfo info; };
    juce::CriticalSection cacheLock;
    std::map<juce::String, CacheEntry> cache;
    juce::File cacheFile;
    bool cacheDirty = false;

    /** The {...} object starting at `start`, or empty if the chunk ends first. */
    std::string_view objectAt (std::string_view s, size_t start)
    {
        int depth = 0;
        bool inString = false, escaped = false;
        for (size_t i = start; i < s.size(); ++i)
        {
            const char c = s[i];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) return s.substr (start, i - start + 1);
        }
        return {};
    }

    int score (const CaptureInfo& c)
    {
        return (c.gearMake.isNotEmpty() ? 3 : 0) + (c.gearModel.isNotEmpty() ? 3 : 0) + (c.gearType.isNotEmpty() ? 2 : 0)
             + (c.toneType.isNotEmpty() ? 1 : 0) + (c.name.isNotEmpty() ? 1 : 0) + (c.t3kId != 0 ? 2 : 0)
             + (c.hasMetadata ? 1 : 0);
    }

    /** Every "metadata": {...} in a chunk of the file; the best one wins (later ones on a tie:
        a container's own metadata comes after its sub-models'). */
    void findMetadata (std::string_view s, juce::int64 offset, CaptureInfo& best, int& bestScore, juce::int64& bestPos)
    {
        static constexpr std::string_view key { "\"metadata\"" };
        for (size_t at = s.find (key); at != std::string_view::npos; at = s.find (key, at + key.size()))
        {
            size_t i = at + key.size();
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
            if (i >= s.size() || s[i] != ':') continue;
            ++i;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
            if (i >= s.size() || s[i] != '{') continue;
            const auto obj = objectAt (s, i);
            if (obj.empty()) continue;
            const auto info = CaptureInfo::fromVar (juce::JSON::parse (juce::String::fromUTF8 (obj.data(), (int) obj.size())));
            const int sc = score (info);
            const auto pos = offset + (juce::int64) at;
            if (sc > bestScore || (sc == bestScore && sc > 0 && pos > bestPos))
            {
                best = info;
                bestScore = sc;
                bestPos = pos;
            }
        }
    }

    CaptureInfo readFromFile (const juce::File& f)
    {
        CaptureInfo best;
        if (! f.hasFileExtension ("nam")) return best;
        juce::FileInputStream in (f);
        if (! in.openedOk()) return best;
        constexpr juce::int64 chunk = 96 * 1024;
        const auto size = in.getTotalLength();
        int bestScore = 0;
        juce::int64 bestPos = -1;

        juce::MemoryBlock head;
        in.readIntoMemoryBlock (head, (juce::pointer_sized_int) juce::jmin (size, chunk));
        findMetadata ({ static_cast<const char*> (head.getData()), head.getSize() }, 0, best, bestScore, bestPos);

        if (size > chunk)
        {
            juce::MemoryBlock tail;
            const auto from = juce::jmax (chunk, size - chunk);
            in.setPosition (from);
            in.readIntoMemoryBlock (tail, (juce::pointer_sized_int) (size - from));
            findMetadata ({ static_cast<const char*> (tail.getData()), tail.getSize() }, from, best, bestScore, bestPos);
        }
        return best;
    }

    void loadCache()
    {
        cache.clear();
        if (! cacheFile.existsAsFile()) return;
        const auto v = juce::JSON::parse (cacheFile);
        if (! v.isObject() || (int) v.getProperty ("version", 0) != 1) return;
        if (auto* files = v.getProperty ("files", {}).getDynamicObject())
            for (auto& p : files->getProperties())
            {
                CacheEntry e;
                e.size = (juce::int64) p.value.getProperty ("s", 0);
                e.modified = (juce::int64) p.value.getProperty ("m", 0);
                e.info = CaptureInfo::fromVar (p.value.getProperty ("i", {}));
                cache[p.name.toString()] = e;
            }
    }
}

void setCaptureCacheFile (const juce::File& f)
{
    const juce::ScopedLock sl (cacheLock);
    if (f == cacheFile) return;
    cacheFile = f;
    loadCache();
    cacheDirty = false;
}

void saveCaptureCache()
{
    const juce::ScopedLock sl (cacheLock);
    if (! cacheDirty || cacheFile == juce::File()) return;
    cacheDirty = false;
    auto* files = new juce::DynamicObject();
    for (auto& [path, e] : cache)
    {
        if (! juce::File (path).existsAsFile()) continue;   // forget deleted files
        auto* o = new juce::DynamicObject();
        o->setProperty ("s", e.size);
        o->setProperty ("m", e.modified);
        o->setProperty ("i", e.info.toVar());
        files->setProperty (path, juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("about", "Rigsmith's cache of what each .nam file says about itself. Safe to delete.");
    root->setProperty ("files", juce::var (files));
    cacheFile.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (cacheFile);
    if (tmp.getFile().replaceWithText (juce::JSON::toString (juce::var (root), true)))
        tmp.overwriteTargetFileWithTemporary();
}

CaptureInfo readCaptureInfo (const juce::File& f)
{
    if (! f.hasFileExtension ("nam")) return {};
    const auto path = f.getFullPathName();
    const auto size = f.getSize();
    const auto modified = f.getLastModificationTime().toMilliseconds();
    {
        const juce::ScopedLock sl (cacheLock);
        if (auto it = cache.find (path); it != cache.end() && it->second.size == size && it->second.modified == modified)
            return it->second.info;
    }
    auto info = readFromFile (f);
    const juce::ScopedLock sl (cacheLock);
    cache[path] = { size, modified, info };
    cacheDirty = true;
    return info;
}

//==============================================================================
juce::String CaptureGroup::creatorText() const
{
    if (creators.isEmpty()) return {};
    if (creators.size() == 1) return creators[0];
    return creators[0] + " + " + juce::String (creators.size() - 1) + " more";
}

juce::String CaptureGroup::toneText() const
{
    juce::StringArray names;
    for (auto& t : toneTypes) names.addIfNotAlreadyThere (toneTypeName (t));
    return names.joinIntoString (", ");
}

namespace
{
    juce::String normalise (const juce::String& s)
    {
        return juce::StringArray::fromTokens (s.toLowerCase(), " \t", {}).joinIntoString (" ");
    }

    struct Facts
    {
        juce::String key, title, label, creator, toneType;
        Gear gear;
    };

    Facts factsFor (const juce::File& f)
    {
        Facts x;
        x.gear = classifyFile (f);
        const auto info = readCaptureInfo (f);
        const auto tone = ToneInfo::forFile (f);
        x.label = info.name.isNotEmpty() ? info.name : f.getFileNameWithoutExtension();
        x.creator = info.modeledBy.isNotEmpty() ? info.modeledBy : tone.creator;
        x.toneType = info.toneType;
        const auto gearTitle = info.gearTitle();
        const auto suffix = "|" + juce::String ((int) x.gear);
        if (gearTitle.isNotEmpty())
        {
            x.title = gearTitle;
            x.key = "g:" + normalise (gearTitle) + suffix;
        }
        else if (tone.valid)
        {
            x.title = tone.title;
            x.key = "t:" + (tone.id != 0 ? juce::String (tone.id) : normalise (tone.title)) + suffix;
        }
        else
        {
            x.title = f.getFileNameWithoutExtension();
            x.key = "f:" + f.getFullPathName();
        }
        return x;
    }

}

    /** What's left of each label after the part they all share ("Omega - Omega-BITE G3" -> "G3"). */
    juce::StringArray shortenCaptureLabels (const juce::StringArray& labels)
    {
        if (labels.size() < 2) return labels;
        int common = labels[0].length();
        for (auto& l : labels)
        {
            int i = 0;
            while (i < common && i < l.length() && juce::CharacterFunctions::toLowerCase (l[i]) == juce::CharacterFunctions::toLowerCase (labels[0][i])) ++i;
            common = i;
        }
        // back off to a word boundary so "Crunch" and "Clean" don't become "runch" and "lean"
        while (common > 0 && juce::String (" -_()[],.").indexOfChar (labels[0][common - 1]) < 0) --common;
        juce::StringArray out;
        for (auto& l : labels)
        {
            auto s = l.substring (common).trimCharactersAtStart (" -_()[],.").trim();
            if (s.isEmpty()) return labels;
            out.add (s);
        }
        return out;
    }

juce::String captureGroupKey (const juce::File& f) { return factsFor (f).key; }

std::vector<CaptureGroupPtr> groupCaptures (const juce::Array<juce::File>& files)
{
    struct Builder { CaptureGroup g; std::vector<std::pair<juce::String, int>> order; juce::StringArray labels, tones; };
    std::map<juce::String, Builder> builders;
    for (auto& f : files)
    {
        const auto x = factsFor (f);
        auto& b = builders[x.key];
        if (b.g.key.isEmpty()) { b.g.key = x.key; b.g.title = x.title; b.g.gear = x.gear; }
        b.g.files.add (f);
        b.labels.add (x.label);
        b.tones.add (x.toneType);
        if (x.creator.isNotEmpty()) b.g.creators.addIfNotAlreadyThere (x.creator, true);
        if (x.toneType.isNotEmpty()) b.g.toneTypes.addIfNotAlreadyThere (x.toneType, true);
    }

    std::vector<CaptureGroupPtr> out;
    out.reserve (builders.size());
    for (auto& [key, b] : builders)
    {
        // order by capture name
        std::vector<int> idx ((size_t) b.g.files.size());
        for (int i = 0; i < b.g.files.size(); ++i) idx[(size_t) i] = i;
        std::sort (idx.begin(), idx.end(), [&b] (int x, int y)
        {
            const int c = b.labels[x].compareNatural (b.labels[y]);
            return c != 0 ? c < 0 : b.g.files[x].getFileName().compareNatural (b.g.files[y].getFileName()) < 0;
        });
        auto g = std::make_shared<CaptureGroup> (b.g);
        g->files.clear();
        for (int i : idx) { g->files.add (b.g.files[i]); g->labels.add (b.labels[i]); g->fileToneTypes.add (b.tones[i]); }
        // captures that share a name get told apart by their file names
        std::map<juce::String, int> uses;
        for (auto& l : g->labels) ++uses[l.toLowerCase()];
        const auto original = g->labels;
        for (int i = 0; i < g->files.size(); ++i)
            if (uses[original[i].toLowerCase()] > 1)
                g->labels.set (i, g->files[i].getFileNameWithoutExtension());
        g->shortLabels = shortenCaptureLabels (g->labels);
        out.push_back (std::move (g));
    }
    std::sort (out.begin(), out.end(), [] (const CaptureGroupPtr& a, const CaptureGroupPtr& b)
    {
        if (a->gear != b->gear) return gearRank (a->gear) < gearRank (b->gear);
        const int c = a->title.compareNatural (b->title);
        return c != 0 ? c < 0 : a->key < b->key;
    });
    return out;
}

//==============================================================================
void CaptureLibrary::rescan()
{
    dirty[0] = dirty[1] = true;
    folderGroups.clear();
}

const CaptureLibrary::Set& CaptureLibrary::get (bool irs, juce::uint32 maxAgeMs)
{
    auto& s = sets[irs ? 1 : 0];
    const auto paths = getPaths ? getPaths (irs) : juce::FileSearchPath();
    const auto pathsKey = paths.toString();
    const auto now = juce::Time::getMillisecondCounter();
    if (! dirty[irs ? 1 : 0] && s.pathsKey == pathsKey && (maxAgeMs == 0 || now - s.scannedAt < maxAgeMs))
        return s;

    dirty[irs ? 1 : 0] = false;
    s = {};
    s.pathsKey = pathsKey;
    s.scannedAt = now;
    const auto pattern = irs ? juce::String ("*.wav;*.aif;*.aiff;*.flac") : juce::String ("*.nam");
    std::set<juce::String> seen;
    for (int p = 0; p < paths.getNumPaths() && s.files.size() < maxFiles; ++p)
        for (const auto& entry : juce::RangedDirectoryIterator (paths.getRawString (p), true, pattern, juce::File::findFiles))
        {
            if (seen.insert (entry.getFile().getFullPathName()).second)
                s.files.add (entry.getFile());
            if (s.files.size() >= maxFiles) break;
        }
    s.groups = groupCaptures (s.files);
    for (auto& g : s.groups)
        for (auto& f : g->files)
            s.byFile[f.getFullPathName()] = g;
    ++version;
    saveCaptureCache();
    return s;
}

CaptureGroupPtr CaptureLibrary::groupFor (const juce::File& f)
{
    if (f == juce::File()) return nullptr;
    const bool irs = ! f.hasFileExtension ("nam");
    const int i = irs ? 1 : 0;
    if (dirty[i]) get (irs);
    if (auto it = sets[i].byFile.find (f.getFullPathName()); it != sets[i].byFile.end())
        return it->second;

    // not in the library folders: group it with the files next to it
    const auto folder = f.getParentDirectory().getFullPathName();
    auto it = folderGroups.find (folder);
    if (it == folderGroups.end())
    {
        const auto files = f.getParentDirectory().findChildFiles (juce::File::findFiles, false,
                                                                  irs ? "*.wav;*.aif;*.aiff;*.flac" : "*.nam");
        it = folderGroups.emplace (folder, groupCaptures (files)).first;
    }
    for (auto& g : it->second)
        if (g->files.contains (f)) return g;

    juce::Array<juce::File> one;
    one.add (f);
    auto g = groupCaptures (one);
    if (g.empty()) return nullptr;
    it->second.push_back (g.front());
    return g.front();
}

} // namespace rig
