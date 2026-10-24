#include "ui/interaction.hpp"

#include "ui/layout.hpp"
#include "ui/ui_backend.hpp"

#include <audio/audio_backend.hpp>
#include <input/input_internal.hpp>
#include <scene/services.hpp>

#include <algorithm>

namespace nova::ui {
namespace {

u64 handleKey(UiHandle h) { return (u64(h.index) << 32) | h.generation; }

Rect grown(Rect r, f32 by) { return {r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by}; }

Touch* findTouch(Canvas::Impl& impl, u64 pointer) {
    for (Touch& t : impl.touches) {
        if (t.active && t.pointer == pointer) return &t;
    }
    return nullptr;
}

Touch* freeTouch(Canvas::Impl& impl) {
    for (Touch& t : impl.touches) {
        if (!t.active) return &t;
    }
    return nullptr;
}

} // namespace

bool isInteractive(Kind kind) { return kind == Kind::Button || kind == Kind::Toggle || kind == Kind::Slider; }

UiHandle hitTest(Canvas::Impl& impl, Vec2 pos, bool& blocked) {
    blocked = false;
    ElementStore& store = impl.store;
    std::vector<UiHandle>& order = impl.hitScratch;
    for (u16 layer = store.layerCount(); layer-- > 0;) {
        // Draw order (pre-order), then search it backwards: the last drawn is on top.
        order.clear();
        std::vector<UiHandle>& stack = impl.hitStack;
        stack.clear();
        stack.push_back(store.layerRoot(layer));
        while (!stack.empty()) {
            UiHandle h = stack.back();
            stack.pop_back();
            order.push_back(h);
            const size_t mark = stack.size();
            store.forEachChild(h, [&](UiHandle c, const Element& e) {
                if (!e.removed && e.spec.visible) stack.push_back(c);
            });
            std::reverse(stack.begin() + long(mark), stack.end());
        }
        for (size_t i = order.size(); i-- > 1;) { // index 0 is the layer root
            const Element& e = *store.get(order[i]);
            if (!visualBox(store, order[i]).contains(pos)) continue;
            if (isInteractive(e.kind) && e.spec.enabled) return order[i];
            if (e.spec.blocksInput) {
                blocked = true;
                return {};
            }
        }
    }
    return {};
}

// ---- Bindings ----------------------------------------------------------------------------

void Canvas::Impl::bindId(std::string_view id, const std::function<void(Binding&)>& edit) {
    auto it = idBindings.find(id);
    auto next = it != idBindings.end() && it->second ? std::make_shared<Binding>(*it->second) : std::make_shared<Binding>();
    edit(*next);
    if (it != idBindings.end()) it->second = std::move(next);
    else idBindings.emplace(std::string(id), std::move(next));
}

void Canvas::Impl::bind(UiHandle h, const std::function<void(Binding&)>& edit) {
    const Element* e = store.live(h);
    if (!e) return;
    if (!e->spec.id.empty()) {
        bindId(e->spec.id, edit);
        return;
    }
    BindingPtr& slot = handleBindings[handleKey(h)];
    auto next = slot ? std::make_shared<Binding>(*slot) : std::make_shared<Binding>();
    edit(*next);
    slot = std::move(next);
}

BindingPtr Canvas::Impl::bindingOf(UiHandle h) const {
    const Element* e = store.get(h);
    if (!e) return nullptr;
    if (!e->spec.id.empty()) {
        auto it = idBindings.find(std::string_view(e->spec.id));
        return it != idBindings.end() ? it->second : nullptr;
    }
    auto it = handleBindings.find(handleKey(h));
    return it != handleBindings.end() ? it->second : nullptr;
}

void Canvas::onClick(std::string_view id, std::function<void()> fn) {
    impl_->bindId(id, [&](Binding& b) { b.click = std::move(fn); });
}

void ButtonRef::onClick(std::function<void()> fn) {
    if (canvas_) canvas_->impl().bind(h_, [&](Binding& b) { b.click = std::move(fn); });
}

// ---- Engine hooks ------------------------------------------------------------------------

void uiInput(Canvas& canvas, InputBackend& input) {
    Canvas::Impl& impl = canvas.impl();
    if (impl.events.capacity() < 16) impl.events.reserve(16);
    if (impl.hitScratch.capacity() < impl.store.size()) {
        impl.hitScratch.reserve(impl.store.size() * 2);
        impl.hitStack.reserve(impl.store.size() * 2);
    }
    refreshLayout(impl);
    for (int i = 0; i < input.pointerCount(); ++i) {
        const Pointer& p = *input.pointer(i);
        Touch* t = findTouch(impl, p.id);
        if (p.pressed && !t) {
            bool blocked = false;
            UiHandle hit = hitTest(impl, p.pos, blocked);
            if (hit.valid()) {
                if ((t = freeTouch(impl))) *t = Touch{p.id, hit, true, false};
                input.setConsumed(p.id);
            } else if (blocked) {
                input.setConsumed(p.id);
            }
        }
        if (!t) continue;
        Element* e = impl.store.live(t->owner);
        if (!e || !e->spec.enabled || !e->spec.visible) t->cancelled = true;
        else if (!t->cancelled && !grown(visualBox(impl.store, t->owner), kCancelDistance).contains(p.pos)) t->cancelled = true;
        if (e) e->pressed = !t->cancelled && !p.released;
        if (p.released) {
            if (e && !t->cancelled && !p.cancelled && e->kind == Kind::Button) impl.events.push_back({UiEvent::Type::Click, t->owner});
            t->active = false;
        }
    }
    // Touches whose pointer vanished (e.g. the app lost focus) end without an event.
    for (Touch& t : impl.touches) {
        if (!t.active) continue;
        bool alive = false;
        for (int i = 0; i < input.pointerCount(); ++i) alive |= input.pointer(i)->id == t.pointer;
        if (!alive) {
            if (Element* e = impl.store.live(t.owner)) e->pressed = false;
            t.active = false;
        }
    }
}

void uiDispatch(Canvas& canvas) {
    Canvas::Impl& impl = canvas.impl();
    for (size_t i = 0; i < impl.events.size(); ++i) {
        const UiEvent ev = impl.events[i];
        const Element* e = impl.store.live(ev.h);
        if (!e) continue; // removed by an earlier callback this frame
        if (ev.type == UiEvent::Type::Click && e->clickSound.valid() && impl.services && impl.services->audio) {
            impl.services->audio->play(e->clickSound);
        }
        BindingPtr b = impl.bindingOf(ev.h); // keeps the callback alive even if it rebinds itself
        if (!b) continue;
        switch (ev.type) {
            case UiEvent::Type::Click:
                if (b->click) b->click();
                break;
            case UiEvent::Type::Toggle:
                if (b->toggle) b->toggle(ev.value != 0);
                break;
            case UiEvent::Type::Change:
                if (b->change) b->change(ev.value);
                break;
            case UiEvent::Type::Commit:
                if (b->commit) b->commit(ev.value);
                break;
        }
    }
    impl.events.clear();
}

} // namespace nova::ui
