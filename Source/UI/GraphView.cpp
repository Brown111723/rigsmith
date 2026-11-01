#include "GraphView.h"
#include "Icons.h"

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
    const auto c = colourFor (node);
    const float alpha = on ? 1.0f : 0.55f;

    g.setColour (selected ? cardHi : (isMouseOver() ? cardHi.darker (0.08f) : card));
    g.fillRoundedRectangle (r, 10.0f);

    if (dropLabel.isNotEmpty()) { g.setColour (teal); g.drawRoundedRectangle (r, 10.0f, 2.5f); }
    else if (selected)      { g.setColour (c); g.drawRoundedRectangle (r, 10.0f, 2.0f); }
    else                    { g.setColour (border); g.drawRoundedRectangle (r, 10.0f, 1.0f); }

    g.setColour (c.withMultipliedAlpha (on ? 1.0f : 0.35f));
    g.fillRoundedRectangle (12.0f, 11.0f, r.getWidth() - 22.0f, 5.0f, 2.5f);

    g.setColour (muted.withMultipliedAlpha (alpha));
    g.setFont (font (10.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (kindLabel (node), 12, 22, getWidth() - 24, 14, juce::Justification::centredLeft);

    g.setColour (text.withMultipliedAlpha (alpha));
    g.setFont (font (14.0f, true));
    g.drawFittedText (node.getName(), 12, 37, getWidth() - 24, 36, juce::Justification::topLeft, 2, 0.9f);

    g.setColour (muted.withMultipliedAlpha (alpha));
    g.setFont (font (11.0f));
    g.drawFittedText (node.getDetail(), 12, 72, getWidth() - 24, 16, juce::Justification::topLeft, 1, 0.85f);

    // footswitch + LED glow
    const auto sw = footswitchArea();
    if (on)
    {
        for (int i = 3; i > 0; --i)
        {
            g.setColour (c.withAlpha (0.08f * (float) i));
            g.fillEllipse (sw.expanded ((float) (4 - i) * 3.0f));
        }
    }
    g.setColour (on ? c : bg);
    g.fillEllipse (sw);
    g.setColour (borderHi);
    g.drawEllipse (sw, 1.5f);

    g.setColour (on ? text : muted);
    g.setFont (font (11.0f));
    g.drawText (on ? "On" : "Bypassed", (int) sw.getRight() + 8, (int) sw.getY(), 70, (int) sw.getHeight(), juce::Justification::centredLeft);

    if (auto lat = node.getLatency(); lat > 0)
    {
        g.setColour (dim);
        g.setFont (mono (10.0f));
        g.drawText (juce::String (lat) + " smp", getWidth() - 62, (int) sw.getY(), 52, (int) sw.getHeight(), juce::Justification::centredRight);
    }
    else if (node.autoGain.load())
    {
        g.setColour (teal.withMultipliedAlpha (alpha));
        g.setFont (mono (10.0f));
        const float db = node.autoGainDb.load();
        g.drawText (node.autoGainProgress.load() < 1.0f ? juce::String ("AG ...") : "AG " + juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1),
                    getWidth() - 72, (int) sw.getY(), 62, (int) sw.getHeight(), juce::Justification::centredRight);
    }

    if (node.locked.load())
        drawIcon (g, Icon::Lock, { (float) getWidth() - 28.0f, 20.0f, 16.0f, 16.0f }, accent.withMultipliedAlpha (alpha));

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
    if (e.mods.isPopupMenu())
    {
        host.select (node.id);
        host.showNodeMenu (node, this);
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
    if (pressedSwitch || dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        dnd->startDragging ("node:" + node.id, this);
    }
}

void BlockCard::mouseUp (const juce::MouseEvent&) { dragging = false; pressedSwitch = false; }

void BlockCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! footswitchArea().expanded (6.0f).contains (e.position))
        host.openNode (node);
}

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
    return s.startsWith ("plugin:") || s.startsWith ("node:");
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
AddButton::AddButton (GraphHost& h, Chain& c, int i) : host (h), chain (c), index (i)
{
    setTitle ("Add block");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void AddButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool over = isMouseOver();
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
    for (auto* it : items)
    {
        const auto p = it->preferredSize();
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
    return s.startsWith ("file:") || s.startsWith ("plugin:");
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

void GraphView::fitTo (juce::Rectangle<int> area)
{
    viewArea = area;
    const auto p = main->preferredSize();
    const int w = juce::jmax (area.getWidth(), 24 + 70 + p.x + 70 + 24);
    const int h = juce::jmax (area.getHeight(), p.y + 48);
    setSize (w, h);
    resized();
}

void GraphView::resized()
{
    const auto p = main->preferredSize();
    const int cy = getHeight() / 2;
    const int contentW = 70 + p.x + 70;
    const int x0 = juce::jmax (24, (getWidth() - contentW) / 2);
    inBox = { x0, cy - 56, 70, 112 };
    main->setBounds (inBox.getRight(), cy - p.y / 2, p.x, p.y);
    outBox = { main->getRight(), cy - 56, 70, 112 };
}

void GraphView::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (juce::Colour (0xff1a1b1f));
    for (int y = 12; y < getHeight(); y += 24)
        for (int x = 12; x < getWidth(); x += 24)
            g.fillRect (x, y, 2, 2);

    auto drawTerminal = [&] (juce::Rectangle<int> r, const juce::String& label, float lvl, juce::Colour c)
    {
        g.setColour (card);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (muted);
        g.setFont (font (11.0f, true));
        g.drawText (label, r.removeFromTop (24), juce::Justification::centred);
        const auto meter = r.reduced (0, 8).withSizeKeepingCentre (10, r.getHeight() - 22);
        g.setColour (bg);
        g.fillRoundedRectangle (meter.toFloat(), 5.0f);
        const float db = gainToDb (lvl);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = meter.toFloat();
        fill = fill.removeFromBottom (fill.getHeight() * frac);
        g.setColour (db > -1.0f ? red : c);
        g.fillRoundedRectangle (fill, 5.0f);
        g.setColour (muted);
        g.setFont (mono (10.0f));
        g.drawText (db <= -99.0f ? juce::String ("-inf") : juce::String (db, 1), r.removeFromBottom (14).translated (0, 4),
                    juce::Justification::centred);
    };
    drawTerminal (inBox, "IN", inLevel, teal);
    drawTerminal (outBox, "OUT", outLevel, accent);
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
