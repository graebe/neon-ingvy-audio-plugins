#pragma once
#include "Phosphor.h"

/*
 * The design system's components that are not a LookAndFeel override: a
 * LookAndFeel styles a control JUCE already has, and JUCE has no ring of
 * steps, no step grid and no hint bar.
 *
 * Each is a straight transcription of its card in the system. Where one
 * departs from the card it says so and why, at the point of departure.
 */

/* A hairlined bg-100 box grouping the controls of ONE function. Never one per
 * control, and never nested: both are in the card. */
class PanelBox : public juce::Component
{
public:
    explicit PanelBox (juce::String title);
    void paint (juce::Graphics&) override;
    /* Where children go: inside the padding, below the title. */
    juce::Rectangle<int> contentArea() const;
    static constexpr int titleBlock = 16 + 12;      /* title height + its margin */

private:
    juce::String name;
};

/*
 * A circle of segments, one per step, with the window's one big number in the
 * middle. Display only -- the count is changed by the Length knob.
 *
 * Geometry from Ring/preview.html at 240px: track circle r=99, segments on
 * r=108 at stroke-width 12, each spanning 12 degrees of its 15-degree slot
 * (so 80% fill, 20% gap, whatever the count).
 */
class RingDisplay : public juce::Component
{
public:
    struct Model
    {
        int   length   = 16;
        int   playhead = -1;        /* -1 when stopped */
        bool  moving   = false;
        std::function<bool(int)> stepOn;    /* is step i lit */
        juce::String centre = "16";
        juce::String label  = "STEPS";
    };

    void setModel (Model m) { model = std::move (m); repaint(); }
    void paint (juce::Graphics&) override;

private:
    Model model;
};

/*
 * The sequencer. Sixteen 40px steps per row at space-2 gaps; the first step
 * of every beat carries a line-200 border so bars read without numbers.
 *
 * The card says patterns over 32 steps become PAGES rather than smaller
 * steps. This build keeps every step on screen instead, and honours the rule
 * the paging exists to protect -- a step is never scaled -- by growing the
 * WINDOW with the row count. So the step is always exactly 40px, and the
 * editor resizes on the eight occasions the row count changes rather than
 * shrinking 128 cells into the space for 32.
 */
class StepGridView : public juce::Component
{
public:
    struct Model
    {
        int   length   = 16;
        int   cursor   = 0;
        int   playhead = -1;
        bool  moving   = false;
        std::function<bool(int)>  stepOn;
        std::function<bool(int)>  stepTied;
        std::function<float(int)> stepAmount;
    };

    static constexpr int cols = 16;

    std::function<void(int index, bool shift)> onToggle;
    std::function<void(int index, float amount)> onAmount;

    void setModel (Model m) { model = std::move (m); repaint(); }
    static int rowsFor (int length) { return juce::jmax (1, (length + cols - 1) / cols); }
    static int heightFor (int length);
    static constexpr int width = cols * phosphor::size::step + (cols - 1) * phosphor::space::s2;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> stepBounds (int i) const;
    int stepAt (juce::Point<int>) const;
    Model model;
};

/*
 * The one line at the bottom edge of every window: its interaction
 * conventions, verb in ink and the rest in ink-muted, clauses separated by en
 * dashes in ink-dim. Three clauses at most -- "if a window needs more, the
 * interaction is too clever".
 */
class HintBar : public juce::Component
{
public:
    /* Each clause is (verb, rest). */
    void setClauses (std::vector<std::pair<juce::String, juce::String>> c);
    void paint (juce::Graphics&) override;
    static constexpr int height = 14 + 8 + 8 + 1;   /* line + padding + the rule */

private:
    std::vector<std::pair<juce::String, juce::String>> clauses;
};

/* A label in the system's `label` style: 11px, uppercase, tracked, ink-muted,
 * centred over its control. Its own component so that tracking (which
 * juce::Label cannot do) is not re-implemented per call site. */
class TrackedLabel : public juce::Component
{
public:
    TrackedLabel (juce::String text, bool asTitle = false);
    void setText (juce::String t) { text = std::move (t); repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::String text;
    bool title;
};
