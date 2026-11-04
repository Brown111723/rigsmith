#include "Icons.h"

namespace rig
{
using namespace theme;

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour c)
{
    // designed on a 24 x 24 grid
    const float s = juce::jmin (box.getWidth(), box.getHeight()) / 24.0f;
    const float ox = box.getCentreX() - 12.0f * s, oy = box.getCentreY() - 12.0f * s;
    auto P = [&] (float x, float y) { return juce::Point<float> (ox + x * s, oy + y * s); };
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * s, oy + y * s, w * s, h * s); };
    const juce::PathStrokeType stroke (1.8f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path p;
    auto line = [&] (float x1, float y1, float x2, float y2) { p.startNewSubPath (P (x1, y1)); p.lineTo (P (x2, y2)); };
    auto poly = [&] (std::initializer_list<juce::Point<float>> pts)
    {
        bool first = true;
        for (auto pt : pts) { if (first) p.startNewSubPath (P (pt.x, pt.y)); else p.lineTo (P (pt.x, pt.y)); first = false; }
    };
    auto arc = [&] (float cx, float cy, float r, float a0, float a1)
    {
        p.addCentredArc (P (cx, cy).x, P (cx, cy).y, r * s, r * s, 0.0f, a0, a1, true);
    };
    const float pi = juce::MathConstants<float>::pi;

    g.setColour (c);
    switch (icon)
    {
        case Icon::Open:   // window with an arrow leaving it
            p.addRoundedRectangle (R (4, 6, 14, 14), 2.5f * s);
            poly ({ { 13, 4 }, { 20, 4 }, { 20, 11 } });
            line (20, 4, 11, 13);
            break;
        case Icon::Folder:
            poly ({ { 3, 7 }, { 3, 19 }, { 21, 19 }, { 21, 9 }, { 11, 9 }, { 9, 6 }, { 3, 6 }, { 3, 7 } });
            break;
        case Icon::Power:
            arc (12, 13, 7.5f, 0.22f * pi, 1.78f * pi);
            line (12, 3.5f, 12, 11.5f);
            break;
        case Icon::AutoGain:   // two level bars made equal by arrows
            line (5, 7, 5, 18);  line (19, 7, 19, 18);
            line (3, 7, 7, 7);   line (17, 7, 21, 7);
            poly ({ { 9.5f, 9 }, { 12, 6.5f }, { 14.5f, 9 } });
            poly ({ { 9.5f, 15 }, { 12, 17.5f }, { 14.5f, 15 } });
            line (12, 6.5f, 12, 17.5f);
            break;
        case Icon::Dice:
        {
            juce::Path d;
            d.addRoundedRectangle (R (3.5f, 3.5f, 17, 17), 4.0f * s);
            g.strokePath (d, stroke);
            for (auto pt : { juce::Point<float> (8, 8), { 16, 8 }, { 12, 12 }, { 8, 16 }, { 16, 16 } })
                g.fillEllipse (R (pt.x - 1.7f, pt.y - 1.7f, 3.4f, 3.4f));
            return;
        }
        case Icon::Shuffle:
            p.startNewSubPath (P (3, 7));
            p.cubicTo (P (10, 7), P (12, 17), P (19, 17));
            p.startNewSubPath (P (3, 17));
            p.cubicTo (P (10, 17), P (12, 7), P (19, 7));
            poly ({ { 16.5f, 4.5f }, { 19.5f, 7 }, { 16.5f, 9.5f } });
            poly ({ { 16.5f, 14.5f }, { 19.5f, 17 }, { 16.5f, 19.5f } });
            break;
        case Icon::Preset:   // bookmark with lines
            poly ({ { 6, 3.5f }, { 18, 3.5f }, { 18, 20.5f }, { 12, 16 }, { 6, 20.5f }, { 6, 3.5f } });
            line (9, 8, 15, 8);
            line (9, 11.5f, 15, 11.5f);
            break;
        case Icon::Lock:
        case Icon::Unlock:
        {
            juce::Path body;
            body.addRoundedRectangle (R (5, 11, 14, 10), 2.0f * s);
            g.fillPath (body);
            p.startNewSubPath (P (8, 11));
            p.lineTo (P (8, 8));
            p.addCentredArc (P (12, 8).x, P (12, 8).y, 4.0f * s, 4.0f * s, 0.0f, -0.5f * pi, 0.5f * pi, false);
            if (icon == Icon::Lock) p.lineTo (P (16, 11));
            g.strokePath (p, stroke);
            return;
        }
        case Icon::Prev: poly ({ { 15, 5 }, { 8, 12 }, { 15, 19 } }); break;
        case Icon::Next: poly ({ { 9, 5 }, { 16, 12 }, { 9, 19 } }); break;
        case Icon::Caret: poly ({ { 7, 10 }, { 12, 15 }, { 17, 10 } }); break;
        case Icon::FullScreen:
            poly ({ { 4, 9 }, { 4, 4 }, { 9, 4 } });     poly ({ { 15, 4 }, { 20, 4 }, { 20, 9 } });
            poly ({ { 20, 15 }, { 20, 20 }, { 15, 20 } }); poly ({ { 9, 20 }, { 4, 20 }, { 4, 15 } });
            break;
        case Icon::ExitFullScreen:
            poly ({ { 4, 9 }, { 9, 9 }, { 9, 4 } });     poly ({ { 15, 4 }, { 15, 9 }, { 20, 9 } });
            poly ({ { 20, 15 }, { 15, 15 }, { 15, 20 } }); poly ({ { 9, 20 }, { 9, 15 }, { 4, 15 } });
            break;
        case Icon::Library:   // window with a side panel
            p.addRoundedRectangle (R (3, 4.5f, 18, 15), 2.5f * s);
            line (14.5f, 4.5f, 14.5f, 19.5f);
            line (16.8f, 8.5f, 18.8f, 8.5f);
            line (16.8f, 12, 18.8f, 12);
            break;
        case Icon::Undo:
            p.startNewSubPath (P (8, 9));
            p.cubicTo (P (16, 7), P (21, 11), P (19, 16));
            p.cubicTo (P (18, 18), P (15, 19), P (12, 19));
            poly ({ { 11, 5 }, { 7, 9 }, { 11, 13 } });
            break;
        case Icon::Redo:
            p.startNewSubPath (P (16, 9));
            p.cubicTo (P (8, 7), P (3, 11), P (5, 16));
            p.cubicTo (P (6, 18), P (9, 19), P (12, 19));
            poly ({ { 13, 5 }, { 17, 9 }, { 13, 13 } });
            break;
        case Icon::Init:   // circular arrow back to the start
            arc (12, 12, 7.5f, -0.35f * pi, 1.35f * pi);
            poly ({ { 3.5f, 6 }, { 5.6f, 9.7f }, { 9.5f, 8 } });
            break;
        case Icon::Menu:
            line (5, 7, 19, 7); line (5, 12, 19, 12); line (5, 17, 19, 17);
            break;
        case Icon::TuningFork:   // U-shaped prongs on a stem, with a little vibration either side
            p.startNewSubPath (P (9, 3));
            p.lineTo (P (9, 11));
            p.addCentredArc (P (12, 11).x, P (12, 11).y, 3.0f * s, 3.0f * s, 0.0f, 1.5f * pi, 0.5f * pi, false);
            p.lineTo (P (15, 3));
            line (12, 14, 12, 21.5f);
            p.startNewSubPath (P (5.5f, 4.5f)); p.quadraticTo (P (4, 7), P (5.5f, 9.5f));
            p.startNewSubPath (P (18.5f, 4.5f)); p.quadraticTo (P (20, 7), P (18.5f, 9.5f));
            break;
        case Icon::Tooltip:   // speech bubble with a question mark
        {
            p.addRoundedRectangle (R (3.5f, 4, 17, 12.5f), 3.0f * s);
            g.strokePath (p, stroke);
            juce::Path tail;
            tail.startNewSubPath (P (8, 16.5f));
            tail.lineTo (P (7, 21));
            tail.lineTo (P (12.5f, 16.5f));
            g.strokePath (tail, stroke);
            juce::Path q;
            q.startNewSubPath (P (10, 8.5f));
            q.cubicTo (P (10, 6.3f), P (14, 6.3f), P (14, 8.5f));
            q.cubicTo (P (14, 10.2f), P (12, 10.2f), P (12, 11.8f));
            g.strokePath (q, juce::PathStrokeType (1.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (R (11.1f, 13.1f, 1.8f, 1.8f));
            return;
        }
        case Icon::Close:
            line (6.5f, 6.5f, 17.5f, 17.5f);
            line (17.5f, 6.5f, 6.5f, 17.5f);
            break;
        case Icon::Save:
            poly ({ { 5, 4 }, { 16, 4 }, { 20, 8 }, { 20, 20 }, { 4, 20 }, { 4, 4 }, { 5, 4 } });
            p.addRectangle (R (8, 13, 8, 7));
            line (8, 4, 8, 8.5f); line (8, 8.5f, 15, 8.5f); line (15, 8.5f, 15, 4);
            break;
    }
    g.strokePath (p, stroke);
}

void drawFootswitch (juce::Graphics& g, juce::Rectangle<float> sw, juce::Colour c, bool on)
{
    if (on)
        for (int i = 3; i > 0; --i)
        {
            g.setColour (c.withAlpha (0.08f * (float) i));
            g.fillEllipse (sw.expanded ((float) (4 - i) * 3.0f));
        }
    g.setColour (on ? c : bg);
    g.fillEllipse (sw);
    g.setColour (borderHi);
    g.drawEllipse (sw, 1.5f);
}

FootswitchButton::FootswitchButton() : juce::Button ("On / bypass")
{
    setTooltip ("On / bypass (Space)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int FootswitchButton::preferredWidth (int h) const
{
    return h + juce::GlyphArrangement::getStringWidthInt (font (12.0f), "Bypassed") + 14;
}

void FootswitchButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    const auto r = getLocalBounds().toFloat();
    const float d = juce::jmin (24.0f, r.getHeight() - 6.0f);
    const auto sw = juce::Rectangle<float> (r.getX() + 4.0f, r.getCentreY() - d * 0.5f, d, d);
    drawFootswitch (g, sw, over && ! on ? colour.withAlpha (0.35f) : colour, on || over);
    if (over && ! on)
    {
        g.setColour (bg);
        g.fillEllipse (sw.reduced (3.0f));
    }
    g.setColour (on ? theme::text : muted);
    g.setFont (font (12.0f));
    g.drawText (on ? "On" : "Bypassed", r.withTrimmedLeft (sw.getRight() + 8.0f - r.getX()), juce::Justification::centredLeft, false);
}

//==============================================================================
IconButton::IconButton (Icon i, const juce::String& tip, juce::Colour on)
    : juce::Button (tip), icon (i), onColour (on)
{
    setTooltip (tip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int IconButton::preferredWidth (int h) const
{
    if (label.isEmpty()) return h;
    return h + juce::GlyphArrangement::getStringWidthInt (font (12.0f, true), label) + 8;
}

void IconButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
    juce::Button::mouseDown (e);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState() || highlighted;
    const bool enabled = isEnabled();

    auto fill = on ? onColour.withAlpha (0.22f) : (over ? cardHi : card);
    if (down) fill = fill.brighter (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (on ? onColour.withAlpha (0.8f) : (over ? borderHi : border));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

    const auto ic = ! enabled ? dim.withAlpha (0.5f) : (on ? onColour : (over ? theme::text : muted));
    auto iconBox = r.withWidth (r.getHeight()).reduced (r.getHeight() * 0.2f);
    drawIcon (g, icon, iconBox, ic);

    if (progress >= 0.0f && progress < 1.0f)
    {
        juce::Path ring;
        const auto rr = r.withWidth (r.getHeight()).reduced (2.5f);
        ring.addCentredArc (rr.getCentreX(), rr.getCentreY(), rr.getWidth() * 0.5f, rr.getHeight() * 0.5f, 0.0f,
                            0.0f, juce::MathConstants<float>::twoPi * juce::jmax (0.04f, progress), true);
        g.setColour (onColour);
        g.strokePath (ring, juce::PathStrokeType (2.0f));
    }

    if (label.isNotEmpty())
    {
        g.setColour (! enabled ? dim : (on ? theme::text : muted));
        g.setFont (font (12.0f, true));
        g.drawText (label, r.withTrimmedLeft (r.getHeight() - 2.0f).withTrimmedRight (6.0f), juce::Justification::centredLeft, false);
    }
}

} // namespace rig
