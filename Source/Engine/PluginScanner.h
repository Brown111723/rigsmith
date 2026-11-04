#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rig
{
/** Scans each plug-in in a separate process (RigsmithScanner), so one that crashes or hangs
    while being scanned is skipped (and put on the list's "blacklist") instead of taking
    Rigsmith, or the DAW Rigsmith is running in, down with it. Rigsmith itself is never scanned. */
class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (juce::File scannerExe);

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    /** RigsmithScanner(.exe) next to Rigsmith.exe, or inside the Rigsmith.vst3 bundle. */
    static juce::File findScannerExe();
    /** Rigsmith's own plug-in: no reason to host Rigsmith inside Rigsmith. */
    static bool isRigsmith (const juce::String& fileOrIdentifier);

    int timeoutMs = 60000;   // a plug-in that takes longer than this to scan is skipped
    /** For tests: extra arguments put before the usual ones. */
    juce::StringArray argsPrefix;

private:
    juce::File exe;
};

/** Installs the out-of-process scanner on a plug-in list (falls back to scanning in-process,
    minus Rigsmith itself, if the helper isn't there). */
void installSafeScanner (juce::KnownPluginList&);

} // namespace rig
