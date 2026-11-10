#include "Theme.h"
#include "RigFonts.h"

namespace rig::theme
{
//==============================================================================
const std::vector<AccentChoice>& accentChoices()
{
    static const std::vector<AccentChoice> c {
        { "Amber",  0xffffa826 },   // valve glow: Rigsmith's own
        { "Ember",  0xffff4a38 },
        { "Gold",   0xffe6c27a },
        { "Ice",    0xff7fd4ff },
        { "Mint",   0xff5fe0b4 },
        { "Violet", 0xffb59cff },
        { "Silver", 0xffe4e4ea },
    };
    return c;
}

static int accentIndex = 0;
void setAccent (int index)
{
    accentIndex = juce::jlimit (0, (int) accentChoices().size() - 1, index);
    accent = juce::Colour (accentChoices()[(size_t) accentIndex].argb);
}
int getAccentIndex() { return accentIndex; }

juce::Colour colourFor (const Node& n)
{
    if (n.getType() == NodeType::Split) return accent;
    if (auto* pb = dynamic_cast<const PluginBlock*> (&n); pb != nullptr && ! PluginBlock::autoCategorise)
    {
        const auto h = (juce::uint32) n.getName().hashCode();   // stable colour per plug-in name
        return juce::Colour::fromHSV ((float) (h % 360) / 360.0f, 0.40f, 0.95f, 1.0f);
    }
    if (auto* nb = dynamic_cast<const NamBlock*> (&n); nb != nullptr && nb->getGear() != Gear::Unknown) return gearColour (nb->getGear());
    if (auto* ir = dynamic_cast<const IrBlock*> (&n); ir != nullptr && ir->getGear() != Gear::Unknown) return gearColour (ir->getGear());
    return categoryColour (n.getCategory());
}

juce::Colour branchColour (int i)
{
    const juce::Colour c[] = { accent, teal, blue, purple, red, green };
    return c[(size_t) i % 6];
}

juce::String kindLabel (const Node& n)
{
    switch (n.getType())
    {
        case NodeType::Nam:
            if (auto* nb = dynamic_cast<const NamBlock*> (&n); nb != nullptr && nb->getGear() != Gear::Unknown)
                return "NAM - " + gearShortName (nb->getGear());
            return "NAM";
        case NodeType::Ir:
            if (auto* ir = dynamic_cast<const IrBlock*> (&n); ir != nullptr && ir->getGear() != Gear::Cab && ir->getGear() != Gear::Unknown)
                return "IR - " + gearShortName (ir->getGear());
            return "CAB IR";
        case NodeType::Cab:    return "CABINET";
        case NodeType::BassDrive: return "BASS";
        case NodeType::Split:  return "SPLIT";
        case NodeType::Chain:  return "CHAIN";
        case NodeType::Plugin:
            return PluginBlock::autoCategorise ? "VST - " + categoryName (n.getCategory()).toUpperCase() : juce::String ("VST");
        default: break;
    }
    return categoryName (n.getCategory()).toUpperCase();
}

//==============================================================================
// Fonts: Inter for everything you read, IBM Plex Mono for numbers, Michroma for wordmarks.
// They're built in, so Rigsmith looks the same on every machine.
namespace
{
struct Faces
{
    juce::Typeface::Ptr regular  = juce::Typeface::createSystemTypefaceFor (RigFonts::InterRegular_ttf, RigFonts::InterRegular_ttfSize);
    juce::Typeface::Ptr mediumW  = juce::Typeface::createSystemTypefaceFor (RigFonts::InterMedium_ttf, RigFonts::InterMedium_ttfSize);
    juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (RigFonts::InterSemiBold_ttf, RigFonts::InterSemiBold_ttfSize);
    juce::Typeface::Ptr monoW    = juce::Typeface::createSystemTypefaceFor (RigFonts::IBMPlexMonoRegular_ttf, RigFonts::IBMPlexMonoRegular_ttfSize);
    juce::Typeface::Ptr wide     = juce::Typeface::createSystemTypefaceFor (RigFonts::MichromaRegular_ttf, RigFonts::MichromaRegular_ttfSize);
};
Faces& faces() { static Faces f; return f; }

juce::Font make (const juce::Typeface::Ptr& face, float size)
{
    if (face == nullptr) return juce::Font (juce::FontOptions (size));
    return juce::Font (juce::FontOptions (face).withHeight (size));
}
}

juce::Font font (float size, bool bold)        { return make (bold ? faces().semibold : faces().regular, size * 1.04f); }
juce::Font medium (float size)                 { return make (faces().mediumW, size * 1.04f); }
juce::Font mono (float size)                   { return make (faces().monoW, size * 1.06f); }
juce::Font display (float size, float tracking) { return make (faces().wide, size).withExtraKerningFactor (tracking); }

//==============================================================================
void drawShadow (juce::Graphics& g, juce::Rectangle<float> r, float radius, float size, float strength)
{
    const int steps = 5;
    for (int i = steps; i > 0; --i)
    {
        const float grow = size * (float) i / (float) steps;
        g.setColour (juce::Colours::black.withAlpha (strength * 0.22f * (1.0f - (float) (i - 1) / (float) steps)));
        g.fillRoundedRectangle (r.expanded (grow * 0.55f).translated (0.0f, grow * 0.45f), radius + grow * 0.55f);
    }
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour edge, float lift)
{
    juce::ColourGradient grad (card.brighter (0.07f + lift), r.getX(), r.getY(),
                               card.darker (0.28f - lift * 0.5f), r.getX(), r.getBottom(), false);
    grad.addColour (0.12, card.brighter (0.02f + lift));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    // the light catching the top edge
    g.setColour (juce::Colours::white.withAlpha (0.055f + lift * 0.2f));
    g.drawRoundedRectangle (r.reduced (1.0f).withHeight (juce::jmin (r.getHeight() - 2.0f, radius * 2.0f + 6.0f)), juce::jmax (1.0f, radius - 1.0f), 1.0f);
    // ...which fades out down the sides (painted over by the body colour)
    g.setGradientFill (grad);
    g.fillRect (r.reduced (0.5f).withTrimmedTop (radius + 2.0f).withTrimmedBottom (radius));
    g.setColour (edge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (juce::Colour (0xff050506));
    g.fillRoundedRectangle (r, radius);
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(),
                                juce::Colours::transparentBlack, r.getX(), r.getY() + 14.0f, false);
    g.setGradientFill (shade);
    g.fillRoundedRectangle (r.withHeight (juce::jmin (r.getHeight(), 16.0f)), radius);
    g.setColour (juce::Colour (0xff1c1c21));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.fillRect (r.getX() + radius, r.getBottom() - 1.0f, r.getWidth() - radius * 2.0f, 1.0f);
}

void drawKnobBody (juce::Graphics& g, juce::Point<float> c, float R, float angle, bool enabled, bool lit)
{
    if (R < 2.0f) return;
    // the shadow it throws on the panel
    if (R > 7.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillEllipse (c.x - R * 1.06f, c.y - R * 0.94f, R * 2.12f, R * 2.16f);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillEllipse (c.x - R, c.y - R * 0.90f, R * 2.0f, R * 2.06f);
    }
    // skirt
    {
        juce::ColourGradient grad (juce::Colour (0xff3b3b42), c.x, c.y - R, juce::Colour (0xff0e0e10), c.x, c.y + R, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
        g.setColour (juce::Colour (0xff050506));
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.0f);
    }
    // knurling
    if (R > 13.0f)
    {
        const int ticks = R > 26.0f ? 48 : 32;
        juce::Path grip;
        for (int i = 0; i < ticks; ++i)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) ticks;
            const auto p0 = c.getPointOnCircumference (R * 0.80f, a), p1 = c.getPointOnCircumference (R * 0.965f, a);
            grip.startNewSubPath (p0); grip.lineTo (p1);
        }
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.strokePath (grip, juce::PathStrokeType (juce::jmax (1.0f, R * 0.045f)));
    }
    // cap
    {
        const float r = R * (R > 13.0f ? 0.76f : 0.82f);
        juce::ColourGradient grad (juce::Colour (0xff34343a), c.x, c.y - r, juce::Colour (0xff17171a), c.x, c.y + r, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        juce::ColourGradient rim (juce::Colours::white.withAlpha (0.16f), c.x, c.y - r, juce::Colours::white.withAlpha (0.0f), c.x, c.y + r * 0.2f, false);
        g.setGradientFill (rim);
        g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, r * 2.0f - 1.0f, r * 2.0f - 1.0f, 1.0f);
    }
    // pointer
    {
        const float w = juce::jlimit (1.4f, 3.2f, R * 0.11f);
        const auto p0 = c.getPointOnCircumference (R * 0.30f, angle), p1 = c.getPointOnCircumference (R * 0.90f, angle);
        juce::Path p; p.startNewSubPath (p0); p.lineTo (p1);
        g.setColour ((lit ? accent : text).withMultipliedAlpha (enabled ? 1.0f : 0.35f));
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
{
    if (on)
    {
        for (int i = 3; i > 0; --i)
        {
            g.setColour (col.withAlpha (0.07f * (float) (4 - i)));
            const float e = r + (float) i * r * 0.9f;
            g.fillEllipse (c.x - e, c.y - e, e * 2.0f, e * 2.0f);
        }
        g.setColour (col);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (c.x - r * 0.45f, c.y - r * 0.55f, r * 0.7f, r * 0.6f);
    }
    else
    {
        g.setColour (col.withMultipliedSaturation (0.5f).withMultipliedBrightness (0.22f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float r)
{
    juce::ColourGradient grad (juce::Colour (0xff3a3a40), c.x, c.y - r, juce::Colour (0xff151517), c.x, c.y + r, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);
    g.drawLine (c.x - r * 0.55f, c.y + r * 0.35f, c.x + r * 0.55f, c.y - r * 0.35f, 1.0f);
}
} // namespace rig::theme

namespace rig
{
using namespace theme;

RigLookAndFeel::RigLookAndFeel()
{
    setDefaultSansSerifTypeface (faces().regular);
    refreshColours();
}

juce::Typeface::Ptr RigLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return f.isBold() ? faces().semibold : faces().regular;
    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return faces().monoW;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void RigLookAndFeel::refreshColours()
{
    setColourScheme ({ bg, panel, card, border, text, accent, bg, accent, text });

    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, card);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, onAccent);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, border);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, bg);
    setColour (juce::Slider::textBoxTextColourId, muted);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff060607));
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent.withAlpha (0.8f));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, muted);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, border);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff060607));
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, muted);
    setColour (juce::ScrollBar::thumbColourId, borderHi);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, border);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::TooltipWindow::backgroundColourId, cardHi);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, borderHi);
}

void RigLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcR = radius - 2.5f;
    const float angle = start + pos * (end - start);
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);
    const bool active = s.isMouseOverOrDragging();
    const float stroke = radius > 22.0f ? 2.6f : 2.2f;

    // the scale: a hairline all the way round, lit up to where the knob is
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff2a2a31));
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar knobs fill from the middle
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        const auto vc = s.isEnabled() ? fill : dim;
        if (active)
        {
            g.setColour (vc.withAlpha (0.22f));
            g.strokePath (value, juce::PathStrokeType (stroke + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (vc);
        g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    drawKnobBody (g, centre, arcR - stroke - (radius > 22.0f ? 4.0f : 2.5f), angle, s.isEnabled());
}

void RigLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                       juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f, (float) w, 4.0f);
    g.setColour (juce::Colour (0xff060607));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (border);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (r.withRight (pos), 2.0f);
    drawKnobBody (g, { pos, r.getCentreY() }, 7.5f, 0.0f, s.isEnabled());
}

static bool isBright (juce::Colour c) { return c.getPerceivedBrightness() > 0.55f && c.getAlpha() > 200; }

void RigLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto offColour = b.findColour (juce::TextButton::buttonColourId);
    const auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    const float rad = juce::jmin (7.0f, r.getHeight() * 0.3f);
    const float a = b.isEnabled() ? 1.0f : 0.5f;

    if (isBright (offColour))
    {
        // a call to action: filled
        g.setColour ((down ? offColour.darker (0.2f) : over ? offColour.brighter (0.12f) : offColour).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, rad);
        return;
    }
    if (on)
    {
        g.setColour (card);
        g.fillRoundedRectangle (r, rad);
        g.setColour (onColour.withAlpha (down ? 0.26f : over ? 0.22f : 0.16f));
        g.fillRoundedRectangle (r, rad);
        g.setColour (onColour.withAlpha (0.85f));
        g.drawRoundedRectangle (r, rad, 1.0f);
        return;
    }
    juce::ColourGradient grad ((over ? cardHi.brighter (0.04f) : offColour.brighter (0.03f)).withMultipliedAlpha (a), 0.0f, r.getY(),
                               (over ? cardHi : offColour.darker (0.12f)).withMultipliedAlpha (a), 0.0f, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, rad);
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (r, rad); }
    g.setColour ((over ? borderHi : border).withMultipliedAlpha (a));
    g.drawRoundedRectangle (r, rad, 1.0f);
}

void RigLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool on = b.getToggleState();
    juce::Colour c = b.findColour (juce::TextButton::textColourOffId);
    if (isBright (b.findColour (juce::TextButton::buttonColourId))) c = onAccent;
    else if (on) c = b.findColour (juce::TextButton::buttonOnColourId).brighter (0.15f);
    g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

void RigLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const auto sw = r.removeFromLeft (36.0f).withSizeKeepingCentre (30.0f, 16.0f);
    const bool on = b.getToggleState();
    g.setColour (juce::Colour (0xff060607));
    g.fillRoundedRectangle (sw, 8.0f);
    if (on) { g.setColour (accent.withAlpha (0.30f)); g.fillRoundedRectangle (sw, 8.0f); }
    g.setColour (on ? accent.withAlpha (0.9f) : (over ? borderHi : border));
    g.drawRoundedRectangle (sw, 8.0f, 1.0f);
    const float kx = on ? sw.getRight() - 14.0f : sw.getX() + 2.0f;
    g.setColour (on ? accent : muted);
    g.fillEllipse (kx, sw.getY() + 2.0f, 12.0f, 12.0f);
    g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.setFont (font (13.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (6), juce::Justification::centredLeft, 1);
}

void RigLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff141417));
    g.setColour (borderHi.withAlpha (0.8f));
    g.drawRect (0, 0, w, h);
}

void RigLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& txt,
                                        const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (border);
        g.fillRect (area.reduced (10, 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
    }
    auto c = textColour != nullptr ? *textColour : text;
    g.setColour (c.withMultipliedAlpha (isActive ? 1.0f : 0.38f));
    r.reduce (8, 0);
    auto tickArea = r.removeFromLeft (18);
    if (icon != nullptr)
        icon->drawWithin (g, tickArea.toFloat().reduced (1.0f), juce::RectanglePlacement::centred, isActive ? 1.0f : 0.4f);
    else if (isTicked)
    {
        juce::Path tick;
        const auto t = tickArea.toFloat().withSizeKeepingCentre (10.0f, 8.0f);
        tick.startNewSubPath (t.getX(), t.getCentreY());
        tick.lineTo (t.getX() + 3.5f, t.getBottom());
        tick.lineTo (t.getRight(), t.getY());
        g.setColour (accent);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withMultipliedAlpha (isActive ? 1.0f : 0.38f));
    }
    r.removeFromLeft (4);
    if (hasSubMenu)
    {
        const auto a = r.removeFromRight (12).toFloat().withSizeKeepingCentre (5.0f, 9.0f);
        juce::Path p; p.startNewSubPath (a.getX(), a.getY()); p.lineTo (a.getRight(), a.getCentreY()); p.lineTo (a.getX(), a.getBottom());
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setFont (getPopupMenuFont());
    if (shortcut.isNotEmpty())
    {
        const auto old = g.getCurrentFont();
        g.setFont (mono (11.5f));
        g.setColour (muted.withMultipliedAlpha (isActive ? 1.0f : 0.4f));
        g.drawText (shortcut, r, juce::Justification::centredRight, true);
        g.setColour (c.withMultipliedAlpha (isActive ? 1.0f : 0.38f));
        g.setFont (old);
    }
    g.drawFittedText (txt, r, juce::Justification::centredLeft, 1);
}

void RigLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) || box.isMouseOver (true) ? borderHi : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    const auto a = juce::Rectangle<float> ((float) w - 22.0f, 0.0f, 12.0f, (float) h).withSizeKeepingCentre (9.0f, 5.0f);
    juce::Path p; p.startNewSubPath (a.getX(), a.getY()); p.lineTo (a.getCentreX(), a.getBottom()); p.lineTo (a.getRight(), a.getY());
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.3f));
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void RigLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int thumbStart, int thumbSize, bool over, bool)
{
    auto r = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour (over ? borderHi.brighter (0.25f) : borderHi.withAlpha (0.8f));
    const auto t = r.toFloat().reduced (vertical ? 3.0f : 2.0f, vertical ? 2.0f : 3.0f);
    g.fillRoundedRectangle (t, juce::jmin (t.getWidth(), t.getHeight()) * 0.5f);
}

void RigLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& txt, int w, int h)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (cardHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (borderHi);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (txt, font (13.0f), text);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) w - 16.0f);
    tl.draw (g, r.reduced (8.0f, 4.0f));
}
} // namespace rig
