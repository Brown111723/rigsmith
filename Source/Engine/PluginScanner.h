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

/** Rigsmith's own plug-in: never scanned or listed (no reason to host Rigsmith inside Rigsmith,
    and loading it during a scan is what used to crash). */
bool isRigsmithPlugin (const juce::String& fileOrIdentifier);

/** Scans each plug-in in this process, writing the file being scanned to Rigsmith.ini first
    (a dead-man's pedal): if it crashes, the next start finds it there, moves it to
    [Skipped Plug-ins] and never scans it again. No second process, no command-line tricks. */
class InProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    explicit InProcessScanner (ScanPolicy p) : policy (std::move (p)) {}
    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

private:
    ScanPolicy policy;
};

/** VST3 (and VST2 if enabled), but never finding Rigsmith itself. */
void addPluginFormats (juce::AudioPluginFormatManager&);

} // namespace rig
