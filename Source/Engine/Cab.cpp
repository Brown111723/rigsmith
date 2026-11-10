#include "Cab.h"

namespace rig
{
const std::vector<CabSpec>& CabBlock::cabs()
{
    static const std::vector<CabSpec> v = []
    {
        std::vector<CabSpec> c;
        auto add = [&c] (const char* name, const char* speakers, const char* blurb, bool bass, int cones, float inches, bool open,
                         float resHz, float resQ, float bodyHz, float bodyDb, float scoopHz, float scoopDb,
                         float p1Hz, float p1Db, float p2Hz, float p2Db, float topHz, float tweeterDb = -100.0f)
        {
            CabSpec s;
            s.name = name; s.speakers = speakers; s.blurb = blurb; s.bass = bass; s.cones = cones; s.inches = inches; s.openBack = open;
            s.resHz = resHz; s.resQ = resQ; s.bodyHz = bodyHz; s.bodyDb = bodyDb; s.scoopHz = scoopHz; s.scoopDb = scoopDb;
            s.peak1Hz = p1Hz; s.peak1Db = p1Db; s.peak2Hz = p2Hz; s.peak2Db = p2Db; s.topHz = topHz; s.tweeterDb = tweeterDb;
            c.push_back (s);
        };
        //   name                     speakers              blurb                                                                     bass  n  in   open   res   Q     body        scoop        peak 1       peak 2       top
        add ("1x12 Blackface",        "Jensen C12N",        "Open-back American combo: glassy cleans with a soft, airy low end",      false, 1, 12, true,  95,  1.0f, 200, 1.0f,  480, -3.0f, 2500, 4.5f, 3800, 3.0f, 5600);
        add ("1x12 Tweed",            "Jensen P12R alnico", "Small, old and honky: breaks up early, all midrange",                    false, 1, 12, true,  105, 1.1f, 240, 1.5f,  700,  1.5f, 2100, 4.0f, 3300, 2.0f, 4600);
        add ("2x12 Alnico Blue",      "Celestion Blue",     "The chime of a British class-A combo: bright, forward upper mids",       false, 2, 12, true,  100, 1.2f, 190, 1.0f,  420, -1.5f, 2800, 5.5f, 4300, 3.5f, 5800);
        add ("2x12 Jazz Clean",       "Roland JC-120",      "Flat, wide and clean: built for chorus and pedals",                      false, 2, 12, true,  85,  0.9f, 160, 0.5f,  500, -1.0f, 3000, 2.5f, 5000, 2.0f, 6500);
        add ("4x10 Tweed",            "Jensen P10R",        "Four tens: tight punch with a snappy, bright attack",                    false, 4, 10, true,  110, 1.1f, 220, 1.5f,  450, -2.0f, 2000, 3.5f, 4000, 3.5f, 5200);
        add ("4x12 Greenback",        "Celestion G12M 25W", "The classic British stack: woody mids, a creamy, rounded top",           false, 4, 12, false, 112, 1.5f, 170, 2.5f,  400, -2.0f, 2300, 5.0f, 3400, 2.0f, 4700);
        add ("4x12 Vintage 30",       "Celestion V30",      "Modern high gain: big tight lows, an aggressive upper-mid spike",        false, 4, 12, false, 105, 1.7f, 130, 3.0f,  520, -3.5f, 2700, 6.5f, 4200, 2.5f, 5300);
        add ("4x12 T75",              "Celestion G12T-75",  "The scooped 80s stack: deep lows, sizzling highs, fewer mids",           false, 4, 12, false, 100, 1.6f, 120, 3.0f,  650, -4.5f, 3500, 5.0f, 4800, 3.0f, 6000);
        add ("2x12 Creamback",        "Celestion G12M-65",  "A Greenback with more lows and headroom: smooth and thick",              false, 2, 12, false, 100, 1.4f, 160, 2.0f,  430, -1.5f, 2400, 4.0f, 3600, 1.5f, 5000);
        add ("1x12 EVM Thiele",       "Electro-Voice 12L",  "A ported box with a hi-fi speaker: tight, loud, full-range lead tones",  false, 1, 12, false, 82,  1.3f, 140, 1.5f,  500, -1.0f, 2600, 3.0f, 4600, 2.0f, 6400);
        add ("8x10 Fridge",           "Ampeg SVT-810E",     "The sealed bass stack on every big stage: punchy low mids, no fizz",     true,  8, 10, false, 58,  1.0f, 240, 2.5f,  600, -1.5f, 1500, 3.0f, 2600, 1.5f, 3400);
        add ("4x10 Modern + horn",    "Aluminium cones",    "Hi-fi modern bass: deep lows, clear slap highs from the tweeter",        true,  4, 10, false, 48,  1.1f, 110, 2.0f,  550, -3.0f, 2200, 2.5f, 4500, 2.0f, 11000, -4.0f);
        add ("1x15 Flip-top",         "Ampeg B-15",         "The studio bass sound of Motown: round, warm and soft on top",           true,  1, 15, false, 50,  1.1f, 130, 2.5f,  420, -1.0f, 1100, 2.0f, 1900, 1.0f, 2500);
        add ("2x15 Vintage",          "Folded horn 15s",    "Huge, slow and woolly: 70s rock and dub lows",                           true,  2, 15, false, 44,  1.3f, 95,  3.0f,  380, -2.0f, 900,  2.5f, 1700, 1.0f, 2300);
        add ("2x12 Neo Bass",         "Neodymium 12s",      "Light, fast and even: a modern all-rounder that takes drive well",       true,  2, 12, false, 52,  1.0f, 120, 1.5f,  500, -1.5f, 1800, 2.5f, 3200, 2.0f, 5000);
        return c;
    }();
    return v;
}

const std::vector<MicSpec>& CabBlock::mics()
{
    static const std::vector<MicSpec> v = []
    {
        std::vector<MicSpec> m;
        auto add = [&m] (const char* name, const char* blurb, float lowHz, float lowDb, float peakHz, float peakDb, float peakQ,
                         float airHz, float airDb, float prox)
        {
            MicSpec s;
            s.name = name; s.blurb = blurb; s.lowHz = lowHz; s.lowDb = lowDb; s.peakHz = peakHz; s.peakDb = peakDb; s.peakQ = peakQ;
            s.airHz = airHz; s.airDb = airDb; s.proximity = prox;
            m.push_back (s);
        };
        add ("Dynamic 57",    "The one on almost every guitar cab: tight lows, a hard presence peak",   170, -2.5f, 5500, 5.0f, 1.1f, 11000, -3.0f, 0.8f);
        add ("Dynamic 421",   "Fuller and scooped: big lows, a wide upper-mid lift",                    110,  1.5f, 4200, 4.0f, 0.8f, 12000, -1.5f, 0.9f);
        add ("Ribbon 121",    "Dark, thick and smooth: tames a fizzy amp",                              160,  2.5f, 2800, 1.0f, 0.7f, 6000,  -7.0f, 1.4f);
        add ("Condenser 87",  "Open and detailed: the whole cab, as you hear it in the room",           0,    0.0f, 9500, 2.0f, 0.7f, 12000,  1.5f, 0.5f);
        add ("Bass dynamic",  "A large-diaphragm kick and bass mic: deep lows, scooped, clicky attack", 80,   3.5f, 3600, 4.0f, 1.0f, 9000,  -2.0f, 1.0f);
        return m;
    }();
    return v;
}

int CabBlock::firstBassCab()
{
    const auto& c = cabs();
    for (size_t i = 0; i < c.size(); ++i) if (c[i].bass) return (int) i;
    return 0;
}

static juce::StringArray cabNames()
{
    juce::StringArray s;
    for (auto& c : CabBlock::cabs()) s.add (c.bass ? "Bass " + c.name : c.name);
    return s;
}

static juce::StringArray micNames()
{
    juce::StringArray s;
    for (auto& m : CabBlock::mics()) s.add (m.name);
    return s;
}

CabBlock::CabBlock()
    : Block (NodeType::Cab, {
        Choice ("cab", "Cabinet", cabNames(), 6),
        Choice ("mic", "Microphone", micNames(), 0),
        P ("position", "Mic Position", 0.0f, 100.0f, 30.0f, "%").rand (5.0f, 70.0f),
        P ("distance", "Mic Distance", 0.0f, 100.0f, 10.0f, "%").rand (0.0f, 40.0f),
        P ("room", "Room", 0.0f, 100.0f, 0.0f, "%").rand (0.0f, 20.0f),
        P ("lowcut", "Low Cut", 20.0f, 400.0f, 20.0f, "Hz").mid (80.0f).rand (20.0f, 110.0f),
        P ("highcut", "High Cut", 2000.0f, 20000.0f, 20000.0f, "Hz").mid (7000.0f).rand (7000.0f, 20000.0f),
        P ("level", "Level", -24.0f, 12.0f, 0.0f, "dB") })
{
}

const CabSpec& CabBlock::cab() const { return cabs()[(size_t) juce::jlimit (0, (int) cabs().size() - 1, getChoice (pCab))]; }
const MicSpec& CabBlock::mic() const { return mics()[(size_t) juce::jlimit (0, (int) mics().size() - 1, getChoice (pMic))]; }

juce::String CabBlock::getDetail() const
{
    if (autoSkipped.load()) return "Off: the amp includes a cab";
    return (cab().bass ? "Bass " : "") + cab().name;
}

juce::String CabBlock::formatValue (int i, float v) const
{
    if (i == pPosition) return v < 4.0f ? juce::String ("Cap") : v > 96.0f ? juce::String ("Edge") : v < 38.0f ? "Cap +" + juce::String (juce::roundToInt (v)) : "Cone " + juce::String (juce::roundToInt (v));
    if (i == pDistance) return v < 2.0f ? juce::String ("Grille") : juce::String (v * 0.3f, 1) + " cm";
    return Block::formatValue (i, v);
}

juce::ValueTree CabBlock::toState() const
{
    auto v = Block::toState();
    v.setProperty ("skipAfterFullRig", skipAfterFullRig.load(), nullptr);
    return v;
}

void CabBlock::fromState (const juce::ValueTree& v)
{
    Block::fromState (v);
    skipAfterFullRig = (bool) v.getProperty ("skipAfterFullRig", true);
}

double CabBlock::Filters::magnitudeDb (double hz, double sr) const noexcept
{
    double db = 0.0;
    for (int i = 0; i < n; ++i) db += f[i].magnitudeDb (hz, sr);
    return db;
}

void CabBlock::design (Filters& F, double sr) const
{
    const auto& c = cab();
    const auto& m = mic();
    const float pos = get (pPosition) * 0.01f, dist = get (pDistance) * 0.01f;
    const double top = juce::jmin (sr * 0.45, (double) c.topHz * (1.2 - 0.6 * pos) * (1.0 - 0.08 * dist));
    F.n = 0;
    auto next = [&F] () -> Biquad& { auto& b = F.f[juce::jmin (kMaxStages - 1, F.n)]; ++F.n; b.reset(); return b; };
    auto peak = [&] (double hz, double q, double db) { if (std::abs (db) > 0.05 && hz < sr * 0.45) next().setPeak (sr, hz, q, db); };

    // the box: a resonant high-pass (open backs let the lows out round the back, so they roll off sooner)
    next().setHighPass (sr, c.resHz, c.resQ);
    if (c.openBack) next().setHighPass (sr, c.resHz * 0.75, 0.6);
    peak (c.bodyHz, 0.9, c.bodyDb + 1.5 * pos);
    peak (c.scoopHz, 1.1, c.scoopDb);
    // the cone: break-up peaks with ragged dips between them, fading as the mic moves off the cap
    peak (c.peak1Hz, 1.6, c.peak1Db * (1.0 - 0.5 * pos));
    peak (c.peak2Hz, 2.2, c.peak2Db * (1.0 - 0.8 * pos));
    peak (c.peak1Hz * 0.71, 4.5, -2.5 * (1.0 - 0.4 * pos));
    peak (std::sqrt ((double) c.peak1Hz * c.peak2Hz) * 1.04, 5.0, -3.5 * (1.0 - 0.5 * pos));
    // and where it stops
    next().setLowPass (sr, top, 0.541);
    next().setLowPass (sr, top, 1.307);
    if (c.tweeterDb > -50.0f) next().setHighShelf (sr, 2600.0, c.tweeterDb - 5.0 * pos);
    // across the cone: bright in the middle, dark and thick at the edge
    next().setHighShelf (sr, 2200.0, 3.0 - 9.0 * pos);

    // the microphone, and how close it is
    if (m.lowHz > 0.0f && std::abs (m.lowDb) > 0.05f) next().setLowShelf (sr, m.lowHz, m.lowDb);
    next().setLowShelf (sr, 170.0, m.proximity * (4.5 - 6.5 * dist));
    peak (m.peakHz, m.peakQ, m.peakDb * (1.0 - 0.3 * dist));
    if (std::abs (m.airDb) > 0.05f) next().setHighShelf (sr, m.airHz, m.airDb);

    // every cab and mic at about the same loudness: the average over the range a guitar (or bass) lives in
    F.n = juce::jmin (F.n, kMaxStages);
    double sum = 0.0;
    const double lo = c.bass ? 60.0 : 110.0, hi = c.bass ? 1500.0 : 3000.0;
    const int points = 24;
    for (int i = 0; i < points; ++i) sum += F.magnitudeDb (lo * std::pow (hi / lo, (double) i / (points - 1)), sr);
    F.gain = dbToGain ((float) (-sum / points));

    if (get (pLowCut) > 21.0f && F.n < kMaxStages) next().setHighPass (sr, get (pLowCut));
    if (get (pHighCut) < 19900.0f && F.n < kMaxStages) next().setLowPass (sr, juce::jmin (sr * 0.45, (double) get (pHighCut)));
    F.n = juce::jmin (F.n, kMaxStages);
}

float CabBlock::responseDb (double hz) const
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    Filters F;
    design (F, sr);
    return (float) (F.magnitudeDb (hz, sr) + gainToDb (F.gain));
}

void CabBlock::prepareImpl (double sr, int)
{
    for (auto& b : filters.f) b.reset();
    for (int ch = 0; ch < 2; ++ch)
    {
        roomDelay[ch].prepare ((int) (sr * 0.05));
        roomLp[ch].setCutoff (sr, 3200.0);
        roomLp[ch].reset();
    }
    level.reset (sr, 0.03);
    level.setCurrentAndTargetValue (dbToGain (get (pLevel)));
    room.reset (sr, 0.05);
    room.setCurrentAndTargetValue (get (pRoom) * 0.01f);
    watch.invalidate();
}

void CabBlock::processImpl (juce::AudioBuffer<float>& b, int n) noexcept
{
    if (watch.changed ({ (float) getChoice (pCab), (float) getChoice (pMic), get (pPosition), get (pDistance), get (pLowCut), get (pHighCut) }))
    {
        // keep the filters' memory across a redesign: a moving mic shouldn't click
        Filters fresh;
        design (fresh, sampleRate);
        for (int i = 0; i < juce::jmin (fresh.n, filters.n); ++i)
            for (int ch = 0; ch < 2; ++ch) { fresh.f[i].z1[ch] = filters.f[i].z1[ch]; fresh.f[i].z2[ch] = filters.f[i].z2[ch]; }
        filters = fresh;
    }
    level.setTargetValue (dbToGain (get (pLevel)) * filters.gain);
    room.setTargetValue (get (pRoom) * 0.01f);

    float inPk = 0.0f, outPk = 0.0f;
    for (int ch = 0; ch < 2; ++ch) inPk = juce::jmax (inPk, b.getMagnitude (ch, 0, n));

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            for (int k = 0; k < filters.n; ++k) x = filters.f[k].process (x, ch);
            d[i] = x;
        }
    }

    // the room: early reflections off the floor and walls, a little different on each side
    static constexpr float tapMs[2][kTaps] { { 7.3f, 11.9f, 17.1f, 23.7f, 31.3f }, { 8.9f, 13.3f, 19.7f, 26.9f, 34.1f } };
    static constexpr float tapGain[kTaps] { 0.50f, -0.40f, 0.33f, -0.25f, 0.19f };
    const bool roomOn = room.getTargetValue() > 0.001f || room.isSmoothing();
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    const float msToSamples = (float) (sampleRate * 0.001);
    for (int i = 0; i < n; ++i)
    {
        const float g = level.getNextValue();
        const float rm = room.getNextValue();
        float xl = l[i], xr = r[i];
        if (roomOn)
        {
            const float mono = 0.5f * (xl + xr);
            roomDelay[0].push (roomLp[0].lp (mono));
            roomDelay[1].push (roomLp[1].lp (mono));
            float el = 0.0f, er = 0.0f;
            for (int t = 0; t < kTaps; ++t)
            {
                el += tapGain[t] * roomDelay[0].readInt ((int) (tapMs[0][t] * msToSamples));
                er += tapGain[t] * roomDelay[1].readInt ((int) (tapMs[1][t] * msToSamples));
            }
            xl += el * rm * 0.9f;
            xr += er * rm * 0.9f;
        }
        l[i] = xl * g;
        r[i] = xr * g;
        outPk = juce::jmax (outPk, std::abs (l[i]), std::abs (r[i]));
    }
    updateMeters (inPk, outPk, 0.0f);
}

} // namespace rig
