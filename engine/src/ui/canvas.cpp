#include "ui/animation.hpp"
#include "ui/canvas_impl.hpp"
#include "ui/layout.hpp"
#include "ui/reload.hpp"
#include "ui/ui_backend.hpp"

#include <nova/core/alloc_stats.hpp>
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

std::string_view Canvas::devError() const { return impl_->devError; }

void uiUpdate(Canvas& canvas, f32 dt) {
    pollReload(canvas.impl(), dt);
    updateTransitions(canvas.impl(), dt);
    updateAnimations(canvas.impl(), dt);
    refreshLayout(canvas.impl());
}

namespace {

// A popup's root is centred unless it chose an anchor, and blocks touches to its backdrop (a tap
// on the popup's own background must not count as a tap outside it).
UiSpec popupRootSpec(UiSpec spec) {
    if (spec.anchor == Anchor::TopLeft && spec.offset == Vec2{}) spec.anchor = Anchor::Center;
    spec.blocksInput = true;
    return spec;
}

} // namespace

void Canvas::Impl::buildLayer(u16 layer, const std::vector<NodeSpec>& nodes, std::string source,
                              const LoadResult& result) {
    const UiHandle root = store.layerRoot(layer);
    std::vector<UiHandle> old;
    store.forEachChild(root, [&](UiHandle h, const Element& e) {
        if (!e.removed && !e.backdrop) old.push_back(h);
    });
    for (UiHandle h : old) store.remove(h);
    std::vector<UiHandle> handles(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const NodeSpec& n = nodes[i];
        const UiHandle parent = n.parent < 0 ? root : handles[size_t(n.parent)];
        handles[i] = store.add(parent, n.kind, layer > 0 && i == 0 ? popupRootSpec(n.spec) : n.spec);
    }
    if (layer > 0 && !handles.empty()) {
        layerInfo[layer].content = handles[0];
        applyTransition(*this, layer); // a reloaded popup keeps its transition state
    }
    if (layerSources.size() <= layer) layerSources.resize(layer + 1u);
    if (layerFiles.size() <= layer) layerFiles.resize(layer + 1u);
    layerSources[layer] = std::move(source);
    layerFiles[layer] = result.files;
    recordLayerStamps(*this, layer);
}

// ---- Popups ----------------------------------------------------------------------------------

u16 Canvas::Impl::createPopupLayer(const PopupOptions& options) {
    // A finger already down belongs to what was under it; nothing beneath a new popup may click.
    for (Touch& t : touches) {
        if (!t.active) continue;
        t.cancelled = true;
        if (Element* e = store.live(t.owner)) e->pressed = false;
    }
    store.addLayer();
    const u16 layer = u16(store.layerCount() - 1);
    layerSources.resize(layer + 1u, "code");
    layerFiles.resize(layer + 1u);
    if (layerStamps.size() > layer) layerStamps.resize(layer);
    layerInfo.resize(layer + 1u);
    LayerInfo& info = layerInfo[layer];
    info = LayerInfo{};
    info.options = options;
    const bool animated = options.enter != Transition::None && options.duration > 0;
    info.phase = animated ? LayerInfo::Phase::Entering : LayerInfo::Phase::Open;
    UiSpec backdrop{.style = "ui-backdrop", .width = Size::fill(), .height = Size::fill(), .blocksInput = true};
    const UiHandle h = store.add(store.layerRoot(layer), options.closeOnBackdrop ? Kind::Button : Kind::Panel, backdrop);
    store.get(h)->backdrop = true;
    info.backdrop = h;
    if (options.closeOnBackdrop) {
        bind(h, [this, h](Binding& b) {
            b.click = [this, h] {
                if (const Element* e = store.live(h)) closeLayer(e->layer);
            };
        });
    }
    return layer;
}

void Canvas::Impl::closeLayer(u16 layer) {
    if (layer == 0 || layer >= layerInfo.size()) return;
    LayerInfo& info = layerInfo[layer];
    if (info.phase == LayerInfo::Phase::Exiting) return;
    const f32 dur = info.options.duration;
    // Start the exit from wherever the entrance got to.
    const f32 shown = info.phase == LayerInfo::Phase::Entering && dur > 0 ? std::min(info.t / dur, 1.f) : 1.f;
    info.phase = LayerInfo::Phase::Exiting;
    info.t = (1 - shown) * dur;
    for (Touch& t : touches) {
        const Element* e = store.get(t.owner);
        if (t.active && e && e->layer == layer) t.cancelled = true;
    }
}

void Canvas::Impl::finishLayer(u16 layer) {
    std::function<void()> done = std::move(layerInfo[layer].onClose);
    store.eraseLayer(layer);
    layerInfo.erase(layerInfo.begin() + layer);
    layerSources.erase(layerSources.begin() + layer);
    layerFiles.erase(layerFiles.begin() + layer);
    if (layer < layerStamps.size()) layerStamps.erase(layerStamps.begin() + layer);
    if (done) done();
}

u16 Canvas::Impl::layerOfContent(UiHandle content) const {
    const Element* e = store.live(content);
    return e && e->layer > 0 && layerInfo[e->layer].content == content ? e->layer : 0;
}

PopupRef Canvas::push(std::string_view file, PopupOptions options) {
    memory::LoadingScope loading;
    Impl& impl = *impl_;
    if (!impl.reader) impl.reader = assetFileReader();
    const Theme* theme = impl.services && impl.services->ui ? &impl.services->ui->theme : nullptr;
    std::vector<NodeSpec> nodes;
    LoadResult result = loadLayout(file, impl.reader, theme, nodes);
    logLoadResult(result);
    if (!result.ok) return {};
    const u16 layer = impl.createPopupLayer(options);
    impl.buildLayer(layer, nodes, std::string(file), result);
    applyTransition(impl, layer);
    return PopupRef(this, impl.layerInfo[layer].content);
}

PopupRef Canvas::push(const UiSpec& root, PopupOptions options) {
    Impl& impl = *impl_;
    const u16 layer = impl.createPopupLayer(options);
    const UiHandle content = impl.store.add(impl.store.layerRoot(layer), Kind::Panel, popupRootSpec(root));
    impl.layerInfo[layer].content = content;
    applyTransition(impl, layer);
    return PopupRef(this, content);
}

void Canvas::pop() {
    for (u16 layer = u16(impl_->layerInfo.size()); layer-- > 1;) {
        if (impl_->layerInfo[layer].phase != LayerInfo::Phase::Exiting) {
            impl_->closeLayer(layer);
            return;
        }
    }
}

u32 Canvas::popupCount() const {
    u32 n = 0;
    for (size_t i = 1; i < impl_->layerInfo.size(); ++i) n += impl_->layerInfo[i].phase != LayerInfo::Phase::Exiting;
    return n;
}

bool PopupRef::valid() const { return canvas_ && canvas_->impl().layerOfContent(content_) != 0; }

void PopupRef::close() {
    if (!canvas_) return;
    if (u16 layer = canvas_->impl().layerOfContent(content_)) canvas_->impl().closeLayer(layer);
}

void PopupRef::onClose(std::function<void()> fn) {
    if (!canvas_) return;
    if (u16 layer = canvas_->impl().layerOfContent(content_)) canvas_->impl().layerInfo[layer].onClose = std::move(fn);
}

ElementRef PopupRef::root() { return valid() ? ElementRef(canvas_, content_) : ElementRef(); }

UiHandle PopupRef::find(std::string_view id, int kind) const {
    if (!canvas_) return {};
    Canvas::Impl& impl = canvas_->impl();
    const u16 layer = impl.layerOfContent(content_);
    if (layer == 0) return {};
    const UiHandle h = impl.store.findIn(layer, id);
    const Element* e = impl.store.live(h);
    if (!e || (kind >= 0 && e->kind != Kind(kind))) {
        impl.reportMissing(id, kind >= 0 ? kindName(Kind(kind)) : "element", layer);
        return {};
    }
    return h;
}

ElementRef PopupRef::element(std::string_view id) { return ElementRef(canvas_, find(id, -1)); }
PanelRef PopupRef::panel(std::string_view id) { return PanelRef(canvas_, find(id, int(Kind::Panel))); }
ImageRef PopupRef::image(std::string_view id) { return ImageRef(canvas_, find(id, int(Kind::Image))); }
LabelRef PopupRef::label(std::string_view id) { return LabelRef(canvas_, find(id, int(Kind::Label))); }
ButtonRef PopupRef::button(std::string_view id) { return ButtonRef(canvas_, find(id, int(Kind::Button))); }
ToggleRef PopupRef::toggle(std::string_view id) { return ToggleRef(canvas_, find(id, int(Kind::Toggle))); }
SliderRef PopupRef::slider(std::string_view id) { return SliderRef(canvas_, find(id, int(Kind::Slider))); }
ProgressRef PopupRef::progress(std::string_view id) { return ProgressRef(canvas_, find(id, int(Kind::ProgressBar))); }

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
