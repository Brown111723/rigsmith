#include "Theme.h"

namespace rig::theme
{
juce::Colour colourFor (const Node& n)
{
    switch (n.getType())
    {
        case NodeType::Nam:    return accent;
        case NodeType::Ir:     return teal;
        case NodeType::Gate:   return juce::Colour (0xff8a8f99);
        case NodeType::Eq:     return green;
        case NodeType::Gain:   return juce::Colour (0xffb8bcc6);
        case NodeType::Comp:   return juce::Colour (0xfff0d264);
        case NodeType::Split:  return accent;
        case NodeType::Chain:  return muted;
        case NodeType::Plugin:
        {
            // stable colour per plug-in name
            const auto h = (juce::uint32) n.getName().hashCode();
            const float hue = (float) (h % 360) / 360.0f;
            return juce::Colour::fromHSV (hue, 0.45f, 0.95f, 1.0f);
        }
    }
    return muted;
}

juce::Colour branchColour (int i)
{
    static const juce::Colour c[] = { accent, teal, blue, purple, red, green };
    return c[(size_t) i % 6];
}

juce::String kindLabel (const Node& n)
{
    switch (n.getType())
    {
        case NodeType::Nam:    return "NAM";
        case NodeType::Ir:     return "CAB IR";
        case NodeType::Gate:   return "BUILT-IN";
        case NodeType::Eq:     return "BUILT-IN";
        case NodeType::Gain:   return "BUILT-IN";
        case NodeType::Comp:   return "BUILT-IN";
        case NodeType::Split:  return "SPLIT";
        case NodeType::Chain:  return "CHAIN";
        case NodeType::Plugin: return "VST3";
    }
    return {};
}

juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}
} // namespace rig::theme

namespace rig
{
using namespace theme;

RigLookAndFeel::RigLookAndFeel()
{
    setColourScheme ({ bg, panel, card, border, text, accent, bg, accent, text });

    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, card);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff141416));
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, border);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, bg);
    setColour (juce::Slider::textBoxTextColourId, muted);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, bg);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, muted);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, border);
    setColour (juce::ComboBox::backgroundColourId, bg);
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
}

void RigLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcR = radius - 3.0f;
    const float angle = start + pos * (end - start);
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (border);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar knobs fill from the middle
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? start + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (end - start) : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? fill : dim);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR * 0.72f;
    g.setColour (juce::Colour (0xff24262b));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2);
    g.setColour (juce::Colour (0xff33363d));
    g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.25f, -knobR + 2.0f, 2.5f, knobR * 0.55f, 1.0f);
    g.setColour (text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void RigLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                       juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
    g.setColour (bg);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (r.withRight (pos), 3.0f);
    g.setColour (text);
    g.fillEllipse (pos - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
}

void RigLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (! on && over) base = cardHi;
    if (down) base = base.darker (0.15f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (on ? base : (over ? borderHi : border));
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
}

void RigLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

void RigLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const auto sw = r.removeFromLeft (36.0f).withSizeKeepingCentre (32.0f, 18.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : (over ? cardHi : card));
    g.fillRoundedRectangle (sw, 9.0f);
    g.setColour (on ? accent : border);
    g.drawRoundedRectangle (sw, 9.0f, 1.0f);
    const float kx = on ? sw.getRight() - 16.0f : sw.getX() + 2.0f;
    g.setColour (on ? juce::Colour (0xff141416) : muted);
    g.fillEllipse (kx, sw.getY() + 2.0f, 14.0f, 14.0f);
    g.setColour (text);
    g.setFont (font (13.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (6), juce::Justification::centredLeft, 1);
}

void RigLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (card);
    g.setColour (border);
    g.drawRect (0, 0, w, h);
}

void RigLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int thumbStart, int thumbSize, bool over, bool)
{
    auto r = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour (over ? borderHi.brighter (0.2f) : borderHi);
    g.fillRoundedRectangle (r.toFloat().reduced (2.0f), 3.0f);
}
} // namespace rig
