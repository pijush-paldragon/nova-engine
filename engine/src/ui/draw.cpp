#include "ui/draw.hpp"

#include "ui/canvas_impl.hpp"
#include "ui/layout.hpp"
#include "ui/ui_backend.hpp"

#include <nova/assets/assets.hpp>
#include <render/renderer_backend.hpp>
#include <scene/services.hpp>
#include <text/text_backend.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace nova::ui {

void TextRunDeleter::operator()(TextRun* run) const { delete run; }

// ---- Shared shape texture ----------------------------------------------------------------

namespace {

constexpr f32 kSupersample = 3; // texels per design unit in the generated shapes
constexpr f32 kOutlineTexels = 2 * kSupersample;

// Coverage of a rounded square [0, n]^2 with corner radius r at point p (antialiased).
f32 coverage(f32 px, f32 py, f32 lo, f32 hi, f32 r) {
    const f32 c = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
    const f32 qx = std::fabs(px - c) - (half - r), qy = std::fabs(py - c) - (half - r);
    const f32 ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    const f32 d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
    return std::clamp(0.5f - d, 0.f, 1.f);
}

struct Cell {
    int size;
    f32 inset;
    f32 radius;
    bool outlined;
};

} // namespace

UiShared::~UiShared() {
    if (renderer && shapes.valid()) renderer->destroyTexture(shapes);
}

const ShapeRegion& UiShared::shape(u8 radius, bool outlined) const {
    size_t best = 0;
    for (size_t i = 1; i < kShapeRadii.size(); ++i) {
        if (std::abs(int(kShapeRadii[i]) - int(radius)) < std::abs(int(kShapeRadii[best]) - int(radius))) best = i;
    }
    return outlined ? outline[best] : filled[best];
}

std::unique_ptr<UiShared> createUiShared(RendererBackend& renderer) {
    auto shared = std::make_unique<UiShared>();
    shared->renderer = &renderer;

    std::vector<Cell> cells;
    for (bool outlined : {false, true}) {
        for (u8 radius : kShapeRadii) {
            const f32 r = f32(radius) * kSupersample;
            const f32 inset = outlined ? std::max(r, kOutlineTexels) : r;
            // A plain square is sampled only from its middle texels: stretched across a whole
            // screen, linear filtering would otherwise blend its edge with the transparent gap.
            const int size = r == 0 && !outlined ? 8 : std::max(4, int(2 * inset) + 2);
            cells.push_back({size, inset, r, outlined});
        }
    }
    cells.push_back({96, 48, 48, false}); // circle (slider knob, switch knob)

    int width = 0, height = 0;
    for (const Cell& c : cells) {
        width += c.size + 1; // 1 texel gap stops linear filtering bleeding between cells
        height = std::max(height, c.size);
    }
    std::vector<u8> pixels(size_t(width) * size_t(height) * 4, 0);
    int x0 = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        const Cell& c = cells[i];
        const f32 n = f32(c.size);
        for (int y = 0; y < c.size; ++y) {
            for (int x = 0; x < c.size; ++x) {
                const f32 px = f32(x) + 0.5f, py = f32(y) + 0.5f;
                f32 a = c.radius > 0 || c.outlined ? coverage(px, py, 0, n, c.radius) : 1.f;
                if (c.outlined) {
                    const f32 innerR = std::max(c.radius - kOutlineTexels, 0.f);
                    a -= coverage(px, py, kOutlineTexels, n - kOutlineTexels, innerR);
                }
                u8* p = &pixels[(size_t(y) * size_t(width) + size_t(x0 + x)) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = u8(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
            }
        }
        ShapeRegion region{{f32(x0), 0, n, n}, c.inset, c.inset / kSupersample};
        if (c.radius == 0 && !c.outlined) region.src = {f32(x0) + 2, 2, n - 4, n - 4};
        if (i < kShapeRadii.size()) shared->filled[i] = region;
        else if (i < 2 * kShapeRadii.size()) shared->outline[i - kShapeRadii.size()] = region;
        else shared->circle = region;
        x0 += c.size + 1;
    }
    shared->shapes = renderer.createTexture(pixels.data(), width, height);
    return shared;
}

// ---- Resolving styles, fonts, sprites ------------------------------------------------------

namespace {

bool carriesText(Kind k) { return k == Kind::Label || k == Kind::Button; }

std::string_view styleNameOf(const Element& e) {
    return e.spec.style.empty() ? std::string_view(defaultStyleName(e.kind)) : std::string_view(e.spec.style);
}

SpriteRegion resolveSprite(Assets& assets, std::string_view sprite, ScopeId scope) {
    if (sprite.empty()) return {};
    if (sprite.size() > 4 && sprite.substr(sprite.size() - 4) == ".png") return {assets.texture(sprite, scope), {}};
    const size_t slash = sprite.rfind('/');
    if (slash == std::string_view::npos) {
        logOnce("ui: sprite \"" + std::string(sprite) + "\" must be \"<atlas>/<region>\" or a .png path");
        return {assets.fallbackTexture(), {}};
    }
    return assets.atlas(sprite.substr(0, slash), scope).region(sprite.substr(slash + 1));
}

void prepareElement(Canvas::Impl& impl, Element& e, const UiShared& shared) {
    const Style& style = shared.theme.resolve(styleNameOf(e), VisualState::Normal);
    if (e.resolveDirty || e.themeGen != shared.theme.generation()) {
        e.padding = e.spec.padding == Insets{} ? style.padding : e.spec.padding;
        e.font = {};
        if (carriesText(e.kind) && impl.services->assets) {
            if (style.font.empty()) logOnce("ui: style \"" + std::string(styleNameOf(e)) + "\" has no font");
            else e.font = impl.services->assets->font(style.font, style.fontSize, impl.scope);
        }
        e.clickSound = style.clickSound.empty() || !impl.services->assets
                           ? SoundHandle{}
                           : impl.services->assets->sound(style.clickSound, impl.scope);
        e.region = {};
        if (impl.services->assets) {
            const std::string& sprite = e.kind == Kind::Image ? e.spec.sprite : e.spec.icon;
            e.region = resolveSprite(*impl.services->assets, sprite, impl.scope);
        }
        e.resolveDirty = false;
        e.themeGen = shared.theme.generation();
    }
    if (carriesText(e.kind) && impl.services->text) {
        if (e.spec.text.empty() || !e.font.valid()) {
            e.textRun.reset();
        } else {
            if (!e.textRun) e.textRun.reset(new TextRun(*impl.services->text));
            if (!e.textRun->set(e.font, e.spec.text)) e.textRun.reset();
        }
    }
}

Vec2 regionSize(const Canvas::Impl& impl, const SpriteRegion& r) {
    if (!r.texture.valid()) return {};
    if (r.src.w > 0) return {r.src.w, r.src.h};
    return impl.services && impl.services->renderer ? impl.services->renderer->textureSize(r.texture) : Vec2{};
}

constexpr f32 kIconGap = 12;

Vec2 measureElement(const Element& e, void* ctx) {
    const auto& impl = *static_cast<const Canvas::Impl*>(ctx);
    Vec2 text = e.textRun ? e.textRun->size() : Vec2{};
    switch (e.kind) {
        case Kind::Label: return text;
        case Kind::Image: return regionSize(impl, e.region);
        case Kind::Button: {
            Vec2 icon = regionSize(impl, e.region);
            if (icon.x > 0 && text.x > 0) return {icon.x + kIconGap + text.x, std::max(icon.y, text.y)};
            return icon.x > 0 ? icon : text;
        }
        // Natural sizes of the built-in widget looks.
        case Kind::Toggle: return e.spec.look == ToggleLook::Switch ? Vec2{96, 56} : Vec2{56, 56};
        case Kind::Slider: return {400, 48};
        case Kind::ProgressBar: return {300, 24};
        default: return {};
    }
}

} // namespace

void refreshLayout(Canvas::Impl& impl) {
    if (!impl.services || !impl.services->renderer || !impl.services->ui) return;
    const UiShared& shared = *impl.services->ui;
    ElementStore& store = impl.store;
    const bool themeChanged = impl.themeGen != shared.theme.generation();
    store.forEach([&](UiHandle, Element& e) {
        if (themeChanged || e.resolveDirty || store.dirty(e.layer)) prepareElement(impl, e, shared);
    });
    if (themeChanged) {
        for (u16 l = 0; l < store.layerCount(); ++l) store.markDirty(l);
        impl.themeGen = shared.theme.generation();
    }
    const RendererBackend& r = *impl.services->renderer;
    layoutCanvas(store, LayoutEnv{r.visibleRect(), r.safeArea(), r.pixelScale()}, &measureElement, &impl);
}

// ---- Draw list ------------------------------------------------------------------------------

namespace {

// Uniform scale + translation: p -> s * p + t.
struct Xf {
    f32 s = 1;
    Vec2 t{};
    Vec2 apply(Vec2 p) const { return p * s + t; }
};

Color modulate(Color a, Color b, f32 opacity) {
    auto mul = [](u8 x, u8 y) { return u8((u32(x) * u32(y) + 127) / 255); };
    Color c{mul(a.r, b.r), mul(a.g, b.g), mul(a.b, b.b), mul(a.a, b.a)};
    c.a = u8(std::lround(f32(c.a) * std::clamp(opacity, 0.f, 1.f)));
    return c;
}

struct Emitter {
    DrawLayer& out;
    u16 order = 0;

    void quad(TextureHandle tex, Rect src, Rect dst, Color color, const Xf& xf) {
        if (dst.w <= 0 || dst.h <= 0 || color.a == 0) return;
        Affine2 m{xf.s, 0, 0, xf.s, xf.s * dst.x + xf.t.x, xf.s * dst.y + xf.t.y};
        out.sprites.push_back({makeSortKey(0, BlendMode::Alpha, 0, order++), tex, src, m, {dst.w, dst.h}, {0, 0}, color,
                               BlendMode::Alpha});
    }

    // Nine pieces: corners keep `dstIn` size, edges stretch one way, the centre both ways.
    void nineSlice(TextureHandle tex, Rect src, Insets srcIn, Insets dstIn, Rect dst, Color color, const Xf& xf) {
        if (srcIn == Insets{}) {
            quad(tex, src, dst, color, xf);
            return;
        }
        // Shrink corners proportionally when the box is smaller than both corners.
        const f32 sx = std::min(1.f, dst.w / std::max(dstIn.left + dstIn.right, 1e-3f));
        const f32 sy = std::min(1.f, dst.h / std::max(dstIn.top + dstIn.bottom, 1e-3f));
        dstIn = {dstIn.top * sy, dstIn.right * sx, dstIn.bottom * sy, dstIn.left * sx};
        const f32 sxs[4] = {src.x, src.x + srcIn.left, src.x + src.w - srcIn.right, src.x + src.w};
        const f32 sys[4] = {src.y, src.y + srcIn.top, src.y + src.h - srcIn.bottom, src.y + src.h};
        const f32 dxs[4] = {dst.x, dst.x + dstIn.left, dst.x + dst.w - dstIn.right, dst.x + dst.w};
        const f32 dys[4] = {dst.y, dst.y + dstIn.top, dst.y + dst.h - dstIn.bottom, dst.y + dst.h};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                Rect s{sxs[col], sys[row], sxs[col + 1] - sxs[col], sys[row + 1] - sys[row]};
                Rect d{dxs[col], dys[row], dxs[col + 1] - dxs[col], dys[row + 1] - dys[row]};
                Affine2 m{xf.s, 0, 0, xf.s, xf.s * d.x + xf.t.x, xf.s * d.y + xf.t.y};
                out.sprites.push_back({makeSortKey(0, BlendMode::Alpha, 0, order++), tex, s, m, {d.w, d.h}, {0, 0},
                                       color, BlendMode::Alpha});
            }
        }
    }
};

void emitBackground(Emitter& em, const UiShared& shared, const Canvas::Impl& impl, const Background& bg, Rect box,
                    Color tint, f32 opacity, const Xf& xf) {
    if (bg.kind == Background::Kind::Shape) {
        const ShapeRegion& s = shared.shape(bg.radius, false);
        em.nineSlice(shared.shapes, s.src, Insets::all(s.insetTexels), Insets::all(s.insetDesign), box,
                     modulate(bg.color, tint, opacity), xf);
    } else if (bg.kind == Background::Kind::Sprite && impl.services->assets) {
        SpriteRegion r = resolveSprite(*impl.services->assets, bg.sprite, impl.scope);
        Rect src = r.src.w > 0 ? r.src : Rect{0, 0, regionSize(impl, r).x, regionSize(impl, r).y};
        em.nineSlice(r.texture, src, bg.slice, bg.slice, box, modulate(bg.color, tint, opacity), xf);
    }
}

void emitImage(Emitter& em, const Canvas::Impl& impl, const Element& e, Rect box, Color color, const Xf& xf) {
    if (!e.region.texture.valid()) return;
    const Vec2 natural = regionSize(impl, e.region);
    Rect src = e.region.src.w > 0 ? e.region.src : Rect{0, 0, natural.x, natural.y};
    if (natural.x <= 0 || natural.y <= 0) return;
    Rect dst = box;
    if (e.spec.slice != Insets{}) {
        em.nineSlice(e.region.texture, src, e.spec.slice, e.spec.slice, box, color, xf);
        return;
    }
    if (e.spec.imageFit == ImageFit::Contain) {
        const f32 k = std::min(box.w / natural.x, box.h / natural.y);
        dst = {box.x + (box.w - natural.x * k) * 0.5f, box.y + (box.h - natural.y * k) * 0.5f, natural.x * k, natural.y * k};
    } else if (e.spec.imageFit == ImageFit::Cover) {
        const f32 k = std::max(box.w / natural.x, box.h / natural.y);
        const f32 cw = box.w / k, ch = box.h / k; // visible part of the source, texels
        src = {src.x + (src.w - cw) * 0.5f, src.y + (src.h - ch) * 0.5f, cw, ch};
    }
    em.quad(e.region.texture, src, dst, color, xf);
}

// Built-in looks for toggles, sliders and progress bars, from the style's background (track)
// and tint (fill, knob accent).
void emitWidget(Emitter& em, const UiShared& shared, const Element& e, const Style& style, f32 opacity, const Xf& xf) {
    const Rect b = e.box;
    const Background& bg = style.background;
    auto shape = [&](Rect r, u8 radius, Color color) {
        const ShapeRegion& s = shared.shape(radius, false);
        em.nineSlice(shared.shapes, s.src, Insets::all(s.insetTexels), Insets::all(s.insetDesign), r,
                     modulate(color, Color::White, opacity), xf);
    };
    auto knob = [&](Vec2 centre, f32 d, Color color) {
        em.quad(shared.shapes, shared.circle.src, {centre.x - d * 0.5f, centre.y - d * 0.5f, d, d},
                modulate(color, Color::White, opacity), xf);
    };
    if (e.kind == Kind::Toggle) {
        if (e.spec.look == ToggleLook::Switch) {
            shape(b, 32, bg.color);
            const f32 d = b.h - 8;
            const f32 x = e.spec.on ? b.x + b.w - 4 - d * 0.5f : b.x + 4 + d * 0.5f;
            knob({x, b.y + b.h * 0.5f}, d, Color::White);
        } else {
            shape(b, 8, bg.color);
            if (e.spec.on) shape({b.x + b.w * 0.25f, b.y + b.h * 0.25f, b.w * 0.5f, b.h * 0.5f}, 8, Color::White);
        }
    } else if (e.kind == Kind::Slider) {
        const f32 range = e.spec.max - e.spec.min;
        const f32 t = range != 0 ? std::clamp((e.spec.value - e.spec.min) / range, 0.f, 1.f) : 0;
        const f32 trackH = 12, cy = b.y + b.h * 0.5f;
        shape({b.x, cy - trackH * 0.5f, b.w, trackH}, 8, bg.color);
        shape({b.x, cy - trackH * 0.5f, b.w * t, trackH}, 8, style.tint);
        knob({b.x + b.w * t, cy}, std::min(40.f, b.h), Color::White);
    } else {
        const f32 v = std::clamp(e.shown, 0.f, 1.f);
        shape(b, 8, bg.color);
        Rect fill = b;
        switch (e.spec.direction) {
            case FillDirection::Right: fill.w = b.w * v; break;
            case FillDirection::Left: fill = {b.x + b.w * (1 - v), b.y, b.w * v, b.h}; break;
            case FillDirection::Up: fill = {b.x, b.y + b.h * (1 - v), b.w, b.h * v}; break;
            case FillDirection::Down: fill.h = b.h * v; break;
        }
        shape(fill, 8, style.tint);
    }
}

Rect contentBox(const Element& e) {
    const Insets& p = e.padding;
    return {e.box.x + p.left, e.box.y + p.top, std::max(0.f, e.box.w - p.left - p.right),
            std::max(0.f, e.box.h - p.top - p.bottom)};
}

struct Frame {
    UiHandle h;
    Xf xf;
    f32 opacity;
};

void buildLayer(Canvas::Impl& impl, u16 layer, DrawLayer& out, std::vector<Frame>& stack) {
    const UiShared& shared = *impl.services->ui;
    ElementStore& store = impl.store;
    Emitter em{out};
    stack.clear();
    stack.push_back({store.layerRoot(layer), {}, 1});
    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        Element& e = *store.get(f.h);
        if (e.removed || !e.spec.visible) continue;
        const VisualState state = !e.spec.enabled                        ? VisualState::Disabled
                                : e.pressed                               ? VisualState::Pressed
                                : e.kind == Kind::Toggle && e.spec.on    ? VisualState::Checked
                                                                          : VisualState::Normal;
        const Style& style = shared.theme.resolve(styleNameOf(e), state);
        const f32 restScale = state == VisualState::Pressed
                                  ? shared.theme.resolve(styleNameOf(e), VisualState::Normal).scale
                                  : style.scale;
        const f32 pressScale = shared.theme.resolve(styleNameOf(e), VisualState::Pressed).scale;

        // This element's local transform (offset, then scale about its centre), under its parent's.
        // The pressed scale eases in over 0.08 s (pressAmount).
        const f32 s = e.scale * (restScale + (pressScale - restScale) * e.pressAmount);
        const Vec2 c = e.box.center() + e.spec.offset;
        const Vec2 localT = c + (e.spec.offset - c) * s;
        const Xf xf{f.xf.s * s, localT * f.xf.s + f.xf.t};
        const f32 opacity = f.opacity * e.spec.opacity * style.opacity;

        if (e.kind == Kind::Toggle || e.kind == Kind::Slider || e.kind == Kind::ProgressBar) {
            emitWidget(em, shared, e, style, opacity, xf);
        } else {
            emitBackground(em, shared, impl, style.background, e.box, style.tint, opacity, xf);
        }
        if (style.outlineColor) {
            const ShapeRegion& o = shared.shape(style.background.radius, true);
            em.nineSlice(shared.shapes, o.src, Insets::all(o.insetTexels), Insets::all(o.insetDesign), e.box,
                         modulate(*style.outlineColor, style.tint, opacity), xf);
        }
        const Color tint = modulate(e.spec.tint, style.tint, opacity);
        const Rect content = contentBox(e);
        if (e.kind == Kind::Image) emitImage(em, impl, e, e.box, tint, xf);

        Rect textArea = content;
        if (e.kind == Kind::Button && e.region.texture.valid()) {
            const Vec2 icon = regionSize(impl, e.region);
            const Vec2 text = e.textRun ? e.textRun->size() : Vec2{};
            const f32 k = icon.y > content.h && icon.y > 0 ? content.h / icon.y : 1;
            const f32 total = icon.x * k + (text.x > 0 ? kIconGap + text.x : 0);
            Rect dst{content.x + (content.w - total) * 0.5f, content.y + (content.h - icon.y * k) * 0.5f, icon.x * k,
                     icon.y * k};
            em.quad(e.region.texture, e.region.src, dst, tint, xf);
            textArea = {dst.x + dst.w + kIconGap, content.y, std::max(0.f, content.x + content.w - dst.x - dst.w - kIconGap),
                        content.h};
            if (text.x > 0) textArea.w = text.x;
        }
        if (e.textRun) {
            const Vec2 size = e.textRun->size();
            const TextAlignH align = e.kind == Kind::Button ? TextAlignH::Center : e.spec.textAlign;
            const f32 x = align == TextAlignH::Left   ? textArea.x
                        : align == TextAlignH::Center ? textArea.x + (textArea.w - size.x) * 0.5f
                                                      : textArea.x + textArea.w - size.x;
            const Rect box{x, textArea.y + (textArea.h - size.y) * 0.5f, size.x, size.y};
            out.texts.push_back({e.textRun.get(), box, xf.apply({box.x, box.y}), xf.s,
                                 modulate(style.textColor, Color::White, opacity)});
        }

        // Children in order: push in reverse so the first child is drawn first.
        const size_t mark = stack.size();
        store.forEachChild(f.h, [&](UiHandle ch, const Element&) { stack.push_back({ch, xf, opacity}); });
        std::reverse(stack.begin() + long(mark), stack.end());
    }
}

} // namespace

void uiDraw(Canvas& canvas, RendererBackend& renderer, SpriteBatch& batch) {
    Canvas::Impl& impl = canvas.impl();
    if (!impl.services || !impl.services->ui) return;
    refreshLayout(impl); // a scene below the top one still re-lays out after a resize
    static thread_local std::vector<Frame> stack;
    if (stack.capacity() < impl.store.size()) stack.reserve(impl.store.size() * 2);
    DrawList& list = impl.drawList;
    if (list.layers.size() != impl.store.layerCount()) list.layers.resize(impl.store.layerCount());
    for (u16 layer = 0; layer < impl.store.layerCount(); ++layer) {
        DrawLayer& out = list.layers[layer];
        out.sprites.clear();
        out.texts.clear();
        buildLayer(impl, layer, out, stack);
        if (!out.sprites.empty()) {
            batch.begin();
            for (const SpriteCmd& cmd : out.sprites) batch.push(cmd);
            renderer.submit(batch);
        }
        for (const TextItem& t : out.texts) t.run->draw(t.topLeft, renderer.fit(), t.color, t.scale);
    }
}

} // namespace nova::ui
