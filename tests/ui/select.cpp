// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Select: its list opens inside the window, under the field, as wide as
 * it and over its hairline; it is chosen from by a click, by press-drag-
 * release and by the keys; a press outside closes it and chooses nothing;
 * and a list never leaves the window. Then the states, as a picture.
 */
#include "Gallery.h"
#include "Info.h"
#include "Pointer.h"
#include "Popup.h"
#include "Select.h"
#include "checks.h"
#include "hostkeys.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Select;
using ni::ui::SelectList;
using ni::ui::gallery::Pointer;
using ni::ui::gallery::key;

namespace
{
/* An editor's root: the window lists open in. */
struct Window final : public juce::Component, public ni::ui::InfoHost
{
    explicit Window (int w = 400, int h = 300) { setSize (w, h); }
    ni::ui::InfoState& infoState() override { return state; }
    ni::ui::InfoState state;
};

/* A labelled Rate select at (20, 20) in a window, with its choices heard. */
struct Fixture
{
    explicit Fixture (int windowHeight = 300) : window (400, windowHeight)
    {
        select.setOptions ({ "1/4", "1/8", "1/16", "1/32" });
        select.setIndex (1);
        select.setLabel ("Rate", 38);
        select.setFieldWidth (84);
        select.setBounds (20, 20, select.idealWidth(), 28);
        select.onChange = [this] (int i) { chosen.push_back (i); };
        window.addAndMakeVisible (select);
    }

    juce::Point<float> fieldCentre() const { return select.field().getCentre().toFloat(); }

    /* A point in row `row` of the open list, in the select's coordinates. */
    juce::Point<float> rowInSelect (int row) const
    {
        auto* list = select.getList();
        return select.getLocalPoint (list, list->rowBounds (row).getCentre()).toFloat();
    }

    Window window;
    Select select;
    std::vector<int> chosen;
};
} // namespace

TEST_CASE ("select: the label sits beside the field, 8px before it; the field is its own width")
{
    Fixture f;
    CHECK (f.select.field() == juce::Rectangle<int> (46, 0, 84, 28));
    CHECK (f.select.idealWidth() == 130);

    Select bare;
    bare.setOptions ({ "1" });
    bare.setSize (200, 28);
    CHECK (bare.field() == juce::Rectangle<int> (0, 0, 96, 28));
}

TEST_CASE ("select: a press on the field opens the list in the window, under it and as wide")
{
    Fixture f;
    Pointer p;
    p.down (f.select, f.fieldCentre());
    REQUIRE (f.select.isOpen());

    auto* list = f.select.getList();
    REQUIRE (list != nullptr);
    /* A layer of the window, not of the select: it is drawn over the rest. */
    CHECK (list->getParentComponent()->getParentComponent() == &f.window);
    const auto field = f.window.getLocalArea (&f.select, f.select.field());
    const auto at = f.window.getLocalArea (list, list->getLocalBounds());
    CHECK (at == juce::Rectangle<int> (field.getX(), field.getBottom() - 1, 84, 4 * 28 + 2));
    CHECK (list->getHighlighted() == 1);   // the current option, under the keys
    p.up();
    CHECK (f.select.isOpen());             // a click leaves it open to choose from
}

TEST_CASE ("select: a click on a row chooses it and closes the list")
{
    Fixture f;
    Pointer p;
    p.click (f.select, f.fieldCentre());
    auto* list = f.select.getList();
    REQUIRE (list != nullptr);

    Pointer q;
    q.click (*list, list->rowBounds (3).getCentre().toFloat());
    CHECK (f.chosen == std::vector<int> { 3 });
    CHECK_FALSE (f.select.isOpen());
}

TEST_CASE ("select: press on the field, drag onto a row, release: chosen, as in a menu")
{
    Fixture f;
    Pointer p;
    p.down (f.select, f.fieldCentre());
    p.drag (f.rowInSelect (2));
    CHECK (f.select.getList()->getHighlighted() == 2);
    p.up();
    CHECK (f.chosen == std::vector<int> { 2 });
    CHECK_FALSE (f.select.isOpen());
}

TEST_CASE ("select: a press outside the list closes it and chooses nothing")
{
    Fixture f;
    Pointer p;
    p.click (f.select, f.fieldCentre());
    auto* layer = f.window.getChildComponent (f.window.getNumChildComponents() - 1);
    REQUIRE (dynamic_cast<ni::ui::Popup*> (layer) != nullptr);

    layer->mouseDown (Pointer::event (*layer, { 350, 250 }));
    CHECK_FALSE (f.select.isOpen());
    CHECK (f.chosen.empty());
    CHECK (f.window.getNumChildComponents() == 1);   // the layer is gone with it
}

TEST_CASE ("select: Enter, Up or Down open it; the arrows move; Enter chooses; Space is the host's")
{
    Fixture f;
    CHECK (key (f.select, juce::KeyPress::downKey));
    REQUIRE (f.select.isOpen());
    CHECK (f.select.getList()->getHighlighted() == 1);

    key (f.select, juce::KeyPress::downKey);
    key (f.select, juce::KeyPress::downKey);
    key (f.select, juce::KeyPress::downKey);   // stops at the last
    CHECK (f.select.getList()->getHighlighted() == 3);
    key (f.select, juce::KeyPress::homeKey);
    CHECK (f.select.getList()->getHighlighted() == 0);
    key (f.select, juce::KeyPress::returnKey);
    CHECK (f.chosen == std::vector<int> { 0 });
    CHECK_FALSE (f.select.isOpen());

    CHECK_FALSE (ni::ui::test::windowUses (f.select, ni::ui::test::spaceKey));
    CHECK_FALSE (f.select.isOpen());
    CHECK (key (f.select, juce::KeyPress::returnKey));
    REQUIRE (f.select.isOpen());

    /* Open, Space chooses nothing and leaves it open. */
    CHECK_FALSE (ni::ui::test::windowUses (f.select, ni::ui::test::spaceKey));
    CHECK (f.select.isOpen());
    CHECK (f.chosen == std::vector<int> { 0 });
}

TEST_CASE ("select: Escape and Tab close it and choose nothing")
{
    Fixture f;
    key (f.select, juce::KeyPress::returnKey);
    key (f.select, juce::KeyPress::upKey);
    CHECK (key (f.select, juce::KeyPress::escapeKey));
    CHECK_FALSE (f.select.isOpen());

    key (f.select, juce::KeyPress::returnKey);
    CHECK (key (f.select, juce::KeyPress::tabKey));
    CHECK_FALSE (f.select.isOpen());
    CHECK (f.chosen.empty());
}

TEST_CASE ("select: choosing the option it shows tells nobody")
{
    Fixture f;
    key (f.select, juce::KeyPress::returnKey);
    key (f.select, juce::KeyPress::returnKey);
    CHECK (f.chosen.empty());
    CHECK_FALSE (f.select.isOpen());
}

TEST_CASE ("select: none is an index -- it stays none, and any row chosen is a change")
{
    Fixture f;
    f.select.setIndex (-1);
    CHECK (f.select.getIndex() == -1);
    /* New options keep it none; an index past them is still clamped. */
    f.select.setOptions ({ "1/4", "1/8" });
    CHECK (f.select.getIndex() == -1);
    f.select.setIndex (7);
    CHECK (f.select.getIndex() == 1);
    f.select.setIndex (-3);
    CHECK (f.select.getIndex() == -1);

    /* The list opens with no row lit; Down lights the first. */
    key (f.select, juce::KeyPress::returnKey);
    REQUIRE (f.select.isOpen());
    CHECK (f.select.getList()->getCurrent() == -1);
    CHECK (f.select.getList()->getHighlighted() == -1);
    key (f.select, juce::KeyPress::downKey);
    CHECK (f.select.getList()->getHighlighted() == 0);
    key (f.select, juce::KeyPress::returnKey);
    CHECK (f.chosen == std::vector<int> { 0 });

    /* Enter on a list with nothing lit chooses nothing, and closes it. */
    f.select.setIndex (-1);
    key (f.select, juce::KeyPress::returnKey);
    key (f.select, juce::KeyPress::returnKey);
    CHECK_FALSE (f.select.isOpen());
    CHECK (f.chosen.size() == 1);
}

TEST_CASE ("select: the keyboard going elsewhere closes it")
{
    Fixture f;
    key (f.select, juce::KeyPress::returnKey);
    f.select.focusLost (juce::Component::focusChangedByMouseClick);
    CHECK_FALSE (f.select.isOpen());
}

TEST_CASE ("select: disabled, nothing opens it")
{
    Fixture f;
    f.select.setEnabled (false);
    Pointer p;
    p.click (f.select, f.fieldCentre());
    CHECK_FALSE (key (f.select, juce::KeyPress::returnKey));
    CHECK_FALSE (f.select.isOpen());
}

TEST_CASE ("select: only the field rises under the pointer, not its label")
{
    Fixture f;
    Pointer p;
    p.enter (f.select, { 10, 14 });
    CHECK_FALSE (f.select.isFieldHovered());
    p.move (f.select, f.fieldCentre());
    CHECK (f.select.isFieldHovered());
    p.exit (f.select);
    CHECK_FALSE (f.select.isFieldHovered());
}

TEST_CASE ("select: a list opens below, else above, else where it fits -- never past the window")
{
    using ni::ui::placeInside;
    const juce::Rectangle<int> window (0, 0, 360, 232);

    /* Room below: under the anchor, over its bottom hairline. */
    CHECK (placeInside (window, { 40, 20, 64, 28 }, 64, 114, 1) == juce::Rectangle<int> (40, 47, 64, 114));
    /* None below, room above. */
    CHECK (placeInside (window, { 40, 190, 64, 28 }, 64, 114, 1) == juce::Rectangle<int> (40, 77, 64, 114));
    /* Neither: as low as fits, over the anchor (the Listen-In's eight buses). */
    CHECK (placeInside (window, { 40, 80, 64, 28 }, 64, 226, 1) == juce::Rectangle<int> (40, 6, 64, 226));
    /* Taller than the window: as tall as the window. */
    CHECK (placeInside (window, { 40, 80, 64, 28 }, 64, 400, 1) == juce::Rectangle<int> (40, 0, 64, 232));
    /* Past the right edge: moved in. */
    CHECK (placeInside (window, { 330, 20, 64, 28 }, 64, 60, 1).getRight() == 360);
}

TEST_CASE ("select: a list taller than its window scrolls to keep the keys' row in sight")
{
    Fixture f (100);
    f.select.setOptions ({ "1", "2", "3", "4", "5", "6", "7", "8" });
    f.select.setIndex (0);
    key (f.select, juce::KeyPress::returnKey);
    auto* list = f.select.getList();
    REQUIRE (list != nullptr);
    CHECK (list->getHeight() == 100);
    CHECK (list->numShown() == 3);

    key (f.select, juce::KeyPress::endKey);
    CHECK (list->getHighlighted() == 7);
    CHECK (list->getFirstShown() == 5);
    Pointer::wheel (*list, { 10, 10 }, 0.5f);   // back up
    CHECK (list->getFirstShown() == 0);
}

TEST_CASE ("select: a screen reader hears a combo box, its label and option, and may set it")
{
    Fixture f;
    auto handler = static_cast<juce::Component&> (f.select).createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::comboBox);
    CHECK (handler->getTitle() == "Rate");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "1/8");
    CHECK (handler->getCurrentState().isCollapsed());

    handler->getValueInterface()->setValueAsString ("1/32");
    CHECK (f.chosen == std::vector<int> { 3 });

    CHECK (handler->getActions().invoke (juce::AccessibilityActionType::showMenu));
    CHECK (f.select.isOpen());
    CHECK (handler->getCurrentState().isExpanded());
}

NI_SNAPSHOT_TEST ("select: its states, a cut value, and one open")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-select")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-select");
}
