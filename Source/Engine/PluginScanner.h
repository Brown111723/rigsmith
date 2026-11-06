#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rig
{
/** What the scanners ask Rigsmith: skip this one? / this one crashed / starting / done. */
struct ScanPolicy
{
    std::function<bool (const juce::String&)> isSkipped;   // listed in Rigsmith.ini as having crashed
    std::function<void (const juce::String&)> onCrashed;   // record it there
    std::function<void (const juce::String&)> onStarting;  // about to scan in-process (written to the ini first)
    std::function<void()> onFinished;
};

/** Scans each plug-in in a separate copy of Rigsmith.exe (started with --rigsmith-scan), so one that
    crashes or hangs while being scanned is skipped, recorded in Rigsmith.ini and never scanned again,
    instead of taking Rigsmith down with it. */
class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    OutOfProcessScanner (juce::File exe, ScanPolicy);

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    /** Rigsmith's own plug-in: never scanned (no reason to host Rigsmith inside Rigsmith). */
    static bool isRigsmith (const juce::String& fileOrIdentifier);

    int timeoutMs = 60000;   // a plug-in that takes longer than this to scan is skipped
    juce::StringArray argsPrefix;   // for tests

private:
    juce::File exe;
    ScanPolicy policy;
};

/** Scans in this process (inside a DAW there's no other choice), writing the file being scanned to
    Rigsmith.ini first: if it crashes, the next start finds it there and never scans it again. */
class InProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    explicit InProcessScanner (ScanPolicy p) : policy (std::move (p)) {}
    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

private:
    ScanPolicy policy;
};

/** The command-line switch that makes Rigsmith.exe scan one plug-in and quit. */
static constexpr const char* scanSwitch = "--rigsmith-scan";
/** Does the scan for "--rigsmith-scan <format> <file> <out.xml>": returns the exit code. */
int runScanCommand (const juce::StringArray& args);

/** VST3 (and VST2 if enabled), but never finding Rigsmith itself. */
void addPluginFormats (juce::AudioPluginFormatManager&);

} // namespace rig
