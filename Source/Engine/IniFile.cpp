#include "IniFile.h"

namespace rig
{
void IniFile::setFile (const juce::File& f)
{
    file = f;
    sections.clear();
    existedAtLoad = file.existsAsFile();
    if (existedAtLoad)
    {
        lastText = file.loadFileAsString();
        parse (lastText);
    }
}

void IniFile::reloadIfChanged()
{
    // the file is small, so just compare its text (timestamps can be too coarse)
    if (! file.existsAsFile()) return;
    auto text = file.loadFileAsString();
    if (text != lastText)
    {
        parse (text);
        lastText = std::move (text);
    }
}

void IniFile::parse (const juce::String& text)
{
    sections.clear();
    Section* current = nullptr;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar (';') || line.startsWithChar ('#')) continue;
        if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            current = &getOrAdd (line.substring (1, line.length() - 1).trim());
            continue;
        }
        const int eq = line.indexOfChar ('=');
        if (eq <= 0) continue;
        if (current == nullptr) current = &getOrAdd ("General");
        auto key = line.substring (0, eq).trim();
        auto value = line.substring (eq + 1).trim();
        if (value.length() >= 2 && value.startsWithChar ('"') && value.endsWithChar ('"'))
            value = value.substring (1, value.length() - 1);
        bool replaced = false;
        for (auto& kv : current->values)
            if (kv.first.equalsIgnoreCase (key)) { kv.second = value; replaced = true; break; }
        if (! replaced) current->values.emplace_back (key, value);
    }
}

juce::String IniFile::toString() const
{
    juce::String s;
    for (auto& h : header)
        s << (h.isEmpty() ? juce::String (";") : "; " + h) << "\r\n";
    for (auto& sec : sections)
    {
        if (s.isNotEmpty()) s << "\r\n";
        s << "[" << sec.name << "]\r\n";
        for (auto& [k, v] : sec.values)
            s << k << "=" << v << "\r\n";
    }
    return s;
}

bool IniFile::save()
{
    pending = false;
    if (file == juce::File()) return false;
    file.getParentDirectory().createDirectory();
    // write to a temp file then swap, so a crash mid-write never leaves half a settings file
    juce::TemporaryFile tmp (file);
    const auto text = toString();
    if (! tmp.getFile().replaceWithText (text, false, false, nullptr)) return false;
    const bool ok = tmp.overwriteTargetFileWithTemporary();
    lastText = file.loadFileAsString();
    return ok;
}

void IniFile::changed()
{
    if (batchDepth > 0) pending = true;
    else save();
}

IniFile::Section* IniFile::find (const juce::String& name)
{
    for (auto& s : sections)
        if (s.name.equalsIgnoreCase (name)) return &s;
    return nullptr;
}

const IniFile::Section* IniFile::find (const juce::String& name) const
{
    for (auto& s : sections)
        if (s.name.equalsIgnoreCase (name)) return &s;
    return nullptr;
}

IniFile::Section& IniFile::getOrAdd (const juce::String& name)
{
    if (auto* s = find (name)) return *s;
    sections.push_back ({ name, {} });
    return sections.back();
}

juce::String IniFile::get (const juce::String& section, const juce::String& key, const juce::String& def) const
{
    if (auto* s = find (section))
        for (auto& kv : s->values)
            if (kv.first.equalsIgnoreCase (key)) return kv.second;
    return def;
}

bool IniFile::has (const juce::String& section, const juce::String& key) const
{
    if (auto* s = find (section))
        for (auto& kv : s->values)
            if (kv.first.equalsIgnoreCase (key)) return true;
    return false;
}

bool IniFile::getBool (const juce::String& section, const juce::String& key, bool def) const
{
    if (! has (section, key)) return def;
    const auto v = get (section, key).trim().toLowerCase();
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

int IniFile::getInt (const juce::String& section, const juce::String& key, int def) const
{
    if (! has (section, key)) return def;
    return get (section, key).getIntValue();
}

void IniFile::set (const juce::String& section, const juce::String& key, const juce::String& value)
{
    if (batchDepth == 0) reloadIfChanged();
    auto& s = getOrAdd (section);
    for (auto& kv : s.values)
        if (kv.first.equalsIgnoreCase (key))
        {
            if (kv.second == value) return;
            kv.second = value;
            changed();
            return;
        }
    s.values.emplace_back (key, value);
    changed();
}

juce::StringArray IniFile::getList (const juce::String& section) const
{
    juce::StringArray out;
    if (auto* s = find (section))
        for (auto& kv : s->values)
            if (kv.second.isNotEmpty()) out.add (kv.second);
    return out;
}

void IniFile::setList (const juce::String& section, const juce::String& prefix, const juce::StringArray& values)
{
    if (batchDepth == 0) reloadIfChanged();
    auto& s = getOrAdd (section);
    std::vector<std::pair<juce::String, juce::String>> fresh;
    for (int i = 0; i < values.size(); ++i)
        fresh.emplace_back (prefix + juce::String (i + 1), values[i]);
    if (fresh == s.values) return;
    s.values = std::move (fresh);
    changed();
}

} // namespace rig
