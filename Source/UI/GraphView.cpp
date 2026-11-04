#include "GraphView.h"
#include "Icons.h"
#include "../Engine/Randomiser.h"

namespace rig
{
using namespace theme;

static void drawWire (juce::Graphics& g, float x1, float x2, float y, juce::Colour c, float level, bool arrow)
{
    if (x2 <= x1) return;
    const float th = 1.5f + 4.0f * juce::jlimit (0.0f, 1.0f, level);
    g.setColour (c.withAlpha (0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, level)));
    g.fillRoundedRectangle (x1, y - th * 0.5f, x2 - x1, th, th * 0.5f);
    if (arrow && x2 - x1 > 14)
    {
        juce::Path p;
        p.addTriangle (x2 - 9, y - 5, x2 - 9, y + 5, x2 - 2, y);
        g.fillPath (p);
    }
}

static void drawCurve (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float level, bool muted)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dx = (b.x - a.x) * 0.55f;
    p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    const float th = 1.5f + 4.0f * juce::jlimit (0.0f, 1.0f, level);
    g.setColour (muted ? dim.withAlpha (0.5f) : c.withAlpha (0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, level)));
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
BlockCard::BlockCard (GraphHost& h, Node& n) : host (h), node (n)
{
    setRepaintsOnMouseActivity (true);
}

juce::Rectangle<float> BlockCard::footswitchArea() const
{
    return { 12.0f, (float) getHeight() - 36.0f, 24.0f, 24.0f };
}

juce::Rectangle<float> BlockCard::diceArea() const
{
    return { (float) getWidth() - 58.0f, (float) getHeight() - 35.0f, 22.0f, 22.0f };
}

juce::Rectangle<float> BlockCard::lockArea() const
{
    // always there, right of the dice: dim when unlocked, lit when locked
    return { (float) getWidth() - 31.0f, (float) getHeight() - 34.0f, 20.0f, 20.0f };
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
    Selector sel;
    if (auto* nb = dynamic_cast<NamBlock*> (&node))
    {
        // NAM: the block's own sweep (blends between captures)
        sel.count = nb->getNumCaptures();
        if (sel.count < 2) return sel;
        sel.pos = nb->getCapturePosition();
        const auto names = nb->getCaptureNames();
        sel.names = names;
        sel.labels = shortenCaptureLabels (names);
        for (int i = 0; i < sel.count; ++i) sel.tones.add (toneTypeName (nb->getInfo (i).toneType));
        sel.gear = nb->getGear();
        sel.sweep = true;
        return sel;
    }
    const auto f = loadedFile (node);
    if (f == juce::File()) return sel;
    if (auto g = host.getEngine().captures.groupFor (f); g != nullptr && g->size() > 1)
    {
        sel.count = g->size();
        sel.pos = (float) juce::jmax (0, g->indexOf (f));
        sel.names = g->labels;
        sel.labels = g->shortLabels;
        for (auto& t : g->fileToneTypes) sel.tones.add (toneTypeName (t));
        sel.gear = g->gear;
        sel.group = g;
    }
    return sel;
}

void BlockCard::setSelector (const Selector& sel, float pos)
{
    pos = juce::jlimit (0.0f, (float) (sel.count - 1), pos);
    if (sel.sweep)
    {
        if (auto* nb = dynamic_cast<NamBlock*> (&node))
        {
            nb->setCapturePosition (pos);
            host.getEngine().markDirty();
        }
    }
    else if (sel.group != nullptr)
    {
        const auto f = sel.group->files[juce::roundToInt (pos)];
        if (f != loadedFile (node)) host.switchCapture (node, f);
    }
    repaint();
}

juce::Rectangle<float> BlockCard::knobArea() const
{
    return { (float) getWidth() - 40.0f, 38.0f, 28.0f, 28.0f };
}

void BlockCard::stepCapture (int delta)
{
    const auto sel = selector();
    if (! sel.valid()) return;
    // a sweep moves in quarter steps (so a scroll can find the blends); IRs a whole step
    const float step = sel.sweep ? 0.25f : 1.0f;
    const float target = sel.sweep ? std::round ((sel.pos + step * (float) delta) / step) * step : sel.pos + (float) delta;
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

void BlockCard::mouseMove (const juce::MouseEvent&)
{
    repaint();
}

bool BlockCard::acceptsFile (const juce::File& f) const
{
    return (node.getType() == NodeType::Nam && f.hasFileExtension ("nam"))
        || (node.getType() == NodeType::Ir && isAudioFile (f));
}

void BlockCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = host.getSelectedId() == node.id;
    const bool on = ! node.bypassed.load();
    const bool skipped = on && node.autoSkipped.load();   // e.g. a cab IR after an "Amp + Cab" model
    const auto c = colourFor (node);
    const float alpha = on && ! skipped ? 1.0f : 0.55f;

    g.setColour (selected ? cardHi : (isMouseOver() ? cardHi.darker (0.08f) : card));
    g.fillRoundedRectangle (r, 10.0f);

    if (dropLabel.isNotEmpty()) { g.setColour (teal); g.drawRoundedRectangle (r, 10.0f, 2.5f); }
    else if (selected)      { g.setColour (c); g.drawRoundedRectangle (r, 10.0f, 2.0f); }
    else                    { g.setColour (border); g.drawRoundedRectangle (r, 10.0f, 1.0f); }

    g.setColour (c.withMultipliedAlpha (on && ! skipped ? 1.0f : 0.35f));
    g.fillRoundedRectangle (12.0f, 11.0f, r.getWidth() - 22.0f, 5.0f, 2.5f);
    if (auto* nb = dynamic_cast<NamBlock*> (&node); nb != nullptr && gearIncludesCab (nb->getGear()))
    {
        // amp + cab: the right end of the stripe takes the cab colour
        g.setColour (gearColour (Gear::Cab).withMultipliedAlpha (on ? 1.0f : 0.35f));
        g.fillRoundedRectangle (12.0f + (r.getWidth() - 22.0f) * 0.62f, 11.0f, (r.getWidth() - 22.0f) * 0.38f, 5.0f, 2.5f);
    }

    g.setColour (muted.withMultipliedAlpha (alpha));
    g.setFont (font (10.0f, true).withExtraKerningFactor (0.08f));
    g.drawFittedText (kindLabel (node), 12, 22, getWidth() - 24 - (node.autoGain.load() || node.getLatency() > 0 ? 58 : 0), 14, juce::Justification::centredLeft, 1, 0.8f);

    const auto sel = selector();
    const bool hasKnob = sel.valid();
    g.setColour (text.withMultipliedAlpha (alpha));
    g.setFont (font (14.0f, true));
    g.drawFittedText (node.getName(), 12, 37, getWidth() - 24 - (hasKnob ? 34 : 0), 36, juce::Justification::topLeft, 2, 0.9f);

    juce::String detail = node.getDetail();
    if (hasKnob)
    {
        // the capture selector: a little pointer knob with "3/12" under it
        const int n = sel.count;
        const int at = juce::jlimit (0, n - 1, juce::roundToInt (sel.pos));
        const auto k = knobArea();
        const bool over = isMouseOver() && k.expanded (4.0f).contains (getMouseXYRelative().toFloat());
        const auto kc = gearColour (sel.gear).withMultipliedAlpha (alpha);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float pos = a0 + (a1 - a0) * sel.pos / (float) juce::jmax (1, n - 1);
        const auto ctr = k.getCentre();
        const float rad = k.getWidth() * 0.5f;

        juce::Path track, value;
        track.addCentredArc (ctr.x, ctr.y, rad - 1.5f, rad - 1.5f, 0.0f, a0, a1, true);
        value.addCentredArc (ctr.x, ctr.y, rad - 1.5f, rad - 1.5f, 0.0f, a0, pos, true);
        g.setColour (border.brighter (0.2f));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kc);
        g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto body = k.reduced (5.0f);
        g.setColour (over || knobDragging ? cardHi.brighter (0.15f) : bg.brighter (0.06f));
        g.fillEllipse (body);
        g.setColour (border.brighter (0.3f));
        g.drawEllipse (body, 1.0f);
        g.setColour (over || knobDragging ? kc.brighter (0.3f) : text.withMultipliedAlpha (alpha));
        const auto tip = ctr.getPointOnCircumference (rad - 7.0f, pos);
        const auto from = ctr.getPointOnCircumference (2.0f, pos);
        g.drawLine ({ from, tip }, 2.0f);

        g.setColour (muted.withMultipliedAlpha (alpha));
        g.setFont (mono (9.0f));
        g.drawText (juce::String (at + 1) + "/" + juce::String (n), k.withY (k.getBottom()).withHeight (10.0f).expanded (6.0f, 0.0f),
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
    auto detailColour = muted.withMultipliedAlpha (alpha);
    if (auto* tb = dynamic_cast<TunerBlock*> (&node))
    {
        const auto& rd = tb->getReading();   // keeps the card's note live
        if (rd.valid) detailColour = std::abs (rd.cents) < 3.0f ? teal : (std::abs (rd.cents) < 15.0f ? accent : red);
    }
    g.setColour (detailColour);
    g.setFont (font (11.0f, node.getType() == NodeType::Tuner));
    const bool hasBadge = [this]
    {
        if (auto* nb = dynamic_cast<NamBlock*> (&node)) return nb->getToneInfo().valid;
        if (auto* ir = dynamic_cast<IrBlock*> (&node)) return ir->getToneInfo().valid;
        return false;
    }();
    g.drawFittedText (detail, 12, 72, getWidth() - 24 - (hasKnob || hasBadge ? 34 : 0), 16, juce::Justification::topLeft, 1, 0.85f);

    // footswitch + LED glow
    const auto sw = footswitchArea();
    drawFootswitch (g, sw, c, on);
    g.setColour (on ? text : muted);
    g.setFont (font (11.0f));
    g.drawText (! on ? "Bypassed" : skipped ? "Auto off" : "On", (int) sw.getRight() + 8, (int) sw.getY(), 70, (int) sw.getHeight(), juce::Justification::centredLeft);

    // TONE3000 tones carry a small mark (with the creator in the detail line)
    ToneInfo tone;
    if (auto* nb = dynamic_cast<NamBlock*> (&node)) tone = nb->getToneInfo();
    else if (auto* ir = dynamic_cast<IrBlock*> (&node)) tone = ir->getToneInfo();
    if (tone.valid)
    {
        const juce::Rectangle<float> badge ((float) getWidth() - 42.0f, 75.0f, 30.0f, 14.0f);
        g.setColour (text.withAlpha (0.12f * alpha + 0.05f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (text.withMultipliedAlpha (0.8f * alpha));
        g.setFont (font (9.5f, true));
        g.drawText ("T3K", badge, juce::Justification::centred);
    }

    // latency or gain-compensation readout, top right
    const int readoutRight = getWidth() - 12;
    if (auto lat = node.getLatency(); lat > 0)
    {
        g.setColour (dim);
        g.setFont (mono (10.0f));
        g.drawText (juce::String (lat) + " smp", readoutRight - 60, 22, 60, 14, juce::Justification::centredRight);
    }
    else if (node.autoGain.load())
    {
        g.setColour (teal.withMultipliedAlpha (alpha));
        g.setFont (mono (10.0f));
        const float db = node.autoGainDb.load();
        g.drawText (node.autoGainProgress.load() < 1.0f ? juce::String ("AG ...") : "AG " + juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1),
                    readoutRight - 60, 22, 60, 14, juce::Justification::centredRight);
    }

    // dice: swap for a random block of the same kind
    if (hasDice())
    {
        const auto d = diceArea();
        const bool over = isMouseOver() && d.expanded (3.0f).contains (getMouseXYRelative().toFloat());
        if (over)
        {
            g.setColour (c.withAlpha (0.18f));
            g.fillRoundedRectangle (d.expanded (3.0f), 6.0f);
        }
        drawIcon (g, Icon::Dice, d, over ? c : (isMouseOver() || selected ? muted : dim.withAlpha (0.8f)));
    }

    if (hasDice())
    {
        const auto l = lockArea();
        const bool locked = node.locked.load();
        const bool over = isMouseOver() && l.expanded (3.0f).contains (getMouseXYRelative().toFloat());
        if (over)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.fillRoundedRectangle (l.expanded (3.0f), 6.0f);
        }
        drawIcon (g, locked ? Icon::Lock : Icon::Unlock, l.reduced (1.0f),
                  locked ? accent : over ? accent.withAlpha (0.8f) : dim.withAlpha (isMouseOver() || selected ? 0.8f : 0.5f));
    }

    if (dropLabel.isNotEmpty())
    {
        auto lr = getLocalBounds().toFloat().reduced (10.0f).withSizeKeepingCentre ((float) getWidth() - 20.0f, 28.0f);
        g.setColour (bg.withAlpha (0.9f));
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
    if (knobDragging)
    {
        // up / right = next capture: a sweep glides (about 40 px a capture), IRs step every 12 px
        const auto sel = selector();
        if (! sel.valid()) return;
        const int dist = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
        knobMoved = juce::jmax (knobMoved, std::abs (e.getDistanceFromDragStartX()) + std::abs (e.getDistanceFromDragStartY()));
        if (sel.sweep) setSelector (sel, knobStart + (float) dist / (e.mods.isShiftDown() ? 160.0f : 40.0f));
        else           setSelector (sel, (float) juce::roundToInt (knobStart + (float) (dist / 12)));
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
    dragging = false; pressedSwitch = false; knobDragging = false;
    if (clickedKnob) showCaptureMenu();
    repaint();
}

void BlockCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! footswitchArea().expanded (6.0f).contains (e.position) && ! (hasDice() && diceArea().expanded (3.0f).contains (e.position))
        && ! (hasDice() && lockArea().expanded (3.0f).contains (e.position))
        && ! (selector().valid() && knobArea().expanded (4.0f).contains (e.position)))
        host.openNode (node);
}

static constexpr int termW = 84, termH = 136;

ChainView* BlockCard::parentChain() const { return dynamic_cast<ChainView*> (getParentComponent()); }

int BlockCard::dropZone (juce::Point<int> p) const
{
    const int edge = getWidth() / 5;
    return p.x < edge ? -1 : (p.x > getWidth() - edge ? 1 : 0);
}

bool BlockCard::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    if (s.startsWith ("file:"))
        return juce::File (s.fromFirstOccurrenceOf ("file:", false, false)).hasFileExtension ("nam;wav;aif;aiff;flac");
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
        else if (s.startsWith ("file:"))
            label = acceptsFile (juce::File (s.fromFirstOccurrenceOf ("file:", false, false))) ? "Load here" : "Replace";
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
    if (s.startsWith ("file:"))
    {
        const juce::File f (s.fromFirstOccurrenceOf ("file:", false, false));
        if (acceptsFile (f)) { host.loadFileInto (&node, nullptr, -1, f); return; }
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
    if (compact)
    {
        // a quiet node on the wire that wakes up when you point at it
        const auto c = getLocalBounds().toFloat().getCentre();
        const float rad = over ? 10.0f : 7.0f;
        const auto r = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
        if (over)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (r.expanded (3.0f));
        }
        g.setColour (over ? cardHi : bg);
        g.fillEllipse (r);
        g.setColour (over ? accent : borderHi);
        g.drawEllipse (r.reduced (0.5f), over ? 1.6f : 1.1f);
        const float arm = over ? 5.0f : 3.5f, th = over ? 2.0f : 1.4f;
        g.setColour (over ? accent : muted);
        g.fillRoundedRectangle (c.x - arm, c.y - th * 0.5f, arm * 2.0f, th, th * 0.5f);
        g.fillRoundedRectangle (c.x - th * 0.5f, c.y - arm, th, arm * 2.0f, th * 0.5f);
        return;
    }

    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (over ? cardHi : bg);
    g.fillEllipse (r);
    juce::Path circle;
    circle.addEllipse (r);
    juce::Path dashed;
    const float dashes[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, circle, dashes, 2);
    g.setColour (over ? accent : borderHi);
    g.fillPath (dashed);
    g.setColour (over ? accent : muted);
    const auto c = r.getCentre();
    g.fillRect (c.x - 6.0f, c.y - 1.0f, 12.0f, 2.0f);
    g.fillRect (c.x - 1.0f, c.y - 6.0f, 2.0f, 12.0f);
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
    return s.startsWith ("file:") || s.startsWith ("plugin:") || s.startsWith ("builtin:");
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
    else if (s.startsWith ("file:"))
        host.loadFileInto (nullptr, &chain, idx, juce::File (s.fromFirstOccurrenceOf ("file:", false, false)));
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
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool selected = host.getSelectedId() == split.id;
        const bool on = ! split.bypassed.load();
        g.setColour (selected || isMouseOver() ? cardHi : card);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (selected ? accent : border);
        g.drawRoundedRectangle (r, 10.0f, selected ? 2.0f : 1.0f);

        // splitter glyph
        juce::Path p;
        const float cx = 22.0f, cy = 26.0f;
        p.startNewSubPath (cx - 10, cy);
        p.lineTo (cx, cy);
        p.startNewSubPath (cx, cy); p.lineTo (cx + 12, cy - 8);
        p.startNewSubPath (cx, cy); p.lineTo (cx + 12, cy);
        p.startNewSubPath (cx, cy); p.lineTo (cx + 12, cy + 8);
        g.setColour (on ? accent : dim);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (text.withMultipliedAlpha (on ? 1.0f : 0.5f));
        g.setFont (font (14.0f, true));
        g.drawText ("Split", 42, 16, getWidth() - 48, 20, juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (font (11.0f));
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
        setTooltip ("Drag up/down to set this path's level. Double-click to mute. Right-click for more.");
    }

    void paint (juce::Graphics& g) override
    {
        if (index >= (int) split.branches.size()) return;
        auto& b = *split.branches[(size_t) index];
        const auto c = b.mute.load() ? dim : branchColour (index);
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (c);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colour (0xff141416));
        g.setFont (mono (11.0f).boldened());
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
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (card);
        g.fillEllipse (r);
        g.setColour (split.bypassed.load() ? dim : teal);
        g.drawEllipse (r, 2.0f);
        g.setFont (font (14.0f, true));
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

    hub->setBounds (0, midY - 52, hubW, 104);
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
        drawCurve (g, from, { (float) pills[i]->getX(), py }, c, lv, muted);
        drawWire (g, (float) pills[i]->getRight(), (float) rows[i]->getX(), py, c, muted ? 0.0f : lv, false);
        drawCurve (g, { (float) rows[i]->getRight(), py }, to, c, lv, muted);
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
    g.fillAll (bg);
    g.setColour (juce::Colour (0xff1a1b1f));
    for (int y = 12; y < getHeight(); y += 24)
        for (int x = 12; x < getWidth(); x += 24)
            g.fillRect (x, y, 2, 2);

    auto meterBar = [&] (juce::Rectangle<float> m, float lvl, juce::Colour c)
    {
        g.setColour (bg);
        g.fillRoundedRectangle (m, 3.0f);
        const float db = gainToDb (lvl);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = m.withTop (m.getBottom() - m.getHeight() * frac);
        juce::ColourGradient grad (c, 0, m.getBottom(), red, 0, m.getY(), false);
        grad.addColour (0.80, c);
        grad.addColour (0.92, accent);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 3.0f);
    };

    auto drawTerminal = [&] (juce::Rectangle<int> box, bool isOut)
    {
        const auto& id = isOut ? kOutputId : kInputId;
        const bool sel = host.getSelectedId() == id;
        const bool over = hoverTerminal == (isOut ? 2 : 1);
        const auto c = isOut ? accent : teal;
        auto r = box;
        g.setColour (sel ? cardHi : (over ? cardHi.darker (0.08f) : card));
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (sel ? c : (over ? borderHi : border));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, sel ? 2.0f : 1.0f);

        g.setColour (sel || over ? text : muted);
        g.setFont (font (11.0f, true).withExtraKerningFactor (0.1f));
        g.drawText (isOut ? "OUT" : "IN", r.removeFromTop (24).translated (0, 2), juce::Justification::centred);

        auto bottom = r.removeFromBottom (34);
        auto meters = r.reduced (0, 4).withSizeKeepingCentre (26, r.getHeight() - 8).toFloat();
        const float l = isOut ? levels.outL : levels.inL, rr = isOut ? levels.outR : levels.inR;
        meterBar (meters.removeFromLeft (11.0f), l, c);
        meters.removeFromLeft (4.0f);
        meterBar (meters, rr, c);

        // gain + (for OUT) limiter readouts
        const float gainDb = isOut ? host.getEngine().outputGainDb.load() : host.getEngine().inputGainDb.load();
        g.setFont (mono (10.0f));
        g.setColour (muted);
        const float pk = juce::jmax (l, rr);
        g.drawText (pk < 1.0e-5f ? juce::String ("-inf") : juce::String (gainToDb (pk), 1), bottom.removeFromTop (14), juce::Justification::centred);
        juce::String sub = (gainDb > 0.05f ? "+" : "") + juce::String (gainDb, 1) + " dB";
        auto subColour = dim;
        if (isOut && host.getEngine().limiterOn.load() && levels.grDb < -0.2f)
        {
            sub = "LIM " + juce::String (levels.grDb, 1);
            subColour = accent;
        }
        g.setColour (subColour);
        g.drawText (sub, bottom.removeFromTop (14), juce::Justification::centred);
    };
    drawTerminal (inBox, false);
    drawTerminal (outBox, true);
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

bool GraphView::dropFile (const juce::File& f, juce::Point<int> pt)
{
    auto* c = getComponentAt (pt);
    while (c != nullptr && c != this)
    {
        if (auto* card = dynamic_cast<BlockCard*> (c))
        {
            const int zone = card->dropZone (card->getLocalPoint (this, pt));
            if (zone == 0)
            {
                if (card->acceptsFile (f)) return host.loadFileInto (card->getNode(), nullptr, -1, f);
                if (! f.hasFileExtension ("rig")) { host.replaceWith (*card->getNode(), "file:" + f.getFullPathName()); return true; }
            }
            else if (auto* cv = dynamic_cast<ChainView*> (card->getParentComponent()))
            {
                const int idx = cv->indexOfItem (card);
                return host.loadFileInto (nullptr, &cv->getChain(), zone < 0 ? idx : idx + 1, f);
            }
        }
        if (auto* cv = dynamic_cast<ChainView*> (c))
            return host.loadFileInto (nullptr, &cv->getChain(), cv->indexForX (cv->getLocalPoint (this, pt).x), f);
        c = c->getParentComponent();
    }
    return host.loadFileInto (nullptr, &host.getEngine().getRoot(), -1, f);
}

} // namespace rig

namespace rig
{
//==============================================================================
RigMap::RigMap (juce::Viewport& v, GraphView& g) : viewport (v), graph (g)
{
    setTooltip ("The whole rig: click or drag to go to any part of it");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (12);
}

juce::Rectangle<float> RigMap::mapArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f, 6.0f);
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
    g.fillAll (panel);
    g.setColour (border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    const auto a = mapArea();
    g.setColour (bg);
    g.fillRoundedRectangle (a.expanded (4.0f, 2.0f), 5.0f);
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
