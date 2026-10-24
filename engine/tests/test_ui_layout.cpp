#include <doctest/doctest.h>
#include <nova/ui/ui.hpp>
#include <ui/canvas_impl.hpp>
#include <ui/layout.hpp>

#include <cmath>

using namespace nova;
using namespace nova::ui;

namespace {

const LayoutEnv kEnv{{0, 0, 720, 1280}, {0, 0, 720, 1280}, 1};

u32 layout(Canvas& c, const LayoutEnv& env = kEnv) { return layoutCanvas(c.impl().store, env, nullptr, nullptr); }

Rect visual(Canvas& c, const ElementRef& e) { return visualBox(c.impl().store, e.handle()); }

UiSpec fixed(f32 w, f32 h, Anchor a = Anchor::TopLeft) {
    return UiSpec{.width = Size::px(w), .height = Size::px(h), .anchor = a};
}

} // namespace

TEST_CASE("Stack anchors place children at the nine points") {
    Canvas c;
    auto tl = c.root().add<Panel>(fixed(100, 50, Anchor::TopLeft));
    auto mid = c.root().add<Panel>(fixed(100, 50, Anchor::Center));
    auto br = c.root().add<Panel>(fixed(100, 50, Anchor::BottomRight));
    auto top = c.root().add<Panel>(fixed(100, 50, Anchor::Top));
    auto right = c.root().add<Panel>(fixed(100, 50, Anchor::Right));
    UiSpec moved = fixed(100, 50, Anchor::TopLeft);
    moved.offset = {10, -5};
    auto off = c.root().add<Panel>(moved);
    layout(c);
    CHECK(tl.box() == Rect{0, 0, 100, 50});
    CHECK(mid.box() == Rect{310, 615, 100, 50});
    CHECK(br.box() == Rect{620, 1230, 100, 50});
    CHECK(top.box() == Rect{310, 0, 100, 50});
    CHECK(right.box() == Rect{620, 615, 100, 50});
    CHECK(off.box() == Rect{0, 0, 100, 50});         // offset is visual: layout box unchanged
    CHECK(visual(c, off) == Rect{10, -5, 100, 50});  // drawn and hit-tested here
}

TEST_CASE("Row distributes fill by weight after fixed and gap") {
    Canvas c;
    auto row = c.root().add<Panel>({.width = Size::fill(), .height = Size::px(100), .layout = LayoutMode::Row,
                                    .gap = 20, .padding = Insets::all(20)});
    auto a = row.add<Panel>({.width = Size::px(100), .height = Size::px(10)});
    auto b = row.add<Panel>({.width = Size::fill(1), .height = Size::px(10)});
    auto d = row.add<Panel>({.width = Size::fill(2), .height = Size::px(10)});
    layout(c);
    CHECK(a.box().w == 100);
    CHECK(b.box().w == doctest::Approx(180));
    CHECK(d.box().w == doctest::Approx(360));
    CHECK(a.box().x == 20);
    CHECK(b.box().x == doctest::Approx(140));
    CHECK(d.box().x == doctest::Approx(340));
    CHECK(a.box().y == 20);
}

TEST_CASE("Column justify and align") {
    for (Justify j : {Justify::Center, Justify::SpaceBetween, Justify::End}) {
        CAPTURE(int(j));
        Canvas c;
        auto col = c.root().add<Panel>({.width = Size::fill(), .height = Size::fill(), .layout = LayoutMode::Column,
                                        .justify = j, .align = Align::Stretch});
        auto a = col.add<Panel>({.height = Size::px(100)});
        auto b = col.add<Panel>({.height = Size::px(100)});
        auto d = col.add<Panel>({.height = Size::px(100)});
        layout(c);
        if (j == Justify::Center) {
            CHECK(a.box().y == 490);
            CHECK(b.box().y == 590);
        } else if (j == Justify::SpaceBetween) {
            CHECK(a.box().y == 0);
            CHECK(b.box().y == 590);
            CHECK(d.box().y == 1180);
        } else {
            CHECK(d.box().y == 1180);
            CHECK(a.box().y == 980);
        }
        CHECK(a.box().w == 720); // stretched across the column
    }
    Canvas c;
    auto col = c.root().add<Panel>({.width = Size::px(400), .height = Size::fill(), .layout = LayoutMode::Column,
                                    .align = Align::Center});
    auto child = col.add<Panel>(fixed(100, 100));
    layout(c);
    CHECK(child.box().x == 150);
}

TEST_CASE("Fit measures children and padding") {
    Canvas c;
    auto fit = c.root().add<Panel>({.layout = LayoutMode::Row, .gap = 5, .padding = Insets::all(10)});
    fit.add<Panel>(fixed(40, 20));
    fit.add<Panel>(fixed(60, 30));
    layout(c);
    CHECK(fit.box() == Rect{0, 0, 125, 50});

    Canvas s;
    auto stack = s.root().add<Panel>({.padding = Insets{1, 2, 3, 4}});
    stack.add<Panel>(fixed(40, 20));
    stack.add<Panel>(fixed(10, 50));
    layout(s);
    CHECK(stack.box().w == 46); // 4 + 40 + 2
    CHECK(stack.box().h == 54); // 1 + 50 + 3
}

TEST_CASE("Min and max clamp") {
    Canvas c;
    auto row = c.root().add<Panel>({.width = Size::fill(), .height = Size::px(50), .layout = LayoutMode::Row});
    auto capped = row.add<Panel>({.width = Size::fill(), .height = Size::px(10), .maxWidth = 200});
    auto floored = row.add<Panel>({.width = Size::px(10), .height = Size::px(10), .minWidth = 50});
    layout(c);
    CHECK(capped.box().w == 200);
    CHECK(floored.box().w == 50);
    CHECK(floored.box().x == 200);
}

TEST_CASE("Safe area adds insets only where the box touches it") {
    Canvas c;
    auto safe = c.root().add<Panel>({.width = Size::fill(), .height = Size::fill(), .safeArea = true});
    auto inner = safe.add<Panel>({.width = Size::fill(), .height = Size::fill()});
    auto plain = c.root().add<Panel>({.width = Size::fill(), .height = Size::fill()});
    auto plainInner = plain.add<Panel>({.width = Size::fill(), .height = Size::fill()});
    LayoutEnv env{{0, 0, 720, 1280}, {0, 88, 720, 1158}, 1};
    layout(c, env);
    CHECK(inner.box() == Rect{0, 88, 720, 1158});
    CHECK(plainInner.box() == Rect{0, 0, 720, 1280});

    // A safe-area panel already inside the safe area gets no extra padding.
    Canvas d;
    auto band = d.root().add<Panel>({.width = Size::fill(), .height = Size::px(200), .anchor = Anchor::Center});
    auto safeBand = band.add<Panel>({.width = Size::fill(), .height = Size::fill(), .safeArea = true});
    auto bandInner = safeBand.add<Panel>({.width = Size::fill(), .height = Size::fill()});
    layout(d, env);
    CHECK(bandInner.box() == Rect{0, 540, 720, 200});
}

TEST_CASE("Wider screen root keeps anchors at the edges") {
    Canvas c;
    auto tr = c.root().add<Panel>(fixed(100, 50, Anchor::TopRight));
    auto tl = c.root().add<Panel>(fixed(100, 50, Anchor::TopLeft));
    LayoutEnv wide{{-90, 0, 900, 1280}, {-90, 0, 900, 1280}, 1};
    layout(c, wide);
    CHECK(tr.box().x == 710);
    CHECK(tl.box().x == -90);
}

TEST_CASE("Pixel snapping at fractional scale") {
    Canvas c;
    auto row = c.root().add<Panel>({.width = Size::px(100), .height = Size::px(30), .layout = LayoutMode::Row});
    row.add<Panel>({.width = Size::px(10.2f), .height = Size::px(10)});
    auto second = row.add<Panel>({.width = Size::px(20.1f), .height = Size::px(10.4f)});
    LayoutEnv env{{0, 0, 720, 1280}, {0, 0, 720, 1280}, 3};
    layout(c, env);
    Rect b = second.box();
    for (f32 edge : {b.x, b.y, b.x + b.w, b.y + b.h}) {
        CAPTURE(edge);
        CHECK(std::fabs(edge * 3 - std::round(edge * 3)) < 1e-3f);
    }
}

TEST_CASE("Layout runs only when dirty") {
    Canvas c;
    auto label = c.root().add<Label>({.id = "l", .width = Size::px(100), .height = Size::px(40)});
    CHECK(layout(c) > 0);
    CHECK(layout(c) == 0);
    label.setOffset({5, 5});
    label.setOpacity(0.5f);
    CHECK(layout(c) == 0);
    label.setText("changed");
    CHECK(layout(c) > 0);
    label.setText("changed"); // same text again: nothing to do
    label.setVisible(true);   // unchanged too
    CHECK(layout(c) == 0);
    label.setVisible(false);
    CHECK(layout(c) > 0);
    CHECK(layout(c) == 0);
    LayoutEnv rotated{{0, 0, 1280, 720}, {0, 0, 1280, 720}, 1};
    CHECK(layout(c, rotated) > 0);
    c.root().add<Panel>({});
    CHECK(layout(c, rotated) > 0);
}
