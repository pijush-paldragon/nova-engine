#include "ui/animation.hpp"
#include "ui/canvas_impl.hpp"
#include "ui/layout.hpp"
#include "ui/ui_backend.hpp"

#include <nova/core/log.hpp>

#include <algorithm>
#include <render/renderer_backend.hpp>
#include <scene/services.hpp>

namespace nova::ui {

const char* kindName(Kind kind) {
    switch (kind) {
        case Kind::Panel: return "panel";
        case Kind::Image: return "image";
        case Kind::Label: return "label";
        case Kind::Button: return "button";
        case Kind::Toggle: return "toggle";
        case Kind::Slider: return "slider";
        case Kind::ProgressBar: return "progress";
    }
    return "element";
}

void Canvas::Impl::reportMissing(std::string_view id, const char* what, u16 layer) {
#if defined(NOVA_DEV_TOOLS)
    std::string key = std::string(what) + ":" + std::string(id);
    if (!reported.insert(key).second) return;
    log::error("ui: no %s \"%.*s\" in %s", what, int(id.size()), id.data(), layerSources[layer].c_str());
#else
    (void)id;
    (void)what;
    (void)layer;
#endif
}

// ---- Canvas ------------------------------------------------------------------------------

Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}
Canvas::~Canvas() = default;

ElementRef Canvas::root() { return ElementRef(this, impl_->store.layerRoot(0)); }

u32 Canvas::elementCount() const { return impl_->store.size(); }

UiHandle Canvas::findTyped(std::string_view id, Kind kind) {
    UiHandle h = impl_->store.find(id);
    const Element* e = impl_->store.live(h);
    if (!e || e->kind != kind) {
        impl_->reportMissing(id, kindName(kind), u16(impl_->store.layerCount() - 1));
        return {};
    }
    return h;
}

ElementRef Canvas::element(std::string_view id) {
    UiHandle h = impl_->store.find(id);
    if (!impl_->store.live(h)) {
        impl_->reportMissing(id, "element", u16(impl_->store.layerCount() - 1));
        return {};
    }
    return ElementRef(this, h);
}
PanelRef Canvas::panel(std::string_view id) { return PanelRef(this, findTyped(id, Kind::Panel)); }
ImageRef Canvas::image(std::string_view id) { return ImageRef(this, findTyped(id, Kind::Image)); }
LabelRef Canvas::label(std::string_view id) { return LabelRef(this, findTyped(id, Kind::Label)); }
ButtonRef Canvas::button(std::string_view id) { return ButtonRef(this, findTyped(id, Kind::Button)); }
ToggleRef Canvas::toggle(std::string_view id) { return ToggleRef(this, findTyped(id, Kind::Toggle)); }
SliderRef Canvas::slider(std::string_view id) { return SliderRef(this, findTyped(id, Kind::Slider)); }
ProgressRef Canvas::progress(std::string_view id) { return ProgressRef(this, findTyped(id, Kind::ProgressBar)); }

// ---- References --------------------------------------------------------------------------

namespace {

Element* resolve(Canvas* canvas, UiHandle h) { return canvas ? canvas->impl().store.live(h) : nullptr; }

// Resolves for a change that can alter size or position: the element's layer re-lays out.
Element* resolveForLayout(Canvas* canvas, UiHandle h) {
    Element* e = resolve(canvas, h);
    if (e) canvas->impl().store.markDirty(e->layer);
    return e;
}

Remembered* memo(Canvas* canvas, const Element& e) { return canvas->impl().rememberFor(e); }

// Assigns into an engaged optional string in place, so repeated same-length sets don't allocate.
void keep(std::optional<std::string>& slot, std::string_view v) {
    if (slot) slot->assign(v.data(), v.size());
    else slot.emplace(v);
}

} // namespace

bool ElementRef::valid() const { return resolve(canvas_, h_) != nullptr; }

Kind ElementRef::kind() const {
    const Element* e = resolve(canvas_, h_);
    return e ? e->kind : Kind::Panel;
}

Rect ElementRef::box() const {
    const Element* e = resolve(canvas_, h_);
    return e ? e->box : Rect{};
}

void ElementRef::setVisible(bool visible) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.visible = visible;
        if (Remembered* r = memo(canvas_, *e)) r->visible = visible;
    }
}
void ElementRef::setEnabled(bool enabled) {
    if (Element* e = resolve(canvas_, h_)) {
        e->spec.enabled = enabled;
        if (Remembered* r = memo(canvas_, *e)) r->enabled = enabled;
    }
}
void ElementRef::setStyle(std::string_view style) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.style.assign(style.data(), style.size());
        e->resolveDirty = true;
        if (Remembered* r = memo(canvas_, *e)) keep(r->style, style);
    }
}
void ElementRef::setOpacity(f32 opacity) {
    if (Element* e = resolve(canvas_, h_)) e->spec.opacity = opacity;
}
void ElementRef::setOffset(Vec2 offset) {
    if (Element* e = resolve(canvas_, h_)) e->spec.offset = offset;
}
void ElementRef::setScale(f32 scale) {
    if (Element* e = resolve(canvas_, h_)) e->scale = scale;
}

UiHandle ElementRef::addChild(Kind kind, const UiSpec& spec) {
    return canvas_ ? canvas_->impl().store.add(h_, kind, spec) : UiHandle{};
}

void ElementRef::remove() {
    if (canvas_) canvas_->impl().store.remove(h_);
}

void ImageRef::setSprite(std::string_view sprite) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.sprite.assign(sprite.data(), sprite.size());
        e->resolveDirty = true;
        if (Remembered* r = memo(canvas_, *e)) keep(r->sprite, sprite);
    }
}
void LabelRef::setText(std::string_view text) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.text.assign(text.data(), text.size());
        if (Remembered* r = memo(canvas_, *e)) keep(r->text, text);
    }
}
void ButtonRef::setText(std::string_view text) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.text.assign(text.data(), text.size());
        if (Remembered* r = memo(canvas_, *e)) keep(r->text, text);
    }
}
void ToggleRef::setOn(bool on) {
    if (Element* e = resolve(canvas_, h_)) {
        e->spec.on = on;
        if (Remembered* r = memo(canvas_, *e)) r->on = on;
    }
}
bool ToggleRef::on() const {
    const Element* e = resolve(canvas_, h_);
    return e && e->spec.on;
}
void SliderRef::setValue(f32 value) {
    if (Element* e = resolve(canvas_, h_)) {
        e->spec.value = std::clamp(value, std::min(e->spec.min, e->spec.max), std::max(e->spec.min, e->spec.max));
        if (Remembered* r = memo(canvas_, *e)) r->value = e->spec.value;
    }
}
f32 SliderRef::value() const {
    const Element* e = resolve(canvas_, h_);
    return e ? e->spec.value : 0;
}
void ProgressRef::setValue(f32 value) {
    Element* e = resolve(canvas_, h_);
    if (!e) return;
    value = std::clamp(value, 0.f, 1.f);
    if (value == e->spec.value) return;
    e->spec.value = value;
    if (e->spec.animate > 0) {
        e->shownFrom = e->shown;
        e->shownT = 0;
    } else {
        e->shown = value;
        e->shownT = 1;
    }
    if (Remembered* r = memo(canvas_, *e)) r->value = value;
}
f32 ProgressRef::value() const {
    const Element* e = resolve(canvas_, h_);
    return e ? e->spec.value : 0;
}
f32 ProgressRef::displayedValue() const {
    const Element* e = resolve(canvas_, h_);
    return e ? e->shown : 0;
}

// ---- Engine hooks ------------------------------------------------------------------------

void uiAttach(Canvas& canvas, Services* services, ScopeId scope) {
    canvas.impl().services = services;
    canvas.impl().scope = scope;
}

void uiReset(Canvas& canvas) {
    canvas.impl().drawList.layers.clear();
    canvas.impl().store = ElementStore();
}

void uiFlush(Canvas& canvas) { canvas.impl().store.flush(); }

void uiUpdate(Canvas& canvas, f32 dt) {
    updateAnimations(canvas.impl(), dt);
    refreshLayout(canvas.impl());
}

Remembered* Canvas::Impl::rememberFor(const Element& e) {
    if (e.spec.id.empty()) return nullptr;
    auto it = remembered.find(std::string_view(e.spec.id));
    if (it == remembered.end()) it = remembered.emplace(e.spec.id, Remembered{}).first;
    return &it->second;
}

std::string_view Canvas::rememberedTextForTests(std::string_view id) const {
    auto it = impl_->remembered.find(id);
    return it != impl_->remembered.end() && it->second.text ? std::string_view(*it->second.text) : std::string_view();
}

} // namespace nova::ui
