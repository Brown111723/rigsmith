#include "Engine.h"

namespace rig
{
//==============================================================================
bool MidiMapping::isGlobal() const noexcept
{
    return target == Target::Scene || target == Target::PresetNext || target == Target::PresetPrev
        || target == Target::Tuner || target == Target::MasterVolume;
}

juce::ValueTree MidiMapping::toState() const
{
    juce::ValueTree v ("Map");
    v.setProperty ("cc", cc, nullptr);
    v.setProperty ("ch", channel, nullptr);
    v.setProperty ("target", (int) target, nullptr);
    v.setProperty ("node", nodeId, nullptr);
    v.setProperty ("index", index, nullptr);
    v.setProperty ("latching", latching, nullptr);
    return v;
}

MidiMapping MidiMapping::fromState (const juce::ValueTree& v)
{
    MidiMapping m;
    m.cc = v.getProperty ("cc", -1);
    m.channel = v.getProperty ("ch", 0);
    m.target = (Target) (int) v.getProperty ("target", 0);
    m.nodeId = v.getProperty ("node").toString();
    m.index = v.getProperty ("index", 0);
    m.latching = v.getProperty ("latching", false);
    return m;
}

//==============================================================================
namespace
{
    /** Where each generic setting key lives in Rigsmith.ini. */
    struct IniKey { const char* key; const char* section; const char* name; };
    const IniKey iniKeys[] =
    {
        { "scanOnStartup",    "Plug-ins",  "ScanOnStartup" },
        { "pluginsScanned",   "Plug-ins",  "Scanned" },
        { "autoCategorise",   "Plug-ins",  "AutoCategorise" },
        { "fullScreen",       "Window",    "FullScreen" },
        { "libraryVisible",   "Window",    "ShowLibrary" },
        { "inspectorHeight",  "Window",    "PanelHeight" },
        { "randomUsePlugins", "Randomise", "UsePlugins" },
        { "randomSplits",     "Randomise", "AllowSplits" },
    };

    std::pair<juce::String, juce::String> iniLocation (const juce::String& key)
    {
        for (auto& k : iniKeys)
            if (key == k.key) return { k.section, k.name };
        return { "General", key };
    }

    const char* folderSection (const juce::String& key)
    {
        if (key == "modelPaths") return "NAM Folders";
        if (key == "irPaths")    return "IR Folders";
        if (key == "modelFolder" || key == "irFolder" || key == "presetFolder") return "Folders";
        return "Folders";
    }

    juce::String pluginSection (const juce::AudioPluginFormat& f)
    {
        return (f.getName() == "VST" ? juce::String ("VST2") : f.getName()) + " Folders";
    }

    constexpr const char* exModels = "Randomise - excluded NAM models";
    constexpr const char* exIrs = "Randomise - excluded IRs";
    constexpr const char* exPlugins = "Randomise - excluded plug-ins";

    juce::String fileKey (const juce::File& f) { return f.getFullPathName().toLowerCase(); }
}

juce::File Engine::findSettingsFile()
{
    // tests (and power users) can point Rigsmith somewhere else
    const auto env = juce::SystemStats::getEnvironmentVariable ("RIGSMITH_INI", {});
    if (env.isNotEmpty()) return juce::File (env);

    // portable copy: an ini next to Rigsmith.exe wins
    const auto beside = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("Rigsmith.ini");
    if (beside.existsAsFile()) return beside;

    // %APPDATA%\Rigsmith\Rigsmith.ini (not %TEMP%: Windows clears that out)
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Rigsmith").getChildFile ("Rigsmith.ini");
}

Engine::Engine() : root (std::make_unique<Chain>())
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Rigsmith";
    o.filenameSuffix = ".settings";
    o.folderName = "Rigsmith";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);

    juce::addDefaultFormatsToManager (formatManager);

    if (auto* s = props.getUserSettings())
        if (auto xml = s->getXmlValue ("pluginList"))
            knownPlugins.recreateFromXml (*xml);

    openSettings();
    PluginBlock::autoCategorise = getSetting ("autoCategorise", true);
    knownPlugins.addChangeListener (this);

    loadDefaultRig();
    for (auto& s : scenes) s = captureScene();
    startTimerHz (100);
}

Engine::~Engine()
{
    stopTimer();
    knownPlugins.removeChangeListener (this);
}

void Engine::openSettings()
{
    ini.setFile (findSettingsFile());
    ini.header = { "Rigsmith settings - read every time Rigsmith starts.",
                   "Safe to edit with Notepad while Rigsmith is closed. Delete it to reset everything.",
                   "Lists are numbered (Folder1=, Folder2=...). 1 = on, 0 = off.",
                   "The scanned plug-in list itself is cached in Rigsmith.settings in the same folder." };

    auto* old = props.getUserSettings();
    IniFile::Batch batch (ini);
    for (auto* sec : { "General", "Folders", "NAM Folders", "IR Folders", "VST3 Folders", "Plug-ins", "Window", "Randomise" })
        ini.addSection (sec);
   #ifdef JucePlugin_VersionString
    ini.set ("General", "Version", JucePlugin_VersionString);
   #endif

    if (! ini.existed() && old != nullptr)
    {
        // first run of v0.5: bring across everything the old settings file remembered
        for (auto& k : iniKeys)
            if (old->containsKey (k.key))
                ini.set (k.section, k.name, old->getValue (k.key) == "1" || old->getValue (k.key) == "true" ? "1"
                                         : old->getValue (k.key) == "0" || old->getValue (k.key) == "false" ? "0"
                                         : old->getValue (k.key));
        for (auto key : { "modelPaths", "irPaths" })
        {
            juce::FileSearchPath p (old->getValue (key));
            if (p.getNumPaths() == 0)
            {
                const juce::File single (old->getValue (juce::String (key) == "modelPaths" ? "modelFolder" : "irFolder"));
                if (single.isDirectory()) p.add (single);
            }
            juce::StringArray list;
            for (int i = 0; i < p.getNumPaths(); ++i) list.add (p.getRawString (i));
            if (! list.isEmpty()) ini.setList (folderSection (key), "Folder", list);
        }
        if (old->containsKey ("presetFolder")) ini.set ("Folders", "Presets", old->getValue ("presetFolder"));
        if (knownPlugins.getNumTypes() > 0) ini.set ("Plug-ins", "Scanned", "1");
    }

    // defaults, written out so they're easy to find and edit
    auto ensure = [this] (const char* section, const char* key, const juce::String& v)
    {
        if (! ini.has (section, key)) ini.set (section, key, v);
    };
    ensure ("Folders", "Presets", getFolder ("presetFolder").getFullPathName());
    ensure ("Plug-ins", "ScanOnStartup", "0");
    ensure ("Plug-ins", "Scanned", knownPlugins.getNumTypes() > 0 ? "1" : "0");
    ensure ("Plug-ins", "AutoCategorise", "1");
    ensure ("Window", "FullScreen", "0");
    ensure ("Window", "ShowLibrary", "1");
    ensure ("Randomise", "UsePlugins", "1");
    ensure ("Randomise", "AllowSplits", "1");

    for (auto* f : formatManager.getFormats())
    {
        if (! f->canScanForPlugins()) continue;
        const auto section = pluginSection (*f);
        if (ini.getList (section).isEmpty())
        {
            // seed from the scanner's last folders, or the standard locations
            auto p = old != nullptr ? juce::PluginListComponent::getLastSearchPath (*old, *f) : juce::FileSearchPath();
            if (p.getNumPaths() == 0) p = f->getDefaultLocationsToSearch();
            juce::StringArray list;
            for (int i = 0; i < p.getNumPaths(); ++i) list.add (p.getRawString (i));
            ini.setList (section, "Folder", list);
        }
        else if (old != nullptr)
            juce::PluginListComponent::setLastSearchPath (*old, *f, getPluginPaths (*f));   // the ini wins
    }
    for (auto key : { "modelPaths", "irPaths" })
        if (ini.getList (folderSection (key)).isEmpty())
            ini.setList (folderSection (key), "Folder", { getFolder (juce::String (key) == "modelPaths" ? "modelFolder" : "irFolder").getFullPathName() });

    for (auto* sec : { exModels, exIrs, exPlugins })
        if (ini.getList (sec).isEmpty()) ini.setList (sec, juce::String (sec) == exPlugins ? "Plugin" : "File", {});

    loadExclusions();
}

void Engine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
        if (auto* s = props.getUserSettings())
        {
            s->setValue ("pluginList", xml.get());
            s->saveIfNeeded();
            // folders changed in the scanner window go back into the ini
            for (auto* f : formatManager.getFormats())
                if (f->canScanForPlugins())
                {
                    const auto p = juce::PluginListComponent::getLastSearchPath (*s, *f);
                    if (p.getNumPaths() > 0) setPluginPaths (*f, p);
                }
        }
    markPluginsScanned();
}

juce::File Engine::getFolder (const juce::String& key) const
{
    const auto name = key == "modelFolder" ? "Models" : key == "irFolder" ? "IRs" : key == "presetFolder" ? "Presets" : "";
    if (juce::String (name).isNotEmpty())
    {
        const juce::File f (ini.get ("Folders", name));
        if (f.isDirectory()) return f;
    }
    auto base = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rigsmith");
    auto f = juce::String (name).isEmpty() ? base : base.getChildFile (name);
    f.createDirectory();
    return f;
}

void Engine::setFolder (const juce::String& key, const juce::File& f)
{
    const auto name = key == "modelFolder" ? "Models" : key == "irFolder" ? "IRs" : key == "presetFolder" ? "Presets" : key.toRawUTF8();
    ini.set ("Folders", name, f.getFullPathName());
}

juce::FileSearchPath Engine::getPaths (const juce::String& key) const
{
    juce::FileSearchPath p;
    for (auto& s : ini.getList (folderSection (key))) p.add (juce::File (s));
    p.removeNonExistentPaths();
    if (p.getNumPaths() == 0)
        p.add (getFolder (key == "modelPaths" ? "modelFolder" : "irFolder"));
    return p;
}

void Engine::setPaths (const juce::String& key, const juce::FileSearchPath& p)
{
    juce::StringArray list;
    for (int i = 0; i < p.getNumPaths(); ++i) list.add (p.getRawString (i));
    ini.setList (folderSection (key), "Folder", list);
}

juce::FileSearchPath Engine::getPluginPaths (juce::AudioPluginFormat& f) const
{
    juce::FileSearchPath p;
    for (auto& s : ini.getList (pluginSection (f))) p.add (juce::File (s));
    if (p.getNumPaths() == 0) p = f.getDefaultLocationsToSearch();
    return p;
}

void Engine::setPluginPaths (juce::AudioPluginFormat& f, const juce::FileSearchPath& p)
{
    juce::StringArray list;
    for (int i = 0; i < p.getNumPaths(); ++i) list.add (p.getRawString (i));
    ini.setList (pluginSection (f), "Folder", list);
    if (auto* s = props.getUserSettings())
        juce::PluginListComponent::setLastSearchPath (*s, f, p);
}

bool Engine::hasScannedPlugins() const
{
    return ini.getBool ("Plug-ins", "Scanned", false);
}

void Engine::markPluginsScanned()
{
    IniFile::Batch b (ini);
    ini.set ("Plug-ins", "Scanned", "1");
    ini.set ("Plug-ins", "LastScan", juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M"));
}

bool Engine::getSetting (const juce::String& key, bool def) const
{
    const auto [section, name] = iniLocation (key);
    return ini.getBool (section, name, def);
}

void Engine::setSetting (const juce::String& key, bool value)
{
    const auto [section, name] = iniLocation (key);
    ini.setBool (section, name, value);
}

int Engine::getIntSetting (const juce::String& key, int def) const
{
    const auto [section, name] = iniLocation (key);
    return ini.getInt (section, name, def);
}

void Engine::setIntSetting (const juce::String& key, int value)
{
    const auto [section, name] = iniLocation (key);
    ini.setInt (section, name, value);
}

//==============================================================================
void Engine::loadExclusions()
{
    excludedFiles.clear();
    excludedPlugins.clear();
    excludedPluginNames.clear();
    for (auto* sec : { exModels, exIrs })
        for (auto& v : ini.getList (sec))
            excludedFiles[v.trim().toLowerCase()] = v.trim();
    // "Name (Maker) | identifier": the name is only there to make the file readable
    for (auto& v : ini.getList (exPlugins))
    {
        const auto id = v.containsChar ('|') ? v.fromLastOccurrenceOf ("|", false, false).trim() : v.trim();
        excludedPlugins.insert (id);
        excludedPluginNames[id] = v.upToLastOccurrenceOf ("|", false, false).trim();
    }
}

void Engine::saveExclusions()
{
    juce::StringArray models, irs, plugins;
    for (auto& [key, path] : excludedFiles)
        (key.endsWithIgnoreCase (".nam") ? models : irs).add (path);
    for (auto& id : excludedPlugins)
    {
        juce::String name = excludedPluginNames.count (id) ? excludedPluginNames[id] : juce::String();
        plugins.add (name.isNotEmpty() ? name + " | " + id : id);
    }
    models.sortNatural(); irs.sortNatural(); plugins.sortNatural();
    ++exclusionsVersion;
    IniFile::Batch b (ini);
    ini.setList (exModels, "File", models);
    ini.setList (exIrs, "File", irs);
    ini.setList (exPlugins, "Plugin", plugins);
}

bool Engine::isRandomExcluded (const juce::File& f) const
{
    return excludedFiles.count (fileKey (f)) > 0;
}

bool Engine::isRandomExcluded (const juce::PluginDescription& d) const
{
    return excludedPlugins.count (d.createIdentifierString()) > 0;
}

void Engine::setRandomExcluded (const juce::Array<juce::File>& files, bool excluded)
{
    ini.reloadIfChanged();
    loadExclusions();   // pick up changes another instance made
    for (auto& f : files)
    {
        if (excluded) excludedFiles[fileKey (f)] = f.getFullPathName();
        else excludedFiles.erase (fileKey (f));
    }
    saveExclusions();
}

void Engine::setRandomExcluded (const juce::Array<juce::PluginDescription>& descs, bool excluded)
{
    ini.reloadIfChanged();
    loadExclusions();
    for (auto& d : descs)
    {
        const auto id = d.createIdentifierString();
        if (excluded)
        {
            excludedPlugins.insert (id);
            excludedPluginNames[id] = d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String());
        }
        else { excludedPlugins.erase (id); excludedPluginNames.erase (id); }
    }
    saveExclusions();
}

int Engine::numRandomExcluded (int kind) const
{
    if (kind == 2) return (int) excludedPlugins.size();
    int n = 0;
    for (auto& kv : excludedFiles)
        if (kv.first.endsWithIgnoreCase (".nam") == (kind == 0)) ++n;
    return n;
}

void Engine::setAnalyzedNode (Node* n)
{
    visitNodes (*root, [] (Node& x) { x.tap = nullptr; });
    analyzer.reset();
    if (n != nullptr) n->tap = &analyzer;
}

juce::File Engine::getDeadMansPedalFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Rigsmith").getChildFile ("scan-in-progress.txt");
}

//==============================================================================
void Engine::prepare (double sr, int block)
{
    sampleRate = sr;
    blockSize = juce::jmax (16, block);
    {
        const juce::ScopedLock sl (lock);
        root->prepare (sampleRate, blockSize);
    }
    tuner.prepare (sr);
    for (auto* v : { &inGain, &outGain, &inPanL, &inPanR, &outPanL, &outPanR, &outWidth })
        v->reset (sr, 0.02);
    limGain = 1.0f;
    reportedLatency = -1;
}

int Engine::getLatency() const noexcept { return root->getLatency(); }

void Engine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        int type = -1, num = 0, val = 0;
        if (m.isController())        { type = 0; num = m.getControllerNumber(); val = m.getControllerValue(); }
        else if (m.isProgramChange()) { type = 1; num = m.getProgramChangeNumber(); }
        if (type >= 0 && midiFifo.getFreeSpace() > 0)
        {
            const auto packed = (juce::uint32) ((type << 24) | ((m.getChannel() & 0xff) << 16) | ((num & 0x7f) << 8) | (val & 0x7f));
            midiFifo.write (1).forEach ([&] (int idx) { midiData[(size_t) idx] = packed; });
        }
    }

    const int total = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || total == 0)
        return;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    // --- input routing
    switch (inputMode.load())
    {
        case Input1:   juce::FloatVectorOperations::copy (R, L, total); break;
        case Input2:   juce::FloatVectorOperations::copy (L, R, total); break;
        case InputSum: juce::FloatVectorOperations::add (L, R, total); juce::FloatVectorOperations::copy (R, L, total); break;
        default: break;
    }

    // --- input stage: gain, balance, polarity
    {
        const float g = dbToGain (inputGainDb.load()) * (inputPhase.load() ? -1.0f : 1.0f);
        const float p = inputPan.load();
        inGain.setTargetValue (g);
        inPanL.setTargetValue (juce::jmin (1.0f, 1.0f - p));
        inPanR.setTargetValue (juce::jmin (1.0f, 1.0f + p));
        for (int i = 0; i < total; ++i)
        {
            const float gi = inGain.getNextValue();
            L[i] *= gi * inPanL.getNextValue();
            R[i] *= gi * inPanR.getNextValue();
        }
    }
    inMeter.write (L, R, total, 0.999f);
    inPeak.store (juce::jmax (inPeak.load(), inMeter.peakL.load(), inMeter.peakR.load()));
    if (tunerActive.load())
        tuner.push (L, total);

    // --- the rig, in chunks no larger than prepared
    {
        const juce::ScopedTryLock tl (lock);
        for (int off = 0; off < total; off += blockSize)
        {
            const int len = juce::jmin (blockSize, total - off);
            float* ptrs[2] = { L + off, R + off };
            juce::AudioBuffer<float> view (ptrs, 2, len);
            if (tl.isLocked()) root->process (view, len);
            else view.clear();
        }
    }

    if (tunerMute.load() && tunerActive.load())
        buffer.clear (0, 0, total), buffer.clear (1, 0, total);

    // --- output stage: width, balance, master
    {
        const float p = outputPan.load();
        outGain.setTargetValue (dbToGain (outputGainDb.load()));
        outPanL.setTargetValue (juce::jmin (1.0f, 1.0f - p));
        outPanR.setTargetValue (juce::jmin (1.0f, 1.0f + p));
        outWidth.setTargetValue (outputWidth.load() * 0.01f);
        for (int i = 0; i < total; ++i)
        {
            const float w = outWidth.getNextValue();
            const float mid = 0.5f * (L[i] + R[i]), side = 0.5f * (L[i] - R[i]) * w;
            const float g = outGain.getNextValue();
            L[i] = (mid + side) * g * outPanL.getNextValue();
            R[i] = (mid - side) * g * outPanR.getNextValue();
        }
    }

    // --- brickwall limiter: instant attack (nothing gets past the ceiling), smooth release, no latency
    const float ceiling = dbToGain (juce::jlimit (-24.0f, 0.0f, limiterCeilingDb.load()));
    if (limiterOn.load())
    {
        const float rel = 1.0f - std::exp (-1.0f / (juce::jmax (1.0f, limiterReleaseMs.load()) * 0.001f * (float) sampleRate));
        float minGain = 1.0f;
        int overs = 0;
        for (int i = 0; i < total; ++i)
        {
            const float pk = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            const float target = pk > ceiling ? ceiling / pk : 1.0f;
            if (pk > ceiling) ++overs;
            limGain = target < limGain ? target : limGain + (target - limGain) * rel;
            L[i] = juce::jlimit (-ceiling, ceiling, L[i] * limGain);
            R[i] = juce::jlimit (-ceiling, ceiling, R[i] * limGain);
            minGain = juce::jmin (minGain, limGain);
        }
        limiterGrDb.store (juce::jmin (limiterGrDb.load(), gainToDb (minGain)));
        outMeter.overs.fetch_add (overs);
        outMeter.write (L, R, total, 2.0f);
    }
    else
    {
        limGain = 1.0f;
        outMeter.write (L, R, total, 0.999f);
    }
    outPeak.store (juce::jmax (outPeak.load(), outMeter.peakL.load(), outMeter.peakR.load()));
}

void Engine::Meter::write (const float* l, const float* r, int n, float clipAt) noexcept
{
    float pl = 0, pr = 0;
    double sl = 0, sr = 0;
    int o = 0;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (l[i]), b = std::abs (r[i]);
        pl = juce::jmax (pl, a); pr = juce::jmax (pr, b);
        sl += (double) a * a; sr += (double) b * b;
        if (a >= clipAt || b >= clipAt) ++o;
    }
    peakL.store (juce::jmax (peakL.load(), pl));
    peakR.store (juce::jmax (peakR.load(), pr));
    rmsL.store ((float) std::sqrt (sl / juce::jmax (1, n)));
    rmsR.store ((float) std::sqrt (sr / juce::jmax (1, n)));
    if (o > 0) overs.fetch_add (o);
}

//==============================================================================
void Engine::timerCallback()
{
    while (midiFifo.getNumReady() > 0)
    {
        juce::uint32 packed = 0;
        midiFifo.read (1).forEach ([&] (int idx) { packed = midiData[(size_t) idx]; });
        handleMidi ((int) (packed >> 24), (int) ((packed >> 16) & 0xff), (int) ((packed >> 8) & 0x7f), (int) (packed & 0x7f));
    }

    const int hs = pendingHostScene.exchange (-1);
    if (hs >= 0 && hs != activeScene)
        setScene (hs);

    if (++tick % 10 == 0)
    {
        const int lat = getLatency();
        if (lat != reportedLatency)
        {
            reportedLatency = lat;
            if (onLatencyChanged) onLatencyChanged (lat);
        }
    }
}

void Engine::handleMidi (int type, int channel, int number, int value)
{
    if (type == 1)
    {
        auto files = getPresetFiles();
        if (juce::isPositiveAndBelow (number, files.size()))
            loadPreset (files[number]);
        return;
    }

    if (learning)
    {
        learning = false;
        learnProto.cc = number;
        learnProto.channel = 0;
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& m)
        {
            return m.target == learnProto.target && m.nodeId == learnProto.nodeId && m.index == learnProto.index;
        }), mappings.end());
        mappings.push_back (learnProto);
        markDirty();
        message ("Mapped CC " + juce::String (number) + " to " + describe (learnProto));
        listeners.call ([] (Listener& l) { l.midiMapChanged(); });
        return;
    }

    const int ch = juce::jlimit (1, 16, channel);
    const int previous = lastCcValue[ch - 1][number];
    lastCcValue[ch - 1][number] = (juce::uint8) value;

    for (const auto& m : mappings)
    {
        if (m.cc != number || (m.channel != 0 && m.channel != ch))
            continue;

        const bool continuous = m.target == MidiMapping::Target::BranchLevel || m.target == MidiMapping::Target::Blend
                             || m.target == MidiMapping::Target::Param || m.target == MidiMapping::Target::MasterVolume;
        if (continuous || m.latching)
            applyMapping (m, value);
        else if (value >= 64 && previous < 64)   // rising edge = one press
            applyMapping (m, 127);
        else if (value >= 64 && previous >= 64 && value == 127)  // switches that only ever send 127
            applyMapping (m, 127);
    }
}

void Engine::applyMapping (const MidiMapping& m, int value)
{
    const float v = (float) value / 127.0f;
    using T = MidiMapping::Target;

    switch (m.target)
    {
        case T::Scene:      if (value >= 64) setScene (m.index); return;
        case T::PresetNext: if (value >= 64) stepPreset (1); return;
        case T::PresetPrev: if (value >= 64) stepPreset (-1); return;
        case T::Tuner:
            if (value >= 64 || m.latching)
            {
                tunerActive = m.latching ? value >= 64 : ! tunerActive.load();
                listeners.call ([] (Listener& l) { l.tunerToggled(); });
            }
            return;
        case T::MasterVolume: outputGainDb = juce::jmap (v, -40.0f, 6.0f); return;
        default: break;
    }

    auto* node = findNode (m.nodeId);
    if (node == nullptr) return;

    if (m.target == T::Bypass)
    {
        node->bypassed = m.latching ? value < 64 : ! node->bypassed.load();
    }
    else if (auto* s = dynamic_cast<Split*> (node))
    {
        if (m.target == T::BranchLevel)
            s->setLevel (m.index, v, true);
        else if (m.target == T::Blend && s->branches.size() >= 2)
        {
            s->branches[0]->level = 1.0f - v;
            s->branches[1]->level = v;
        }
    }
    else if (auto* pb = dynamic_cast<PluginBlock*> (node); pb != nullptr && m.target == T::Param && m.index >= 1000)
    {
        if (auto* inst = pb->getInstance())
        {
            auto params = inst->getParameters();
            if (juce::isPositiveAndBelow (m.index - 1000, params.size()))
                params[m.index - 1000]->setValueNotifyingHost (v);
        }
    }
    else if (auto* b = dynamic_cast<Block*> (node))
    {
        if (m.target == T::Param && juce::isPositiveAndBelow (m.index, b->numParams()))
        {
            const auto& sp = b->getSpecs()[(size_t) m.index];
            b->set (m.index, sp.min + (sp.max - sp.min) * v);
        }
    }

    storeScene();
    listeners.call ([] (Listener& l) { l.sceneChanged(); });
}

void Engine::startLearn (MidiMapping proto)
{
    learnProto = proto;
    learning = true;
    message ("MIDI learn: move a controller or press a footswitch for " + describe (proto));
}

void Engine::cancelLearn()
{
    learning = false;
    message ({});
}

void Engine::removeMapping (int index)
{
    if (juce::isPositiveAndBelow (index, (int) mappings.size()))
    {
        mappings.erase (mappings.begin() + index);
        markDirty();
        listeners.call ([] (Listener& l) { l.midiMapChanged(); });
    }
}

juce::String Engine::describe (const MidiMapping& m) const
{
    using T = MidiMapping::Target;
    auto nodeName = [this, &m]
    {
        auto* n = findNode (m.nodeId);
        return n != nullptr ? n->getName() : juce::String ("(missing block)");
    };
    switch (m.target)
    {
        case T::Scene:        return "Scene " + sceneNames[juce::jlimit (0, kNumScenes - 1, m.index)];
        case T::Bypass:       return "Bypass " + nodeName();
        case T::BranchLevel:  return nodeName() + " path " + juce::String::charToString ((juce::juce_wchar) ('A' + m.index)) + " level";
        case T::Blend:        return nodeName() + " A/B blend";
        case T::Param:
        {
            if (auto* pb = dynamic_cast<PluginBlock*> (findNode (m.nodeId)); pb != nullptr && m.index >= 1000 && pb->getInstance() != nullptr)
            {
                auto params = pb->getInstance()->getParameters();
                if (juce::isPositiveAndBelow (m.index - 1000, params.size()))
                    return pb->getName() + " " + params[m.index - 1000]->getName (32);
            }
            if (auto* b = dynamic_cast<Block*> (findNode (m.nodeId)))
                if (juce::isPositiveAndBelow (m.index, b->numParams()))
                    return b->getName() + " " + b->getSpecs()[(size_t) m.index].name;
            return nodeName();
        }
        case T::PresetNext:   return "Next preset";
        case T::PresetPrev:   return "Previous preset";
        case T::Tuner:        return "Tuner";
        case T::MasterVolume: return "Master volume";
    }
    return {};
}

//==============================================================================
Node* Engine::findNode (const juce::String& id) const
{
    Node* found = nullptr;
    if (id.isEmpty()) return nullptr;
    visitNodes (*root, [&] (Node& n) { if (found == nullptr && n.id == id) found = &n; });
    return found;
}

Engine::Location Engine::locate (const juce::String& id) const
{
    Location loc;
    visitNodes (*root, [&] (Node& n)
    {
        if (loc.chain != nullptr) return;
        if (auto* c = dynamic_cast<Chain*> (&n))
            for (int i = 0; i < (int) c->nodes.size(); ++i)
                if (c->nodes[(size_t) i]->id == id) { loc.chain = c; loc.index = i; return; }
    });
    return loc;
}

Split* Engine::findOwnerSplit (const Chain& chain, int* branchIndex) const
{
    Split* owner = nullptr;
    visitNodes (*root, [&] (Node& n)
    {
        if (owner != nullptr) return;
        if (auto* s = dynamic_cast<Split*> (&n))
            for (int i = 0; i < (int) s->branches.size(); ++i)
                if (s->branches[(size_t) i]->chain.get() == &chain)
                {
                    owner = s;
                    if (branchIndex != nullptr) *branchIndex = i;
                    return;
                }
    });
    return owner;
}

std::unique_ptr<Node> Engine::createNode (NodeType type) const
{
    switch (type)
    {
        case NodeType::Chain: return std::make_unique<Chain>();
        case NodeType::Split:
        {
            auto s = std::make_unique<Split>();
            for (int i = 0; i < 2; ++i)
            {
                s->branches.push_back (std::make_unique<Split::Branch>());
                s->branches.back()->level = 0.5f;
            }
            return s;
        }
        case NodeType::Nam:    return std::make_unique<NamBlock>();
        case NodeType::Ir:     return std::make_unique<IrBlock>();
        case NodeType::Gate:   return std::make_unique<GateBlock>();
        case NodeType::Eq:     return std::make_unique<EqBlock>();
        case NodeType::Gain:   return std::make_unique<GainBlock>();
        case NodeType::Comp:      return std::make_unique<CompBlock>();
        case NodeType::Limiter:   return std::make_unique<LimiterBlock>();
        case NodeType::Expander:  return std::make_unique<ExpanderBlock>();
        case NodeType::Transient: return std::make_unique<TransientBlock>();
        case NodeType::Swell:     return std::make_unique<SwellBlock>();
        case NodeType::Overdrive: return std::make_unique<OverdriveBlock>();
        case NodeType::Fuzz:      return std::make_unique<FuzzBlock>();
        case NodeType::Boost:     return std::make_unique<BoostBlock>();
        case NodeType::Crusher:   return std::make_unique<CrusherBlock>();
        case NodeType::Wah:       return std::make_unique<WahBlock>();
        case NodeType::Chorus:    return std::make_unique<ChorusBlock>();
        case NodeType::Flanger:   return std::make_unique<FlangerBlock>();
        case NodeType::Phaser:    return std::make_unique<PhaserBlock>();
        case NodeType::Tremolo:   return std::make_unique<TremoloBlock>();
        case NodeType::Vibe:      return std::make_unique<VibeBlock>();
        case NodeType::Rotary:    return std::make_unique<RotaryBlock>();
        case NodeType::Delay:     return std::make_unique<DelayBlock>();
        case NodeType::Reverb:    return std::make_unique<ReverbBlock>();
        case NodeType::Pitch:     return std::make_unique<PitchBlock>();
        case NodeType::Tuner:     return std::make_unique<TunerBlock>();
        case NodeType::Plugin: break;
    }
    return nullptr;
}

std::unique_ptr<Node> Engine::createPlugin (const juce::PluginDescription& desc, juce::String& error)
{
    auto inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, error);
    if (inst == nullptr)
        return nullptr;
    return std::make_unique<PluginBlock> (std::move (inst), desc);
}

void Engine::prepareNode (Node& n) { n.prepare (sampleRate, blockSize); }

void Engine::announceDeletion (Node& n)
{
    visitNodes (n, [this] (Node& x) { listeners.call ([&x] (Listener& l) { l.nodeWillBeDeleted (x); }); });
}

Node* Engine::insert (std::unique_ptr<Node> node, Chain& target, int index)
{
    if (node == nullptr) return nullptr;
    prepareNode (*node);
    auto* raw = node.get();
    {
        const juce::ScopedLock sl (lock);
        index = juce::jlimit (0, (int) target.nodes.size(), index < 0 ? (int) target.nodes.size() : index);
        target.nodes.insert (target.nodes.begin() + index, std::move (node));
    }
    storeScene();
    markDirty();
    notifyGraphChanged();
    return raw;
}

Node* Engine::replace (const juce::String& id, std::unique_ptr<Node> n, bool keepMappings)
{
    auto loc = locate (id);
    if (loc.chain == nullptr || n == nullptr) return nullptr;
    checkpoint();
    auto& slot = loc.chain->nodes[(size_t) loc.index];
    n->id = id;
    n->bypassed = slot->bypassed.load();
    n->locked = slot->locked.load();
    n->autoGain = slot->autoGain.load();
    if (n->autoGain.load())
    {
        n->autoGainDb = slot->autoGainDb.load();   // start from the old match, then measure the new block
        n->requestRelearn();
    }
    prepareNode (*n);
    auto* raw = n.get();
    std::unique_ptr<Node> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (slot);
        slot = std::move (n);
    }
    announceDeletion (*old);
    old.reset();
    // mappings that pointed at parameters of the old block no longer make sense
    if (! keepMappings)
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& m)
        {
            return m.nodeId == id && m.target == MidiMapping::Target::Param;
        }), mappings.end());
    markDirty();
    notifyGraphChanged();
    return raw;
}

std::unique_ptr<Node> Engine::extract (const juce::String& id, Location* where)
{
    auto loc = locate (id);
    if (loc.chain == nullptr) return nullptr;
    if (where != nullptr) *where = loc;
    const juce::ScopedLock sl (lock);
    auto n = std::move (loc.chain->nodes[(size_t) loc.index]);
    loc.chain->nodes.erase (loc.chain->nodes.begin() + loc.index);
    return n;
}

bool Engine::swap (const juce::String& a, const juce::String& b)
{
    if (a == b) return false;
    auto la = locate (a), lb = locate (b);
    if (la.chain == nullptr || lb.chain == nullptr) return false;
    if (! canDropInto (a, *lb.chain) || ! canDropInto (b, *la.chain)) return false;
    checkpoint();
    {
        const juce::ScopedLock sl (lock);
        std::swap (la.chain->nodes[(size_t) la.index], lb.chain->nodes[(size_t) lb.index]);
    }
    markDirty();
    notifyGraphChanged();
    return true;
}

void Engine::installRoot (std::unique_ptr<Chain> r, const std::function<void (Chain&, Chain&)>& splice)
{
    if (r == nullptr) return;
    r->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        if (splice) splice (*root, *r);
        std::swap (root, r);
    }
    announceDeletion (*r);
    r.reset();
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [this] (const MidiMapping& m)
    {
        return ! m.isGlobal() && findNode (m.nodeId) == nullptr;
    }), mappings.end());
    for (auto& s : scenes) s = captureScene();
    markDirty();
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); });
}

void Engine::remove (const juce::String& id)
{
    if (locate (id).chain == nullptr) return;
    checkpoint();
    auto n = extract (id);
    if (n == nullptr) return;
    announceDeletion (*n);
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& m) { return m.nodeId == id; }), mappings.end());
    n.reset();
    markDirty();
    notifyGraphChanged();
}

bool Engine::canDropInto (const juce::String& movingId, const Chain& target) const
{
    auto* moving = findNode (movingId);
    if (moving == nullptr) return false;
    bool inside = false;
    visitNodes (*moving, [&] (Node& n) { if (&n == &target) inside = true; });
    return ! inside;
}

bool Engine::move (const juce::String& id, Chain& target, int index)
{
    if (! canDropInto (id, target)) return false;
    auto loc = locate (id);
    if (loc.chain == nullptr) return false;
    {
        const juce::ScopedLock sl (lock);
        auto n = std::move (loc.chain->nodes[(size_t) loc.index]);
        loc.chain->nodes.erase (loc.chain->nodes.begin() + loc.index);
        if (loc.chain == &target && index > loc.index) --index;
        index = juce::jlimit (0, (int) target.nodes.size(), index);
        target.nodes.insert (target.nodes.begin() + index, std::move (n));
    }
    markDirty();
    notifyGraphChanged();
    return true;
}

static void stripIds (juce::ValueTree v)
{
    v.removeProperty ("id", nullptr);
    for (auto c : v) stripIds (c);
}

Node* Engine::duplicate (const juce::String& id)
{
    auto loc = locate (id);
    if (loc.chain == nullptr) return nullptr;
    auto state = loc.chain->nodes[(size_t) loc.index]->toState().createCopy();
    stripIds (state);
    return insert (buildNode (state), *loc.chain, loc.index + 1);
}

Split* Engine::addSplit (Chain& target, int index, int numBranches)
{
    auto s = std::make_unique<Split>();
    for (int i = 0; i < numBranches; ++i)
    {
        s->branches.push_back (std::make_unique<Split::Branch>());
        s->branches.back()->level = 1.0f / (float) numBranches;
    }
    return static_cast<Split*> (insert (std::move (s), target, index));
}

Split* Engine::wrapInSplit (const juce::String& id, int numBranches)
{
    Location loc;
    auto n = extract (id, &loc);
    if (n == nullptr) return nullptr;
    auto s = std::make_unique<Split>();
    for (int i = 0; i < numBranches; ++i)
    {
        s->branches.push_back (std::make_unique<Split::Branch>());
        s->branches.back()->level = 1.0f / (float) numBranches;
    }
    s->branches[0]->chain->nodes.push_back (std::move (n));
    return static_cast<Split*> (insert (std::move (s), *loc.chain, loc.index));
}

void Engine::addBranch (Split& split)
{
    if ((int) split.branches.size() >= kMaxBranches)
    {
        message ("A split can have up to " + juce::String (kMaxBranches) + " paths");
        return;
    }
    auto b = split.makeBranch();
    {
        const juce::ScopedLock sl (lock);
        b->level = split.getMode() == SplitMode::Split ? 0.0f : 1.0f;
        split.branches.push_back (std::move (b));
    }
    if (split.getMode() == SplitMode::Split)
        split.setLevel ((int) split.branches.size() - 1, 1.0f / (float) split.branches.size(), true);
    storeScene();
    markDirty();
    notifyGraphChanged();
}

void Engine::removeBranch (Split& split, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) split.branches.size()) || split.branches.size() <= 1)
        return;
    checkpoint();
    std::unique_ptr<Split::Branch> b;
    {
        const juce::ScopedLock sl (lock);
        b = std::move (split.branches[(size_t) index]);
        split.branches.erase (split.branches.begin() + index);
    }
    announceDeletion (*b->chain);
    b.reset();
    if (split.getMode() == SplitMode::Split)
        split.setMode (SplitMode::Split);  // renormalise to 100%
    storeScene();
    markDirty();
    notifyGraphChanged();
}

void Engine::unwrapSplit (Split& split)
{
    auto loc = locate (split.id);
    if (loc.chain == nullptr) return;
    checkpoint();
    std::unique_ptr<Node> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (loc.chain->nodes[(size_t) loc.index]);
        loc.chain->nodes.erase (loc.chain->nodes.begin() + loc.index);
        auto& s = static_cast<Split&> (*old);
        if (! s.branches.empty())
        {
            auto& keep = s.branches[0]->chain->nodes;
            for (int i = 0; i < (int) keep.size(); ++i)
                loc.chain->nodes.insert (loc.chain->nodes.begin() + loc.index + i, std::move (keep[(size_t) i]));
            keep.clear();
        }
    }
    announceDeletion (*old);
    old.reset();
    markDirty();
    notifyGraphChanged();
}

void Engine::notifyGraphChanged()
{
    listeners.call ([] (Listener& l) { l.graphChanged(); });
}

void Engine::message (const juce::String& text)
{
    listeners.call ([&text] (Listener& l) { l.showMessage (text); });
}

void Engine::markDirty()
{
    if (! dirty)
    {
        dirty = true;
        listeners.call ([] (Listener& l) { l.presetChanged(); });
    }
}

//==============================================================================
juce::ValueTree Engine::captureScene() const
{
    juce::ValueTree s ("Scene");
    visitNodes (*root, [&] (Node& n)
    {
        if (&n == root.get()) return;
        juce::ValueTree e ("N");
        e.setProperty ("id", n.id, nullptr);
        e.setProperty ("bypass", n.bypassed.load(), nullptr);
        if (auto* sp = dynamic_cast<Split*> (&n))
        {
            juce::StringArray lv, mu;
            for (auto& b : sp->branches)
            {
                lv.add (juce::String (b->level.load(), 4));
                mu.add (b->mute.load() ? "1" : "0");
            }
            e.setProperty ("levels", lv.joinIntoString (","), nullptr);
            e.setProperty ("mutes", mu.joinIntoString (","), nullptr);
        }
        s.appendChild (e, nullptr);
    });
    return s;
}

void Engine::recallScene (const juce::ValueTree& s)
{
    for (auto e : s)
    {
        auto* n = findNode (e["id"].toString());
        if (n == nullptr) continue;
        n->bypassed = (bool) e["bypass"];
        if (auto* sp = dynamic_cast<Split*> (n))
        {
            auto lv = juce::StringArray::fromTokens (e["levels"].toString(), ",", {});
            auto mu = juce::StringArray::fromTokens (e["mutes"].toString(), ",", {});
            for (int i = 0; i < (int) sp->branches.size(); ++i)
            {
                if (i < lv.size()) sp->branches[(size_t) i]->level = lv[i].getFloatValue();
                if (i < mu.size()) sp->branches[(size_t) i]->mute = mu[i] == "1";
            }
        }
    }
}

void Engine::storeScene()
{
    scenes[activeScene] = captureScene();
}

void Engine::copySceneTo (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumScenes)) return;
    storeScene();
    scenes[index] = captureScene();
    markDirty();
}

void Engine::setScene (int index)
{
    index = juce::jlimit (0, kNumScenes - 1, index);
    if (index == activeScene) return;
    storeScene();
    activeScene = index;
    if (scenes[index].isValid() && scenes[index].getNumChildren() > 0)
        recallScene (scenes[index]);
    else
        storeScene();
    if (onSceneChanged) onSceneChanged (index);
    listeners.call ([] (Listener& l) { l.sceneChanged(); });
}

//==============================================================================
std::unique_ptr<Node> Engine::createFromState (const juce::ValueTree& v) { return buildNode (v); }

static void syncAll (Node& n) { visitNodes (n, [] (Node& x) { x.syncEdits(); }); }

std::unique_ptr<Node> Engine::buildNode (const juce::ValueTree& v)
{
    const auto type = nodeTypeFromString (v["type"].toString());

    if (type == NodeType::Chain)
    {
        auto c = std::make_unique<Chain>();
        c->fromState (v);
        for (auto child : v)
            if (child.hasType ("Node"))
                if (auto n = buildNode (child))
                    c->nodes.push_back (std::move (n));
        syncAll (*c);
        return c;
    }

    if (type == NodeType::Split)
    {
        auto s = std::make_unique<Split>();
        s->fromState (v);
        for (auto bt : v)
        {
            if (! bt.hasType ("Branch")) continue;
            auto b = std::make_unique<Split::Branch>();
            b->level = (float) bt.getProperty ("level", 0.5f);
            b->pan = (float) bt.getProperty ("pan", 0.0f);
            b->mute = (bool) bt.getProperty ("mute", false);
            b->phase = (bool) bt.getProperty ("phase", false);
            auto ct = bt.getChildWithName ("Node");
            if (ct.isValid())
            {
                auto built = buildNode (ct);
                if (auto* chain = dynamic_cast<Chain*> (built.get()))
                {
                    built.release();
                    b->chain.reset (chain);
                }
            }
            s->branches.push_back (std::move (b));
        }
        syncAll (*s);
        return s;
    }

    if (type == NodeType::Plugin)
    {
        auto desc = PluginBlock::descriptionFromState (v);
        std::unique_ptr<juce::AudioPluginInstance> inst;
        juce::String err;

        const juce::PluginDescription* match = nullptr;
        auto types = knownPlugins.getTypes();
        for (auto& t : types)
            if (t.uniqueId == desc.uniqueId && t.pluginFormatName == desc.pluginFormatName && t.name == desc.name)
                { match = &t; break; }
        if (match == nullptr)
            for (auto& t : types)
                if (t.name == desc.name && t.manufacturerName == desc.manufacturerName)
                    { match = &t; break; }

        if (match != nullptr) inst = formatManager.createPluginInstance (*match, sampleRate, blockSize, err);
        if (inst == nullptr && desc.fileOrIdentifier.isNotEmpty())
            inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, err);
        if (inst == nullptr)
            message ("Missing plug-in: " + desc.name + " (" + desc.manufacturerName + ")");

        auto pb = std::make_unique<PluginBlock> (std::move (inst), match != nullptr ? *match : desc);
        pb->fromState (v);
        pb->syncEdits();
        return pb;
    }

    auto n = createNode (type);
    if (n != nullptr)
    {
        n->fromState (v);
        n->syncEdits();
    }
    return n;
}

juce::ValueTree Engine::getState (bool includeSession) const
{
    juce::ValueTree v ("Rigsmith");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("name", presetName, nullptr);
    v.setProperty ("scene", activeScene, nullptr);
    v.appendChild (root->toState(), nullptr);

    juce::ValueTree sc ("Scenes");
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto s = (i == activeScene ? captureScene() : scenes[i]).createCopy();
        if (! s.isValid()) s = juce::ValueTree ("Scene");
        s.setProperty ("name", sceneNames[i], nullptr);
        sc.appendChild (s, nullptr);
    }
    v.appendChild (sc, nullptr);

    juce::ValueTree mm ("Midi");
    for (auto& m : mappings)
        if (includeSession || ! m.isGlobal())
            mm.appendChild (m.toState(), nullptr);
    v.appendChild (mm, nullptr);

    if (includeSession)
    {
        juce::ValueTree s ("Session");
        s.setProperty ("inputMode", inputMode.load(), nullptr);
        s.setProperty ("inputGain", inputGainDb.load(), nullptr);
        s.setProperty ("outputGain", outputGainDb.load(), nullptr);
        s.setProperty ("limiter", limiterOn.load(), nullptr);
        s.setProperty ("inputPan", inputPan.load(), nullptr);
        s.setProperty ("inputPhase", inputPhase.load(), nullptr);
        s.setProperty ("outputPan", outputPan.load(), nullptr);
        s.setProperty ("outputWidth", outputWidth.load(), nullptr);
        s.setProperty ("ceiling", limiterCeilingDb.load(), nullptr);
        s.setProperty ("limiterRelease", limiterReleaseMs.load(), nullptr);
        s.setProperty ("presetFile", presetFile.getFullPathName(), nullptr);
        s.setProperty ("dirty", dirty, nullptr);
        v.appendChild (s, nullptr);
    }
    return v;
}

void Engine::setState (const juce::ValueTree& v, bool includeSession)
{
    if (! v.hasType ("Rigsmith"))
        return;

    auto rootState = v.getChildWithName ("Node");
    std::unique_ptr<Chain> newRoot;
    if (rootState.isValid())
    {
        auto built = buildNode (rootState);
        if (auto* c = dynamic_cast<Chain*> (built.get()))
        {
            built.release();
            newRoot.reset (c);
        }
    }
    if (newRoot == nullptr)
        newRoot = std::make_unique<Chain>();

    newRoot->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        std::swap (root, newRoot);
    }
    announceDeletion (*newRoot);
    newRoot.reset();

    presetName = v.getProperty ("name", "Untitled").toString();
    activeScene = juce::jlimit (0, kNumScenes - 1, (int) v.getProperty ("scene", 0));

    auto sc = v.getChildWithName ("Scenes");
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto s = sc.getChild (i);
        scenes[i] = s.isValid() ? s.createCopy() : juce::ValueTree ("Scene");
        sceneNames[i] = s.isValid() ? s.getProperty ("name", juce::String::charToString ((juce::juce_wchar) ('A' + i))).toString()
                                    : juce::String::charToString ((juce::juce_wchar) ('A' + i));
    }
    if (scenes[activeScene].getNumChildren() > 0)
        recallScene (scenes[activeScene]);

    // node mappings come from the preset; global ones (scenes, tuner...) survive unless this is a full session
    std::vector<MidiMapping> next;
    if (! includeSession)
        for (auto& m : mappings)
            if (m.isGlobal()) next.push_back (m);
    for (auto mt : v.getChildWithName ("Midi"))
    {
        auto m = MidiMapping::fromState (mt);
        if (includeSession || ! m.isGlobal()) next.push_back (m);
    }
    mappings = next;

    if (includeSession)
    {
        auto s = v.getChildWithName ("Session");
        if (s.isValid())
        {
            inputMode = (int) s.getProperty ("inputMode", 0);
            inputGainDb = (float) s.getProperty ("inputGain", 0.0f);
            outputGainDb = (float) s.getProperty ("outputGain", 0.0f);
            limiterOn = (bool) s.getProperty ("limiter", true);
            inputPan = (float) s.getProperty ("inputPan", 0.0f);
            inputPhase = (bool) s.getProperty ("inputPhase", false);
            outputPan = (float) s.getProperty ("outputPan", 0.0f);
            outputWidth = (float) s.getProperty ("outputWidth", 100.0f);
            limiterCeilingDb = (float) s.getProperty ("ceiling", -0.3f);
            limiterReleaseMs = (float) s.getProperty ("limiterRelease", 60.0f);
            presetFile = juce::File (s.getProperty ("presetFile").toString());
            dirty = (bool) s.getProperty ("dirty", false);
        }
    }
    else
    {
        dirty = false;
    }

    if (onSceneChanged) onSceneChanged (activeScene);
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); l.midiMapChanged(); });
}

void Engine::loadDefaultRig() { loadFactory (0); }

const juce::StringArray& Engine::getFactoryNames()
{
    static const juce::StringArray names { initName, "Classic Rig", "Dual Amp Rig" };
    return names;
}

juce::String Engine::getFactoryDescription (int index)
{
    switch (index)
    {
        case 1:  return "gate, drive, amp, cab, chorus, delay, reverb";
        case 2:  return "the same, with two amps + cabs in parallel";
        default: return "gate, amp, cab";
    }
}

int Engine::getFactoryIndex() const
{
    if (presetFile != juce::File()) return -1;
    return getFactoryNames().indexOf (presetName);
}

juce::Array<juce::File> Engine::pickLibraryFiles (bool models, int count) const
{
    // what's loaded now comes first, so switching to a starting point keeps your amp and cab
    juce::Array<juce::File> out;
    visitNodes (*root, [&] (Node& n)
    {
        juce::File f;
        if (auto* nb = dynamic_cast<NamBlock*> (&n)) { if (models) f = nb->getFile(); }
        else if (auto* ir = dynamic_cast<IrBlock*> (&n)) { if (! models) f = ir->getFile(); }
        if (f.existsAsFile()) out.addIfNotAlreadyThere (f);
    });
    if (out.size() >= count) { out.resize (count); return out; }

    // then the first files (by name) in the folder of the first one, then in the library folders
    const auto pattern = models ? juce::String ("*.nam") : juce::String ("*.wav;*.aif;*.aiff;*.flac");
    auto addFrom = [&] (const juce::File& folder, bool recursive)
    {
        if (! folder.isDirectory() || out.size() >= count) return;
        auto found = folder.findChildFiles (juce::File::findFiles, recursive, pattern);
        std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
        });
        for (auto& f : found)
        {
            if (out.size() >= count) break;
            if (! isRandomExcluded (f)) out.addIfNotAlreadyThere (f);
        }
    };
    if (! out.isEmpty()) addFrom (out[0].getParentDirectory(), false);
    const auto paths = getPaths (models ? "modelPaths" : "irPaths");
    for (int i = 0; i < paths.getNumPaths(); ++i) addFrom (paths.getRawString (i), true);
    return out;
}

std::unique_ptr<Chain> Engine::buildFactory (int index, juce::StringArray& sceneLabels)
{
    auto r = std::make_unique<Chain>();
    sceneLabels = { "A", "B", "C", "D" };
    juce::String err;

    if (index <= 0)
    {
        r->nodes.push_back (std::make_unique<GateBlock>());
        r->nodes.push_back (std::make_unique<NamBlock>());
        r->nodes.push_back (std::make_unique<IrBlock>());
        return r;
    }

    const int amps = index == 2 ? 2 : 1;
    const auto models = pickLibraryFiles (true, amps);
    const auto irFiles = pickLibraryFiles (false, amps);

    auto ampAndCab = [&] (Chain& c, int which)
    {
        auto nam = std::make_unique<NamBlock>();
        // the second amp falls back to the first model (or IR) if the library only has one
        if (! models.isEmpty()) nam->loadModel (models[juce::jmin (which, models.size() - 1)], err);
        c.nodes.push_back (std::move (nam));
        auto cab = std::make_unique<IrBlock>();
        if (! irFiles.isEmpty()) cab->loadIr (irFiles[juce::jmin (which, irFiles.size() - 1)], err);
        c.nodes.push_back (std::move (cab));
    };

    // the classic order: gate -> drive -> amp -> cab -> modulation -> delay -> reverb
    auto gate = std::make_unique<GateBlock>();
    auto drive = std::make_unique<OverdriveBlock>();
    drive->set (OverdriveBlock::pDrive, 3.5f);
    drive->set (OverdriveBlock::pTone, 5.5f);
    drive->set (OverdriveBlock::pLevel, 2.0f);
    auto chorus = std::make_unique<ChorusBlock>();
    chorus->set (ChorusBlock::pRate, 0.6f);
    chorus->set (ChorusBlock::pDepth, 35.0f);
    chorus->set (ChorusBlock::pMix, 35.0f);
    auto delay = std::make_unique<DelayBlock>();
    delay->set (DelayBlock::pTime, 380.0f);
    delay->set (DelayBlock::pFeedback, 28.0f);
    delay->set (DelayBlock::pMix, 18.0f);
    delay->set (DelayBlock::pHighCut, 5500.0f);
    auto reverb = std::make_unique<ReverbBlock>();
    reverb->set (ReverbBlock::pType, 2.0f);   // plate
    reverb->set (ReverbBlock::pDecay, 1.8f);
    reverb->set (ReverbBlock::pMix, 18.0f);

    auto* driveP = drive.get();
    auto* chorusP = chorus.get();
    auto* delayP = delay.get();
    auto* reverbP = reverb.get();

    r->nodes.push_back (std::move (gate));
    r->nodes.push_back (std::move (drive));
    if (amps == 1)
        ampAndCab (*r, 0);
    else
    {
        auto split = std::make_unique<Split>();
        for (int i = 0; i < 2; ++i)
        {
            auto b = split->makeBranch();
            ampAndCab (*b->chain, i);
            b->pan = i == 0 ? -0.4f : 0.4f;   // a little width between the two amps
            split->branches.push_back (std::move (b));
        }
        split->setMode (SplitMode::Parallel);
        split->autoGainSum = true;
        r->nodes.push_back (std::move (split));
    }
    r->nodes.push_back (std::move (chorus));
    r->nodes.push_back (std::move (delay));
    r->nodes.push_back (std::move (reverb));

    // scenes: clean, crunch, lead, dry (bypass states; set up by loadFactory)
    sceneLabels = { "Clean", "Crunch", "Lead", "Dry" };
    factoryScenes =
    {
        //  drive  chorus delay  reverb  (true = on)
        { driveP->id, chorusP->id, delayP->id, reverbP->id },
    };
    return r;
}

void Engine::loadFactory (int index)
{
    if (dirty) checkpoint();
    index = juce::jlimit (0, getFactoryNames().size() - 1, index);

    factoryScenes.clear();
    juce::StringArray labels;
    auto r = buildFactory (index, labels);
    r->prepare (sampleRate, blockSize);
    {
        const juce::ScopedLock sl (lock);
        std::swap (root, r);
    }
    if (r != nullptr) announceDeletion (*r);
    r.reset();

    presetName = getFactoryNames()[index];
    presetFile = juce::File();
    for (int i = 0; i < kNumScenes; ++i) sceneNames[i] = labels[i];

    if (! factoryScenes.empty())
    {
        const auto& ids = factoryScenes[0];   // drive, chorus, delay, reverb
        static constexpr bool on[kNumScenes][4] =
        {
            { false, true,  true,  true  },   // Clean: chorus, delay, reverb
            { true,  false, false, true  },   // Crunch: drive, reverb
            { true,  false, true,  true  },   // Lead: drive, delay, reverb
            { false, false, false, false },   // Dry: just the amp
        };
        for (int sc = kNumScenes - 1; sc >= 0; --sc)
        {
            for (int k = 0; k < 4; ++k)
                if (auto* n = findNode (ids[(size_t) k])) n->bypassed = ! on[sc][k];
            scenes[sc] = captureScene();
        }
        // leaves scene A (Clean) live
    }
    else
        for (auto& s : scenes) s = captureScene();

    activeScene = 0;
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [] (const MidiMapping& m) { return ! m.isGlobal(); }), mappings.end());
    dirty = false;
    notifyGraphChanged();
    listeners.call ([] (Listener& l) { l.presetChanged(); l.sceneChanged(); });

    if (index > 0)
    {
        int missing = 0;
        visitNodes (*root, [&] (Node& n)
        {
            if (auto* nb = dynamic_cast<NamBlock*> (&n)) missing += nb->getFile().existsAsFile() ? 0 : 1;
        });
        if (missing > 0)
            message ("No .nam models found yet: add your model folder in Settings, or drop a model on the amp");
    }
}

//==============================================================================
juce::File Engine::getPresetFolder() const
{
    return getFolder ("presetFolder");
}

juce::Array<juce::File> Engine::getPresetFiles() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, true, "*.rig");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    return files;
}

bool Engine::loadPreset (const juce::File& f)
{
    if (dirty) checkpoint();   // unsaved changes can be got back with undo
    auto xml = juce::parseXML (f);
    if (xml == nullptr)
    {
        message ("Couldn't read preset " + f.getFileName());
        return false;
    }
    setState (juce::ValueTree::fromXml (*xml), false);
    presetFile = f;
    presetName = f.getFileNameWithoutExtension();
    dirty = false;
    listeners.call ([] (Listener& l) { l.presetChanged(); });
    return true;
}

bool Engine::savePreset (const juce::File& f)
{
    presetName = f.getFileNameWithoutExtension();
    storeScene();
    auto state = getState (false);
    if (auto xml = state.createXml())
    {
        f.getParentDirectory().createDirectory();
        if (xml->writeTo (f))
        {
            presetFile = f;
            dirty = false;
            listeners.call ([] (Listener& l) { l.presetChanged(); });
            message ("Saved " + f.getFileName());
            return true;
        }
    }
    message ("Couldn't save " + f.getFullPathName());
    return false;
}

void Engine::stepPreset (int delta)
{
    // Init and the starting points first, then the saved presets
    auto files = getPresetFiles();
    const int nf = getFactoryNames().size();
    const int count = files.size() + nf;
    int cur = files.indexOf (presetFile);
    cur = cur >= 0 ? cur + nf : juce::jmax (0, getFactoryIndex());   // an unsaved rig counts as Init
    const int next = ((cur + delta) % count + count) % count;
    if (next < nf) loadFactory (next);
    else loadPreset (files[next - nf]);
}

//==============================================================================
juce::ValueTree Engine::snapshot() const
{
    auto v = getState (false);
    v.setProperty ("undoFile", presetFile.getFullPathName(), nullptr);
    v.setProperty ("undoDirty", dirty, nullptr);
    return v;
}

void Engine::restore (const juce::ValueTree& v)
{
    setState (v, false);
    presetFile = juce::File (v.getProperty ("undoFile").toString());
    dirty = (bool) v.getProperty ("undoDirty", true);
    listeners.call ([] (Listener& l) { l.presetChanged(); });
}

void Engine::checkpoint()
{
    if (undoSuppress > 0) return;
    undoStack.push_back (snapshot());
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool Engine::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (snapshot());
    auto v = undoStack.back();
    undoStack.pop_back();
    restore (v);
    return true;
}

bool Engine::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (snapshot());
    auto v = redoStack.back();
    redoStack.pop_back();
    restore (v);
    return true;
}

//==============================================================================
juce::File Engine::getBlockPresetFolder (const Node& n) const
{
    juce::String key;
    if (auto* pb = dynamic_cast<const PluginBlock*> (&n)) key = "VST - " + pb->getDescription().name;
    else if (n.getType() == NodeType::Nam) key = "NAM Amp";
    else if (n.getType() == NodeType::Ir) key = "Cab IR";
    else key = n.getName();
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rigsmith")
        .getChildFile ("Block Presets").getChildFile (juce::File::createLegalFileName (key));
}

juce::Array<juce::File> Engine::getBlockPresets (const Node& n) const
{
    auto files = getBlockPresetFolder (n).findChildFiles (juce::File::findFiles, false, "*.rbp");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files;
}

bool Engine::saveBlockPreset (const Node& n, const juce::String& name)
{
    auto v = n.toState().createCopy();
    for (auto* prop : { "id", "bypass", "autoGain", "autoGainDb", "locked" }) v.removeProperty (prop, nullptr);
    v.setProperty ("presetName", name, nullptr);
    const auto f = getBlockPresetFolder (n).getChildFile (juce::File::createLegalFileName (name) + ".rbp");
    f.getParentDirectory().createDirectory();
    if (auto xml = v.createXml(); xml != nullptr && xml->writeTo (f))
    {
        message ("Saved block preset " + name);
        return true;
    }
    message ("Couldn't save " + f.getFullPathName());
    return false;
}

Node* Engine::loadBlockPreset (const juce::String& id, const juce::File& f)
{
    auto* current = findNode (id);
    auto xml = juce::parseXML (f);
    if (current == nullptr || xml == nullptr) return nullptr;
    auto v = juce::ValueTree::fromXml (*xml);
    if (nodeTypeFromString (v["type"].toString()) != current->getType()) { message ("That preset is for a different block"); return nullptr; }
    auto n = buildNode (v);
    if (n == nullptr) return nullptr;
    auto* r = replace (id, std::move (n), true);
    if (r != nullptr) message ("Loaded " + f.getFileNameWithoutExtension());
    return r;
}

Node* Engine::initBlock (const juce::String& id)
{
    auto* current = findNode (id);
    if (current == nullptr) return nullptr;
    if (auto* pb = dynamic_cast<PluginBlock*> (current))
    {
        checkpoint();
        pb->resetToDefaults();
        for (int i = 0; i < pb->numParams(); ++i) pb->set (i, pb->getSpecs()[(size_t) i].def);
        markDirty();
        return pb;
    }
    auto v = current->toState().createCopy();
    if (auto* b = dynamic_cast<Block*> (current))
        for (auto& sp : b->getSpecs())
            v.setProperty ("p_" + sp.id, sp.def, nullptr);
    auto n = buildNode (v);
    return n != nullptr ? replace (id, std::move (n), true) : nullptr;
}

} // namespace rig
