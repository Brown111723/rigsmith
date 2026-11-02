// System headers first (DPAPI for the token file on Windows).
#if defined(_WIN32)
 #include <windows.h>
 #include <wincrypt.h>
 #if defined(_MSC_VER)
  #pragma comment(lib, "crypt32.lib")
 #endif
#endif

#include "Tone3000.h"
#include <juce_cryptography/juce_cryptography.h>

#ifndef RIGSMITH_T3K_KEY
 #define RIGSMITH_T3K_KEY ""
#endif

namespace rig::t3k
{
namespace
{
    constexpr juce::int64 refreshLeadMs = 60 * 1000;

    juce::String str (const juce::var& v, const char* key)
    {
        auto p = v.getProperty (key, juce::var());
        return p.isVoid() || p.isUndefined() ? juce::String() : p.toString();
    }

    juce::String base64Url (const void* data, size_t size)
    {
        return juce::Base64::toBase64 (data, size).replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
    }

    juce::String randomUrlSafe (int numBytes)
    {
        juce::Random r (juce::Random::getSystemRandom().nextInt64() ^ juce::Time::currentTimeMillis());
        juce::MemoryBlock b ((size_t) numBytes);
        for (size_t i = 0; i < b.getSize(); ++i) b[i] = (char) r.nextInt (256);
        return base64Url (b.getData(), b.getSize());
    }

    juce::String formEncode (std::initializer_list<std::pair<juce::String, juce::String>> fields)
    {
        juce::StringArray parts;
        for (auto& [k, v] : fields)
            parts.add (juce::URL::addEscapeChars (k, true) + "=" + juce::URL::addEscapeChars (v, true));
        return parts.joinIntoString ("&");
    }

    juce::Array<Tone> parseTones (const juce::var& v)
    {
        juce::Array<Tone> out;
        const auto* arr = v.isArray() ? v.getArray() : v.getProperty ("data", juce::var()).getArray();
        if (arr != nullptr)
            for (auto& t : *arr) out.add (Tone::fromVar (t));
        return out;
    }

    juce::String errorText (const HttpResponse& r)
    {
        const auto body = r.json();
        auto d = str (body, "error_description");
        if (d.isEmpty()) d = str (body, "error");
        if (d.isEmpty()) d = str (body, "message");
        return d.isNotEmpty() ? d : r.error;
    }

    // --- token file: DPAPI-encrypted on Windows -------------------------------------
    juce::String protect (const juce::String& plain)
    {
       #if defined(_WIN32)
        DATA_BLOB in { (DWORD) plain.getNumBytesAsUTF8(), (BYTE*) plain.toRawUTF8() };
        DATA_BLOB out {};
        if (! CryptProtectData (&in, L"Rigsmith TONE3000 sign-in", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return {};
        auto s = "dpapi:" + juce::Base64::toBase64 (out.pbData, out.cbData);
        LocalFree (out.pbData);
        return s;
       #else
        return "plain:" + juce::Base64::toBase64 (plain);
       #endif
    }

    juce::String unprotect (const juce::String& stored)
    {
        juce::MemoryOutputStream raw;
        if (! juce::Base64::convertFromBase64 (raw, stored.fromFirstOccurrenceOf (":", false, false)))
            return {};
       #if defined(_WIN32)
        if (! stored.startsWith ("dpapi:")) return {};
        DATA_BLOB in { (DWORD) raw.getDataSize(), (BYTE*) raw.getData() };
        DATA_BLOB out {};
        if (! CryptUnprotectData (&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return {};
        auto s = juce::String::fromUTF8 ((const char*) out.pbData, (int) out.cbData);
        LocalFree (out.pbData);
        return s;
       #else
        if (! stored.startsWith ("plain:")) return {};
        return raw.toString();
       #endif
    }
}

//==============================================================================
Tone Tone::fromVar (const juce::var& v)
{
    Tone t;
    t.id = (int) v.getProperty ("id", 0);
    t.title = str (v, "title");
    t.description = str (v, "description");
    t.gear = str (v, "gear");
    t.format = str (v, "format");
    if (t.format.isEmpty()) t.format = str (v, "platform");   // older responses
    if (t.gear == "ir") { t.gear = "cab"; t.format = "ir"; }
    t.license = str (v, "license");
    t.url = str (v, "url");
    if (auto* imgs = v.getProperty ("images", juce::var()).getArray(); imgs != nullptr && ! imgs->isEmpty())
        t.image = imgs->getFirst().toString();
    const auto user = v.getProperty ("user", juce::var());
    t.creator = str (user, "display_name");
    if (t.creator.isEmpty()) t.creator = str (user, "username");
    t.creatorAvatar = str (user, "avatar_url");
    t.modelsCount = (int) v.getProperty ("models_count", 0) + (int) v.getProperty ("irs_count", 0);
    t.isPublic = (bool) v.getProperty ("is_public", true);
    return t;
}

juce::String Model::extension() const
{
    const auto file = juce::URL (modelUrl).getSubPath().fromLastOccurrenceOf ("/", false, false);
    return file.contains (".") ? "." + file.fromLastOccurrenceOf (".", false, false).toLowerCase() : juce::String();
}

Model Model::fromVar (const juce::var& v)
{
    Model m;
    m.id = (int) v.getProperty ("id", 0);
    m.toneId = (int) v.getProperty ("tone_id", 0);
    m.name = str (v, "name");
    m.modelUrl = str (v, "model_url");
    m.size = str (v, "size");
    m.architecture = str (v, "architecture_version");
    return m;
}

juce::String Tokens::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("access_token", accessToken);
    o->setProperty ("refresh_token", refreshToken);
    o->setProperty ("expires_at", expiresAtMs);
    return juce::JSON::toString (juce::var (o), true);
}

Tokens Tokens::fromJson (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    Tokens t;
    t.accessToken = str (v, "access_token");
    t.refreshToken = str (v, "refresh_token");
    t.expiresAtMs = (juce::int64) v.getProperty ("expires_at", 0);
    return t;
}

Tokens Tokens::fromTokenResponse (const juce::var& body)
{
    Tokens t;
    t.accessToken = str (body, "access_token");
    t.refreshToken = str (body, "refresh_token");
    t.expiresAtMs = juce::Time::currentTimeMillis() + (juce::int64) ((double) body.getProperty ("expires_in", 3600) * 1000.0);
    return t;
}

//==============================================================================
namespace oauth
{
    Pkce Pkce::generate()
    {
        Pkce p;
        p.verifier = randomUrlSafe (32);
        juce::SHA256 hash (p.verifier.toRawUTF8(), p.verifier.getNumBytesAsUTF8());
        const auto raw = hash.getRawData();
        p.challenge = base64Url (raw.getData(), raw.getSize());
        p.state = randomUrlSafe (16);
        return p;
    }

    juce::URL authorizeUrl (const juce::String& origin, const juce::String& key, const juce::String& redirectUri,
                            const Pkce& p, const juce::StringPairArray& extra)
    {
        auto url = juce::URL (origin + "/api/v1/oauth/authorize")
                       .withParameter ("client_id", key)
                       .withParameter ("redirect_uri", redirectUri)
                       .withParameter ("response_type", "code")
                       .withParameter ("code_challenge", p.challenge)
                       .withParameter ("code_challenge_method", "S256")
                       .withParameter ("state", p.state);
        for (auto& k : extra.getAllKeys())
            url = url.withParameter (k, extra[k]);
        return url;
    }

    Callback Callback::parse (const juce::String& queryOrUrl)
    {
        auto q = queryOrUrl.contains ("?") ? queryOrUrl.fromFirstOccurrenceOf ("?", false, false) : queryOrUrl;
        q = q.upToFirstOccurrenceOf ("#", false, false);
        Callback cb;
        for (auto& pair : juce::StringArray::fromTokens (q, "&", {}))
        {
            const auto k = juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false));
            const auto v = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' '));
            if (k == "code") cb.code = v;
            else if (k == "state") cb.state = v;
            else if (k == "error") cb.error = v;
            else if (k == "tone_id") cb.toneId = v;
            else if (k == "canceled") cb.canceled = v == "true";
        }
        return cb;
    }
}

//==============================================================================
LoopbackServer::LoopbackServer() : juce::Thread ("TONE3000 sign-in") {}
LoopbackServer::~LoopbackServer() { stop(); }

bool LoopbackServer::start()
{
    if (isRunning()) return true;
    if (! listener.createListener (0, "127.0.0.1")) return false;   // loopback only, any free port
    port = listener.getBoundPort();
    if (port <= 0) { listener.close(); port = 0; return false; }
    startThread();
    return true;
}

void LoopbackServer::stop()
{
    signalThreadShouldExit();
    listener.close();
    stopThread (3000);
    port = 0;
}

juce::String LoopbackServer::redirectUri() const
{
    return isRunning() ? "http://127.0.0.1:" + juce::String (port.load()) + "/callback" : juce::String();
}

void LoopbackServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
        if (c == nullptr || threadShouldExit()) continue;
        handle (*c);
    }
}

void LoopbackServer::handle (juce::StreamingSocket& c)
{
    juce::MemoryOutputStream head;
    char buf[1024];
    while (! head.toString().contains ("\r\n\r\n") && head.getDataSize() < 16 * 1024)
    {
        if (c.waitUntilReady (true, 2000) != 1) break;
        const int n = c.read (buf, (int) sizeof (buf), false);
        if (n <= 0) break;
        head.write (buf, (size_t) n);
    }
    const auto line = head.toString().upToFirstOccurrenceOf ("\r\n", false, false);
    const auto target = line.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    const auto query = target.fromFirstOccurrenceOf ("?", false, false);

    juce::String response;
    if (line.startsWith ("GET ") && path == "/callback")
    {
        const juce::String html =
            "<!doctype html><html><head><meta charset=\"utf-8\"><title>Rigsmith</title>"
            "<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
            "background:#141518;color:#f1f1f1;font:16px/1.5 Segoe UI,Helvetica,Arial,sans-serif}"
            "main{text-align:center;padding:32px}h1{font-size:22px;margin:0 0 8px;color:#f5a83a}p{margin:0;opacity:.75}</style></head>"
            "<body><main><h1>Back to Rigsmith</h1><p>You can close this tab - your tone is on its way.</p></main></body></html>";
        response << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " << (int) html.getNumBytesAsUTF8()
                 << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" << html;
        if (onCallback != nullptr)
            juce::MessageManager::callAsync ([cb = onCallback, query] { cb (query); });
    }
    else
        response << "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    c.write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
    c.close();
}

//==============================================================================
Client::Client (Engine& e) : engine (e), builtInKey (RIGSMITH_T3K_KEY)
{
    tokens = Tokens::fromJson (unprotect (tokenFile().loadFileAsString().trim()));
    userName = engine.ini.get ("TONE3000", "User");
}

Client::~Client()
{
    *alive = false;
    if (server != nullptr) server->stop();
    pool.removeAllJobs (true, 4000);
}

template <typename Fn>
void Client::async (Fn&& fn)
{
    pool.addJob (std::forward<Fn> (fn));
}

juce::String Client::getKey() const
{
    const auto k = engine.ini.get ("TONE3000", "PublishableKey").trim();
    return k.isNotEmpty() ? k : builtInKey.trim();
}

void Client::setKey (const juce::String& k) { engine.ini.set ("TONE3000", "PublishableKey", k.trim()); }

juce::String Client::getOrigin() const
{
    const auto o = engine.ini.get ("TONE3000", "ApiOrigin").trim();
    return (o.isNotEmpty() ? o : juce::String (defaultOrigin)).trimCharactersAtEnd ("/");
}

int Client::getArchitecture() const
{
    return engine.ini.getInt ("TONE3000", "Architecture", 2) == 1 ? 1 : 2;
}

juce::File Client::tokenFile() const
{
    return engine.ini.getFile().getSiblingFile ("tone3000.dat");
}

Tokens Client::currentTokens() const
{
    const juce::ScopedLock sl (lock);
    return tokens;
}

bool Client::isConnected() const { return currentTokens().isValid(); }

void Client::storeTokens (const Tokens& t, bool notify)
{
    {
        const juce::ScopedLock sl (lock);
        tokens = t;
    }
    if (t.isValid()) tokenFile().replaceWithText (protect (t.toJson()));
    else tokenFile().deleteFile();
    if (notify) sendChangeMessage();
}

void Client::signOut()
{
    cancelFlow();
    storeTokens ({}, true);
    userName.clear();
    avatarUrl.clear();
    engine.ini.set ("TONE3000", "User", {});
}

juce::String Client::describe (const juce::String& error, int status)
{
    if (status == 0)   return error.isNotEmpty() ? error : "Couldn't reach TONE3000 - check your internet connection";
    if (status == 401) return "Your TONE3000 sign-in has ended - sign in again";
    if (status == 403) return "TONE3000 didn't allow that" + (error.isNotEmpty() ? ": " + error : juce::String());
    if (status == 404) return "That tone isn't on TONE3000 any more (or is private)";
    if (status == 429) return "TONE3000 is busy - wait a moment and try again";
    return "TONE3000 error " + juce::String (status) + (error.isNotEmpty() ? ": " + error : juce::String());
}

//--- HTTP (worker threads) -----------------------------------------------------
HttpResponse Client::send (const juce::String& method, const juce::URL& urlIn, const juce::StringPairArray& headers, const juce::String& body) const
{
    HttpResponse r;
    juce::String headerBlock;
    for (auto& k : headers.getAllKeys()) headerBlock << k << ": " << headers[k] << "\r\n";
    auto url = urlIn;
    if (method == "POST" || method == "PUT") url = url.withPOSTData (body);
    juce::StringPairArray responseHeaders;
    int status = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withHttpRequestCmd (method)
                       .withExtraHeaders (headerBlock)
                       .withConnectionTimeoutMs (30000)
                       .withNumRedirectsToFollow (5)
                       .withStatusCode (&status)
                       .withResponseHeaders (&responseHeaders);
    if (auto stream = url.createInputStream (options))
    {
        stream->readIntoMemoryBlock (r.body);
        r.status = status;
        for (auto& k : responseHeaders.getAllKeys())
            if (k.equalsIgnoreCase ("X-Tone3000-Deprecations")) r.deprecations = responseHeaders[k];
        if (r.deprecations.isNotEmpty()) DBG ("[TONE3000] deprecated: " << r.deprecations);
    }
    else
    {
        r.status = status;
        if (status == 0) r.error = "Couldn't reach TONE3000 - check your internet connection";
    }
    return r;
}

bool Client::refreshNow()
{
    std::lock_guard<std::mutex> single (refreshMutex);
    const auto cur = currentTokens();
    if (! cur.isValid()) return false;
    if (juce::Time::currentTimeMillis() < cur.expiresAtMs - refreshLeadMs) return true;   // someone else just did

    juce::StringPairArray h;
    h.set ("Content-Type", "application/x-www-form-urlencoded");
    const auto r = send ("POST", juce::URL (getOrigin() + "/api/v1/oauth/token"), h,
                         formEncode ({ { "grant_type", "refresh_token" }, { "refresh_token", cur.refreshToken }, { "client_id", getKey() } }));
    if (r.ok()) { storeTokens (Tokens::fromTokenResponse (r.json()), false); return true; }
    if (r.status == 400 || r.status == 401)
    {
        // the sign-in itself has ended
        juce::MessageManager::callAsync ([this, a = alive] { if (*a) storeTokens ({}, true); });
        return false;
    }
    return true;   // offline / 5xx: keep the tokens
}

juce::String Client::accessToken (bool force)
{
    auto cur = currentTokens();
    if (! cur.isValid()) return {};
    if (force || juce::Time::currentTimeMillis() > cur.expiresAtMs - refreshLeadMs)
    {
        if (force) { const juce::ScopedLock sl (lock); tokens.expiresAtMs = 0; }
        if (! refreshNow()) return {};
        cur = currentTokens();
    }
    return cur.accessToken;
}

HttpResponse Client::authed (const juce::String& method, const juce::String& pathOrUrl)
{
    const auto token = accessToken (false);
    if (token.isEmpty()) { HttpResponse r; r.status = 401; return r; }
    auto go = [&] (const juce::String& bearer)
    {
        juce::StringPairArray h;
        h.set ("Authorization", "Bearer " + bearer);
        h.set ("Accept", "application/json");
        const juce::URL url (pathOrUrl.startsWith ("http") ? pathOrUrl : getOrigin() + pathOrUrl);
        return send (method, url, h, {});
    };
    auto r = go (token);
    if (r.status == 401)   // expired between the check and the request: once more
        if (auto fresh = accessToken (true); fresh.isNotEmpty())
            r = go (fresh);
    return r;
}

//--- Sign-in / Select flows ----------------------------------------------------
void Client::cancelFlow()
{
    if (server != nullptr) server->stop();
    server.reset();
}

void Client::startFlow (const juce::String& prompt, const juce::String& format, const juce::String& gears,
                        std::function<void (int, juce::String)> done)
{
    cancelFlow();
    if (! hasKey()) { done (0, "Add your TONE3000 publishable key first (Settings)"); return; }

    server = std::make_unique<LoopbackServer>();
    if (! server->start()) { server.reset(); done (0, "Couldn't open a local port for the TONE3000 sign-in"); return; }

    pkce = oauth::Pkce::generate();
    const auto redirect = server->redirectUri();
    juce::StringPairArray extra;
    if (prompt.isNotEmpty()) extra.set ("prompt", prompt);
    if (format.isNotEmpty()) extra.set ("format", format);
    if (gears.isNotEmpty()) extra.set ("gears", gears);
    if (format == "nam") extra.set ("architecture", juce::String (getArchitecture()));
    if (prompt == "select_tone") extra.set ("preview", "true");
    const auto url = oauth::authorizeUrl (getOrigin(), getKey(), redirect, pkce, extra);

    server->onCallback = [this, done, redirect, a = alive] (juce::String query)
    {
        if (! *a) return;
        const auto cb = oauth::Callback::parse (query);
        cancelFlow();
        if (cb.state != pkce.state) { done (0, "The TONE3000 sign-in didn't match - please try again"); return; }
        if (cb.error.isNotEmpty() && cb.code.isEmpty()) { done (0, cb.error == "access_denied" ? juce::String() : describe (cb.error, 400)); return; }
        if (cb.code.isEmpty()) { done (0, {}); return; }   // closed without signing in

        const int toneId = cb.canceled ? 0 : cb.toneId.getIntValue();
        const auto verifier = pkce.verifier;
        async ([this, a, done, redirect, verifier, code = cb.code, toneId]
        {
            juce::StringPairArray h;
            h.set ("Content-Type", "application/x-www-form-urlencoded");
            const auto r = send ("POST", juce::URL (getOrigin() + "/api/v1/oauth/token"), h,
                                 formEncode ({ { "grant_type", "authorization_code" }, { "code", code }, { "code_verifier", verifier },
                                               { "redirect_uri", redirect }, { "client_id", getKey() } }));
            juce::MessageManager::callAsync ([this, a, done, r, toneId]
            {
                if (! *a) return;
                if (! r.ok()) { done (0, describe (errorText (r), r.status)); return; }
                storeTokens (Tokens::fromTokenResponse (r.json()), true);
                fetchUser ([done, toneId] (bool) { done (toneId, {}); });
            });
        });
    };

    if (openBrowser) openBrowser (url);
    else url.launchInDefaultBrowser();
}

//--- API -----------------------------------------------------------------------
void Client::fetchUser (std::function<void (bool)> done)
{
    async ([this, a = alive, done]
    {
        const auto r = authed ("GET", "/api/v1/user");
        juce::MessageManager::callAsync ([this, a, done, r]
        {
            if (! *a) return;
            if (r.ok())
            {
                const auto v = r.json();
                userName = str (v, "display_name");
                if (userName.isEmpty()) userName = str (v, "username");
                avatarUrl = str (v, "avatar_url");
                engine.ini.set ("TONE3000", "User", userName);
                sendChangeMessage();
            }
            done (r.ok());
        });
    });
}

void Client::listTones (const juce::String& which, int page, std::function<void (juce::Array<Tone>, juce::String)> done)
{
    juce::String path = "/api/v1/tones/" + which;
    if (which != "trending" && which != "latest") path << "?page=" << juce::jmax (1, page) << "&page_size=50";
    async ([this, a = alive, done, path]
    {
        const auto r = authed ("GET", path);
        juce::MessageManager::callAsync ([a, done, r]
        {
            if (! *a) return;
            if (! r.ok()) { done ({}, describe (errorText (r), r.status)); return; }
            done (parseTones (r.json()), {});
        });
    });
}

void Client::getTone (int id, std::function<void (Tone, juce::String)> done)
{
    const auto path = "/api/v1/tones/" + juce::String (id) + "?architecture=" + juce::String (getArchitecture());
    async ([this, a = alive, done, path]
    {
        const auto r = authed ("GET", path);
        juce::MessageManager::callAsync ([a, done, r]
        {
            if (! *a) return;
            if (! r.ok()) { done ({}, describe (errorText (r), r.status)); return; }
            done (Tone::fromVar (r.json()), {});
        });
    });
}

void Client::listModels (int toneId, std::function<void (juce::Array<Model>, juce::String)> done)
{
    const int pref = getArchitecture();
    async ([this, a = alive, done, toneId, pref]
    {
        // the chosen architecture first, then the other one, then whatever the tone has (IRs, custom)
        juce::Array<Model> models;
        HttpResponse last;
        for (auto arch : { juce::String (pref), juce::String (pref == 2 ? 1 : 2), juce::String() })
        {
            juce::String path = "/api/v1/models?tone_id=" + juce::String (toneId) + "&page_size=300";
            if (arch.isNotEmpty()) path << "&architecture=" << arch;
            last = authed ("GET", path);
            if (! last.ok()) break;
            const auto v = last.json();
            if (auto* arr = v.getProperty ("data", juce::var()).getArray())
                for (auto& m : *arr) models.add (Model::fromVar (m));
            if (! models.isEmpty()) break;
        }
        juce::MessageManager::callAsync ([a, done, models, last]
        {
            if (! *a) return;
            if (models.isEmpty() && ! last.ok()) { done ({}, describe (errorText (last), last.status)); return; }
            done (models, models.isEmpty() ? juce::String ("This tone has no NAM or IR files Rigsmith can load") : juce::String());
        });
    });
}

juce::File Client::libraryFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rigsmith").getChildFile ("TONE3000");
}

juce::File Client::folderFor (const Tone& t) const
{
    auto name = t.title.isNotEmpty() ? t.title : "Tone " + juce::String (t.id);
    if (t.creator.isNotEmpty()) name << " - " << t.creator;
    name << " [" << t.id << "]";
    return libraryFolder().getChildFile (juce::File::createLegalFileName (gearName (t.getGear())))
                          .getChildFile (juce::File::createLegalFileName (name.substring (0, 120)));
}

void Client::download (const Tone& tone, const Model& model, std::function<void (juce::File, juce::String)> done)
{
    auto ext = model.extension();
    if (ext.isEmpty() || ! juce::String (".nam.wav.aif.aiff.flac").contains (ext)) ext = tone.format == "ir" ? ".wav" : ".nam";
    const auto folder = folderFor (tone);
    const auto target = folder.getChildFile (juce::File::createLegalFileName (model.name.isNotEmpty() ? model.name : "model " + juce::String (model.id)) + ext);

    // make sure the library looks in Documents\Rigsmith\TONE3000
    for (auto key : { "modelPaths", "irPaths" })
    {
        auto paths = engine.getPaths (key);
        bool covered = false;
        for (int i = 0; i < paths.getNumPaths(); ++i)
            covered |= libraryFolder() == juce::File (paths.getRawString (i)) || libraryFolder().isAChildOf (paths.getRawString (i));
        if (! covered) { paths.add (libraryFolder()); engine.setPaths (key, paths); }
    }

    if (target.existsAsFile() && target.getSize() > 0)
    {
        juce::MessageManager::callAsync ([done, target] { done (target, {}); });
        return;
    }

    async ([this, a = alive, done, tone, model, folder, target]
    {
        const auto r = authed ("GET", model.modelUrl);
        juce::String err;
        if (! r.ok()) err = describe (errorText (r), r.status);
        else if (r.body.getSize() < 16) err = "TONE3000 sent an empty file";
        else
        {
            folder.createDirectory();
            if (! target.replaceWithData (r.body.getData(), r.body.getSize())) err = "Couldn't save " + target.getFullPathName();
            else
            {
                ToneInfo info;
                info.valid = true;
                info.id = tone.id; info.title = tone.title; info.gear = tone.gear; info.format = tone.format;
                info.creator = tone.creator; info.url = tone.url; info.license = tone.license; info.image = tone.image;
                info.write (folder);
            }
        }
        juce::MessageManager::callAsync ([a, done, target, err]
        {
            if (! *a) return;
            done (err.isEmpty() ? target : juce::File(), err);
        });
    });
}

void Client::fetchImage (const juce::String& url, std::function<void (juce::Image)> done)
{
    if (url.isEmpty()) { done ({}); return; }
    if (auto it = images.find (url); it != images.end()) { done (it->second); return; }
    images[url] = {};   // one request per image
    async ([this, a = alive, url, done]
    {
        const auto r = send ("GET", juce::URL (url), {}, {});
        auto img = r.ok() ? juce::ImageFileFormat::loadFrom (r.body.getData(), r.body.getSize()) : juce::Image();
        if (img.isValid() && (img.getWidth() > 160 || img.getHeight() > 160))
            img = img.rescaled (juce::jmin (160, img.getWidth()), juce::jmin (160, img.getHeight()), juce::Graphics::mediumResamplingQuality);
        juce::MessageManager::callAsync ([this, a, url, img, done]
        {
            if (! *a) return;
            images[url] = img;
            done (img);
        });
    });
}

} // namespace rig::t3k
