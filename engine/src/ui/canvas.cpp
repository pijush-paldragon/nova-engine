#include "ui/animation.hpp"
#include "ui/canvas_impl.hpp"
#include "ui/layout.hpp"
#include "ui/reload.hpp"
#include "ui/ui_backend.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/log.hpp>

#include <algorithm>
#include <render/renderer_backend.hpp>
#include <input/input_internal.hpp>
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

// Dev builds remember values for live reload; that bookkeeping is tooling, so its allocations
// never count against --fail-on-alloc. Release builds keep nothing.
template <class F>
void remember(Canvas* canvas, const Element& e, F&& fn) {
#if defined(NOVA_DEV_TOOLS)
    memory::LoadingScope tooling;
    if (Remembered* r = canvas->impl().rememberFor(e)) fn(*r);
#else
    (void)canvas;
    (void)e;
    (void)fn;
#endif
}

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
    if (Element* e = resolve(canvas_, h_)) {
        if (e->spec.visible != visible) canvas_->impl().store.markDirty(e->layer);
        e->spec.visible = visible;
        remember(canvas_, *e, [&](Remembered& r) { r.visible = visible; });
    }
}
void ElementRef::setEnabled(bool enabled) {
    if (Element* e = resolve(canvas_, h_)) {
        e->spec.enabled = enabled;
        remember(canvas_, *e, [&](Remembered& r) { r.enabled = enabled; });
    }
}
void ElementRef::setStyle(std::string_view style) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.style.assign(style.data(), style.size());
        e->resolveDirty = true;
        remember(canvas_, *e, [&](Remembered& r) { keep(r.style, style); });
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
    memory::LoadingScope building; // adding UI is setup, not per-frame work
    return canvas_ ? canvas_->impl().store.add(h_, kind, spec) : UiHandle{};
}

void ElementRef::remove() {
    if (canvas_) canvas_->impl().store.remove(h_);
}

void ImageRef::setSprite(std::string_view sprite) {
    if (Element* e = resolveForLayout(canvas_, h_)) {
        e->spec.sprite.assign(sprite.data(), sprite.size());
        e->resolveDirty = true;
        remember(canvas_, *e, [&](Remembered& r) { keep(r.sprite, sprite); });
    }
}
void LabelRef::setText(std::string_view text) {
    Element* e = resolve(canvas_, h_);
    if (!e || e->spec.text == text) return; // unchanged: no relayout
    canvas_->impl().store.markDirty(e->layer);
    e->spec.text.assign(text.data(), text.size());
    e->keyArgs = KeyArgs{};
    e->keyDirty = true;
    remember(canvas_, *e, [&](Remembered& r) { keep(r.text, text); });
}
void ButtonRef::setText(std::string_view text) {
    Element* e = resolve(canvas_, h_);
    if (!e || e->spec.text == text) return; // unchanged: no relayout
    canvas_->impl().store.markDirty(e->layer);
    e->spec.text.assign(text.data(), text.size());
    e->keyArgs = KeyArgs{};
    e->keyDirty = true;
    remember(canvas_, *e, [&](Remembered& r) { keep(r.text, text); });
}
namespace {

// "@key" in spec.text plus arguments; re-renders only when the key or a value changed.
void applyKey(Canvas* canvas, UiHandle h, std::string_view key, std::initializer_list<FormatArg> args) {
    Element* e = resolve(canvas, h);
    if (!e) return;
    const bool sameKey = e->spec.text.size() == key.size() + 1 && e->spec.text[0] == '@' &&
                         std::string_view(e->spec.text).substr(1) == key;
    if (!sameKey) {
        e->spec.text.assign("@");
        e->spec.text.append(key.data(), key.size());
    }
    const bool argsChanged = e->keyArgs.assign(args);
    if (sameKey && !argsChanged) return;
    e->keyDirty = true;
    canvas->impl().store.markDirty(e->layer);
}

} // namespace

void LabelRef::setKey(std::string_view key, std::initializer_list<FormatArg> args) { applyKey(canvas_, h_, key, args); }
void ButtonRef::setKey(std::string_view key, std::initializer_list<FormatArg> args) { applyKey(canvas_, h_, key, args); }

void ToggleRef::setOn(bool on) {
    if (Element* e = resolve(canvas_, h_)) {
        e->spec.on = on;
        remember(canvas_, *e, [&](Remembered& r) { r.on = on; });
    }
}
bool ToggleRef::on() const {
    const Element* e = resolve(canvas_, h_);
    return e && e->spec.on;
}
void SliderRef::setValue(f32 value) {
    if (Element* e = resolve(canvas_, h_)) {
        e->spec.value = std::clamp(value, std::min(e->spec.min, e->spec.max), std::max(e->spec.min, e->spec.max));
        remember(canvas_, *e, [&](Remembered& r) { r.value = e->spec.value; });
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
    remember(canvas_, *e, [&](Remembered& r) { r.value = value; });
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
    if (!handles.empty()) {
        setLayerContent(layer, handles[0]);
        applyTransition(*this, layer); // a reloaded popup keeps its transition state
    }
    if (layerSources.size() <= layer) layerSources.resize(layer + 1u);
    if (layerFiles.size() <= layer) layerFiles.resize(layer + 1u);
    layerSources[layer] = std::move(source);
    layerFiles[layer] = result.files;
    recordLayerStamps(*this, layer);
}

// ---- Popups ----------------------------------------------------------------------------------

void Canvas::Impl::setLayerContent(u16 layer, UiHandle content) {
    LayerInfo& info = layerInfo[layer];
    info.content = content;
    if (const Element* e = store.live(content)) {
        info.rootOffset = e->spec.offset;
        info.rootOpacity = e->spec.opacity;
        info.rootScale = e->scale;
    }
}

u16 Canvas::Impl::createPopupLayer(const PopupOptions& options, Transition enter, Transition exit) {
    // A finger already down belongs to what was under it; nothing beneath a new popup may click,
    // and gameplay stops seeing it too.
    for (Touch& t : touches) {
        if (!t.active) continue;
        t.cancelled = true;
        if (Element* e = store.live(t.owner)) e->pressed = false;
    }
    if (services && services->input) {
        InputBackend& input = *services->input;
        for (int i = 0; i < input.pointerCount(); ++i) {
            if (input.pointer(i)->held) input.setConsumed(input.pointer(i)->id);
        }
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
    info.enter = enter;
    info.exit = exit;
    info.token = nextToken++;
    info.phase = enter != Transition::None && options.duration > 0 ? LayerInfo::Phase::Entering : LayerInfo::Phase::Open;
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
    scopedBindings.erase(layerInfo[layer].token);
    store.eraseLayer(layer);
    layerInfo.erase(layerInfo.begin() + layer);
    layerSources.erase(layerSources.begin() + layer);
    layerFiles.erase(layerFiles.begin() + layer);
    if (layer < layerStamps.size()) layerStamps.erase(layerStamps.begin() + layer);
    if (done) done();
}

u16 Canvas::Impl::layerOfToken(u32 token) const {
    if (token == 0) return 0;
    for (u16 layer = 1; layer < layerInfo.size(); ++layer) {
        if (layerInfo[layer].token == token) return layer;
    }
    return 0;
}

void Canvas::Impl::prepareNow() {
    const size_t n = store.size() * 2 + 16;
    if (hitScratch.capacity() < n) hitScratch.reserve(n);
    if (hitStack.capacity() < n) hitStack.reserve(n);
    if (events.capacity() < 16) events.reserve(16);
    reserveLayoutScratch(n);
    reserveDrawScratch(n);
    if (drawList.layers.size() < store.layerCount()) drawList.layers.resize(store.layerCount());
    for (DrawLayer& layer : drawList.layers) {
        // Every element can emit up to 9 quads (nine-slice) plus a few for widgets.
        if (layer.sprites.capacity() < store.size() * 12) layer.sprites.reserve(store.size() * 12);
        if (layer.texts.capacity() < store.size()) layer.texts.reserve(store.size());
    }
    refreshLayout(*this); // fonts, text objects and layout, now rather than in the next frame
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
    // The caller's choice, else the file's root "enter"/"exit", else pop in and fade out.
    const Transition enter = options.enter.value_or(result.enter.value_or(Transition::Pop));
    const Transition exit = options.exit.value_or(result.exit.value_or(Transition::Fade));
    const u16 layer = impl.createPopupLayer(options, enter, exit);
    impl.buildLayer(layer, nodes, std::string(file), result);
    applyTransition(impl, layer);
    impl.prepareNow();
    return PopupRef(this, impl.layerInfo[layer].token);
}

PopupRef Canvas::push(const UiSpec& root, PopupOptions options) {
    memory::LoadingScope loading;
    Impl& impl = *impl_;
    const u16 layer = impl.createPopupLayer(options, options.enter.value_or(Transition::Pop),
                                            options.exit.value_or(Transition::Fade));
    const UiHandle content = impl.store.add(impl.store.layerRoot(layer), Kind::Panel, popupRootSpec(root));
    impl.setLayerContent(layer, content);
    applyTransition(impl, layer);
    impl.prepareNow();
    return PopupRef(this, impl.layerInfo[layer].token);
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

bool PopupRef::valid() const { return canvas_ && canvas_->impl().layerOfToken(token_) != 0; }

void PopupRef::close() {
    if (!canvas_) return;
    if (u16 layer = canvas_->impl().layerOfToken(token_)) canvas_->impl().closeLayer(layer);
}

void PopupRef::onClose(std::function<void()> fn) {
    if (!canvas_) return;
    if (u16 layer = canvas_->impl().layerOfToken(token_)) canvas_->impl().layerInfo[layer].onClose = std::move(fn);
}

ElementRef PopupRef::root() {
    if (!canvas_) return {};
    const u16 layer = canvas_->impl().layerOfToken(token_);
    return layer ? ElementRef(canvas_, canvas_->impl().layerInfo[layer].content) : ElementRef();
}

UiHandle PopupRef::find(std::string_view id, int kind) const {
    if (!canvas_) return {};
    Canvas::Impl& impl = canvas_->impl();
    const u16 layer = impl.layerOfToken(token_);
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
#if !defined(NOVA_DEV_TOOLS)
    (void)e;
    return nullptr; // only live reload reads remembered values
#else
    if (e.spec.id.empty()) return nullptr;
    auto it = remembered.find(std::string_view(e.spec.id));
    if (it == remembered.end()) it = remembered.emplace(e.spec.id, Remembered{}).first;
    return &it->second;
#endif
}

std::string_view Canvas::rememberedTextForTests(std::string_view id) const {
    auto it = impl_->remembered.find(id);
    return it != impl_->remembered.end() && it->second.text ? std::string_view(*it->second.text) : std::string_view();
}

} // namespace nova::ui
