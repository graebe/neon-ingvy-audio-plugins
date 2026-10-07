// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A choice and its list. Select.h has how it behaves and where its list opens.
 */
#include "Select.h"

#include "ChildLights.h"
#include "Popup.h"
#include "UvIcons.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hair = uv::tok::stroke::strokeHair;
constexpr int hairPx = (int) uv::tok::size::hairline;
constexpr int controlH = (int) uv::tok::size::controlH;
constexpr int pad = (int) uv::tok::space::space3;     // padding: 0 28px 0 12px
constexpr int caretRoom = (int) uv::tok::size::controlH;
constexpr int caretInset = 6;                           // .select .icon { right: 6px }
constexpr int glyph = 16;

/* An index the options hold, or -1 for none: a negative stays none, anything
 * else is clamped to the list. */
int indexIn (const juce::StringArray& options, int i)
{
    return i < 0 ? -1 : juce::jlimit (0, juce::jmax (0, options.size() - 1), i);
}

/* What a screen reader hears of a select: its option, which it may set by
 * name, and whether its list is open. */
class SelectAccessibility final : public juce::AccessibilityHandler
{
public:
    explicit SelectAccessibility (Select& s)
        : juce::AccessibilityHandler (s, juce::AccessibilityRole::comboBox, actionsFor (s),
                                      Interfaces { std::make_unique<Value> (s) }),
          select (s)
    {
    }

    juce::AccessibleState getCurrentState() const override
    {
        auto state = juce::AccessibilityHandler::getCurrentState().withExpandable();
        return select.isOpen() ? state.withExpanded() : state.withCollapsed();
    }

private:
    class Value final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit Value (Select& s) : select (s) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override
        {
            return select.getOptions()[select.getIndex()];
        }
        void setValueAsString (const juce::String& text) override
        {
            const int i = select.getOptions().indexOf (text);
            if (i >= 0 && i != select.getIndex() && select.onChange)
                select.onChange (i);
        }

    private:
        Select& select;
    };

    static juce::AccessibilityActions actionsFor (Select& s)
    {
        juce::AccessibilityActions actions;
        actions.addAction (juce::AccessibilityActionType::press,
                           [&s] { s.isOpen() ? s.close() : s.open(); });
        actions.addAction (juce::AccessibilityActionType::showMenu, [&s] { s.open(); });
        return actions;
    }

    Select& select;
};
} // namespace

/* =============================================================== list == */

SelectList::SelectList (const juce::StringArray& o, int cur)
    : options (o), current (cur), highlighted (cur)
{
    /* The keyboard stays with the Select; a click on a row must not hand it
     * to some other control on its way (JUCE passes a click's focus up to a
     * parent that will take it), which would close the list mid-click. */
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Options");
}

SelectList::~SelectList() = default;

int SelectList::heightFor (int count)
{
    return juce::jmax (0, count) * rowHeight + 2 * hairPx;
}

int SelectList::numShown() const
{
    const int fit = juce::jmax (1, (getHeight() - 2 * hairPx) / rowHeight);
    return juce::jmin (getNumRows(), fit);
}

juce::Rectangle<int> SelectList::rowBounds (int row) const
{
    return { hairPx, hairPx + (row - first) * rowHeight, getWidth() - 2 * hairPx, rowHeight };
}

int SelectList::rowAt (juce::Point<int> p) const
{
    if (! getLocalBounds().reduced (hairPx).contains (p))
        return -1;
    const int row = first + (p.y - hairPx) / rowHeight;
    return row < first + numShown() ? row : -1;
}

void SelectList::setHighlighted (int row)
{
    row = juce::jlimit (-1, getNumRows() - 1, row);
    if (row == highlighted)
        return;
    highlighted = row;
    scrollToShow (row);
    repaint();
}

void SelectList::scrollTo (int firstRow)
{
    const int top = juce::jlimit (0, juce::jmax (0, getNumRows() - numShown()), firstRow);
    if (top != first)
    {
        first = top;
        repaint();
    }
}

void SelectList::scrollToShow (int row)
{
    if (row < 0)
        return;
    if (row < first)
        scrollTo (row);
    else if (row >= first + numShown())
        scrollTo (row - numShown() + 1);
}

void SelectList::resized()
{
    scrollTo (first);
    scrollToShow (highlighted >= 0 ? highlighted : current);
}

bool SelectList::handleKey (const juce::KeyPress& key)
{
    const int last = getNumRows() - 1;
    const int from = highlighted >= 0 ? highlighted : current;
    const int code = key.getKeyCode();

    if (code == juce::KeyPress::upKey)            setHighlighted (juce::jmax (0, from - 1));
    else if (code == juce::KeyPress::downKey)     setHighlighted (juce::jmin (last, from + 1));
    else if (code == juce::KeyPress::homeKey)     setHighlighted (0);
    else if (code == juce::KeyPress::endKey)      setHighlighted (last);
    else if (code == juce::KeyPress::pageUpKey)   setHighlighted (juce::jmax (0, from - numShown()));
    else if (code == juce::KeyPress::pageDownKey) setHighlighted (juce::jmin (last, from + numShown()));
    else if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
    {
        if (onChoose)
            onChoose (from);   // may delete this
    }
    else
        return false;

    return true;
}

void SelectList::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (c::bg100);

    const auto font = uv::type::value();
    for (int row = first; row < first + numShown(); ++row)
    {
        const auto box = rowBounds (row).toFloat();
        if (row == highlighted)
        {
            g.setColour (c::bg300);
            g.fillRect (box);
        }
        /* "There is no separate selected colour: selected is uv." */
        uv::type::draw (g, options[row], box.reduced ((float) pad, 0.0f), font,
                        row == current ? c::uv : c::ink);
    }

    /* More rows than the window holds: a quiet thumb, as the system scrolls
     * a list (UvLookAndFeel's scrollbar), on the right inside the hairline. */
    if (numShown() < getNumRows())
    {
        const auto track = r.reduced (hair).removeFromRight (uv::tok::space::space2).reduced (2.0f, 2.0f);
        const float share = (float) numShown() / (float) getNumRows();
        const float top = track.getY() + track.getHeight() * (float) first / (float) getNumRows();
        g.setColour (c::line200);
        g.fillRect (track.withY (top).withHeight (track.getHeight() * share));
    }

    g.setColour (c::uv);
    g.drawRect (r, hair);
}

void SelectList::mouseMove (const juce::MouseEvent& e)
{
    /* Under the pointer, without scrolling to it: the pointer is on it. */
    const int row = rowAt (e.getPosition());
    if (row != highlighted)
    {
        highlighted = row;
        repaint();
    }
}

void SelectList::mouseExit (const juce::MouseEvent&)
{
    if (highlighted != -1)
    {
        highlighted = -1;
        repaint();
    }
}

void SelectList::mouseDown (const juce::MouseEvent& e)
{
    if (onPress)
        onPress (e);   // may delete this
}

void SelectList::mouseUp (const juce::MouseEvent& e)
{
    const int row = rowAt (e.getPosition());
    if (row >= 0 && onChoose)
        onChoose (row);   // may delete this
}

void SelectList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    /* A row per tenth of a wheel's travel, kept between events so a
     * trackpad's small deltas add up. */
    wheel += w.deltaY;
    const int rows = (int) (wheel / 0.1f);
    if (rows != 0)
    {
        wheel -= (float) rows * 0.1f;
        scrollTo (first - rows);
    }
}

std::unique_ptr<juce::AccessibilityHandler> SelectList::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::list);
}

/* ============================================================= select == */

Select::Select()
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

Select::~Select()
{
    close();
}

void Select::setOptions (const juce::StringArray& o)
{
    close();
    options = o;
    index = indexIn (options, index);
    repaint();
}

void Select::setIndex (int i)
{
    i = indexIn (options, i);
    if (i == index)
        return;
    index = i;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void Select::setLabel (const juce::String& text, int width)
{
    if (getTitle().isEmpty() || getTitle() == label)
        setTitle (text);
    label = text;
    labelWidth = width;
    repaint();
}

void Select::setFieldWidth (int w)
{
    fieldWidth = w;
    repaint();
}

int Select::idealWidth() const
{
    return field().getRight();
}

juce::Rectangle<int> Select::field() const
{
    /* .select-group { gap: var(--s2) }, the field centred on the row. */
    const int x = label.isEmpty() ? 0 : labelWidth + (int) uv::tok::space::space2;
    return { x, (getHeight() - controlH) / 2, fieldWidth, controlH };
}

/* ============================================================== list == */

void Select::open()
{
    if (list != nullptr || options.isEmpty() || ! isEnabled())
        return;

    auto& window = windowOf (*this);
    if (&window == this)
        return;   // nowhere to open in

    const auto anchor = window.getLocalArea (this, field());
    const auto at = placeInside (window.getLocalBounds(), anchor, fieldWidth,
                                 SelectList::heightFor (options.size()), hairPx);

    list = std::make_unique<SelectList> (options, index);
    list->onChoose = [this] (int row) { choose (row); };
    list->onPress = [this] (const juce::MouseEvent& e) { return secondPress (e); };
    popup = std::make_unique<Popup> ([this] (const juce::MouseEvent& e)
                                     {
                                         if (! secondPress (e))
                                             close();
                                     });
    popup->show (window, *list, at);

    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::structureChanged);
}

void Select::close()
{
    if (list == nullptr)
        return;

    /* Out of the window first, then gone: this may run inside one of the
     * list's or the layer's own mouse callbacks, which touch nothing after
     * calling here. */
    auto layer = std::move (popup);
    auto rows = std::move (list);
    layer.reset();
    rows.reset();

    pressOpened = false;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::structureChanged);
}

void Select::choose (int row)
{
    close();
    if (row != index && row >= 0 && row < options.size() && onChange)
        onChange (row);   // may delete this
}

bool Select::secondPress (const juce::MouseEvent& e)
{
    /* The second press of a double-click on the field: the first opened the
     * list, so this one reaches the list or its layer -- whichever is over
     * the field -- and comes here first. Not a row, not a dismissal: the
     * reset. */
    const auto at = getLocalPoint (e.eventComponent, e.position);
    if (e.getNumberOfClicks() < 2 || ! isEnabled() || ! field().toFloat().contains (at))
        return false;

    close();
    if (onReset)
        onReset();   // may delete this
    return true;
}

/* ============================================================ pointer == */

void Select::setFieldHovered (bool over)
{
    if (fieldHovered != over)
    {
        fieldHovered = over;
        repaint();
    }
}

void Select::mouseEnter (const juce::MouseEvent& e) { setFieldHovered (field().toFloat().contains (e.position)); }
void Select::mouseMove (const juce::MouseEvent& e)  { setFieldHovered (field().toFloat().contains (e.position)); }
void Select::mouseExit (const juce::MouseEvent&)    { setFieldHovered (false); }

void Select::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();
    relight (*this);

    /* The list opens on the press, as a menu does. */
    if (! isEnabled() || ! e.mods.isLeftButtonDown() || ! field().toFloat().contains (e.position))
        return;

    if (isOpen())
    {
        close();
        return;
    }
    open();
    pressOpened = isOpen();
}

void Select::mouseDrag (const juce::MouseEvent& e)
{
    /* Press, drag onto a row: the row under the pointer, as in a menu. The
     * drag is still this field's, wherever the pointer is. */
    if (list == nullptr || ! pressOpened)
        return;
    list->setHighlighted (list->rowAt (list->getLocalPoint (this, e.position).toInt()));
}

void Select::mouseUp (const juce::MouseEvent& e)
{
    if (list == nullptr || ! pressOpened)
        return;
    pressOpened = false;

    /* ... and release on it: chosen. A click without a drag leaves the list
     * open to choose from. */
    if (! e.mouseWasDraggedSinceMouseDown())
        return;
    const int row = list->rowAt (list->getLocalPoint (this, e.position).toInt());
    if (row >= 0)
        choose (row);   // may delete this
}

/* =========================================================== keyboard == */

bool Select::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    const int code = key.getKeyCode();

    if (list != nullptr)
    {
        if (code == juce::KeyPress::escapeKey || code == juce::KeyPress::tabKey)
        {
            close();
            return true;
        }
        focus.keyUsed();
        relight (*this);
        if (const int row = typedOption (key); row >= -1)
        {
            if (row >= 0)
                list->setHighlighted (row);
            return true;
        }
        return list->handleKey (key);   // may delete the list
    }

    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey
        || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        focus.keyUsed();
        relight (*this);
        open();
        return true;
    }

    if (const int row = typedOption (key); row >= -1)
    {
        focus.keyUsed();
        relight (*this);
        if (row >= 0)
            choose (row);   // may delete this
        return true;
    }
    return false;
}

int Select::typedOption (const juce::KeyPress& key)
{
    /* Not a character -- a shortcut, a navigation key, Space, which opens and
     * chooses -- is no typing: -2. */
    const auto ch = key.getTextCharacter();
    const auto mods = key.getModifiers();
    if (ch <= ' ' || ch == 0x7f || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return -2;

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - typedAt > typeAheadMs)
        typed.clear();
    typedAt = now;
    typed += juce::String::charToString (ch).toLowerCase();

    /* One key, or the same key again: the next option that starts with it,
     * after the one on show. Several: the first that starts with them all,
     * from the one on show, which may already. */
    const bool oneKey = typed.containsOnly (typed.substring (0, 1));
    const auto prefix = oneKey ? typed.substring (0, 1) : typed;
    const int shown = list != nullptr && list->getHighlighted() >= 0 ? list->getHighlighted() : index;
    const int from = shown < 0 ? 0 : shown + (oneKey ? 1 : 0);
    const int n = options.size();
    for (int i = 0; i < n; ++i)
        if (const int row = (from + i) % n; options[row].toLowerCase().startsWith (prefix))
            return row;
    return -1;   // typed, and nothing starts so
}

void Select::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    relight (*this);
}

void Select::focusLost (FocusChangeType)
{
    /* The keyboard gone elsewhere -- the host's, another control's: an open
     * list has nobody left to answer it. */
    close();
    focus.focusLost();
    relight (*this);
}

void Select::enablementChanged()
{
    if (! isEnabled())
        close();
    setMouseCursor (isEnabled() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

/* ============================================================== paint == */

void Select::paint (juce::Graphics& g)
{
    const bool live = isEnabled();
    const auto box = field().toFloat();

    if (label.isNotEmpty())
    {
        /* white-space: nowrap, and no ellipsis: a label is a word. */
        const auto& style = uv::tok::type::label;
        const auto text = uv::type::cased (style, label);
        const auto font = uv::type::font (style);
        const float w = juce::jmax ((float) labelWidth, uv::type::width (font, text) + 2.0f);
        uv::type::draw (g, text, { 0.0f, 0.0f, w, (float) getHeight() }, font, live ? c::inkMuted : c::inkDim);
    }

    /* The field's focus ring, over the label as CSS paints the field after
     * it; the part past the select is paintLight()'s. */
    if (isFocusVisible (*this))
        uv::light::glowFocus (g, box);

    g.setColour (! live ? c::bg100 : (fieldHovered ? c::bg300 : c::bg200));
    g.fillRect (box);
    g.setColour (! live ? c::line100 : isOpen() ? c::uv : fieldHovered ? c::inkDim : c::line200);
    g.drawRect (box, hair);

    /* One line, and cut if it has to be: a wrapped value would make the
     * control twice as tall (.select-value). */
    /* None is the em dash, as a reading with nothing to read. */
    if (options.size() > 0)
        uv::type::draw (g, index >= 0 ? options[index] : juce::String::fromUTF8 ("\xe2\x80\x94"),
                        box.withTrimmedLeft (hair + (float) pad).withTrimmedRight (hair + (float) caretRoom),
                        uv::type::value(), live ? c::ink : c::inkDim);

    uv::drawIcon (g, "chevron", live ? c::inkMuted : c::inkDim,
                  { box.getRight() - hair - (float) caretInset - (float) glyph,
                    box.getCentreY() - (float) glyph * 0.5f, (float) glyph, (float) glyph });
}

void Select::paintLight (juce::Graphics& g)
{
    if (! isFocusVisible (*this))
        return;
    juce::Graphics::ScopedSaveState state (g);
    excludeOwnBounds (g, *this);
    uv::light::glowFocus (g, field().toFloat());
}

std::unique_ptr<juce::AccessibilityHandler> Select::createAccessibilityHandler()
{
    return std::make_unique<SelectAccessibility> (*this);
}

} // namespace ni::ui
