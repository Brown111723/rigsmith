#include "PluginScanner.h"

namespace rig
{
OutOfProcessScanner::OutOfProcessScanner (juce::File scannerExe) : exe (std::move (scannerExe)) {}

bool OutOfProcessScanner::isRigsmith (const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
    auto name = juce::File::isAbsolutePath (fileOrIdentifier) ? f.getFileNameWithoutExtension() : fileOrIdentifier;
    // also the bundle that contains the file (Rigsmith.vst3/Contents/x86_64-win/Rigsmith.vst3)
    return name.startsWithIgnoreCase ("Rigsmith") || fileOrIdentifier.containsIgnoreCase ("Rigsmith.vst3");
}

juce::File OutOfProcessScanner::findScannerExe()
{
   #if JUCE_WINDOWS
    const juce::String name ("RigsmithScanner.exe");
   #else
    const juce::String name ("RigsmithScanner");
   #endif
    // the running exe, or the plug-in's own binary when Rigsmith runs inside a DAW
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { self.getParentDirectory(),
                      self.getParentDirectory().getSiblingFile ("Resources"),            // Rigsmith.vst3/Contents/Resources
                      self.getParentDirectory().getParentDirectory().getChildFile ("Resources") })
        if (auto f = dir.getChildFile (name); f.existsAsFile())
            return f;
    return {};
}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    if (isRigsmith (fileOrIdentifier)) return true;   // found nothing, and that's fine (not a failure)

    const auto outFile = juce::File::createTempFile (".xml");
    outFile.deleteFile();   // the helper creates it only if it finishes
    juce::StringArray args;
    args.add (exe.getFullPathName());
    args.addArray (argsPrefix);
    args.add (format.getName());
    args.add (fileOrIdentifier);
    args.add (outFile.getFullPathName());

    juce::ChildProcess child;
    if (! child.start (args, 0))
        return false;

    const auto started = juce::Time::getMillisecondCounter();
    while (child.isRunning())
    {
        if (shouldExit()) { child.kill(); outFile.deleteFile(); return true; }   // cancelled: not the plug-in's fault
        if (juce::Time::getMillisecondCounter() - started > (juce::uint32) timeoutMs)
        {
            child.kill();   // hung: skip it
            outFile.deleteFile();
            return false;
        }
        juce::Thread::sleep (20);
    }

    // a crash leaves no results file (and on some systems still reports exit code 0)
    const bool ok = child.getExitCode() == 0 && outFile.existsAsFile();
    if (ok)
        if (auto xml = juce::XmlDocument::parse (outFile))
            for (auto* e : xml->getChildIterator())
            {
                auto d = std::make_unique<juce::PluginDescription>();
                if (d->loadFromXml (*e)) result.add (d.release());
            }
    outFile.deleteFile();
    return ok;   // false: it crashed the scanner, so the list skips it from now on
}

namespace
{
    /** Without the helper: scan in-process as before, but never Rigsmith itself. */
    struct InProcessScanner : juce::KnownPluginList::CustomScanner
    {
        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            if (OutOfProcessScanner::isRigsmith (fileOrIdentifier)) return true;
            format.findAllTypesForFile (result, fileOrIdentifier);
            return true;
        }
    };
}

void installSafeScanner (juce::KnownPluginList& list)
{
    const auto exe = OutOfProcessScanner::findScannerExe();
    if (exe.existsAsFile()) list.setCustomScanner (std::make_unique<OutOfProcessScanner> (exe));
    else                    list.setCustomScanner (std::make_unique<InProcessScanner>());
}

} // namespace rig
