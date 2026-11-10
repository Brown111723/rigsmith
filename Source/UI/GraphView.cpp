#include "GraphView.h"
#include "Icons.h"
#include "../Engine/Randomiser.h"

namespace rig
{
using namespace theme;

static void drawWire (juce::Graphics& g, float x1, float x2, float y, juce::Colour c, float level, bool arrow)
{
    if (x2 <= x1) return;
    const float th = 1.2f + 2.4f * juce::jlimit (0.0f, 1.0f, level);
    g.setColour (c.withAlpha (0.20f + 0.62f * juce::jlimit (0.0f, 1.0f, level)));
    g.fillRoundedRectangle (x1, y - th * 0.5f, x2 - x1 - (arrow ? 5.0f : 0.0f), th, th * 0.5f);
    if (arrow && x2 - x1 > 14)
    {
        juce::Path p;
        p.addTriangle (x2 - 8, y - 3.5f, x2 - 8, y + 3.5f, x2 - 1.5f, y);
        g.fillPath (p);
    }
}

static void drawCurve (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float level, bool muted)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dx = (b.x - a.x) * 0.55f;
    p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    const float th = 1.2f + 2.4f * juce::jlimit (0.0f, 1.0f, level);
    g.setColour (muted ? dim.withAlpha (0.5f) : c.withAlpha (0.20f + 0.62f * juce::jlimit (0.0f, 1.0f, level)));
    if (muted)
    {
        juce::Path dashed;
        const float dashes[] = { 5.0f, 5.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
    }
    else
    {
        g.strokePath (p, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

static bool isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac");
}

//==============================================================================
juce::Array<juce::File> CaptureDrop::forBlock (const Node& n) const
{
    if (n.getType() == NodeType::Nam) return nams;
    if (n.getType() == NodeType::Ir) return irs;
    return {};
}

bool CaptureDrop::isCaptureDescription (const juce::String& s)
{
    return s.startsWith ("file:") || s.startsWith ("files:") || s.startsWith ("folder:");
}

CaptureDrop CaptureDrop::fromPaths (const juce::StringArray& paths)
{
    CaptureDrop d;
    for (auto& p : paths)
    {
        const juce::File f (p);
        if (f.isDirectory())
        {
            // a whole folder: every capture in it (and its sub-folders)
            d.folder = true;
            for (auto& x : Engine::capturesInFolder (f, false)) d.nams.addIfNotAlreadyThere (x);
            for (auto& x : Engine::capturesInFolder (f, true)) d.irs.addIfNotAlreadyThere (x);
        }
        else if (Sweepable::isNamFile (f)) d.nams.addIfNotAlreadyThere (f);
        else if (Sweepable::isIrFile (f)) d.irs.addIfNotAlreadyThere (f);
    }
    return d;
}

CaptureDrop CaptureDrop::fromDescription (const juce::String& s)
{
    if (s.startsWith ("folder:")) return fromPaths ({ s.fromFirstOccurrenceOf ("folder:", false, false) });
    if (s.startsWith ("files:")) return fromPaths (juce::StringArray::fromLines (s.fromFirstOccurrenceOf ("files:", false, false)));
    if (s.startsWith ("file:")) return fromPaths ({ s.fromFirstOccurrenceOf ("file:", false, false) });
    return {};
}

juce::String CaptureDrop::describe (const juce::Array<juce::File>& files)
{
    if (files.size() == 1) return "file:" + files[0].getFullPathName();
    juce::StringArray paths;
    for (auto& f : files) paths.add (f.getFullPathName());
    return "files:" + paths.joinIntoString ("\n");
}

//==============================================================================
BlockCard::BlockCard (GraphHost& h, Node& n) : host (h), node (n)
{
    setRepaintsOnMouseActivity (true);
}

BlockCard::Form BlockCard::form() const
{
    if (auto* nb = dynamic_cast<const NamBlock*> (&node))
    {
        const auto gear = nb->getGear();
        return gear == Gear::Pedal || gear == Gear::Outboard || gear == Gear::Experimental || gear == Gear::Space ? Form::Pedal : Form::Head;
    }
    if (auto* ir = dynamic_cast<const IrBlock*> (&node)) return ir->getGear() == Gear::Space ? Form::Pedal : Form::Cab;
    if (node.getType() == NodeType::Cab) return Form::Cab;
    if (node.getType() == NodeType::Amp) return Form::Head;
    return Form::Pedal;
}

juce::Point<int> BlockCard::preferredSize() const
{
    switch (form())
    {
        case Form::Head:  return { 248, cardHeight };
        case Form::Cab:   return { 196, cardHeight };
        case Form::Pedal: break;
    }
    return { 152, cardHeight };
}

juce::Rectangle<float> BlockCard::plateArea() const
{
    // room round the enclosure for its shadow
    return getLocalBounds().toFloat().reduced (4.0f, 3.0f).withTrimmedBottom (4.0f);
}

juce::Rectangle<float> BlockCard::footswitchArea() const
{
    const auto r = plateArea();
    return { r.getCentreX() - 14.5f, r.getBottom() - 46.0f, 29.0f, 29.0f };
}

juce::Rectangle<float> BlockCard::diceArea() const
{
    const auto r = plateArea();
    return { r.getX() + 12.0f, r.getBottom() - 41.0f, 19.0f, 19.0f };
}

juce::Rectangle<float> BlockCard::lockArea() const
{
    // always there, opposite the dice: dim when unlocked, lit when locked
    const auto r = plateArea();
    return { r.getRight() - 30.0f, r.getBottom() - 40.0f, 17.0f, 17.0f };
}

bool BlockCard::hasDice() const
{
    return dynamic_cast<Block*> (&node) != nullptr && node.getType() != NodeType::Tuner;
}

static juce::File loadedFile (const Node& n)
{
    if (auto* nb = dynamic_cast<const NamBlock*> (&n)) return nb->getFile();
    if (auto* ir = dynamic_cast<const IrBlock*> (&n)) return ir->getFile();
    return {};
}

BlockCard::Selector BlockCard::selector() const
{
    // NAM and IR blocks: the block's own sweep (blends between captures)
    Selector sel;
    auto* sw = dynamic_cast<Sweepable*> (&node);
    if (sw == nullptr) return sel;
    sel.count = sw->getNumCaptures();
    if (sel.count < 2) return sel;
    sel.pos = sw->getCapturePosition();
    const auto names = sw->getCaptureNames();
    sel.names = names;
    sel.labels = host.getEngine().captures.shortNames (sw->getCaptureFiles(), names);
    for (int i = 0; i < sel.count; ++i) sel.tones.add (toneTypeName (sw->getCaptureToneType (i)));
    sel.gear = sw->getSweepGear();
    return sel;
}

void BlockCard::setSelector (const Selector& sel, float pos)
{
    pos = juce::jlimit (0.0f, (float) (sel.count - 1), pos);
    if (auto* sw = dynamic_cast<Sweepable*> (&node))
    {
        sw->setCapturePosition (pos);
        host.getEngine().markDirty();
    }
    repaint();
}

int BlockCard::maxKnobs() const
{
    switch (form()) { case Form::Head: return 6; case Form::Cab: return 0; case Form::Pedal: break; }
    return 4;
}

float BlockCard::knobRadius (int slots) const
{
    if (form() == Form::Head) return 11.0f;
    return slots <= 2 ? 14.5f : slots == 3 ? 13.0f : 11.5f;
}

juce::Point<float> BlockCard::knobCentre (int slot, int slots) const
{
    const auto r = plateArea();
    if (form() == Form::Cab) return { r.getRight() - 33.0f, r.getY() + 52.0f };   // only ever the capture selector
    const float y = form() == Form::Head ? r.getY() + 108.0f : r.getY() + 50.0f;
    return { r.getX() + 9.0f + (r.getWidth() - 18.0f) * ((float) slot + 0.5f) / (float) juce::jmax (1, slots), y };
}

juce::Rectangle<float> BlockCard::knobArea() const
{
    // the capture selector: the last knob in the row
    const int slots = shownSlots();
    const float R = (form() == Form::Cab ? 13.0f : knobRadius (slots)) + 3.5f;
    const auto c = knobCentre (slots - 1, slots);
    return { c.x - R, c.y - R, R * 2.0f, R * 2.0f };
}

void BlockCard::stepCapture (int delta)
{
    const auto sel = selector();
    if (! sel.valid()) return;
    // quarter steps, so a scroll can find the blends
    const float step = 0.25f;
    const float target = std::round ((sel.pos + step * (float) delta) / step) * step;
    setSelector (sel, target);
}

void BlockCard::showCaptureMenu()
{
    const auto sel = selector();
    if (! sel.valid()) return;
    const int nearest = juce::roundToInt (sel.pos);
    juce::PopupMenu m;
    m.addSectionHeader (node.getName() + "  -  " + juce::String (sel.count) + " captures");
    for (int i = 0; i < sel.count; ++i)
        m.addItem (i + 1, sel.names[i] + (sel.tones[i].isNotEmpty() ? "   (" + sel.tones[i] + ")" : juce::String()), true, i == nearest);
    juce::Component::SafePointer<BlockCard> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        const auto now = safe->selector();
        if (r > now.count) return;
        safe->host.select (safe->node.id);
        safe->setSelector (now, (float) (r - 1));
    });
}

void BlockCard::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (selector().valid() && knobArea().expanded (4.0f).contains (e.position))
    {
        const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
        if (d != 0.0f) stepCapture ((d > 0.0f) != w.isReversed ? -1 : 1);
        return;
    }
    GraphItem::mouseWheelMove (e, w);
}

juce::String BlockCard::getTooltip()
{
    const auto p = getMouseXYRelative().toFloat();
    if (const auto sel = selector(); sel.valid() && knobArea().expanded (4.0f).contains (p))
    {
        const int i = juce::roundToInt (sel.pos);
        return "Capture " + juce::String (i + 1) + " of " + juce::String (sel.count) + ": " + sel.names[i]
             + (sel.tones[i].isNotEmpty() ? " (" + sel.tones[i] + ")" : juce::String())
             + (sel.sweep ? "\nDrag up / down or scroll to sweep between captures, click for the list"
                          : "\nDrag up / down or scroll to switch, click for the list");
    }
    if (const int kp = knobParamAt (p); kp >= 0)
        if (auto* b = dynamic_cast<const Block*> (&node))
            return b->getParamName (kp) + ": " + b->formatValue (kp, b->getBase (kp)) + "\nDrag up / down to turn it (Shift = fine), double-click to reset";
    if (hasDice() && lockArea().expanded (3.0f).contains (p))
        return node.locked.load() ? "Locked: the dice leave this block alone. Click to unlock"
                                  : "Lock this block so the dice leave it alone";
    if (hasDice() && diceArea().expanded (3.0f).contains (p))
        return "Random " + categoryName (Randomiser::categoryOf (node)).toLowerCase() + " block (Shift+R). Ctrl+Z undoes it";
    if (footswitchArea().expanded (6.0f).contains (p))
        return "On / bypass";
    return node.getName() + "\nClick to edit it below, drag to move it, double-click to "
         + (node.getType() == NodeType::Plugin ? juce::String ("open its window")
            : node.getType() == NodeType::Nam || node.getType() == NodeType::Ir ? juce::String ("load a file") : juce::String ("edit"))
         + ", right-click for options. Drop a file or plug-in on it to replace it";
}

void BlockCard::mouseMove (const juce::MouseEvent& e)
{
    const int kp = knobParamAt (e.position);
    if (kp != paramHover)
    {
        paramHover = kp;
        setMouseCursor (kp >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }
    repaint();
}

bool BlockCard::acceptsFile (const juce::File& f) const
{
    return (node.getType() == NodeType::Nam && f.hasFileExtension ("nam"))
        || (node.getType() == NodeType::Ir && isAudioFile (f));
}

/** The first few knobs of the block, as the panel orders them (for the little knobs on its card). */
static std::vector<int> cardKnobs (const Node& node, int max)
{
    std::vector<int> v;
    if (auto* b = dynamic_cast<const Block*> (&node))
    {
        if (b->getType() == NodeType::Tuner) return v;
        for (int i : panelKnobs (*b))
        {
            if ((int) v.size() >= max) break;
            const auto& id = b->getSpecs()[(size_t) i].id;
            if (id == "capture") continue;   // that one has its own selector knob
            v.push_back (i);
        }
    }
    return v;
}

int BlockCard::shownSlots() const
{
    const bool sel = selector().valid();
    return juce::jmax (1, (int) cardKnobs (node, maxKnobs() - (sel ? 1 : 0)).size() + (sel ? 1 : 0));
}

/** A knob's name, short enough to sit under a small knob. */
static juce::String shortLabel (juce::String name)
{
    static const std::pair<const char*, const char*> known[] {
        { "Clean Blend", "BLEND" }, { "Blend (clean)", "BLEND" }, { "Bass (clean)", "CLEAN" }, { "Balance (dry)", "BAL" }, { "Threshold", "THRESH" },
        { "Mid Freq", "FREQ" }, { "Frequency", "FREQ" }, { "Low Cut", "LO CUT" }, { "High Cut", "HI CUT" }, { "Sensitivity", "SENS" }, { "Feedback", "FDBK" },
        { "Presence", "PRES" }, { "Intensity", "INTENS" }, { "Semitones", "SEMI" }, { "Midrange", "MID" }, { "Distortion", "DIST" }, { "Auto makeup", "AUTO" },
        { "Mic Position", "MIC" }, { "Mic Distance", "DIST" }, { "Predelay", "PRE" }, { "Pre-delay", "PRE" }, { "Resonance", "RES" }, { "Hysteresis", "HYST" },
        { "Lookahead", "LOOK" }, { "Crossover", "XOVER" }, { "Color Low", "LOW" }, { "Color High", "HIGH" }, { "Treble", "TREBLE" }, { "LFO Rate", "RATE" },
        { "Bass Freq", "LO F" }, { "Treble Freq", "HI F" }, { "Sustain", "SUST" }, { "Attack", "ATTACK" }, { "Release", "REL" },
        { "Voice 1 Level", "VOICE 1" }, { "Voice 2 Level", "VOICE 2" }, { "Humanize", "HUMAN" }, { "Transpose", "TRANS" }, { "Body Resonance", "BODY" },
        { "Top Sparkle", "TOP" }, { "Notch Depth", "NOTCH" }, { "Body Image", "IMAGE" }, { "Mic Gain", "GAIN" }, { "Pre-amp", "PRE-AMP" },
        { "Lead Drive", "DRIVE" }, { "Normal Vol", "VOLUME" }, { "Post Gain", "POST" }, { "Pre Gain", "GAIN" }, { "Lead Gain", "GAIN" } };
    for (auto& [full, brief] : known) if (name.equalsIgnoreCase (full)) return brief;
    name = name.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
    if (name.length() > 7 && name.containsChar (' ')) name = name.fromLastOccurrenceOf (" ", false, false);
    return name.substring (0, 7);
}

int BlockCard::knobParamAt (juce::Point<float> at) const
{
    if (dynamic_cast<const Block*> (&node) == nullptr) return -1;
    const bool sel = selector().valid();
    const auto shown = cardKnobs (node, maxKnobs() - (sel ? 1 : 0));
    const int slots = juce::jmax (1, (int) shown.size() + (sel ? 1 : 0));
    const float R = knobRadius (slots) + 3.0f;
    for (size_t k = 0; k < shown.size(); ++k)
        if (knobCentre ((int) k, slots).getDistanceFrom (at) <= R) return shown[k];
    return -1;
}

juce::uint64 BlockCard::signature() const
{
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint64 v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix (host.getSelectedId() == node.id ? 1u : 0u);
    mix (node.bypassed.load() ? 1u : 0u);
    mix (node.autoSkipped.load() ? 1u : 0u);
    mix (node.locked.load() ? 1u : 0u);
    mix ((juce::uint64) node.getName().hashCode64());
    mix ((juce::uint64) node.getDetail().hashCode64());
    mix ((juce::uint64) node.getLatency());
    mix (node.autoGain.load() ? (juce::uint64) (1000 + juce::roundToInt (node.autoGainDb.load() * 10.0f) + (node.autoGainProgress.load() < 1.0f ? 5000 : 0)) : 0u);
    // the signal line: about 1.5 dB steps
    const float db = gainToDb (node.flow.load (std::memory_order_relaxed));
    mix ((juce::uint64) juce::jlimit (0, 40, juce::roundToInt ((db + 60.0f) / 1.5f)));
    if (auto* b = dynamic_cast<const Block*> (&node))
    {
        for (int i : cardKnobs (node, maxKnobs()))
            mix ((juce::uint64) juce::roundToInt (b->getSpecs()[(size_t) i].toProportion (b->get (i)) * 90.0f));
        if (b->hasModels()) mix ((juce::uint64) b->getChoice (b->getModelParam()));
        if (b->getType() == NodeType::Cab) mix ((juce::uint64) (b->getChoice (0) * 1000 + juce::roundToInt (b->get (2))));
    }
    if (auto* sw = dynamic_cast<const Sweepable*> (&node)) mix ((juce::uint64) juce::roundToInt (sw->getCapturePosition() * 64.0f));
    if (auto* tb = dynamic_cast<TunerBlock*> (&node)) { const auto& rd = tb->getReading(); mix ((juce::uint64) (rd.valid ? 500 + juce::roundToInt (rd.cents) : 0)); }
    return h;
}

void BlockCard::refresh()
{
    const auto s = signature();
    if (s != lastSignature) { lastSignature = s; repaint(); }
}

bool BlockCard::needsRelayout() const { return preferredSize().x != getWidth(); }

void BlockCard::paintGrille (juce::Graphics& g, juce::Rectangle<float> area, bool live) const
{
    // grille cloth: a dark weave behind glass
    drawGlass (g, area, 5.0f);
    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip; clip.addRoundedRectangle (area.reduced (1.0f), 4.0f);
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::white.withAlpha (live ? 0.040f : 0.022f));
    for (float x = area.getX() - area.getHeight(); x < area.getRight(); x += 5.0f)
    {
        g.drawLine (x, area.getBottom(), x + area.getHeight(), area.getY(), 1.0f);
        g.drawLine (x, area.getY(), x + area.getHeight(), area.getBottom(), 1.0f);
    }
}

void BlockCard::paintSpeakers (juce::Graphics& g, juce::Rectangle<float> area, bool live, juce::Colour c) const
{
    paintGrille (g, area, live);
    int cones = 1;
    float micPos = -1.0f;
    if (auto* cb = dynamic_cast<const CabBlock*> (&node)) { cones = cb->cab().cones; micPos = cb->get (CabBlock::pPosition) * 0.01f; }
    const bool sel = selector().valid();
    auto space = area.reduced (6.0f, 5.0f);
    if (sel) space = space.withTrimmedRight (44.0f);
    const int cols = cones >= 8 ? 4 : cones >= 2 ? 2 : 1, rows = cones >= 4 ? 2 : 1;
    const float R = juce::jmin (space.getWidth() / (float) cols, space.getHeight() / (float) rows) * 0.5f - 1.5f;
    const float x0 = space.getCentreX() - (float) (cols - 1) * (R + 1.5f), y0 = space.getCentreY() - (float) (rows - 1) * (R + 1.5f);
    for (int i = 0; i < cols * rows; ++i)
    {
        const juce::Point<float> ctr (x0 + (float) (i % cols) * (R * 2.0f + 3.0f), y0 + (float) (i / cols) * (R * 2.0f + 3.0f));
        // surround, cone, dust cap
        g.setColour (juce::Colour (0xff232329).withMultipliedAlpha (live ? 1.0f : 0.6f));
        g.drawEllipse (ctr.x - R, ctr.y - R, R * 2.0f, R * 2.0f, 1.2f);
        juce::ColourGradient cone (juce::Colour (0xff0b0b0d), ctr.x, ctr.y, juce::Colour (0xff1d1d22), ctr.x, ctr.y + R, true);
        g.setGradientFill (cone);
        g.fillEllipse (ctr.x - R * 0.9f, ctr.y - R * 0.9f, R * 1.8f, R * 1.8f);
        g.setColour (juce::Colours::white.withAlpha (live ? 0.05f : 0.025f));
        g.drawEllipse (ctr.x - R * 0.62f, ctr.y - R * 0.62f, R * 1.24f, R * 1.24f, 1.0f);
        const float cap = R * 0.30f;
        juce::ColourGradient dome (juce::Colour (0xff2c2c33), ctr.x - cap * 0.3f, ctr.y - cap, juce::Colour (0xff101013), ctr.x, ctr.y + cap, false);
        g.setGradientFill (dome);
        g.fillEllipse (ctr.x - cap, ctr.y - cap, cap * 2.0f, cap * 2.0f);
        // where the mic is pointing (the built-in Cabinet)
        if (i == 0 && micPos >= 0.0f)
            drawLed (g, { ctr.x + micPos * R * 0.86f, ctr.y }, juce::jmax (2.0f, R * 0.09f), c, live);
    }
}

void BlockCard::paint (juce::Graphics& g)
{
    const auto r = plateArea();
    const bool selected = host.getSelectedId() == node.id;
    const bool on = ! node.bypassed.load();
    const bool skipped = on && node.autoSkipped.load();   // e.g. a cab IR after an "Amp + Cab" model
    const bool live = on && ! skipped;
    const auto c = colourFor (node);
    const float alpha = live ? 1.0f : 0.45f;
    const bool over = isMouseOver();
    const float W = (float) getWidth();
    const auto shape = form();
    const int X = (int) r.getX(), Y = (int) r.getY(), RW = (int) r.getWidth();

    // the enclosure
    drawShadow (g, r, 10.0f, selected ? 8.0f : 6.0f, selected ? 0.8f : 0.6f);
    drawPlate (g, r, 10.0f, selected ? c.withAlpha (0.0f) : (over ? borderHi : border), selected ? 0.05f : (over ? 0.025f : 0.0f));
    if (dropLabel.isNotEmpty()) { g.setColour (teal); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 2.0f); }
    else if (selected)
    {
        g.setColour (c.withAlpha (0.16f));
        g.drawRoundedRectangle (r.expanded (1.5f), 11.5f, 3.0f);
        g.setColour (c.withAlpha (0.95f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.3f);
    }

    // LED + what kind of thing it is
    ToneInfo tone;
    if (auto* nb = dynamic_cast<NamBlock*> (&node)) tone = nb->getToneInfo();
    else if (auto* ir = dynamic_cast<IrBlock*> (&node)) tone = ir->getToneInfo();
    const bool hasReadout = node.autoGain.load() || node.getLatency() > 0;
    const bool twoLeds = [this] { auto* nb = dynamic_cast<NamBlock*> (&node); return nb != nullptr && gearIncludesCab (nb->getGear()); }();
    drawLed (g, { r.getX() + 14.0f, r.getY() + 15.0f }, 3.0f, c, live);
    if (twoLeds) drawLed (g, { r.getX() + 23.0f, r.getY() + 15.0f }, 3.0f, gearColour (Gear::Cab), live);   // amp + cab: two lights
    g.setColour ((selected ? text : muted).withMultipliedAlpha (live ? 0.9f : 0.5f));
    g.setFont (font (9.0f, true).withExtraKerningFactor (0.12f));
    const int kx = X + (twoLeds ? 32 : 23);
    const int rightUsed = hasReadout ? 48 : (tone.valid ? 36 : 0);
    g.drawFittedText (kindLabel (node), kx, Y + 8, X + RW - 8 - kx - rightUsed, 14, juce::Justification::centredLeft, 1, 0.75f);

    // top right: latency or gain compensation, else the TONE3000 mark
    if (auto lat = node.getLatency(); lat > 0)
    {
        g.setColour (dim);
        g.setFont (mono (9.0f));
        g.drawText (juce::String (lat) + " smp", X + RW - 70, Y + 8, 60, 14, juce::Justification::centredRight);
    }
    else if (node.autoGain.load())
    {
        g.setColour (teal.withMultipliedAlpha (alpha));
        g.setFont (mono (9.0f));
        const float db = node.autoGainDb.load();
        g.drawText (node.autoGainProgress.load() < 1.0f ? juce::String ("AG ...") : "AG " + juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1),
                    X + RW - 70, Y + 8, 60, 14, juce::Justification::centredRight);
    }
    else if (tone.valid)
    {
        const juce::Rectangle<float> badge (r.getRight() - 38.0f, r.getY() + 9.0f, 27.0f, 12.0f);
        g.setColour (text.withAlpha (0.10f * alpha + 0.04f));
        g.fillRoundedRectangle (badge, 3.5f);
        g.setColour (text.withMultipliedAlpha (0.75f * alpha));
        g.setFont (font (8.0f, true));
        g.drawText ("T3K", badge, juce::Justification::centred);
    }

    // amp heads have a grille with the amp's name on it; cabs show their speakers
    juce::Rectangle<float> face;
    if (shape == Form::Head) face = { r.getX() + 10.0f, r.getY() + 27.0f, r.getWidth() - 20.0f, 60.0f };
    if (shape == Form::Cab)  face = { r.getX() + 10.0f, r.getY() + 27.0f, r.getWidth() - 20.0f, 84.0f };
    if (shape == Form::Head)
    {
        paintGrille (g, face, live);
        // the piping between grille and controls, in the amp's colour
        g.setColour (c.withAlpha (live ? 0.55f : 0.15f));
        g.fillRect (face.getX() + 2.0f, face.getBottom() + 3.0f, face.getWidth() - 4.0f, 1.0f);
    }
    else if (shape == Form::Cab) paintSpeakers (g, face, live, c);

    // the block's own knobs, where they're set right now (they move with modulation too)
    const auto sel = selector();
    const bool hasKnob = sel.valid();
    const auto knobsShown = cardKnobs (node, maxKnobs() - (hasKnob ? 1 : 0));
    const int slots = juce::jmax (1, (int) knobsShown.size() + (hasKnob ? 1 : 0));
    const float kr = knobRadius (slots);
    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
    if (auto* b = dynamic_cast<const Block*> (&node))
    {
        const float slotW = (r.getWidth() - 18.0f) / (float) slots;
        for (size_t k = 0; k < knobsShown.size(); ++k)
        {
            const int i = knobsShown[k];
            const auto ctr = knobCentre ((int) k, slots);
            const float p = b->getSpecs()[(size_t) i].toProportion (b->get (i));
            g.setOpacity (alpha);
            const bool turning = i == paramDrag, hot = turning || (i == paramHover && over);
            drawKnobBody (g, ctr, kr, a0 + (a1 - a0) * p, live, hot);
            g.setOpacity (1.0f);
            g.setColour (hot ? text : muted.withMultipliedAlpha (live ? 0.85f : 0.4f));
            g.setFont (font (7.4f, true).withExtraKerningFactor (0.06f));
            g.drawFittedText (shortLabel (b->getParamName (i)), juce::roundToInt (ctr.x - slotW * 0.5f) + 1, juce::roundToInt (ctr.y + kr + 4.0f),
                              juce::roundToInt (slotW) - 2, 10, juce::Justification::centred, 1, 0.65f);
        }
    }
    if (shape == Form::Pedal && knobsShown.empty() && ! hasKnob)
    {
        // nothing to show as knobs (a hosted plug-in, a tuner): a quiet row of vents instead
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        const int vents = 9;
        const float pitch = (r.getWidth() - 26.0f) / (float) vents;
        for (int i = 0; i < vents; ++i)
            g.fillRoundedRectangle (r.getX() + 13.0f + (float) i * pitch + (pitch - 7.0f) * 0.5f, r.getY() + 36.0f, 7.0f, 26.0f, 3.5f);
    }

    juce::String detail = node.getDetail();
    if (auto* mb = dynamic_cast<const Block*> (&node); mb != nullptr && mb->currentModel() != nullptr)
        detail = mb->currentModel()->name.upToFirstOccurrenceOf (" (", false, false);
    if (hasKnob)
    {
        // the capture selector: a pointer knob with its scale lit up to where it is, and "3/12" under it
        const int n = sel.count;
        const int at = juce::jlimit (0, n - 1, juce::roundToInt (sel.pos));
        const auto k = knobArea();
        const bool overKnob = over && k.expanded (4.0f).contains (getMouseXYRelative().toFloat());
        const auto kc = gearColour (sel.gear).withMultipliedAlpha (alpha);
        const float pos = a0 + (a1 - a0) * sel.pos / (float) juce::jmax (1, n - 1);
        const auto ctr = k.getCentre();
        const float rad = k.getWidth() * 0.5f;

        juce::Path track, value;
        track.addCentredArc (ctr.x, ctr.y, rad - 1.2f, rad - 1.2f, 0.0f, a0, a1, true);
        value.addCentredArc (ctr.x, ctr.y, rad - 1.2f, rad - 1.2f, 0.0f, a0, pos, true);
        g.setColour (juce::Colour (0xff2a2a31));
        g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (overKnob || knobDragging ? kc.brighter (0.25f) : kc);
        g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawKnobBody (g, ctr, rad - 4.5f, pos, live, overKnob || knobDragging);

        g.setColour (muted.withMultipliedAlpha (alpha));
        g.setFont (mono (8.5f));
        g.drawText (juce::String (at + 1) + "/" + juce::String (n), k.withY (k.getBottom()).withHeight (10.0f).expanded (10.0f, 0.0f),
                    juce::Justification::centred);

        const int a = juce::jlimit (0, n - 1, (int) std::floor (sel.pos));
        const float f = sel.pos - (float) a;
        if (sel.sweep && f > 0.02f && f < 0.98f && a + 1 < n)
            detail = sel.labels[a] + " > " + sel.labels[a + 1];
        else
        {
            detail = sel.labels[at];
            if (sel.tones[at].isNotEmpty()) detail << "  -  " << sel.tones[at];
        }
    }

    // its name: built-in effects wear theirs like a pedal does; captures and plug-ins keep theirs as written
    const bool wordmark = dynamic_cast<Block*> (&node) != nullptr && dynamic_cast<PluginBlock*> (&node) == nullptr
                          && node.getType() != NodeType::Nam && node.getType() != NodeType::Ir;
    juce::Rectangle<int> nameArea, detailArea;
    if (shape == Form::Head)      { nameArea = face.toNearestInt().reduced (10, 6).withTrimmedBottom (15); detailArea = face.toNearestInt().reduced (10, 0).removeFromBottom (22).withTrimmedBottom (7); }
    else if (shape == Form::Cab)  { nameArea = { X + 10, Y + 114, RW - 20, 17 }; detailArea = { X + 10, Y + 131, RW - 20, 13 }; }
    else                          { nameArea = { X + 9, Y + 80, RW - 18, 36 }; detailArea = { X + 9, Y + 118, RW - 18, 14 }; }

    g.setColour (text.withMultipliedAlpha (alpha));
    if (wordmark)
    {
        g.setFont (display (shape == Form::Cab ? 10.0f : 11.5f, 0.1f));
        g.drawFittedText (node.getName().toUpperCase(), nameArea, juce::Justification::centred, shape == Form::Cab ? 1 : 2, 0.75f);
    }
    else
    {
        g.setFont (font (shape == Form::Head ? 14.5f : 13.0f, true));
        g.drawFittedText (node.getName(), nameArea, juce::Justification::centred, shape == Form::Cab ? 1 : 2, 0.8f);
    }

    auto detailColour = muted.withMultipliedAlpha (live ? 1.0f : 0.6f);
    if (auto* tb = dynamic_cast<TunerBlock*> (&node))
    {
        const auto& rd = tb->getReading();   // keeps the card's note live
        if (rd.valid) detailColour = std::abs (rd.cents) < 3.0f ? teal : (std::abs (rd.cents) < 15.0f ? accent : red);
    }
    // while a knob on the card is under the mouse or being turned, the line reads its value
    if (auto* vb = dynamic_cast<const Block*> (&node); vb != nullptr && (paramDrag >= 0 || (paramHover >= 0 && over)))
    {
        const int pi = paramDrag >= 0 ? paramDrag : paramHover;
        detail = vb->getParamName (pi) + "  " + vb->formatValue (pi, vb->getBase (pi));
        detailColour = text;
    }
    g.setColour (detailColour);
    g.setFont (font (10.0f, node.getType() == NodeType::Tuner));
    g.drawFittedText (detail, detailArea, juce::Justification::centred, 1, 0.75f);

    // footswitch (lit round its edge when the block is on)
    drawFootswitch (g, footswitchArea(), c, live);

    // dice: swap for a random block of the same kind. Quiet until the mouse is near.
    if (hasDice())
    {
        const auto d = diceArea();
        const bool overDice = over && d.expanded (3.0f).contains (getMouseXYRelative().toFloat());
        if (overDice)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (d.expanded (3.0f), 6.0f);
        }
        drawIcon (g, Icon::Dice, d, overDice ? text : (over || selected ? muted : dim.withAlpha (0.4f)));

        const auto l = lockArea();
        const bool locked = node.locked.load();
        const bool overLock = over && l.expanded (3.0f).contains (getMouseXYRelative().toFloat());
        if (overLock)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (l.expanded (3.0f), 6.0f);
        }
        drawIcon (g, locked ? Icon::Lock : Icon::Unlock, l,
                  locked ? accent : overLock ? text : dim.withAlpha (over || selected ? 0.8f : 0.3f));
    }

    // the signal line: how much is leaving this block right now
    {
        const auto line = juce::Rectangle<float> (r.getX() + 12.0f, r.getBottom() - 7.0f, r.getWidth() - 24.0f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (line, 1.0f);
        const float db = gainToDb (node.flow.load (std::memory_order_relaxed));
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (frac > 0.01f)
        {
            // from the middle outwards, like light under the pedal
            g.setColour ((db > -1.0f ? red : c).withAlpha (live ? 0.9f : 0.35f));
            g.fillRoundedRectangle (line.withSizeKeepingCentre (line.getWidth() * frac, line.getHeight()), 1.0f);
        }
    }

    if (dropLabel.isNotEmpty())
    {
        auto lr = getLocalBounds().toFloat().reduced (10.0f).withSizeKeepingCentre (W - 20.0f, 28.0f);
        g.setColour (bg.withAlpha (0.92f));
        g.fillRoundedRectangle (lr, 8.0f);
        g.setColour (teal);
        g.setFont (font (12.0f, true));
        g.drawFittedText (dropLabel, lr.toNearestInt().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void BlockCard::mouseDown (const juce::MouseEvent& e)
{
    pressedSwitch = false;
    knobDragging = false;
    if (! e.mods.isPopupMenu())
        if (const auto sel = selector(); sel.valid() && knobArea().expanded (4.0f).contains (e.position))
        {
            pressedSwitch = true;   // not a block drag
            knobDragging = true;
            knobStart = sel.pos;
            knobMoved = 0;
            host.select (node.id);
            repaint();
            return;
        }
    if (e.mods.isPopupMenu())
    {
        host.select (node.id);
        host.showNodeMenu (node, this);
        return;
    }
    if (const int kp = knobParamAt (e.position); kp >= 0)
    {
        // turn the knob right here on the rig, like reaching down to the pedal
        auto& b = dynamic_cast<Block&> (node);
        pressedSwitch = true;
        paramDrag = kp;
        paramStart = b.getSpecs()[(size_t) kp].toProportion (b.getBase (kp));
        host.getEngine().checkpoint();
        host.select (node.id);
        repaint();
        return;
    }
    if (hasDice() && lockArea().expanded (3.0f).contains (e.position))
    {
        pressedSwitch = true;   // not a drag
        host.toggleLock (node);
        repaint();
        return;
    }
    if (hasDice() && diceArea().expanded (3.0f).contains (e.position))
    {
        pressedSwitch = true;   // not a drag
        host.select (node.id);
        host.randomise (node, GraphHost::RandomType);
        return;
    }
    if (footswitchArea().expanded (6.0f).contains (e.position))
    {
        pressedSwitch = true;
        node.bypassed = ! node.bypassed.load();
        host.getEngine().storeScene();
        host.getEngine().markDirty();
        repaint();
        return;
    }
    host.select (node.id);
}

void BlockCard::mouseDrag (const juce::MouseEvent& e)
{
    if (paramDrag >= 0)
    {
        auto& b = dynamic_cast<Block&> (node);
        const auto& spec = b.getSpecs()[(size_t) paramDrag];
        const float travel = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / (e.mods.isShiftDown() ? 700.0f : 160.0f);
        float v = spec.fromProportion (juce::jlimit (0.0f, 1.0f, paramStart + travel));
        if (spec.decimals == 0) v = std::round (v);
        b.set (paramDrag, v);
        host.getEngine().markDirty();
        repaint();
        return;
    }
    if (knobDragging)
    {
        // up / right = next capture: a sweep glides (about 40 px a capture), IRs step every 12 px
        const auto sel = selector();
        if (! sel.valid()) return;
        const int dist = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
        knobMoved = juce::jmax (knobMoved, std::abs (e.getDistanceFromDragStartX()) + std::abs (e.getDistanceFromDragStartY()));
        setSelector (sel, knobStart + (float) dist / (e.mods.isShiftDown() ? 160.0f : 40.0f));
        return;
    }
    if (pressedSwitch || dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        dnd->startDragging ("node:" + node.id, this);
    }
}

void BlockCard::mouseUp (const juce::MouseEvent&)
{
    const bool clickedKnob = knobDragging && knobMoved < 4;
    dragging = false; pressedSwitch = false; knobDragging = false; paramDrag = -1;
    if (clickedKnob) showCaptureMenu();
    repaint();
}

void BlockCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int kp = knobParamAt (e.position); kp >= 0)
    {
        // back to where the knob starts
        auto& b = dynamic_cast<Block&> (node);
        host.getEngine().checkpoint();
        b.set (kp, b.getSpecs()[(size_t) kp].def);
        host.getEngine().markDirty();
        repaint();
        return;
    }
    if (! footswitchArea().expanded (6.0f).contains (e.position) && ! (hasDice() && diceArea().expanded (3.0f).contains (e.position))
        && ! (hasDice() && lockArea().expanded (3.0f).contains (e.position))
        && ! (selector().valid() && knobArea().expanded (4.0f).contains (e.position)))
        host.openNode (node);
}

static constexpr int termW = 78, termH = BlockCard::cardHeight;

ChainView* BlockCard::parentChain() const { return dynamic_cast<ChainView*> (getParentComponent()); }

int BlockCard::dropZone (juce::Point<int> p) const
{
    if (isOverKnob (p)) return 0;   // the capture knob takes drops itself (it sits near the edge)
    const int edge = getWidth() / 5;
    return p.x < edge ? -1 : (p.x > getWidth() - edge ? 1 : 0);
}

bool BlockCard::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    if (CaptureDrop::isCaptureDescription (s)) return true;
    return s.startsWith ("plugin:") || s.startsWith ("node:") || s.startsWith ("builtin:");
}

void BlockCard::itemDragMove (const SourceDetails& d)
{
    const auto s = d.description.toString();
    const int zone = dropZone (d.localPosition);
    auto* cv = parentChain();
    juce::String label;

    if (zone == 0)
    {
        if (s.startsWith ("node:"))
        {
            const auto otherId = s.fromFirstOccurrenceOf ("node:", false, false);
            label = otherId == node.id ? juce::String() : "Swap places";
        }
        else if (CaptureDrop::isCaptureDescription (s))
        {
            const auto drop = CaptureDrop::fromDescription (s);
            const auto mine = drop.forBlock (node);
            auto* sw = dynamic_cast<Sweepable*> (&node);
            if (mine.isEmpty()) label = "Replace";
            else if (sw != nullptr && sw->getNumCaptures() > 0 && knobArea().expanded (6.0f).contains (d.localPosition.toFloat()))
                label = "Add to the sweep";
            else if (sw != nullptr && sw->getNumCaptures() > 0 && ! drop.folder)
                label = "Replace or add?";
            else
                label = mine.size() > 1 ? "Load " + juce::String (mine.size()) + " captures" : juce::String ("Load here");
        }
        else
            label = "Replace";
        if (cv != nullptr) cv->setDropIndex (-1);
    }
    else if (cv != nullptr)
    {
        const int idx = cv->indexOfItem (this);
        cv->setDropIndex (zone < 0 ? idx : idx + 1);
    }

    if (label != dropLabel) { dropLabel = label; repaint(); }
}

void BlockCard::itemDragExit (const SourceDetails&)
{
    dropLabel.clear();
    if (auto* cv = parentChain()) cv->setDropIndex (-1);
    repaint();
}

void BlockCard::itemDropped (const SourceDetails& d)
{
    const auto s = d.description.toString();
    const int zone = dropZone (d.localPosition);
    dropLabel.clear();
    repaint();
    auto* cv = parentChain();
    if (cv != nullptr) cv->setDropIndex (-1);

    if (zone != 0 && cv != nullptr)
    {
        const int idx = cv->indexOfItem (this);
        cv->dropAt (s, zone < 0 ? idx : idx + 1);
        return;
    }
    if (CaptureDrop::isCaptureDescription (s))
    {
        // onto the knob: straight into the sweep; onto the block: replace or add (folders replace)
        const auto drop = CaptureDrop::fromDescription (s);
        auto* sw = dynamic_cast<Sweepable*> (&node);
        const bool onKnob = sw != nullptr && sw->getNumCaptures() > 0 && knobArea().expanded (6.0f).contains (d.localPosition.toFloat());
        host.dropCaptures (&node, nullptr, -1, drop, onKnob ? GraphHost::DropMode::Add : GraphHost::DropMode::Ask);
        return;
    }
    if (s == "node:" + node.id) return;
    host.replaceWith (node, s);
}

//==============================================================================
AddButton::AddButton (GraphHost& h, Chain& c, int i, bool small) : host (h), chain (c), index (i), compact (small)
{
    setTitle ("Add block");
    setTooltip (compact ? "Add a block here" : "Add a block, split or plug-in");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void AddButton::paint (juce::Graphics& g)
{
    const bool over = isMouseOver();
    const auto c = getLocalBounds().toFloat().getCentre();
    if (compact)
    {
        // a quiet node on the wire that wakes up when you point at it
        const float rad = over ? 9.5f : 4.0f;
        const auto r = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
        if (over)
        {
            g.setColour (accent.withAlpha (0.18f));
            g.fillEllipse (r.expanded (3.0f));
        }
        g.setColour (over ? cardHi : bg);
        g.fillEllipse (r);
        g.setColour (over ? accent : borderHi);
        g.drawEllipse (r.reduced (0.5f), over ? 1.4f : 1.0f);
        if (over)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (c.x - 4.5f, c.y - 0.8f, 9.0f, 1.6f, 0.8f);
            g.fillRoundedRectangle (c.x - 0.8f, c.y - 4.5f, 1.6f, 9.0f, 0.8f);
        }
        return;
    }

    const auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (over) { g.setColour (accent.withAlpha (0.14f)); g.fillEllipse (r.expanded (2.5f)); }
    g.setColour (over ? cardHi : card.darker (0.2f));
    g.fillEllipse (r);
    g.setColour (over ? accent : borderHi);
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setColour (over ? accent : muted);
    g.fillRoundedRectangle (c.x - 5.5f, c.y - 0.8f, 11.0f, 1.6f, 0.8f);
    g.fillRoundedRectangle (c.x - 0.8f, c.y - 5.5f, 1.6f, 11.0f, 0.8f);
}

void AddButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        host.showAddMenu (chain, index, this);
}

//==============================================================================
ChainView::ChainView (GraphHost& h, Chain& c, juce::Colour wire, std::function<float()> lvl)
    : host (h), chain (c), wireColour (wire), level (std::move (lvl))
{
    for (auto& n : chain.nodes)
    {
        GraphItem* item = nullptr;
        if (auto* s = dynamic_cast<Split*> (n.get())) item = new SplitView (host, *s);
        else item = new BlockCard (host, *n);
        items.add (item);
        addAndMakeVisible (item);
    }
    for (int i = 0; i < items.size(); ++i)
        addAndMakeVisible (gapButtons.add (new AddButton (host, chain, i, true)));
    addButton = std::make_unique<AddButton> (host, chain, (int) chain.nodes.size());
    addAndMakeVisible (*addButton);
}

juce::Point<int> ChainView::preferredSize() const
{
    int w = gap, h = 56;
    for (auto* it : items)
    {
        const auto p = it->preferredSize();
        w += p.x + gap;
        h = juce::jmax (h, p.y);
    }
    w += 34 + gap / 2;
    return { w, h };
}

void ChainView::resized()
{
    int x = gap;
    for (int i = 0; i < items.size(); ++i)
    {
        auto* it = items[i];
        const auto p = it->preferredSize();
        // the small + sits on the wire just before the arrowhead
        gapButtons[i]->setBounds (x - gap / 2 - miniSize / 2 - 4, getHeight() / 2 - miniSize / 2, miniSize, miniSize);
        it->setBounds (x, (getHeight() - p.y) / 2, p.x, p.y);
        x += p.x + gap;
    }
    addButton->setBounds (x, getHeight() / 2 - 17, 34, 34);
}

void ChainView::paint (juce::Graphics& g)
{
    const float y = (float) getHeight() * 0.5f;
    const float lv = level ? level() : 1.0f;
    float x = 0;
    for (auto* it : items)
    {
        drawWire (g, x, (float) it->getX(), y, wireColour, lv, true);
        x = (float) it->getRight();
    }
    drawWire (g, x, (float) addButton->getX() + 2.0f, y, wireColour, lv, false);
    drawWire (g, (float) addButton->getRight() - 2.0f, (float) getWidth(), y, wireColour, lv, false);
}

void ChainView::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0) return;
    int x = gap / 2;
    if (dropIndex < items.size()) x = items[dropIndex]->getX() - gap / 2;
    else if (! items.isEmpty()) x = items.getLast()->getRight() + gap / 2;
    g.setColour (accent);
    g.fillRoundedRectangle ((float) x - 2.0f, 6.0f, 4.0f, (float) getHeight() - 12.0f, 2.0f);
}

int ChainView::indexForX (int x) const
{
    for (int i = 0; i < items.size(); ++i)
        if (x < items[i]->getBounds().getCentreX())
            return i;
    return items.size();
}

bool ChainView::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    if (s.startsWith ("node:"))
        return host.getEngine().canDropInto (s.fromFirstOccurrenceOf ("node:", false, false), chain);
    return CaptureDrop::isCaptureDescription (s) || s.startsWith ("plugin:") || s.startsWith ("builtin:");
}

void ChainView::itemDragMove (const SourceDetails& d)
{
    const int idx = indexForX (d.localPosition.x);
    if (idx != dropIndex) { dropIndex = idx; repaint(); }
}

void ChainView::itemDragExit (const SourceDetails&)
{
    dropIndex = -1;
    repaint();
}

void ChainView::itemDropped (const SourceDetails& d)
{
    const int idx = indexForX (d.localPosition.x);
    dropIndex = -1;
    repaint();
    dropAt (d.description.toString(), idx);
}

void ChainView::dropAt (const juce::String& s, int idx)
{
    if (s.startsWith ("node:"))
        host.getEngine().move (s.fromFirstOccurrenceOf ("node:", false, false), chain, idx);
    else if (CaptureDrop::isCaptureDescription (s))
        host.dropCaptures (nullptr, &chain, idx, CaptureDrop::fromDescription (s), GraphHost::DropMode::Replace);
    else if (s.startsWith ("plugin:"))
        host.insertPlugin (s.fromFirstOccurrenceOf ("plugin:", false, false), chain, idx);
    else if (s.startsWith ("builtin:"))
        host.insertBuiltIn (nodeTypeFromString (s.fromFirstOccurrenceOf ("builtin:", false, false)), chain, idx);
}

//==============================================================================
struct SplitView::Hub : public juce::Component
{
    Hub (GraphHost& h, Split& s) : host (h), split (s) { setRepaintsOnMouseActivity (true); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (4.0f, 3.0f).withTrimmedBottom (4.0f);
        const bool selected = host.getSelectedId() == split.id;
        const bool on = ! split.bypassed.load();
        drawShadow (g, r, 9.0f, 5.0f, 0.55f);
        drawPlate (g, r, 9.0f, isMouseOver() ? borderHi : border, selected ? 0.05f : 0.0f);
        if (selected)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.drawRoundedRectangle (r.expanded (1.5f), 10.5f, 3.0f);
            g.setColour (accent.withAlpha (0.95f));
            g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.3f);
        }

        // splitter glyph
        juce::Path p;
        const float cx = 22.0f, cy = 26.0f;
        p.startNewSubPath (cx - 10, cy);
        p.lineTo (cx, cy);
        p.startNewSubPath (cx, cy); p.lineTo (cx + 12, cy - 8);
        p.startNewSubPath (cx, cy); p.lineTo (cx + 12, cy);
        p.startNewSubPath (cx, cy); p.lineTo (cx + 12, cy + 8);
        g.setColour (on ? accent : dim);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (text.withMultipliedAlpha (on ? 1.0f : 0.5f));
        g.setFont (font (13.5f, true));
        g.drawText ("Split", 42, 16, getWidth() - 48, 20, juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (font (10.5f));
        g.drawText (splitModeName (split.getMode()), 12, 44, getWidth() - 20, 16, juce::Justification::centredLeft);
        g.drawText (on ? juce::String ((int) split.branches.size()) + " paths" : "Bypassed",
                    12, 62, getWidth() - 20, 16, juce::Justification::centredLeft);
        if (split.getMode() == SplitMode::Crossover)
        {
            g.setFont (mono (10.0f));
            g.drawText (juce::String ((int) split.xoverLow.load()) + " / " + juce::String ((int) split.xoverHigh.load()) + " Hz",
                        12, 80, getWidth() - 20, 14, juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        host.select (split.id);
        if (e.mods.isPopupMenu()) host.showNodeMenu (split, this);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6) return;
        if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            dnd->startDragging ("node:" + split.id, this);
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    GraphHost& host;
    Split& split;
    bool dragging = false;
};

struct SplitView::Pill : public juce::Component,
                         public juce::SettableTooltipClient
{
    Pill (GraphHost& h, Split& s, int i) : host (h), split (s), index (i)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("Drag up/down to set this path's level. Double-click to mute. Right-click for more.");
    }

    void paint (juce::Graphics& g) override
    {
        if (index >= (int) split.branches.size()) return;
        auto& b = *split.branches[(size_t) index];
        const auto c = b.mute.load() ? dim : branchColour (index);
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff0c0c0e));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (c.withAlpha (isMouseOver() ? 0.22f : 0.12f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (c.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.2f);
        g.setColour (b.mute.load() ? muted : c.brighter (0.25f));
        g.setFont (mono (10.5f));
        juce::String label = juce::String::charToString ((juce::juce_wchar) ('A' + index)) + " ";
        if (b.mute.load()) label << "M";
        else label << juce::roundToInt (b.level.load() * 100.0f) << "%";
        if (b.phase.load()) label << " " << juce::String::charToString ((juce::juce_wchar) 0x00d8);
        g.drawText (label, getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        host.select (split.id);
        if (e.mods.isPopupMenu()) { host.showBranchMenu (split, index, this); return; }
        startLevel = split.branches[(size_t) index]->level.load();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const float v = startLevel - (float) e.getDistanceFromDragStartY() / 160.0f;
        split.setLevel (index, v, true);
        host.getEngine().storeScene();
        host.getEngine().markDirty();
        if (auto* p = getParentComponent()) p->repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        auto& b = *split.branches[(size_t) index];
        b.mute = ! b.mute.load();
        host.getEngine().storeScene();
        host.getEngine().markDirty();
        if (auto* p = getParentComponent()) p->repaint();
    }

    GraphHost& host;
    Split& split;
    int index;
    float startLevel = 0;
};

struct SplitView::Merge : public juce::Component
{
    explicit Merge (Split& s) : split (s) {}
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (r.translated (0.0f, 2.0f));
        juce::ColourGradient grad (card.brighter (0.08f), 0.0f, r.getY(), card.darker (0.3f), 0.0f, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (r);
        g.setColour (borderHi);
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        g.setColour (split.bypassed.load() ? dim : muted);
        g.setFont (font (13.0f, true));
        g.drawText (juce::String::charToString ((juce::juce_wchar) 0x03a3), getLocalBounds(), juce::Justification::centred);
    }
    Split& split;
};

SplitView::SplitView (GraphHost& h, Split& s) : host (h), split (s)
{
    hub = std::make_unique<Hub> (host, split);
    merge = std::make_unique<Merge> (split);
    addAndMakeVisible (*hub);
    addAndMakeVisible (*merge);

    for (int i = 0; i < (int) split.branches.size(); ++i)
    {
        auto* sp = &split;
        auto* row = new ChainView (host, *split.branches[(size_t) i]->chain, branchColour (i), [sp, i]
        {
            if (i >= (int) sp->branches.size()) return 0.0f;
            auto& b = *sp->branches[(size_t) i];
            return b.mute.load() || sp->bypassed.load() ? 0.0f : b.level.load();
        });
        rows.add (row);
        addAndMakeVisible (row);
        auto* pill = new Pill (host, split, i);
        pills.add (pill);
        addAndMakeVisible (pill);
    }

    addPath.setTooltip ("Add another path to this split");
    addPath.onClick = [this] { host.getEngine().addBranch (split); };
    addAndMakeVisible (addPath);
}

juce::Point<int> SplitView::preferredSize() const
{
    int contentW = 120, rowsH = 0;
    for (auto* r : rows)
    {
        const auto p = r->preferredSize();
        contentW = juce::jmax (contentW, p.x);
        rowsH += p.y;
    }
    rowsH += rowGap * juce::jmax (0, rows.size() - 1);
    const int h = juce::jmax (112, rowsH + 34);
    const int w = hubW + margin + pillW + 6 + contentW + margin + mergeW;
    return { w, h };
}

void SplitView::resized()
{
    int contentW = 120, rowsH = 0;
    for (auto* r : rows)
    {
        const auto p = r->preferredSize();
        contentW = juce::jmax (contentW, p.x);
        rowsH += p.y;
    }
    rowsH += rowGap * juce::jmax (0, rows.size() - 1);
    const int top = juce::jmax (0, (getHeight() - 34 - rowsH) / 2);
    const int midY = top + rowsH / 2;

    hub->setBounds (0, midY - 58, hubW, 116);
    merge->setBounds (getWidth() - mergeW, midY - mergeW / 2, mergeW, mergeW);

    int y = top;
    const int pillX = hubW + margin;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto p = rows[i]->preferredSize();
        pills[i]->setBounds (pillX, y + p.y / 2 - 13, pillW, 26);
        rows[i]->setBounds (pillX + pillW + 6, y, contentW, p.y);
        y += p.y + rowGap;
    }
    addPath.setBounds (pillX, top + rowsH + 8, 72, 24);
}

void SplitView::paint (juce::Graphics& g)
{
    const auto from = juce::Point<float> ((float) hub->getRight(), (float) hub->getBounds().getCentreY());
    const auto to = juce::Point<float> ((float) merge->getX(), (float) merge->getBounds().getCentreY());
    const bool off = split.bypassed.load();

    for (int i = 0; i < rows.size() && i < (int) split.branches.size(); ++i)
    {
        auto& b = *split.branches[(size_t) i];
        const bool muted = b.mute.load() || off;
        const float lv = b.level.load();
        const auto c = branchColour (i);
        const float py = (float) pills[i]->getBounds().getCentreY();
        const int in = b.input.load(), out = b.output.load();
        // a path with its own input or output: a tag where the wire would have been
        auto tag = [&g, c, muted] (juce::Rectangle<float> r, const juce::String& text)
        {
            g.setColour (juce::Colour (0xff0c0c0e));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (c.withAlpha (muted ? 0.35f : 0.9f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            g.setFont (mono (9.5f));
            g.drawText (text, r, juce::Justification::centred);
        };
        if (in == 0) drawCurve (g, from, { (float) pills[i]->getX(), py }, c, lv, muted);
        else
        {
            const auto label = pathInputNames()[juce::jlimit (0, 12, in)].replace ("Inputs ", "IN ").replace ("Input ", "IN ");
            tag ({ (float) pills[i]->getX(), py - 30.0f, (float) pills[i]->getWidth(), 14.0f }, label);
        }
        drawWire (g, (float) pills[i]->getRight(), (float) rows[i]->getX(), py, c, muted ? 0.0f : lv, false);
        if (out == 0 || out >= 4) drawCurve (g, { (float) rows[i]->getRight(), py }, to, c, lv, muted);
        if (out > 0)
        {
            const auto label = juce::String ("OUT ") + pathOutputNames()[juce::jlimit (0, 6, out)].fromFirstOccurrenceOf ("Out ", false, false).upToFirstOccurrenceOf (" ", false, false);
            tag ({ (float) rows[i]->getRight() + 4.0f, py - (out >= 4 ? 22.0f : 7.0f), 50.0f, 14.0f }, label);
        }
    }
}

//==============================================================================
GraphView::GraphView (GraphHost& h) : host (h) { rebuild(); }
GraphView::~GraphView() = default;

void GraphView::rebuild()
{
    main.reset();
    main = std::make_unique<ChainView> (host, host.getEngine().getRoot(), muted, [] { return 1.0f; });
    addAndMakeVisible (*main);
    fitTo (viewArea);
}

juce::Point<int> GraphView::contentSize() const
{
    const auto p = main->preferredSize();
    return { 24 + termW + p.x + termW + 24, p.y + 48 };
}

void GraphView::fitTo (juce::Rectangle<int> area)
{
    viewArea = area;
    const auto p = main->preferredSize();
    const int w = juce::jmax (area.getWidth(), 24 + termW + p.x + termW + 24);
    const int h = juce::jmax (area.getHeight(), p.y + 48);
    setSize (w, h);
    resized();
}

void GraphView::resized()
{
    const auto p = main->preferredSize();
    const int cy = getHeight() / 2;
    const int contentW = termW + p.x + termW;
    const int x0 = juce::jmax (24, (getWidth() - contentW) / 2);
    inBox = { x0, cy - termH / 2, termW, termH };
    main->setBounds (inBox.getRight(), cy - p.y / 2, p.x, p.y);
    outBox = { main->getRight(), cy - termH / 2, termW, termH };
}

void GraphView::paint (juce::Graphics& g)
{
    // a dark stage with a little light falling on the middle of it
    g.fillAll (bg);
    {
        const auto area = getLocalBounds().toFloat();
        juce::ColourGradient light (juce::Colour (0xff15151a), area.getCentreX(), area.getCentreY() - area.getHeight() * 0.15f,
                                    bg, area.getCentreX(), area.getCentreY() + juce::jmax (area.getHeight(), area.getWidth() * 0.45f), true);
        g.setGradientFill (light);
        g.fillAll();
    }
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    const auto clip = g.getClipBounds();
    for (int y = 12 + ((clip.getY() - 12) / 24) * 24; y < clip.getBottom(); y += 24)
        for (int x = 12 + ((clip.getX() - 12) / 24) * 24; x < clip.getRight(); x += 24)
            g.fillRect (x, y, 1, 1);

    auto meterBar = [&] (juce::Rectangle<float> m, float lvl, juce::Colour c)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (m, 2.0f);
        const float db = gainToDb (lvl);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (frac <= 0.0f) return;
        auto fill = m.withTop (m.getBottom() - m.getHeight() * frac);
        juce::ColourGradient grad (c, 0, m.getBottom(), red, 0, m.getY(), false);
        grad.addColour (0.86, c);
        grad.addColour (0.95, red.interpolatedWith (c, 0.3f));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);
    };

    auto drawTerminal = [&] (juce::Rectangle<int> box, bool isOut)
    {
        const auto& id = isOut ? kOutputId : kInputId;
        const bool sel = host.getSelectedId() == id;
        const bool over = hoverTerminal == (isOut ? 2 : 1);
        const auto c = isOut ? accent : text.withAlpha (0.85f);
        const auto plate = box.toFloat().reduced (4.0f, 3.0f).withTrimmedBottom (4.0f);
        drawShadow (g, plate, 9.0f, 5.0f, 0.55f);
        drawPlate (g, plate, 9.0f, over ? borderHi : border, sel ? 0.05f : 0.0f);
        if (sel)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.drawRoundedRectangle (plate.expanded (1.5f), 10.5f, 3.0f);
            g.setColour (accent.withAlpha (0.95f));
            g.drawRoundedRectangle (plate.reduced (0.5f), 9.0f, 1.3f);
        }
        auto r = plate.toNearestInt();

        g.setColour (sel || over ? text : muted);
        g.setFont (font (10.0f, true).withExtraKerningFactor (0.16f));
        g.drawText (isOut ? "OUT" : "IN", r.removeFromTop (26).translated (1, 1), juce::Justification::centred);

        auto bottom = r.removeFromBottom (36);
        auto meters = r.reduced (0, 2).withSizeKeepingCentre (15, r.getHeight() - 4).toFloat();
        const float l = isOut ? levels.outL : levels.inL, rr = isOut ? levels.outR : levels.inR;
        meterBar (meters.removeFromLeft (5.0f), l, c);
        meters.removeFromLeft (5.0f);
        meterBar (meters, rr, c);

        // gain + (for OUT) limiter readouts
        const float gainDb = isOut ? host.getEngine().outputGainDb.load() : host.getEngine().inputGainDb.load();
        g.setFont (mono (10.0f));
        g.setColour (muted);
        const float pk = juce::jmax (l, rr);
        bottom.removeFromTop (3);
        g.drawText (pk < 1.0e-5f ? juce::String ("-inf") : juce::String (gainToDb (pk), 1), bottom.removeFromTop (14), juce::Justification::centred);
        juce::String sub = (gainDb > 0.05f ? "+" : "") + juce::String (gainDb, 1) + " dB";
        auto subColour = dim;
        if (isOut && host.getEngine().limiterOn.load() && levels.grDb < -0.2f)
        {
            sub = "LIM " + juce::String (levels.grDb, 1);
            subColour = accent;
        }
        g.setColour (subColour);
        g.setFont (mono (9.5f));
        g.drawText (sub, bottom.removeFromTop (13), juce::Justification::centred);
    };
    drawTerminal (inBox, false);
    drawTerminal (outBox, true);
}

void GraphView::setMeters (const Levels& l)
{
    auto differs = [] (float a, float b) { return std::abs (gainToDb (juce::jmax (1.0e-4f, a)) - gainToDb (juce::jmax (1.0e-4f, b))) > 0.4f; };
    const bool in = differs (l.inL, levels.inL) || differs (l.inR, levels.inR);
    const bool out = differs (l.outL, levels.outL) || differs (l.outR, levels.outR) || std::abs (l.grDb - levels.grDb) > 0.1f;
    if (in || out) levels = l;
    if (in) repaint (inBox);
    if (out) repaint (outBox);
}

void GraphView::refreshLive()
{
    // hidden while blocks are being deleted (the cards point at them until the rebuild): hands off
    if (! isVisible()) return;
    // only what has changed is repainted: the cards and splits each know what they're showing
    bool relayout = false;
    std::function<void (juce::Component&)> walk = [&walk, &relayout] (juce::Component& parent)
    {
        for (auto* ch : parent.getChildren())
        {
            if (auto* card = dynamic_cast<BlockCard*> (ch)) { card->refresh(); relayout = relayout || card->needsRelayout(); continue; }
            if (auto* sv = dynamic_cast<SplitView*> (ch)) sv->refresh();
            walk (*ch);
        }
    };
    walk (*this);
    if (relayout) { rebuild(); return; }   // a card's gear changed shape (a pedal capture loaded where an amp was)
    const auto sel = host.getSelectedId();
    const float gains = host.getEngine().inputGainDb.load() + 1000.0f * host.getEngine().outputGainDb.load();
    if (sel != shownSelection || gains != shownGains) { shownSelection = sel; shownGains = gains; repaint (inBox); repaint (outBox); }
    if (++liveTicks % 60 == 0) repaint();   // and everything now and then, in case something was missed
}

juce::uint64 SplitView::signature() const
{
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint64 v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix (host.getSelectedId() == split.id ? 1u : 0u);
    mix (split.bypassed.load() ? 1u : 0u);
    mix ((juce::uint64) split.mode.load());
    mix ((juce::uint64) juce::roundToInt (split.xoverLow.load()) * 100000u + (juce::uint64) juce::roundToInt (split.xoverHigh.load()));
    for (auto& b : split.branches)
    {
        mix ((juce::uint64) juce::roundToInt (b->level.load() * 1000.0f) * 8u + (b->mute.load() ? 1u : 0u) + (b->phase.load() ? 2u : 0u));
        mix ((juce::uint64) (b->input.load() * 16 + b->output.load()));
    }
    return h;
}

void SplitView::refresh()
{
    const auto s = signature();
    if (s != lastSignature) { lastSignature = s; repaint(); }
}

void GraphView::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    if (inBox.contains (e.getPosition()))  { host.select (kInputId); return; }
    if (outBox.contains (e.getPosition())) { host.select (kOutputId); return; }
    if (onBackgroundClick) onBackgroundClick();
}

void GraphView::mouseMove (const juce::MouseEvent& e)
{
    const int h = inBox.contains (e.getPosition()) ? 1 : (outBox.contains (e.getPosition()) ? 2 : 0);
    if (h != hoverTerminal) { hoverTerminal = h; setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void GraphView::dropFiles (const CaptureDrop& drop, juce::Point<int> pt)
{
    if (drop.isEmpty()) return;
    auto* c = getComponentAt (pt);
    while (c != nullptr && c != this)
    {
        if (auto* card = dynamic_cast<BlockCard*> (c))
        {
            const auto local = card->getLocalPoint (this, pt);
            const int zone = card->dropZone (local);
            if (zone == 0)
            {
                auto* sw = dynamic_cast<Sweepable*> (card->getNode());
                const bool onKnob = sw != nullptr && sw->getNumCaptures() > 0 && card->isOverKnob (local);
                host.dropCaptures (card->getNode(), nullptr, -1, drop, onKnob ? GraphHost::DropMode::Add : GraphHost::DropMode::Ask);
                return;
            }
            if (auto* cv = dynamic_cast<ChainView*> (card->getParentComponent()))
            {
                const int idx = cv->indexOfItem (card);
                host.dropCaptures (nullptr, &cv->getChain(), zone < 0 ? idx : idx + 1, drop, GraphHost::DropMode::Replace);
                return;
            }
        }
        if (auto* cv = dynamic_cast<ChainView*> (c))
        {
            host.dropCaptures (nullptr, &cv->getChain(), cv->indexForX (cv->getLocalPoint (this, pt).x), drop, GraphHost::DropMode::Replace);
            return;
        }
        c = c->getParentComponent();
    }
    host.dropCaptures (nullptr, &host.getEngine().getRoot(), -1, drop, GraphHost::DropMode::Replace);
}

} // namespace rig

namespace rig
{
//==============================================================================
RigMap::RigMap (juce::Viewport& v, GraphView& g) : viewport (v), graph (g)
{
    setTooltip ("The whole rig: click or drag to go to any part of it");
    setOpaque (false);
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (12);
}

juce::Rectangle<float> RigMap::mapArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 5.0f);
}

juce::Point<float> RigMap::toMap (juce::Point<float> p) const
{
    const auto a = mapArea();
    const float sx = a.getWidth() / (float) juce::jmax (1, graph.getWidth());
    const float sy = a.getHeight() / (float) juce::jmax (1, graph.getHeight());
    return { a.getX() + p.x * sx, a.getY() + p.y * sy };
}

void RigMap::jumpTo (juce::Point<float> m)
{
    // centre the view on the point clicked
    const auto a = mapArea();
    const float gx = (m.x - a.getX()) / a.getWidth() * (float) graph.getWidth();
    const float gy = (m.y - a.getY()) / a.getHeight() * (float) graph.getHeight();
    viewport.setViewPosition (juce::roundToInt (gx - (float) viewport.getViewWidth() * 0.5f),
                              juce::roundToInt (gy - (float) viewport.getViewHeight() * 0.5f));
    repaint();
}

void RigMap::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    viewport.setViewPosition (viewport.getViewPositionX() - juce::roundToInt (d * 500.0f), viewport.getViewPositionY());
}

void RigMap::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (panel.withAlpha (0.94f));
    g.fillRoundedRectangle (box, 7.0f);
    g.setColour (isMouseOverOrDragging() ? borderHi : border);
    g.drawRoundedRectangle (box, 7.0f, 1.0f);
    const auto a = mapArea();
    if (! graph.isVisible()) return;   // being rebuilt: its cards may point at removed blocks

    auto mapRect = [this] (juce::Rectangle<int> r)
    {
        const auto tl = toMap (r.getTopLeft().toFloat()), br = toMap (r.getBottomRight().toFloat());
        return juce::Rectangle<float> (tl, br);
    };

    // the signal line, then every block in its colour
    const auto in = mapRect (graph.getInBox()), out = mapRect (graph.getOutBox());
    g.setColour (borderHi);
    g.fillRect (juce::Rectangle<float> (in.getRight(), in.getCentreY() - 0.5f, out.getX() - in.getRight(), 1.0f));
    g.setColour (muted.withAlpha (0.6f));
    g.fillRoundedRectangle (in, 2.0f);
    g.fillRoundedRectangle (out, 2.0f);

    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* card = dynamic_cast<BlockCard*> (child))
            {
                auto r = mapRect (graph.getLocalArea (card, card->getLocalBounds())).reduced (0.6f, 0.0f);
                if (r.getWidth() < 2.0f) r = r.withSizeKeepingCentre (2.0f, r.getHeight());
                auto* n = card->getNode();
                const bool on = n != nullptr && ! n->bypassed.load();
                const auto col = n != nullptr ? colourFor (*n) : muted;
                g.setColour (on ? col.withAlpha (0.85f) : col.withAlpha (0.3f));
                g.fillRoundedRectangle (r, 2.0f);
                if (n != nullptr && n->id == graph.getSelectedId())
                {
                    g.setColour (text);
                    g.drawRoundedRectangle (r.expanded (1.0f), 2.0f, 1.2f);
                }
            }
            else visit (*child);
        }
    };
    visit (graph);

    // what's on screen
    auto view = mapRect (viewport.getViewArea()).getIntersection (a.expanded (2.0f));
    g.setColour (accent.withAlpha (0.10f));
    g.fillRoundedRectangle (view, 3.0f);
    g.setColour (accent.withAlpha (0.9f));
    g.drawRoundedRectangle (view, 3.0f, 1.5f);
}

} // namespace rig
