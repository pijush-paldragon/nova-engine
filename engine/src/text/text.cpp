#include "text/text_backend.hpp"

#include "core/parse.hpp"
#include "platform/asset_io.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/log.hpp>
#include <nova/ecs/components.hpp>
#include <nova/ecs/world.hpp>
#include <nova/platform/file.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace nova {
namespace {

std::atomic<u32> gLiveTexts{0};

struct TextDeleter {
    void operator()(TTF_Text* t) const {
        TTF_DestroyText(t);
        gLiveTexts.fetch_sub(1, std::memory_order_relaxed);
    }
};
struct FontDeleter {
    void operator()(TTF_Font* f) const { TTF_CloseFont(f); }
};

// Engine-private component holding a label's laid-out text. Being a component, it is freed
// with its entity (or its World) automatically.
struct TextCache {
    std::unique_ptr<TTF_Text, TextDeleter> text;
    FontHandle font;
    u32 revision = 0;
    u32 fontGeneration = 0;
    Color color{0, 0, 0, 0};
};

struct FontEntry {
    std::unique_ptr<TTF_Font, FontDeleter> font;
    f32 sizeDesign;
};

struct DrawItem {
    u8 layer;
    u32 order;
    TTF_Text* text;
    f32 x, y;
};

} // namespace

struct Text::Impl {
    TTF_TextEngine* engine = nullptr;
    HandlePool<FontEntry, FontTag> fonts;
    u32 fontGeneration = 1;
    std::vector<DrawItem> drawList;

    ~Impl() {
        fonts.clear();
        if (engine) {
            TTF_DestroyRendererTextEngine(engine);
            TTF_Quit();
        }
    }
};

Text::Text() : impl_(std::make_unique<Impl>()) {}
Text::~Text() = default;

Vec2 Text::measure(FontHandle font, std::string_view s) const {
    const FontEntry* f = impl_->fonts.get(font);
    if (!f) return {};
    int w = 0, h = 0;
    TTF_GetStringSize(f->font.get(), s.data(), s.size(), &w, &h);
    return Vec2{f32(w), f32(h)} / pixelScale_;
}

Result<std::unique_ptr<TextBackend>> TextBackend::create(SDL_Renderer* renderer) {
    if (!TTF_Init()) return Result<std::unique_ptr<TextBackend>>::fail(std::string("TTF_Init failed: ") + SDL_GetError());
    TTF_TextEngine* engine = TTF_CreateRendererTextEngine(renderer);
    if (!engine) {
        TTF_Quit();
        return Result<std::unique_ptr<TextBackend>>::fail(std::string("Text engine failed: ") + SDL_GetError());
    }
    std::unique_ptr<TextBackend> text(new TextBackend());
    text->impl_->engine = engine;
    text->impl_->drawList.reserve(64);
    return text;
}

TextBackend::~TextBackend() {
    if (u32 live = liveTextCount(); live > 0) log::error("TextBackend destroyed with %u labels alive", live);
}

std::optional<FontHandle> TextBackend::loadFont(std::string_view key) {
    size_t at = key.rfind('@');
    f32 size = 0;
    if (at == std::string_view::npos ||
        !parseFloat(key.substr(at + 1), size) || size <= 0) {
        log::error("Font key '%.*s' must look like 'path@size'", int(key.size()), key.data());
        return std::nullopt;
    }
    std::string path = assetPath(key.substr(0, at));
    SDL_IOStream* io = openAsset(key.substr(0, at));
    TTF_Font* font = io ? TTF_OpenFontIO(io, true, size * pixelScale_) : nullptr;
    if (!font) {
        log::error("Font '%s' failed to load: %s", path.c_str(), SDL_GetError());
        return std::nullopt;
    }
    return impl_->fonts.insert(FontEntry{std::unique_ptr<TTF_Font, FontDeleter>(font), size});
}

void TextBackend::unloadFont(FontHandle font) {
    if (!impl_->fonts.get(font)) return;
    ++impl_->fontGeneration; // labels using it re-check their font before drawing
    impl_->fonts.remove(font);
}

void TextBackend::setPixelScale(f32 scale) {
    if (scale <= 0 || scale == pixelScale_) return;
    pixelScale_ = scale;
    impl_->fonts.forEach([scale](FontHandle, FontEntry& f) { TTF_SetFontSize(f.font.get(), f.sizeDesign * scale); });
    ++impl_->fontGeneration;
}

void TextBackend::collectAndDraw(World& world, const ScreenFit& fit, Vec2 viewOrigin) {
    auto& registry = world.registry();
    auto& items = impl_->drawList;
    items.clear();
    u32 order = 0;
    // Engine-side data follows the game's TextLabel capacity, so World::reserve<TextLabel>(n)
    // also covers the private per-label caches and the draw list.
    const size_t labelCapacity = registry.storage<TextLabel>().capacity();
    if (auto& caches = registry.storage<TextCache>(); caches.capacity() < labelCapacity) caches.reserve(labelCapacity);
    if (items.capacity() < labelCapacity) items.reserve(labelCapacity);

    for (auto [e, t, label] : world.view<Transform, TextLabel>()) {
        if (!label.visible || world.pendingDestroy(e)) continue;
        FontEntry* font = impl_->fonts.get(label.font);
        if (!font) {
            registry.remove<TextCache>(e); // font gone: drop any stale layout
            continue;
        }
        TextCache* cache = registry.try_get<TextCache>(e);
        if (!cache) cache = &registry.emplace<TextCache>(e);

        const std::string& s = label.string();
        if (!cache->text || !(cache->font == label.font)) {
            {
                memory::DriverAllocScope driver; // SDL_ttf layout + GPU glyph uploads
                cache->text.reset(TTF_CreateText(impl_->engine, font->font.get(), s.data(), s.size()));
            }
            if (!cache->text) continue;
            gLiveTexts.fetch_add(1, std::memory_order_relaxed);
            cache->color = {0, 0, 0, 0};
            ++rebuilds_;
        } else if (cache->revision != label.revision() || cache->fontGeneration != impl_->fontGeneration) {
            memory::DriverAllocScope driver;
            TTF_SetTextString(cache->text.get(), s.data(), s.size());
            ++rebuilds_;
        }
        cache->font = label.font;
        cache->revision = label.revision();
        cache->fontGeneration = impl_->fontGeneration;
        if (!(cache->color == label.color)) {
            TTF_SetTextColor(cache->text.get(), label.color.r, label.color.g, label.color.b, label.color.a);
            cache->color = label.color;
        }

        int w = 0, h = 0;
        {
            memory::DriverAllocScope driver; // may lay out lazily
            TTF_GetTextSize(cache->text.get(), &w, &h);
        }
        Vec2 px = (t.pos - viewOrigin - Vec2{fit.visible.x, fit.visible.y}) * fit.scale;
        f32 x = label.align == TextAlign::Left ? px.x : label.align == TextAlign::Center ? px.x - f32(w) * 0.5f : px.x - f32(w);
        items.push_back({label.layer, order++, cache->text.get(), x, px.y - f32(h) * 0.5f});
    }

    std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.layer != b.layer ? a.layer < b.layer : a.order < b.order;
    });
    memory::DriverAllocScope driver;
    for (const DrawItem& item : items) TTF_DrawRendererText(item.text, std::round(item.x), std::round(item.y));
}

u32 TextBackend::liveTextCount() const { return gLiveTexts.load(std::memory_order_relaxed); }

u32 TextBackend::liveFontCount() const { return impl_->fonts.size(); }

} // namespace nova
