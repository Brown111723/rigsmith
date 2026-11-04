// RigsmithScanner: looks inside ONE plug-in file and writes what it found to an XML file.
// Rigsmith runs this for every plug-in it scans, so a plug-in that crashes while being
// scanned only takes this little process down, never Rigsmith or your DAW.
//
//   RigsmithScanner <format name> <file or identifier> <output.xml>
//
// Exit code 0 = scanned (the XML lists 0 or more plug-ins), anything else = failed / crashed.

#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[])
{
    if (argc < 4) { std::fprintf (stderr, "usage: RigsmithScanner <format> <file> <out.xml>\n"); return 2; }

    juce::ScopedJuceInitialiser_GUI init;   // some plug-ins need a message manager to be scanned
    const juce::String formatName = juce::CharPointer_UTF8 (argv[1]);
    const juce::String fileOrIdentifier = juce::CharPointer_UTF8 (argv[2]);
    const juce::File out { juce::String (juce::CharPointer_UTF8 (argv[3])) };

    if (formatName == "--crash-test") { std::abort(); }   // used by Rigsmith's tests

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
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
