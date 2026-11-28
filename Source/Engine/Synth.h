#pragma once

#include "Nodes.h"

namespace rig
{
/** The MIDI of the block being processed, set by the Engine around the rig (audio thread only).
    Blocks that play notes - Keysmith's Synth and Sampler - take the events between RigIo::offset
    and RigIo::offset + numSamples. */
struct RigMidi
{
    static inline thread_local const juce::MidiBuffer* events = nullptr;
};

namespace keys
{
/** ADSR, linear attack and exponential decay / release (times in ms, sustain 0..1). */
struct Env
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0.0f;
    void set (double sr, float attackMs, float decayMs, float sustain, float releaseMs) noexcept;
    void start() noexcept { stage = Attack; }
    void stop() noexcept { if (stage != Idle) stage = Release; }
    void kill() noexcept { stage = Idle; level = 0.0f; }
    bool active() const noexcept { return stage != Idle; }
    float next() noexcept;
private:
    float attackStep = 1.0f, decayCoef = 0.0f, releaseCoef = 0.0f, sus = 1.0f;
};

/** Zero-delay-feedback state-variable filter (one per channel). */
struct Svf
{
    float s1 = 0.0f, s2 = 0.0f;
    void reset() noexcept { s1 = s2 = 0.0f; }
    /** mode: 0 low, 1 band, 2 high, 3 notch. g = tan (pi fc / sr), k = 1 / Q. */
    float process (float x, float g, float k, int mode) noexcept;
};

/** The filter types a Synth or Sampler voice offers. */
const juce::StringArray& filterTypes();   // LP 12, LP 24, BP, HP, Notch, Off
/** Note number -> "C3" (60 = C3, as most DAWs show it). */
juce::String noteName (int note);

/** Keysmith's built-in banks: single-cycle waveforms made into wavetables (the Adventure Kid AKWF
    collection, CC0) and pitched instruments for the Sampler (the Versilian VSCO 2 Community Edition,
    CC0). A reference is "Category/Name". */
struct BankEntry
{
    bool sample = false;
    juce::String category, name;
    int root = 60;                 // samples: the note it plays
    int frames = 0, frameLen = 0;  // tables
    const char* data = nullptr;
    size_t size = 0;
    juce::String ref() const { return category + "/" + name; }
};
const std::vector<BankEntry>& banks();
const BankEntry* findBank (const juce::String& ref);
juce::StringArray bankCategories (bool samples);
} // namespace keys

//==============================================================================
/** A wavetable: frames of 2048 samples, each kept at several band limits (one per octave) so the
    high notes never alias. */
class Wavetable
{
public:
    static constexpr int size = 2048, numMips = 9;   // mip k holds harmonics up to 1024 >> k
    juce::String name;
    int frames = 1;

    /** phase 0..1, position 0..1 across the frames, mip from mipFor(). */
    float read (double phase, float position, int mip) const noexcept;
    /** The band limit for a note at hz (sr: the sample rate). */
    static int mipFor (double hz, double sr) noexcept;
    /** One frame, as drawn on the display (the most harmonics). */
    const float* frame (int f) const noexcept { return mips[0].data() + (size_t) juce::jlimit (0, frames - 1, f) * size; }

    /** Built from single-cycle frames (any length: each is resampled to 2048). */
    static std::shared_ptr<const Wavetable> fromFrames (const juce::String& name, const std::vector<std::vector<float>>& frames);
    /** A .wav of 2048-sample frames (Serum's format) - or any short sound, cut into frames. */
    static std::shared_ptr<const Wavetable> fromFile (const juce::File&, juce::String& error);
    /** Keysmith's own tables: Basic shapes, PWM, Harmonics, Formant, Organ, FM Bell, Sync, Digital... */
    static const std::vector<std::shared_ptr<const Wavetable>>& factory();
    static juce::StringArray factoryNames();

private:
    std::vector<std::vector<float>> mips;   // [mip][frame * size + i]
};

//==============================================================================
/** Shared by the Synth and the Sampler: voices, MIDI, envelopes, the filter, glide. */
class NoteBlock : public Block
{
public:
    NoteBlock (NodeType, std::vector<ParamSpec>);
    static constexpr int maxVoices = 16;

    /** Notes sounding right now, for the display (bit per note). */
    std::array<std::atomic<juce::uint32>, 4> sounding {};
    std::vector<std::pair<juce::String, std::vector<int>>> panelGroups() const override { return groups; }

protected:
    struct Voice
    {
        int note = -1;
        float velocity = 0.0f;
        bool held = false, sustained = false;
        juce::uint32 age = 0;
        double pitch = 60.0, target = 60.0;   // with glide
        keys::Env amp, mod;
        keys::Svf fl[2], fl2[2];
        keys::Svf fv[3][2];                  // vowel filters: three formants
        float ap[4][2] {}, onePole[2] {};    // phaser all-passes, the 6 dB low pass
        std::vector<float> comb[2];          // comb filters' delay lines
        int combAt = 0;
        juce::Random rng;
        bool active() const noexcept { return amp.active(); }
    };

    void prepareImpl (double sr, int maxBlockSize) override;
    void processImpl (juce::AudioBuffer<float>&, int n) noexcept override;

    /** A voice starting (fresh, or retriggered legato = false). */
    virtual void startVoice (Voice&, bool fresh) noexcept = 0;
    /** Adds the voice's next n samples to l / r (amp envelope and filter applied by the caller). */
    virtual void renderVoice (Voice&, int index, float* l, float* r, int n) noexcept = 0;
    /** Once per chunk, before the voices: read the knobs. */
    virtual void beginChunk() noexcept {}
    /** A one-shot sound plays to its end: note-off doesn't release it. */
    virtual bool holdsThroughNoteOff() const noexcept { return false; }

    std::array<Voice, maxVoices> voices;
    // parameter indices, set by the derived block's constructor
    int pFilter = -1, pCutoff = -1, pRes = -1, pDrive = -1, pKeyTrack = -1, pFilterEnv = -1,
        pAttack = -1, pDecay = -1, pSustain = -1, pRelease = -1, pVel = -1,
        pModA = -1, pModD = -1, pModS = -1, pModR = -1,
        pVoices = -1, pMode = -1, pGlide = -1, pBend = -1, pLevel = -1;
    std::vector<std::pair<juce::String, std::vector<int>>> groups;
    float bend = 0.0f;           // semitones, from the wheel
    float modWheel = 0.0f;       // 0..1
    std::vector<float> mixL, mixR, vl, vr;

private:
    void noteOn (int note, float vel) noexcept;
    void noteOff (int note) noexcept;
    void render (float* l, float* r, int n) noexcept;
    juce::uint32 clock = 0;
    bool pedal = false;
    std::vector<int> monoStack;
};

//==============================================================================
/** Keysmith's sample engine: one layer of a sound - played back pitched (Classic), time-stretched
    (Stretch: the speed through the sound is its own knob) or as a grain cloud (Granular). Layers
    are Samplers on the paths of a split. */
class SamplerBlock : public NoteBlock
{
public:
    SamplerBlock();
    juce::String getName() const override { return "Sampler"; }
    std::vector<int> cardParams() const override { return { pStart, pFCut, pAAtk, pARel }; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Sample; }

    enum
    {
        pSource, pModeS, pRoot, pStart, pLength, pLoop, pReverse, pTune, pFine,
        pSpeed, pGrain, pDensity, pSpray, pPitchSpread, pSpread,
        pFType, pFCut, pFRes, pFDrive, pFKey, pFEnv,
        pAAtk, pADec, pASus, pARel, pAVel,
        pEAtk, pEDec, pESus, pERel, pEPos,
        pVVoices, pVMode, pVGlide, pVBend, pVLevel
    };

    struct Sample
    {
        juce::String name;
        juce::AudioBuffer<float> data;   // stereo
        double rate = 48000.0;
        std::vector<float> peaks;        // for the display: 512 columns of |max|
    };
    /** Load a sound file (wav, aiff, flac, ogg, mp3): it becomes the "File" source. */
    bool loadSample (const juce::File&, juce::String& error);
    /** An instrument from Keysmith's banks ("Strings/Cellos"): the "File" source, its root set. */
    bool loadBankSample (const juce::String& ref);
    juce::String getBankRef() const { return bankRef; }
    juce::File getFile() const { return file; }
    const Sample* currentSample() const noexcept;
    /** Playheads of the sounding voices (0..1 through the sound), for the display; -1 = none. */
    std::array<std::atomic<float>, 8> playheads {};

    static const std::vector<std::shared_ptr<const Sample>>& factory();
    static juce::StringArray factoryNames();
    static bool isSoundFile (const juce::File&);

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;
    using Block::formatValue;
    juce::String formatValue (int i, float v) const override;
    void randomise (juce::Random&, float amount) override;

protected:
    void startVoice (Voice&, bool fresh) noexcept override;
    void renderVoice (Voice&, int index, float* l, float* r, int n) noexcept override;
    void beginChunk() noexcept override;

private:
    struct Grain { double pos = 0, step = 0; int age = 0, length = 0; float panL = 1, panR = 1; bool on = false; };
    static constexpr int maxGrains = 24;
    struct Play { double head = 0, regionHead = 0; double untilGrain = 0; bool done = false; std::array<Grain, maxGrains> grains {}; };
    std::array<Play, maxVoices> play {};
    std::atomic<const Sample*> loaded { nullptr };
    std::vector<std::shared_ptr<const Sample>> keep;
    juce::File file;
    juce::String bankRef;
    const Sample* now = nullptr;   // this chunk's sound
    int modeNow = 0;
    float startNow = 0, lengthNow = 1, speedNow = 1, sprayNow = 0, pitchSpreadNow = 0, spreadNow = 0, posEnvNow = 0;
    int grainLen = 4096;
    double grainGap = 1024;
    bool loopNow = false, reverseNow = false;
};

//==============================================================================
/** Keysmith's engine: two oscillators, each a wavetable (with unison, harmonics, FM, waveshaping
    and hard sync) or a sample (pitched, looped, time-stretched or granular, with unison), then a
    filter and two envelopes, a sub and noise. The second envelope moves the wavetables, the
    filter and the samples' pitch. */
class SynthBlock : public NoteBlock
{
public:
    SynthBlock();
    juce::String getName() const override { return "Synth"; }
    std::vector<int> cardParams() const override { return { pPosA, pFCut, pAAtk, pARel }; }
    juce::String getDetail() const override;
    Visual getVisual() const override { return Visual::Wavetable; }

    enum
    {
        pTableA, pPosA, pSemiA, pFineA, pUniA, pDetA, pLevelA, pPanA,
        pTableB, pPosB, pSemiB, pFineB, pUniB, pDetB, pLevelB, pPanB,
        pSub, pSubOct, pNoise,
        pFType, pFCut, pFRes, pFDrive, pFKey, pFEnv,
        pAAtk, pADec, pASus, pARel, pAVel,
        pEAtk, pEDec, pESus, pERel, pEPos,
        pVVoices, pVMode, pVGlide, pVBend, pVLevel,
        pOscA   // each oscillator's own knobs follow (OscParam), then the master's
    };
    /** An oscillator's knobs, from oscParam (osc, k). */
    enum OscParam
    {
        oType,
        oPhase, oPhaseRand, oSym, oSync,                       // WAVE (with table, position)
        oUSpread, oWidth, oBlend,                              // UNISON (with voices, detune)
        oHAmt, oHCount, oHTilt, oHOddEven, oHDecay,            // HARM
        oFm, oFmRatio, oFmFine, oFmShape, oFmFb,               // FM
        oWs, oWsDrive, oWsBias, oWsSym, oWsMix,                // WS
        oSrc, oSStart, oSEnd, oRoot, oSGain, oSRev,            // SAMPLE
        oSMode, oSSpeed, oSDir, oSRand, oSRetrig,              // PLAYBACK
        oTrack, oPEnv, oStretch, oFormant,                     // PITCH (with tune = semi, fine)
        oLoop, oLStart, oLEnd, oLXfade, oLMode,                // LOOP
        oGran, oGSize, oGDens, oGPos, oGPosRand, oGPitch, oGOver, oGSpread,   // GRANULAR
        numOscParams
    };
    static constexpr int oscParam (int osc, int k) noexcept { return pOscA + (osc > 0 ? 1 : 0) * numOscParams + k; }
    static constexpr int pMPan = pOscA + 2 * numOscParams, pMix = pMPan + 1, pTranspose = pMPan + 2;
    /** Each oscillator on / off (its light). */
    static constexpr int pOnA = pMPan + 3, pOnB = pMPan + 4;
    static constexpr int oscOnParam (int osc) noexcept { return osc > 0 ? pOnB : pOnA; }
    /** The knobs a page (tab) of an oscillator shows: "WAVE", "UNISON", "HARM", "FM", "WS" (a wavetable),
        "SAMPLE", "PLAYBACK", "PITCH", "LOOP", "GRANULAR" (a sample). */
    std::vector<int> pageParams (int osc, const juce::String& page) const;
    bool isSampleOsc (int osc) const noexcept { return getChoice (oscParam (osc, oType)) == 1; }
    /** Knobs the dice never touch: anything that changes the tuning. */
    static bool isTuning (const juce::String& paramId);

    /** A wavetable from a file: it becomes the "User" table, on oscillator A (or B). */
    bool loadWavetable (const juce::File&, int osc, juce::String& error);
    /** A table from Keysmith's banks ("Instruments/Cello") on oscillator A (or B). */
    bool loadBankTable (const juce::String& ref, int osc);
    /** What an oscillator's "User" slot holds: a bank reference or a file path ("" = none). */
    juce::String userRef (int osc) const { return oscRef[(size_t) juce::jlimit (0, 1, osc)]; }
    /** The name of the table an oscillator plays. */
    juce::String tableName (int osc) const;
    juce::File getUserFile() const { return userFile; }
    /** For the display: the table an oscillator plays and where it is in it (0..1, with modulation). */
    const Wavetable* tableFor (int osc) const noexcept;
    float livePosition (int osc) const noexcept { return livePos[(size_t) juce::jlimit (0, 1, osc)].load (std::memory_order_relaxed); }

    // the sample side of an oscillator
    bool loadSample (const juce::File&, int osc, juce::String& error);
    bool loadBankSample (const juce::String& ref, int osc);
    juce::String sampleRef (int osc) const { return smpRef[(size_t) juce::jlimit (0, 1, osc)]; }
    const SamplerBlock::Sample* sampleFor (int osc) const noexcept;
    /** Where the newest voice is in an oscillator's sample (0..1), for the display; -1 = none. */
    float samplePlayhead (int osc) const noexcept { return liveHead[(size_t) juce::jlimit (0, 1, osc)].load (std::memory_order_relaxed); }

    juce::ValueTree toState() const override;
    void fromState (const juce::ValueTree&) override;
    using Block::formatValue;
    juce::String formatValue (int i, float v) const override;
    void randomise (juce::Random&, float amount) override;

protected:
    void startVoice (Voice&, bool fresh) noexcept override;
    void renderVoice (Voice&, int index, float* l, float* r, int n) noexcept override;
    void beginChunk() noexcept override;
    bool holdsThroughNoteOff() const noexcept override;

private:
    static constexpr int maxUnison = 8, maxGrains = 24;
    struct Grain { double pos = 0, step = 0; int age = 0, length = 0; float panL = 1, panR = 1; bool on = false; };
    struct Osc
    {
        std::array<double, maxUnison> phase {}, slave {}, fmPhase {};
        std::array<float, maxUnison> fmLast {};
        // the sample side
        std::array<double, maxUnison> head {};
        std::array<int, maxUnison> dir {};
        double gHead = 0, untilGrain = 0;
        bool done = false;
        std::array<Grain, maxGrains> grains {};
        float fz1[2] {}, fz2[2] {};   // the formant peak
    };
    std::array<std::array<Osc, 2>, maxVoices> osc {};
    std::array<double, maxVoices> subPhase {};
    std::atomic<const Wavetable*> user { nullptr };
    std::array<std::atomic<const Wavetable*>, 2> userOsc { nullptr, nullptr };   // each oscillator's own (banks)
    std::array<juce::String, 2> oscRef, smpRef;
    std::vector<std::shared_ptr<const Wavetable>> keep;   // every table ever loaded stays alive
    std::array<std::atomic<const SamplerBlock::Sample*>, 2> smpLoaded { nullptr, nullptr };
    std::vector<std::shared_ptr<const SamplerBlock::Sample>> keepSmp;
    juce::File userFile;
    std::array<std::atomic<float>, 2> livePos {}, liveHead {};
    std::array<double, 2> lastHead {};   // Continue: where the last note was in each sample
    // knobs, read once per chunk
    struct OscKnobs
    {
        bool sample = false;
        const Wavetable* table = nullptr;
        const SamplerBlock::Sample* smp = nullptr;
        float pos = 0, semi = 0, fine = 0, det = 0, level = 0, pan = 0;
        int uni = 1;
        float phase = 0, phaseRand = 1, sym = 0, sync = 0, uspread = 0.75f, width = 1, blend = 0.5f;
        float hAmt = 0, hTilt = 0, hOdd = 0, hDecay = 0.5f; int hCount = 8;
        std::array<float, 33> hGain {};
        float fm = 0, fmRatio = 2, fmFb = 0; int fmShape = 0;
        int ws = 0; float wsDrive = 0, wsBias = 0, wsSym = 0, wsMix = 1;
        float sStart = 0, sEnd = 1, root = 60, gain = 1; bool rev = false;
        int mode = 2, dir = 0, retrig = 0; float speed = 1, sRand = 0;
        float track = 1, pEnv = 0, stretch = 0, formant = 0;
        bool loop = true; float lStart = 0, lEnd = 1, lXfade = 0; int lMode = 0;
        bool gran = false; float gSize = 0.09f, gDens = 4, gPos = 0, gPosRand = 0, gPitch = 0, gOver = 0.5f, gSpread = 0.5f;
    };
    std::array<OscKnobs, 2> k {};
    float subLevel = 0, noiseLevel = 0, posEnv = 0, mixAB = 0, mPan = 0, transpose = 0;
    int subOct = 1;
    void renderWave (Voice&, int vi, int o, float* l, float* r, int m) noexcept;
    void renderSample (Voice&, int vi, int o, float* l, float* r, int m) noexcept;
};

} // namespace rig
