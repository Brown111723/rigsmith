#pragma once

// TONE3000 (tone3000.com) client: sign in, browse the catalog, list your favourites /
// downloads / created tones, and download NAM captures and IRs straight into the library.
//
// OAuth 2.0 + PKCE with a loopback redirect (http://127.0.0.1:<port>/callback), which
// TONE3000 always accepts without registration, opened in the system browser.
// Adapted from TONE3000's MIT-licensed JUCE example (github.com/tone-3000/api, juce/Source/T3K).

#include "../Engine/Engine.h"
#include <mutex>

namespace rig::t3k
{
//==============================================================================
struct Tone
{
    int id = 0;
    juce::String title, description, gear, format, license, url, image, creator, creatorAvatar;
    int modelsCount = 0;
    bool isPublic = true;

    Gear getGear() const { return format == "ir" && gearFromTone3000 (gear) == Gear::Unknown ? Gear::Cab : gearFromTone3000 (gear); }
    bool canLoad() const { return format == "nam" || format == "ir"; }
    static Tone fromVar (const juce::var&);
};

struct Model
{
    int id = 0, toneId = 0;
    juce::String name, modelUrl, size, architecture;
    juce::String extension() const;   // ".nam" / ".wav"
    static Model fromVar (const juce::var&);
};

struct Tokens
{
    juce::String accessToken, refreshToken;
    juce::int64 expiresAtMs = 0;
    bool isValid() const { return accessToken.isNotEmpty() && refreshToken.isNotEmpty(); }
    juce::String toJson() const;
    static Tokens fromJson (const juce::String&);
    static Tokens fromTokenResponse (const juce::var&);
};

//==============================================================================
namespace oauth
{
    struct Pkce
    {
        juce::String verifier, challenge, state;
        static Pkce generate();
    };

    juce::URL authorizeUrl (const juce::String& origin, const juce::String& key, const juce::String& redirectUri,
                            const Pkce&, const juce::StringPairArray& extra);

    struct Callback
    {
        juce::String code, state, error, toneId;
        bool canceled = false;
        static Callback parse (const juce::String& queryOrUrl);
    };
}

//==============================================================================
/** Receives the browser's redirect on 127.0.0.1 (one tiny page, then closes). */
class LoopbackServer : private juce::Thread
{
public:
    LoopbackServer();
    ~LoopbackServer() override;
    bool start();
    void stop();
    bool isRunning() const { return port != 0; }
    juce::String redirectUri() const;
    std::function<void (juce::String query)> onCallback;   // message thread

private:
    void run() override;
    void handle (juce::StreamingSocket&);
    juce::StreamingSocket listener;
    std::atomic<int> port { 0 };
};

//==============================================================================
struct HttpResponse
{
    int status = 0;
    juce::MemoryBlock body;
    juce::String error, deprecations;
    bool ok() const { return status >= 200 && status < 300; }
    juce::var json() const { return juce::JSON::parse (body.toString()); }
};

/** One signed-in TONE3000 session per editor; the tokens are shared through an encrypted
    file next to Rigsmith.ini (Windows DPAPI: only this Windows user can read it). */
class Client : public juce::ChangeBroadcaster
{
public:
    explicit Client (Engine&);
    ~Client() override;

    static constexpr const char* defaultOrigin = "https://www.tone3000.com";
    static constexpr const char* keysPage = "https://www.tone3000.com/settings";
    static constexpr const char* siteUrl = "https://www.tone3000.com";

    /** The publishable key (t3k_pub_...): built in, or pasted into Settings / Rigsmith.ini. */
    juce::String getKey() const;
    bool hasKey() const { return getKey().isNotEmpty(); }
    void setKey (const juce::String&);
    juce::String getOrigin() const;
    /** NAM architecture to ask for: 2 (A2, TONE3000's recommendation) or 1 (A1). Both load. */
    int getArchitecture() const;

    bool isConnected() const;
    juce::String userName, avatarUrl;
    void signOut();

    /** Opens TONE3000 in the browser to sign in (prompt empty), or to pick a tone
        (prompt "select_tone": format "nam" / "ir", optional gears "amp_amp-cab").
        done(toneId or 0, error) on the message thread; error is empty if the user just closed it. */
    void startFlow (const juce::String& prompt, const juce::String& format, const juce::String& gears,
                    std::function<void (int toneId, juce::String error)> done);
    void cancelFlow();
    bool isFlowRunning() const { return server != nullptr; }
    /** Test hook: what startFlow opens (instead of launching a browser) when set. */
    std::function<void (const juce::URL&)> openBrowser;

    //--- API (callbacks on the message thread) ----------------------------------
    void fetchUser (std::function<void (bool ok)> done);
    /** which: "favorited", "downloaded", "created", "trending" */
    void listTones (const juce::String& which, int page, std::function<void (juce::Array<Tone>, juce::String error)> done);
    void getTone (int id, std::function<void (Tone, juce::String error)> done);
    /** The tone's models, preferring the chosen architecture and falling back to the other. */
    void listModels (int toneId, std::function<void (juce::Array<Model>, juce::String error)> done);
    /** Downloads one model / IR into the library (Documents\Rigsmith\TONE3000\<gear>\<tone>\)
        with a tone3000.json beside it, so it's categorised and credited. */
    void download (const Tone&, const Model&, std::function<void (juce::File, juce::String error)> done);
    /** Tone images and avatars (cached). */
    void fetchImage (const juce::String& url, std::function<void (juce::Image)> done);

    juce::File libraryFolder() const;
    /** The folder a tone's files go in. */
    juce::File folderFor (const Tone&) const;
    /** Already downloaded? */
    bool isInLibrary (const Tone& t) const { return folderFor (t).getChildFile (ToneInfo::sidecarName).existsAsFile(); }

    static juce::String describe (const juce::String& error, int status);

private:
    HttpResponse send (const juce::String& method, const juce::URL&, const juce::StringPairArray& headers, const juce::String& body) const;
    HttpResponse authed (const juce::String& method, const juce::String& pathAndQuery);
    juce::String accessToken (bool forceRefresh);
    bool refreshNow();
    void storeTokens (const Tokens&, bool notify);
    Tokens currentTokens() const;
    juce::File tokenFile() const;
    template <typename Fn> void async (Fn&&);

    Engine& engine;
    juce::String builtInKey;
    mutable juce::CriticalSection lock;
    Tokens tokens;
    std::mutex refreshMutex;
    std::unique_ptr<LoopbackServer> server;
    oauth::Pkce pkce;
    juce::ThreadPool pool { 3 };
    std::map<juce::String, juce::Image> images;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

} // namespace rig::t3k
