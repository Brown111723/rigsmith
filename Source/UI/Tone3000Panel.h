#pragma once

#include "Theme.h"
#include "../T3K/Tone3000.h"

namespace rig
{
/** The TONE3000 page of the library: set up (publishable key), sign in, Browse TONE3000
    (their catalog, in the browser), and your Favorites / Downloaded / Created / Trending
    tones. Click a tone to download it into the library and load it into the rig. */
class Tone3000Panel : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::ChangeListener
{
public:
    Tone3000Panel (Engine&, t3k::Client&);
    ~Tone3000Panel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /** A downloaded file, ready to load (into the selected block, or a new one). */
    std::function<void (const juce::File&)> onFileReady;
    std::function<void()> onLibraryChanged;

    /** Opens the catalog in the browser; the picked tone downloads and goes to `into`
        (or onFileReady). format: "nam" or "ir". */
    void browse (const juce::String& format, std::function<void (const juce::File&)> into = {});
    /** Model picker + download for one tone. */
    void loadTone (const t3k::Tone&, std::function<void (const juce::File&)> into = {});
    void loadToneById (int id, std::function<void (const juce::File&)> into = {});

private:
    enum class State { NeedsKey, SignedOut, SignedIn };
    State state() const;
    void updateState();
    void reloadList();
    void setStatus (const juce::String&, bool busy = false);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    int getNumRows() override { return tones.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int row) override;

    Engine& engine;
    t3k::Client& client;

    // set-up
    juce::Label keyHelp;
    juce::TextEditor keyEditor;
    juce::TextButton saveKey { "Save key" }, getKey { "Open TONE3000 settings" };
    // signed out
    juce::TextButton signIn { "Sign in" };
    // both
    juce::TextButton browseNam { "Browse TONE3000" }, browseIr { "IRs" }, signOut { "Sign out" }, openSite;
    juce::TextButton lists[4] { juce::TextButton ("Favorites"), juce::TextButton ("Downloaded"),
                                juce::TextButton ("Created"), juce::TextButton ("Trending") };
    int listIndex = 0;
    juce::ListBox list;
    juce::Label status;
    juce::Array<t3k::Tone> tones;
    juce::Image avatar;
    std::map<juce::String, juce::Image> thumbs;
    juce::Rectangle<int> splashArea, userArea;
    bool busy = false;
    int listRequest = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace rig
