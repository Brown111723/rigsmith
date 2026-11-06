#include "PluginScanner.h"

namespace rig
{
bool OutOfProcessScanner::isRigsmith (const juce::String& fileOrIdentifier)
{
    const auto lower = fileOrIdentifier.toLowerCase().replaceCharacter ('\\', '/');
    if (lower.contains ("/rigsmith.vst3") || lower.endsWith ("rigsmith.vst3") || lower.contains ("/rigsmith.dll")) return true;
    const juce::File f (fileOrIdentifier);
    return juce::File::isAbsolutePath (fileOrIdentifier) && f.getFileNameWithoutExtension().equalsIgnoreCase ("Rigsmith");
}

//==============================================================================
OutOfProcessScanner::OutOfProcessScanner (juce::File e, ScanPolicy p) : exe (std::move (e)), policy (std::move (p)) {}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    if (isRigsmith (fileOrIdentifier)) return true;
    if (policy.isSkipped && policy.isSkipped (fileOrIdentifier)) return false;   // crashed before: leave it alone

    const auto outFile = juce::File::createTempFile (".xml");
    outFile.deleteFile();   // the scan creates it only if it finishes
    juce::StringArray args;
    args.add (exe.getFullPathName());
    args.addArray (argsPrefix);
    args.add (scanSwitch);
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
            child.kill();   // hung
            outFile.deleteFile();
            if (policy.onCrashed) policy.onCrashed (fileOrIdentifier);
            return false;
        }
        juce::Thread::sleep (20);
    }

    // a crash leaves no results file (and on some systems still reports exit code 0)
    const bool ok = child.getExitCode() == 0 && outFile.existsAsFile();
    if (ok)
    {
        if (auto xml = juce::XmlDocument::parse (outFile))
            for (auto* e : xml->getChildIterator())
            {
                auto d = std::make_unique<juce::PluginDescription>();
                if (d->loadFromXml (*e)) result.add (d.release());
            }
    }
    else if (policy.onCrashed) policy.onCrashed (fileOrIdentifier);
    outFile.deleteFile();
    return ok;
}

//==============================================================================
bool InProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                           const juce::String& fileOrIdentifier)
{
    if (OutOfProcessScanner::isRigsmith (fileOrIdentifier)) return true;
    if (policy.isSkipped && policy.isSkipped (fileOrIdentifier)) return false;
    if (policy.onStarting) policy.onStarting (fileOrIdentifier);   // in the ini before we touch it
    format.findAllTypesForFile (result, fileOrIdentifier);
    if (policy.onFinished) policy.onFinished();
    return true;
}

//==============================================================================
int runScanCommand (const juce::StringArray& raw)
{
    juce::StringArray args;
    for (auto& a : raw) args.add (a.unquoted());
    const int at = args.indexOf (scanSwitch);
    if (at < 0 || args.size() < at + 4) return 2;
    const auto formatName = args[at + 1], fileOrIdentifier = args[at + 2];
    const juce::File out (args[at + 3]);

    if (formatName == "--crash-test") std::abort();   // used by Rigsmith's tests

    juce::AudioPluginFormatManager formats;
    addPluginFormats (formats);
    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == formatName) format = f;
    if (format == nullptr) return 3;

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, fileOrIdentifier);
    juce::XmlElement xml ("PLUGINS");
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    return xml.writeTo (out) ? 0 : 4;
}

//==============================================================================
namespace
{
    /** A plug-in format that never finds Rigsmith (so scans never try to load it). */
    class WithoutRigsmith final : public juce::AudioPluginFormat
    {
    public:
        explicit WithoutRigsmith (std::unique_ptr<juce::AudioPluginFormat> f) : inner (std::move (f)) {}

        juce::String getName() const override { return inner->getName(); }
        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& r, const juce::String& f) override
        {
            if (! OutOfProcessScanner::isRigsmith (f)) inner->findAllTypesForFile (r, f);
        }
        bool fileMightContainThisPluginType (const juce::String& f) override
        {
            return ! OutOfProcessScanner::isRigsmith (f) && inner->fileMightContainThisPluginType (f);
        }
        juce::String getNameOfPluginFromIdentifier (const juce::String& f) override { return inner->getNameOfPluginFromIdentifier (f); }
        bool pluginNeedsRescanning (const juce::PluginDescription& d) override { return inner->pluginNeedsRescanning (d); }
        bool doesPluginStillExist (const juce::PluginDescription& d) override { return inner->doesPluginStillExist (d); }
        bool canScanForPlugins() const override { return inner->canScanForPlugins(); }
        bool isTrivialToScan() const override { return inner->isTrivialToScan(); }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& dirs, bool recursive, bool allowAsync) override
        {
            auto found = inner->searchPathsForPlugins (dirs, recursive, allowAsync);
            for (int i = found.size(); --i >= 0;)
                if (OutOfProcessScanner::isRigsmith (found[i])) found.remove (i);
            return found;
        }
        juce::FileSearchPath getDefaultLocationsToSearch() override { return inner->getDefaultLocationsToSearch(); }
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription& d) const override
        {
            return inner->requiresUnblockedMessageThreadDuringCreation (d);
        }
        void createARAFactoryAsync (const juce::PluginDescription& d, ARAFactoryCreationCallback cb) override
        {
            inner->createARAFactoryAsync (d, std::move (cb));
        }

    protected:
        void createPluginInstance (const juce::PluginDescription& d, double sr, int bs, PluginCreationCallback cb) override
        {
            inner->createPluginInstanceAsync (d, sr, bs, std::move (cb));
        }

    private:
        std::unique_ptr<juce::AudioPluginFormat> inner;
    };
}

void addPluginFormats (juce::AudioPluginFormatManager& m)
{
   #if JUCE_PLUGINHOST_VST3
    m.addFormat (std::make_unique<WithoutRigsmith> (std::make_unique<juce::VST3PluginFormat>()));
   #endif
   #if JUCE_PLUGINHOST_VST
    m.addFormat (std::make_unique<WithoutRigsmith> (std::make_unique<juce::VSTPluginFormat>()));
   #endif
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    m.addFormat (std::make_unique<WithoutRigsmith> (std::make_unique<juce::AudioUnitPluginFormat>()));
   #endif
}

} // namespace rig
