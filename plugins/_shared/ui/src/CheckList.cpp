// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A dropdown of switches. CheckList.h has why it exists and how it behaves.
 */
#include "CheckList.h"

#include "ChildLights.h"
#include "Popup.h"
#include "Toggle.h"
#include "UvIcons.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hair = uv::tok::stroke::strokeHair;
constexpr int hairPx = (int) uv::tok::size::hairline;
constexpr int controlH = (int) uv::tok::size::controlH;
constexpr int s1 = (int) uv::tok::space::space1;
constexpr int s2 = (int) uv::tok::space::space2;
constexpr int s3 = (int) uv::tok::space::space3;

/* .checklist-panel { min-width: 160px; max-height: 220px } */
constexpr int panelMinWidth = 160;
constexpr int panelMaxHeight = 220;
/* A row: a switch's 28px line in space-1 of padding. */
constexpr int rowHeight = controlH + 2 * s1;

juce::Font hintFont() { return uv::type::font (uv::tok::type::hint); }
} // namespace

/* =============================================================== face == */

class CheckList::Face final : public Pressable
{
public:
    explicit Face (CheckList& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool live = isEnabled();

        g.setColour (! live ? c::bg100 : (isHovered() ? c::bg300 : c::bg200));
        g.fillRect (r);
        g.setColour (! live ? c::line100 : (owner.isOpen() ? c::uv : c::line200));
        g.drawRect (r, hair);

        /* .select { padding: 0 28px 0 12px }, the chevron 6px in. */
        uv::type::draw (g, owner.summary, r.withTrimmedLeft (hair + (float) s3).withTrimmedRight (hair + (float) controlH),
                        uv::type::value(), live ? c::ink : c::inkDim);
        uv::drawIcon (g, "chevron", live ? c::inkMuted : c::inkDim,
                      { r.getRight() - hair - 6.0f - 16.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f });
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.isKeyCode (juce::KeyPress::escapeKey) && owner.isOpen())
        {
            focus.keyUsed();
            owner.close();
            return true;
        }
        /* Down or Up opens the panel and goes into it, at its first switch or
         * its last: the panel is the window's, not the face's, so Tab from
         * the face does not reach it as it does on the web page. */
        if (key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::upKey))
        {
            focus.keyUsed();
            owner.open();
            owner.focusSwitch (key.isKeyCode (juce::KeyPress::downKey) ? 0 : -1, 1);
            return true;
        }
        return Pressable::keyPressed (key);
    }

    void refresh() { stateChanged(); }
    void showFocusFromKeys()
    {
        if (isShowing())
            grabKeyboardFocus();
        focus.keyUsed();
        stateChanged();
    }

protected:
    void pressed() override { owner.toggleOpen(); }

private:
    CheckList& owner;
};

/* ================================================================ row == */

class CheckList::Row final : public juce::Component,
                             public Luminous
{
public:
    Row (CheckList& owner, const Option& o) : option (o), toggle (o.name), list (owner)
    {
        toggle.setOn (owner.isSelected (o.id));
        toggle.setEnabled (! o.disabled);
        toggle.onChange = [&owner, id = o.id] (bool on) { owner.choose (id, on); };
        addAndMakeVisible (toggle);
        /* Hover is the row's and everything in it, as CSS :hover is: the
         * row hears the switch's pointer as well as its own. */
        addMouseListener (this, true);
        setMouseClickGrabsKeyboardFocus (false);
    }

    ~Row() override { removeMouseListener (this); }

    void mouseEnter (const juce::MouseEvent& e) override { point (e); }
    void mouseMove (const juce::MouseEvent& e) override { point (e); }
    void mouseExit (const juce::MouseEvent& e) override { point (e); }

    int idealWidth() const
    {
        const int hint = option.hint.isEmpty() ? 0 : s3 + (int) std::ceil (uv::type::width (hintFont(), option.hint));
        return s1 + toggle.idealWidth() + hint + s1;
    }

    void resized() override
    {
        toggle.setBounds (s1, s1, toggle.idealWidth(), controlH);
    }

    void paint (juce::Graphics& g) override
    {
        if (hovered && ! option.disabled)
        {
            g.setColour (c::bg300);
            g.fillRect (getLocalBounds());
        }

        if (option.hint.isNotEmpty())
            uv::type::draw (g, option.hint, getLocalBounds().reduced (s1, 0).toFloat(), hintFont(),
                            option.disabled ? c::inkDim : c::inkMuted, juce::Justification::centredRight);

        paintChildLights (g, *this);
    }

    void paintLight (juce::Graphics& g) override { forwardChildLights (g, *this); }

    /* Up and Down on a switch: the one before or after it, skipping those
     * that are refused. What the switch does not take comes here. */
    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool down = key.isKeyCode (juce::KeyPress::downKey);
        if (! down && ! key.isKeyCode (juce::KeyPress::upKey))
            return false;
        list.focusSwitch (list.indexOf (option.id) + (down ? 1 : -1), down ? 1 : -1);
        return true;
    }

    void setOn (bool on) { toggle.setOn (on); }
    bool isHovered() const noexcept { return hovered; }

    const Option option;
    Toggle toggle;

private:
    void point (const juce::MouseEvent& e)
    {
        const bool over = getLocalBounds().contains (e.getEventRelativeTo (this).getPosition());
        if (over != hovered)
        {
            hovered = over;
            repaint();
        }
    }

    CheckList& list;
    bool hovered = false;
};

/* ============================================================== panel == */

class CheckList::Panel final : public juce::Component
{
public:
    explicit Panel (CheckList& o) : owner (o)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setAlwaysOnTop (true);

        for (const auto& option : owner.options)
            content.addAndMakeVisible (rows.add (new Row (owner, option)));

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setWantsKeyboardFocus (false);
        addAndMakeVisible (viewport);
    }

    ~Panel() override { viewport.setViewedComponent (nullptr, false); }

    juce::Point<int> idealSize() const
    {
        int w = 0;
        for (auto* r : rows)
            w = juce::jmax (w, r->idealWidth());
        if (rows.isEmpty())
            w = (int) std::ceil (uv::type::width (hintFont(), owner.emptyText)) + 2 * s2;

        const int n = rows.size();
        const int h = n > 0 ? n * rowHeight + (n - 1) * s2
                            : (int) uv::tok::type::hint.lineHeight + 2 * s2;
        return { juce::jmax (panelMinWidth, w + 2 * s2 + 2 * hairPx),
                 juce::jmin (panelMaxHeight, h + 2 * s2 + 2 * hairPx) };
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().reduced (hairPx));

        const int n = rows.size();
        const int h = n > 0 ? n * rowHeight + (n - 1) * s2 : (int) uv::tok::type::hint.lineHeight + 2 * s2;
        content.setSize (viewport.getWidth(), h + 2 * s2);
        if (content.getHeight() > viewport.getHeight())
            content.setSize (viewport.getMaximumVisibleWidth(), content.getHeight());

        int y = s2;
        for (auto* r : rows)
        {
            r->setBounds (s2, y, content.getWidth() - 2 * s2, rowHeight);
            y += rowHeight + s2;
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::bg100);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        g.setColour (c::uv);
        g.drawRect (getLocalBounds().toFloat(), hair);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        /* Escape on a switch in the panel reaches here: close, and the
         * keyboard goes back to the face. */
        if (key.isKeyCode (juce::KeyPress::escapeKey))
        {
            owner.close();   // deletes this
            return true;
        }
        return false;
    }

    Row* rowFor (int id) const
    {
        for (auto* r : rows)
            if (r->option.id == id)
                return r;
        return nullptr;
    }

    void refresh()
    {
        for (auto* r : rows)
            r->setOn (owner.isSelected (r->option.id));
    }

    /* Scrolls the least that shows all of `row`. */
    void reveal (const Row& row)
    {
        const auto r = row.getBoundsInParent().expanded (0, s2);
        auto pos = viewport.getViewPosition();
        if (r.getY() < pos.y)
            pos.y = r.getY();
        else if (r.getBottom() > pos.y + viewport.getViewHeight())
            pos.y = r.getBottom() - viewport.getViewHeight();
        viewport.setViewPosition (pos);
    }

    const juce::OwnedArray<Row>& getRows() const noexcept { return rows; }

private:
    /* The rows' container in the viewport: transparent, it paints the
     * switches' light over the panel's ground -- and, with nothing to list,
     * says so. */
    struct Content final : public juce::Component
    {
        explicit Content (CheckList& o) : owner (o) {}
        void paint (juce::Graphics& g) override
        {
            if (getNumChildComponents() == 0)
                uv::type::draw (g, owner.emptyText,
                                getLocalBounds().reduced (s2).reduced (s2).toFloat()
                                    .withHeight (uv::tok::type::hint.lineHeight),
                                hintFont(), c::inkMuted);
            paintChildLights (g, *this);
        }
        CheckList& owner;
    };

    CheckList& owner;
    juce::Viewport viewport;
    Content content { owner };
    juce::OwnedArray<Row> rows;
};

/* ============================================================ outside == */

/* The window's ear for a press, while the panel is open. */
class CheckList::Outside final : public juce::MouseListener
{
public:
    explicit Outside (CheckList& o) : owner (o) {}
    void mouseDown (const juce::MouseEvent& e) override { owner.pressedInWindow (e.eventComponent); }

private:
    CheckList& owner;
};

/* ========================================================== checklist == */

CheckList::CheckList()
    : faceButton (std::make_unique<Face> (*this)),
      outside (std::make_unique<Outside> (*this))
{
    addAndMakeVisible (*faceButton);
}

CheckList::~CheckList()
{
    close();
}

void CheckList::setOptions (std::vector<Option> o)
{
    const bool wasOpen = isOpen();
    close();
    options = std::move (o);
    if (wasOpen)
        open();
}

void CheckList::setSelected (std::vector<int> ids)
{
    selected = std::move (ids);
    if (panel != nullptr)
        panel->refresh();
}

bool CheckList::isSelected (int id) const
{
    return std::find (selected.begin(), selected.end(), id) != selected.end();
}

void CheckList::setSummary (const juce::String& text)
{
    summary = text;
    faceButton->setTitle (text);
    refreshFace();
}

void CheckList::setEmptyText (const juce::String& text)
{
    emptyText = text;
}

void CheckList::setLabel (const juce::String& text, int width)
{
    if (getTitle().isEmpty() || getTitle() == label)
        setTitle (text);
    label = text;
    labelWidth = width;
    resized();
    repaint();
}

void CheckList::setFaceWidth (int w)
{
    faceWidth = w;
    resized();
}

juce::Rectangle<int> CheckList::face() const
{
    const int x = label.isEmpty() ? 0 : labelWidth + s2;
    return { x, (getHeight() - controlH) / 2, faceWidth, controlH };
}

int CheckList::idealWidth() const
{
    return face().getRight();
}

void CheckList::resized()
{
    faceButton->setBounds (face());
}

void CheckList::refreshFace()
{
    faceButton->refresh();
}

juce::Component* CheckList::getPanel() const noexcept
{
    return panel.get();
}

Toggle* CheckList::getSwitch (int id) const
{
    if (panel == nullptr)
        return nullptr;
    auto* row = panel->rowFor (id);
    return row != nullptr ? &row->toggle : nullptr;
}

int CheckList::indexOf (int id) const
{
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i].id == id)
            return (int) i;
    return -1;
}

Toggle* CheckList::focusSwitch (int from, int direction)
{
    if (panel == nullptr || options.empty())
        return nullptr;
    const int n = (int) options.size();
    /* -1 is the last; past either end stays where it is, as a list does. */
    int i = from < 0 ? n - 1 : from;
    for (; i >= 0 && i < n; i += direction)
        if (! options[(size_t) i].disabled)
        {
            auto* row = panel->getRows()[i];
            panel->reveal (*row);
            if (row->toggle.isShowing())
                row->toggle.grabKeyboardFocus();
            return &row->toggle;
        }
    return nullptr;
}

/* ============================================================= choice == */

void CheckList::choose (int id, bool on)
{
    /* The others as they are, this one added at the end or taken out. */
    std::vector<int> next;
    for (const int x : selected)
        if (x != id)
            next.push_back (x);
    if (on)
        next.push_back (id);
    if (onChange)
        onChange (std::move (next));
}

/* ============================================================== panel == */

void CheckList::toggleOpen()
{
    isOpen() ? close() : open();
}

void CheckList::open()
{
    if (isOpen() || ! isEnabled())
        return;

    auto& w = windowOf (*this);
    if (&w == this)
        return;
    window = &w;

    panel = std::make_unique<Panel> (*this);
    const auto size = panel->idealSize();

    /* right: 0, top: control-h + space-1 -- its right edge on the face's,
     * space-1 under it (or over it, where it opens above). */
    const auto f = w.getLocalArea (this, face());
    const juce::Rectangle<int> anchor (f.getRight() - size.x, f.getY(), size.x, f.getHeight());
    w.addAndMakeVisible (*panel);
    panel->setBounds (placeInside (w.getLocalBounds(), anchor, size.x, size.y, -s1));
    panel->toFront (false);

    w.addMouseListener (outside.get(), true);
    refreshFace();
}

void CheckList::close()
{
    if (! isOpen())
        return;

    if (window != nullptr)
        window->removeMouseListener (outside.get());

    /* The keyboard was on a switch in the panel: back to the face, which
     * opened it. */
    const bool hadFocus = panel->hasKeyboardFocus (true);
    auto gone = std::move (panel);
    gone.reset();
    window = nullptr;

    refreshFace();
    if (hadFocus)
        faceButton->showFocusFromKeys();
}

void CheckList::pressedInWindow (juce::Component* target)
{
    if (! isOpen() || target == nullptr)
        return;
    const auto inside = [target] (juce::Component& c) { return &c == target || c.isParentOf (target); };
    if (inside (*panel) || inside (*faceButton))
        return;
    close();
}

/* ============================================================== paint == */

void CheckList::paint (juce::Graphics& g)
{
    if (label.isNotEmpty())
    {
        const auto& style = uv::tok::type::label;
        const auto text = uv::type::cased (style, label);
        const auto font = uv::type::font (style);
        const float w = juce::jmax ((float) labelWidth, uv::type::width (font, text) + 2.0f);
        uv::type::draw (g, text, { 0.0f, 0.0f, w, (float) getHeight() }, font,
                        isEnabled() ? c::inkMuted : c::inkDim);
    }
    paintChildLights (g, *this);
}

void CheckList::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
}

void CheckList::enablementChanged()
{
    if (! isEnabled())
        close();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> CheckList::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace ni::ui
