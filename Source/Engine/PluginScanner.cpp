#include "../Common.h"
#include "PluginScanner.h"

namespace rig
{
bool isRigsmithPlugin (const juce::String& fileOrIdentifier)
{
    const auto lower = fileOrIdentifier.toLowerCase().replaceCharacter ('\\', '/');
    if (lower.contains ("/rigsmith.vst3") || lower.endsWith ("rigsmith.vst3") || lower.contains ("/rigsmith.dll")) return true;
    const juce::File f (fileOrIdentifier);
    return juce::File::isAbsolutePath (fileOrIdentifier) && f.getFileNameWithoutExtension().equalsIgnoreCase ("Rigsmith");
}

//==============================================================================
bool InProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                           const juce::String& fileOrIdentifier)
{
    if (isRigsmithPlugin (fileOrIdentifier)) return true;
    if (policy.isSkipped && policy.isSkipped (fileOrIdentifier)) return false;
    if (policy.onStarting) policy.onStarting (fileOrIdentifier);   // in the ini before we touch it
    rig::trace ("scanning: " + fileOrIdentifier);
    format.findAllTypesForFile (result, fileOrIdentifier);
    if (policy.onFinished) policy.onFinished();
    return true;
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
            if (! isRigsmithPlugin (f)) inner->findAllTypesForFile (r, f);
        }
        bool fileMightContainThisPluginType (const juce::String& f) override
        {
            return ! isRigsmithPlugin (f) && inner->fileMightContainThisPluginType (f);
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
                if (isRigsmithPlugin (found[i])) found.remove (i);
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
