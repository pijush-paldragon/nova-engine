#include "ui/canvas_impl.hpp"
#include "ui/layout.hpp"
#include "ui/ui_backend.hpp"

#include <nova/core/log.hpp>
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
    if (Element* e = resolveForLayout(canvas_, h_)) e->spec.visible = visible;
}
void ElementRef::setEnabled(bool enabled) {
    if (Element* e = resolve(canvas_, h_)) e->spec.enabled = enabled;
}
void ElementRef::setStyle(std::string_view style) {
    if (Element* e = resolveForLayout(canvas_, h_)) e->spec.style.assign(style.data(), style.size());
}
void ElementRef::setOpacity(f32 opacity) {
    if (Element* e = resolve(canvas_, h_)) e->spec.opacity = opacity;
}
void ElementRef::setOffset(Vec2 offset) {
    if (Element* e = resolve(canvas_, h_)) e->spec.offset = offset;
}
void ElementRef::setScale(f32) {} // drawn scale arrives with animations (Task 5)

UiHandle ElementRef::addChild(Kind kind, const UiSpec& spec) {
    return canvas_ ? canvas_->impl().store.add(h_, kind, spec) : UiHandle{};
}

void ElementRef::remove() {
    if (canvas_) canvas_->impl().store.remove(h_);
}

void ImageRef::setSprite(std::string_view sprite) {
    if (Element* e = resolveForLayout(canvas_, h_)) e->spec.sprite.assign(sprite.data(), sprite.size());
}
void LabelRef::setText(std::string_view text) {
    if (Element* e = resolveForLayout(canvas_, h_)) e->spec.text.assign(text.data(), text.size());
}
void ButtonRef::setText(std::string_view text) {
    if (Element* e = resolveForLayout(canvas_, h_)) e->spec.text.assign(text.data(), text.size());
}
void ToggleRef::setOn(bool on) {
    if (Element* e = resolve(canvas_, h_)) e->spec.on = on;
}
bool ToggleRef::on() const {
    const Element* e = resolve(canvas_, h_);
    return e && e->spec.on;
}
void SliderRef::setValue(f32 value) {
    if (Element* e = resolve(canvas_, h_)) e->spec.value = value;
}
f32 SliderRef::value() const {
    const Element* e = resolve(canvas_, h_);
    return e ? e->spec.value : 0;
}
void ProgressRef::setValue(f32 value) {
    if (Element* e = resolve(canvas_, h_)) e->spec.value = value;
}

// ---- Engine hooks ------------------------------------------------------------------------

void uiAttach(Canvas& canvas, Services* services) { canvas.impl().services = services; }

void uiFlush(Canvas& canvas) { canvas.impl().store.flush(); }

void uiUpdate(Canvas& canvas, f32) {
    Canvas::Impl& impl = canvas.impl();
    if (!impl.services || !impl.services->renderer) return;
    const RendererBackend& r = *impl.services->renderer;
    layoutCanvas(impl.store, LayoutEnv{r.visibleRect(), r.safeArea(), r.pixelScale()}, nullptr, nullptr);
}

} // namespace nova::ui
