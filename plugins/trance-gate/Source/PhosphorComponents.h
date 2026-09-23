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

/*
 * A KNOB THAT CAN BE DRAGGED FINELY.
 *
 * "Knobs and sliders: drag vertically, SHIFT-DRAG FOR FINE, double-click to
 * reset, click the readout to type" is the system's own interaction note, and
 * shift-fine was the half of it JUCE does not do for free.
 *
 * It matters most where one scale serves two units: attack/decay/release run
 * 0..500, so JUCE's default 250px-for-the-whole-range is 0.4% of a step per
 * pixel -- enough to land between percents, but not to land ON one.
 *
 * The sensitivity is chosen at mouseDown and not re-read during the drag:
 * JUCE measures the whole delta from the press position, so changing it
 * halfway rescales everything since the press and the value jumps. Shift is
 * therefore held BEFORE the press, which is what "shift-drag" means anyway.
 */
class PhosphorKnob : public juce::Slider
{
public:
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int coarse = 250;   /* JUCE's default, and the system's feel */
    static constexpr int fine   = 2500;  /* ten times the travel for the same range */
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


/*
 * ------------------------------------------------------------- plots --
 *
 * NOT FROM THE SYSTEM'S CARD SET. There is no plot card, and two plots in one
 * window with two copies of the same drawing is exactly how a design system
 * starts to drift -- so the shared parts live here, in the file that already
 * holds the components the system does not have, and each departure from the
 * cards is a token used the way a card uses it.
 *
 * A plot is a bg-000 well with a hairline, a hint-style caption, and a curve
 * drawn in phosphor over a phosphor-glow fill.
 */
namespace phosphor::plot
{
/* The padding inside the well, and the room the caption takes above the
 * curve. Both on the 4px grid, bar the caption's 14 which is the hint
 * style's line height. */
constexpr int inset    = 6;
constexpr int captionH = 14;

/* Fills the well, hairlines it, draws the caption, and returns the rectangle
 * the curve belongs in. */
juce::Rectangle<float> well (juce::Graphics&, juce::Rectangle<float> bounds,
                             const juce::String& caption);

/* The "nothing to draw yet" state, so it reads the same in both plots. */
void empty (juce::Graphics&, juce::Rectangle<int> bounds);

/* A vertical rule at a fraction of the plot's width -- a position, not a
 * value, which is why it is never phosphor. */
void rule (juce::Graphics&, juce::Rectangle<float> plot, double xFrac,
           juce::Colour, float thickness);

/*
 * A TIME AXIS ALONG THE BOTTOM OF A PLOT: ticks in `line-200`, labels in the
 * hint style, and the unit on the last one only, as the system prints every
 * other value.
 *
 * `spanMs` is what the full width covers. `markMs`, when positive, is a
 * landmark that ALWAYS gets its exact value -- the step edge, which is the
 * number that decides whether a release fits, and therefore the one that must
 * not be rounded away. A ladder tick landing within a label's width of it is
 * dropped rather than overprinted.
 *
 * The interval comes off a ladder (1, 2, 5, 10, 20, 25, 50, 100 ... ms)
 * rather than being a fraction of the span, because the span is whatever the
 * rate makes it: a 1/128 step is ~16 ms and a 1/1T one is ~2 s, and fifths of
 * either are numbers nobody can read. `axisHeight` is what to reserve.
 */
constexpr int axisHeight = 12;
void axis (juce::Graphics&, juce::Rectangle<float> plot, double spanMs,
           double markMs);

/*
 * n samples down to exactly `cols` (min, max) pairs.
 *
 * MIN AND MAX, NOT A MEAN OR A PICK. At 128 steps a Gate of 5% is a third of
 * a pixel wide, and averaging or sampling loses it entirely -- the plot would
 * quietly report a pattern that is not the one playing. Keeping both bounds
 * costs one more vector and cannot drop a feature however narrow.
 *
 * Fewer samples than columns is interpolation rather than decimation, and it
 * is close, and was once exact: the envelope was piecewise linear in time, so
 * a polyline through the samples WAS the curve. With a curved stage it is an
 * approximation instead -- accurate in practice only because a render is
 * ~44,100 samples against ~228 columns, so this decimates and essentially
 * never interpolates. lo == hi throughout in the interpolating case.
 */
void decimate (const float* v, int n, int cols,
               std::vector<float>& lo, std::vector<float>& hi);

/*
 * The curve itself, built once per refresh in UNIT SPACE (x and y both 0..1,
 * y measured downwards) and mapped at paint time.
 *
 * Unit space is what lets the global Amount be a transform rather than a
 * rebuild: the engine's gain is
 *
 *     m = 1 - amount*(1 - g) = floor + (1 - floor)*g,   floor = 1 - amount
 *
 * which is affine in g, so raising the floor is an AffineTransform and never
 * a re-render. Dragging Amount therefore costs a repaint and nothing else.
 */
struct Curve
{
    juce::Path under;    /* the glow fill, closed to the baseline */
    juce::Path band;     /* min..max, where decimation kept a range */
    juce::Path hull;     /* the stroked line: max going out, min coming back */

    void build (const std::vector<float>& lo, const std::vector<float>& hi);
    void draw (juce::Graphics&, juce::Rectangle<float> plot, float floorLevel) const;
    /* The line alone, in a colour of your choosing and with no fill under it
     * -- for a trace that is context rather than the subject. */
    void drawOutline (juce::Graphics&, juce::Rectangle<float> plot, float floorLevel,
                      juce::Colour, float thickness) const;
    bool empty() const { return hull.isEmpty(); }
};
}
