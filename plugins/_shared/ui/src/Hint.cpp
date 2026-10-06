// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The hint bar. Hint.h has the card, the precedence and the layout's promise.
 */
#include "Hint.h"

#include "ChildLights.h"
#include "UvType.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

const juce::String enDash = juce::String::fromUTF8 ("\xe2\x80\x93");
const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");

/* One stretch of the line in one colour, `before` px after the last. */
struct Run
{
    juce::String text;
    juce::Colour colour;
    float before;
};

std::vector<Clause> capped (const std::vector<Clause>& clauses)
{
    /* "Three clauses at most": a fourth is a window to simplify, never a
     * smaller font. */
    jassert (clauses.size() <= (size_t) Hint::maxClauses);
    return { clauses.begin(), clauses.begin() + (std::ptrdiff_t) juce::jmin (clauses.size(), (size_t) Hint::maxClauses) };
}

/*
 * <b>verb</b> rest<span class="sep">–</span><b>verb</b> rest ...: the verb in
 * ink, a space and the rest in ink-muted, an en dash in ink-dim 12px either
 * side between clauses. An info line is one clause whose rest keeps its dash.
 */
std::vector<Run> runsOf (const std::vector<Clause>& clauses)
{
    std::vector<Run> runs;
    for (size_t i = 0; i < clauses.size(); ++i)
    {
        if (i > 0)
            runs.push_back ({ enDash, c::inkDim, Hint::separatorMargin });
        runs.push_back ({ clauses[i].name, c::ink, i > 0 ? Hint::separatorMargin : 0.0f });
        if (clauses[i].rest.isNotEmpty())
            runs.push_back ({ " " + clauses[i].rest, c::inkMuted, 0.0f });
    }
    return runs;
}

float widthOf (const std::vector<Run>& runs)
{
    const auto font = uv::type::hint();
    float w = 0.0f;
    for (const auto& r : runs)
        w += r.before + uv::type::width (font, r.text);
    return w;
}

void drawRun (juce::Graphics& g, const juce::String& text, juce::Colour colour, float x,
              juce::Rectangle<float> line)
{
    /* Never JUCE's own ellipsis: the cut is drawRuns' to make, once, for the
     * whole line. The box is the run's width with room to spare. */
    const auto font = uv::type::hint();
    g.setFont (font);
    g.setColour (colour);
    g.drawText (text, juce::Rectangle<float> (x, line.getY(), uv::type::width (font, text) + 4.0f, line.getHeight()),
                juce::Justification::centredLeft, false);
}

/*
 * text-overflow: ellipsis, across the whole line: everything when it fits;
 * otherwise as much as fits with the ellipsis after it, cut inside whichever
 * run it falls in. The ellipsis is the bar's ink-muted, as CSS draws it in
 * the line's own colour.
 */
void drawRuns (juce::Graphics& g, const std::vector<Run>& runs, juce::Rectangle<float> cell)
{
    if (cell.getWidth() <= 0.0f)
        return;

    const auto font = uv::type::hint();
    const bool fits = widthOf (runs) <= cell.getWidth() + 0.01f;
    const float room = cell.getRight() - (fits ? 0.0f : uv::type::width (font, ellipsis));

    float x = cell.getX();
    for (const auto& r : runs)
    {
        const float start = x + r.before;
        const float w = uv::type::width (font, r.text);
        if (fits || start + w <= room)
        {
            drawRun (g, r.text, r.colour, start, cell);
            x = start + w;
            continue;
        }

        /* The longest head of this run that leaves room for the ellipsis. */
        int lo = 0, hi = r.text.length();
        while (lo < hi)
        {
            const int mid = (lo + hi + 1) / 2;
            if (start + uv::type::width (font, r.text.substring (0, mid)) <= room)
                lo = mid;
            else
                hi = mid - 1;
        }

        const auto head = r.text.substring (0, lo);
        if (head.isNotEmpty())
        {
            drawRun (g, head, r.colour, start, cell);
            x = start + uv::type::width (font, head);
        }
        drawRun (g, ellipsis, c::inkMuted, x, cell);
        return;
    }
}

juce::String plain (const std::vector<Clause>& clauses)
{
    juce::StringArray parts;
    for (const auto& cl : clauses)
        parts.add (cl.rest.isEmpty() ? cl.name : cl.name + " " + cl.rest);
    return parts.joinIntoString (" " + enDash + " ");
}
} // namespace

float clausesWidth (const std::vector<Clause>& clauses)
{
    return widthOf (runsOf (clauses));
}

Hint::Hint()
{
    addAndMakeVisible (sig);
    setSize (360, height);
}

Hint::~Hint() = default;

void Hint::setConventions (std::vector<Clause> clauses)
{
    if (clauses == conventions)
        return;
    conventions = std::move (clauses);
    resized();
    changed();
}

void Hint::setInfoLine (const juce::String& line)
{
    if (line == infoLine)
        return;
    infoLine = line;
    changed();
}

void Hint::setOutcome (std::optional<Clause> clause)
{
    if (clause == outcome)
        return;
    outcome = std::move (clause);
    changed();
}

HintContent Hint::content() const
{
    return hintClauses (capped (conventions), infoLine, outcome);
}

void Hint::setPadding (int horizontal)
{
    padding = horizontal;
    resized();
    repaint();
}

void Hint::setMotionSwitch (juce::Component* m)
{
    if (m == motion)
        return;
    if (motion != nullptr && motion->getParentComponent() == this)
        removeChildComponent (motion);
    motion = m;
    if (motion != nullptr)
        addAndMakeVisible (*motion);
    resized();
    repaint();
}

juce::Rectangle<int> Hint::tipsBounds() const
{
    return tips;
}

juce::Rectangle<int> Hint::motionBounds() const
{
    return motion != nullptr ? motion->getBounds() : juce::Rectangle<int>();
}

void Hint::resized()
{
    const int line = (int) uv::tok::type::hint.lineHeight;

    /* The Signature closes the bar on the right, level with the text. */
    const int sigWidth = Signature::preferredWidth();
    sig.setBounds (getWidth() - padding - sigWidth, lineTop, sigWidth, Signature::height);

    /* Everything left of it is the tips' and the switch's, the switch keeping
     * space-4 to either side: the tips give way first. */
    int end = sig.getX() - gap;
    if (motion != nullptr)
        end -= motion->getWidth() + gap;

    const int natural = (int) std::ceil (clausesWidth (capped (conventions)));
    tips = { padding, lineTop, juce::jlimit (0, juce::jmax (0, end - padding), natural), line };

    /* Centred on the text's line. */
    if (motion != nullptr)
        motion->setTopLeftPosition (tips.getRight() + gap, lineTop + (line - motion->getHeight()) / 2);
}

void Hint::paint (juce::Graphics& g)
{
    /* border-top: 1px solid line-100, the window's full width. */
    g.setColour (c::line100);
    g.fillRect (0, 0, getWidth(), (int) uv::tok::size::hairline);

    /* A line is laid over the conventions, which stay underneath, held:
     * they size the tips, and are not drawn while the line is. */
    const auto bar = content();
    drawRuns (g, runsOf (bar.info.has_value() ? std::vector<Clause> { *bar.info } : bar.clauses),
              tips.toFloat());

    paintChildLights (g, *this);
}

void Hint::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
}

void Hint::changed()
{
    repaint();

    /* What a screen reader reads as the bar's text: what is on show. */
    const auto bar = content();
    setTitle (plain (bar.info.has_value() ? std::vector<Clause> { *bar.info } : bar.clauses));
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
}

std::unique_ptr<juce::AccessibilityHandler> Hint::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText);
}

} // namespace ni::ui
