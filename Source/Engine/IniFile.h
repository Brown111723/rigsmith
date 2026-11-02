#pragma once

#include "Common.h"

namespace rig
{
/** A plain, hand-editable Windows-style .ini file:

        ; comment
        [Section]
        Key=Value

    Sections and keys keep their order. Lists (folders, exclusions) are stored as
    numbered keys: Folder1=, Folder2=... Every change re-reads the file first if
    something else (another Rigsmith instance, a text editor) changed it, then writes
    it straight back, so several plug-in instances never undo each other's changes. */
class IniFile
{
public:
    IniFile() = default;
    explicit IniFile (const juce::File& f) { setFile (f); }

    void setFile (const juce::File&);
    const juce::File& getFile() const noexcept { return file; }
    bool existed() const noexcept { return existedAtLoad; }

    /** Re-reads the file if it changed on disk since we last looked. */
    void reloadIfChanged();
    bool save();

    juce::String get (const juce::String& section, const juce::String& key, const juce::String& def = {}) const;
    bool has (const juce::String& section, const juce::String& key) const;
    bool getBool (const juce::String& section, const juce::String& key, bool def) const;
    int getInt (const juce::String& section, const juce::String& key, int def) const;

    /** These save immediately (only if the value actually changed). */
    void set (const juce::String& section, const juce::String& key, const juce::String& value);
    void setBool (const juce::String& section, const juce::String& key, bool v) { set (section, key, v ? "1" : "0"); }
    void setInt (const juce::String& section, const juce::String& key, int v) { set (section, key, juce::String (v)); }

    /** Every value in a section, in order (for numbered lists). */
    juce::StringArray getList (const juce::String& section) const;
    /** Replaces a section with prefix1=..., prefix2=... */
    void setList (const juce::String& section, const juce::String& keyPrefix, const juce::StringArray& values);

    /** Makes sure a section exists (new ones go at the end), so a fresh file has a tidy order. */
    void addSection (const juce::String& name) { getOrAdd (name); }

    /** Several edits, one write. */
    struct Batch
    {
        explicit Batch (IniFile& i) : ini (i) { ini.reloadIfChanged(); ++ini.batchDepth; }
        ~Batch() { if (--ini.batchDepth == 0 && ini.pending) ini.save(); }
        IniFile& ini;
    };

    /** Text written above the first section (each line becomes a ; comment). */
    juce::StringArray header;

    juce::String toString() const;
    void parse (const juce::String& text);

private:
    struct Section
    {
        juce::String name;
        std::vector<std::pair<juce::String, juce::String>> values;
    };
    Section* find (const juce::String& name);
    const Section* find (const juce::String& name) const;
    Section& getOrAdd (const juce::String& name);
    void changed();

    juce::File file;
    std::vector<Section> sections;
    juce::String lastText;   // what the file held when we last read or wrote it
    bool existedAtLoad = false, pending = false;
    int batchDepth = 0;
};

} // namespace rig
