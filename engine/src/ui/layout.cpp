#include "ui/layout.hpp"

#include <algorithm>
#include <cmath>

namespace nova::ui {
namespace {

constexpr f32 anchorX(Anchor a) {
    switch (a) {
        case Anchor::Top: case Anchor::Center: case Anchor::Bottom: return 0.5f;
        case Anchor::TopRight: case Anchor::Right: case Anchor::BottomRight: return 1;
        default: return 0;
    }
}
constexpr f32 anchorY(Anchor a) {
    switch (a) {
        case Anchor::Left: case Anchor::Center: case Anchor::Right: return 0.5f;
        case Anchor::BottomLeft: case Anchor::Bottom: case Anchor::BottomRight: return 1;
        default: return 0;
    }
}

f32 clampAxis(f32 v, f32 lo, f32 hi) { return std::max(lo, std::min(v, hi)); }

// The size a parent should plan for on one axis, before any fill share is known.
f32 preferred(const Size& s, f32 measured, f32 lo, f32 hi) {
    return clampAxis(s.mode == Size::Mode::Fixed ? s.value : measured, lo, hi);
}

Insets paddingOf(const Element& e, const LayoutEnv& env, const Rect& box) {
    Insets p = e.padding;
    if (e.spec.safeArea) {
        const Rect& s = env.safeArea;
        p.left += std::max(0.f, s.x - box.x);
        p.top += std::max(0.f, s.y - box.y);
        p.right += std::max(0.f, box.right() - s.right());
        p.bottom += std::max(0.f, box.bottom() - s.bottom());
    }
    return p;
}

struct Snap {
    f32 scale;
    f32 operator()(f32 v) const { return std::round(v * scale) / scale; }
    Rect operator()(Rect r) const {
        f32 x0 = (*this)(r.x), y0 = (*this)(r.y), x1 = (*this)(r.x + r.w), y1 = (*this)(r.y + r.h);
        return {x0, y0, x1 - x0, y1 - y0};
    }
};

bool shown(const Element& e) { return e.spec.visible && !e.removed; }

// Pass 1, bottom-up: content sizes (padding included) for fit axes.
void measureLayer(ElementStore& store, UiHandle root, MeasureFn measure, void* ctx, std::vector<UiHandle>& stack,
                  std::vector<UiHandle>& order) {
    order.clear();
    stack.clear();
    stack.push_back(root);
    while (!stack.empty()) { // pre-order into `order`; walked backwards it is post-order
        UiHandle h = stack.back();
        stack.pop_back();
        order.push_back(h);
        store.forEachChild(h, [&](UiHandle c, const Element& e) {
            if (shown(e)) stack.push_back(c);
        });
    }
    for (size_t i = order.size(); i-- > 0;) {
        Element& e = *store.get(order[i]);
        Vec2 content = measure ? measure(e, ctx) : Vec2{};
        f32 main = 0, cross = 0;
        u32 n = 0;
        store.forEachChild(order[i], [&](UiHandle, const Element& c) {
            if (!shown(c)) return;
            const UiSpec& s = c.spec;
            f32 w = preferred(s.width, c.measured.x, s.minWidth, s.maxWidth);
            f32 h = preferred(s.height, c.measured.y, s.minHeight, s.maxHeight);
            if (e.spec.layout == LayoutMode::Row) {
                main += w;
                cross = std::max(cross, h);
            } else if (e.spec.layout == LayoutMode::Column) {
                main += h;
                cross = std::max(cross, w);
            } else {
                main = std::max(main, w);
                cross = std::max(cross, h);
            }
            ++n;
        });
        f32 gaps = (e.spec.layout != LayoutMode::Stack && n > 1) ? e.spec.gap * f32(n - 1) : 0;
        Vec2 kids = e.spec.layout == LayoutMode::Column ? Vec2{cross, main + gaps}
                  : e.spec.layout == LayoutMode::Row   ? Vec2{main + gaps, cross}
                                                       : Vec2{main, cross};
        const Insets& p = e.padding;
        e.measured = {std::max(content.x, kids.x) + p.left + p.right, std::max(content.y, kids.y) + p.top + p.bottom};
    }
}

// Pass 2, top-down: final boxes.
u32 arrangeLayer(ElementStore& store, UiHandle root, const LayoutEnv& env, std::vector<UiHandle>& stack) {
    const Snap snap{env.pixelScale > 0 ? env.pixelScale : 1};
    store.get(root)->box = env.root;
    u32 count = 0;
    stack.clear();
    stack.push_back(root);
    while (!stack.empty()) {
        UiHandle h = stack.back();
        stack.pop_back();
        ++count;
        const Element& parent = *store.get(h);
        const Insets p = paddingOf(parent, env, parent.box);
        const Rect content{parent.box.x + p.left, parent.box.y + p.top, std::max(0.f, parent.box.w - p.left - p.right),
                           std::max(0.f, parent.box.h - p.top - p.bottom)};
        const LayoutMode mode = parent.spec.layout;
        const bool row = mode == LayoutMode::Row;

        // Main-axis bookkeeping for row/column.
        f32 used = 0, weights = 0;
        u32 n = 0;
        store.forEachChild(h, [&](UiHandle, const Element& c) {
            if (!shown(c)) return;
            const Size& s = row ? c.spec.width : c.spec.height;
            f32 measured = row ? c.measured.x : c.measured.y;
            if (s.mode == Size::Mode::Fill) weights += std::max(0.f, s.value);
            else used += preferred(s, measured, row ? c.spec.minWidth : c.spec.minHeight,
                                   row ? c.spec.maxWidth : c.spec.maxHeight);
            ++n;
        });
        const f32 avail = row ? content.w : content.h;
        f32 gap = n > 1 ? parent.spec.gap : 0;
        const f32 leftover = std::max(0.f, avail - used - gap * f32(n > 0 ? n - 1 : 0));
        f32 cursor = 0;
        f32 fillUsed = 0;
        store.forEachChild(h, [&](UiHandle, const Element& c) {
            if (!shown(c) || weights <= 0) return;
            const Size& s = row ? c.spec.width : c.spec.height;
            if (s.mode != Size::Mode::Fill) return;
            fillUsed += clampAxis(leftover * s.value / weights, row ? c.spec.minWidth : c.spec.minHeight,
                                  row ? c.spec.maxWidth : c.spec.maxHeight);
        });
        const f32 free = std::max(0.f, leftover - fillUsed);
        switch (parent.spec.justify) {
            case Justify::Center: cursor = free * 0.5f; break;
            case Justify::End: cursor = free; break;
            case Justify::SpaceBetween:
                if (n > 1) gap += free / f32(n - 1);
                break;
            case Justify::Start: break;
        }

        store.forEachChild(h, [&](UiHandle ch, const Element& cc) {
            Element& c = *store.get(ch);
            if (!shown(cc)) {
                c.box = {};
                return;
            }
            const UiSpec& s = c.spec;
            auto axis = [&](const Size& size, f32 measured, f32 lo, f32 hi, f32 space) {
                if (size.mode == Size::Mode::Fill) return clampAxis(space, lo, hi);
                return preferred(size, measured, lo, hi);
            };
            Rect box{};
            if (mode == LayoutMode::Stack) {
                box.w = axis(s.width, c.measured.x, s.minWidth, s.maxWidth, content.w);
                box.h = axis(s.height, c.measured.y, s.minHeight, s.maxHeight, content.h);
                box.x = content.x + (content.w - box.w) * anchorX(s.anchor);
                box.y = content.y + (content.h - box.h) * anchorY(s.anchor);
            } else {
                const Size& mainSize = row ? s.width : s.height;
                const Size& crossSize = row ? s.height : s.width;
                f32 mainLen = mainSize.mode == Size::Mode::Fill && weights > 0
                                  ? clampAxis(leftover * mainSize.value / weights, row ? s.minWidth : s.minHeight,
                                              row ? s.maxWidth : s.maxHeight)
                                  : preferred(mainSize, row ? c.measured.x : c.measured.y,
                                              row ? s.minWidth : s.minHeight, row ? s.maxWidth : s.maxHeight);
                const f32 crossSpace = row ? content.h : content.w;
                const f32 crossLo = row ? s.minHeight : s.minWidth, crossHi = row ? s.maxHeight : s.maxWidth;
                f32 crossLen = parent.spec.align == Align::Stretch
                                   ? clampAxis(crossSpace, crossLo, crossHi)
                                   : axis(crossSize, row ? c.measured.y : c.measured.x, crossLo, crossHi, crossSpace);
                f32 crossPos = parent.spec.align == Align::Center ? (crossSpace - crossLen) * 0.5f
                             : parent.spec.align == Align::End    ? crossSpace - crossLen
                                                                  : 0;
                if (row) box = {content.x + cursor, content.y + crossPos, mainLen, crossLen};
                else box = {content.x + crossPos, content.y + cursor, crossLen, mainLen};
                cursor += mainLen + gap;
            }
            c.box = snap(box);
            stack.push_back(ch);
        });
    }
    return count;
}

struct LayoutScratch {
    std::vector<UiHandle> stack, order;
};

} // namespace

u32 layoutCanvas(ElementStore& store, const LayoutEnv& env, MeasureFn measure, void* ctx) {
    static thread_local LayoutScratch scratch;
    if (scratch.stack.capacity() < store.size()) {
        scratch.stack.reserve(store.size() * 2);
        scratch.order.reserve(store.size() * 2);
    }
    const bool envChanged = !(store.lastEnv() == env);
    u32 total = 0;
    for (u16 layer = 0; layer < store.layerCount(); ++layer) {
        if (!envChanged && !store.dirty(layer)) continue;
        UiHandle root = store.layerRoot(layer);
        measureLayer(store, root, measure, ctx, scratch.stack, scratch.order);
        total += arrangeLayer(store, root, env, scratch.stack);
        store.clearDirty(layer);
    }
    store.setLastEnv(env);
    return total;
}

void markLayoutDirty(ElementStore& store, UiHandle h) {
    if (const Element* e = store.get(h)) store.markDirty(e->layer);
}

Rect visualBox(const ElementStore& store, UiHandle h) {
    const Element* e = store.live(h);
    if (!e) return {};
    // Apply each element's local transform, innermost first: offset, then scale about the
    // (offset) centre of its own box.
    Rect r = e->box;
    for (const Element* cur = e; cur; cur = store.get(cur->parent)) {
        const Vec2 off = cur->spec.offset;
        const Vec2 c = cur->box.center() + off;
        const f32 s = cur->scale;
        r = {c.x + (r.x + off.x - c.x) * s, c.y + (r.y + off.y - c.y) * s, r.w * s, r.h * s};
    }
    return r;
}

} // namespace nova::ui
