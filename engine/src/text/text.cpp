#include "text/text_backend.hpp"

#include "core/parse.hpp"
#include "text/localization_impl.hpp"
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

struct FontDeleter {
    void operator()(TTF_Font* f) const { TTF_CloseFont(f); }
};

// Engine-private component holding a label's laid-out text. Being a component, it is freed
// with its entity (or its World) automatically.
struct TextCache {
    explicit TextCache(TextBackend& backend) : block(backend) {}
    TextBlock block;
    u32 revision = ~0u; // label revision the translation was rendered for
};

struct FontEntry {
    // Declared first, destroyed last: the main font refers to its fallbacks.
    std::vector<std::unique_ptr<TTF_Font, FontDeleter>> fallbacks;
    std::unique_ptr<TTF_Font, FontDeleter> font;
    f32 sizeDesign;
};

struct DrawItem {
    u8 layer;
    u32 order;
    Entity entity; // looked up again at draw: caches may move while labels are collected
    Vec2 topLeft;
    f32 width;
    ui::TextAlignH align;
    Color color;
};

ui::TextAlignH toAlignH(TextAlign a) {
    return a == TextAlign::Left ? ui::TextAlignH::Left : a == TextAlign::Center ? ui::TextAlignH::Center : ui::TextAlignH::Right;
}

} // namespace

void TtfTextDeleter::operator()(TTF_Text* t) const {
    TTF_DestroyText(t);
    gLiveTexts.fetch_sub(1, std::memory_order_relaxed);
}

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

Text::Text() : impl_(std::make_unique<Impl>()), lang_(std::make_unique<Localization>()) {}
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
    text->renderer_ = renderer;
    text->impl_->drawList.reserve(64);
    return text;
}

TextBackend::~TextBackend() {
    if (u32 live = liveTextCount(); live > 0) log::error("TextBackend destroyed with %u labels alive", live);
}

namespace {

TTF_Font* openFont(std::string_view rel, f32 pixels) {
    SDL_IOStream* io = openAsset(rel);
    return io ? TTF_OpenFontIO(io, true, pixels) : nullptr;
}

} // namespace

std::optional<FontHandle> TextBackend::loadFont(std::string_view key) {
    // "path@size" or "path@size#lang": a language adds its fallback fonts and language tag.
    std::string_view lang;
    if (size_t hash = key.rfind('#'); hash != std::string_view::npos) {
        lang = key.substr(hash + 1);
        key = key.substr(0, hash);
    }
    size_t at = key.rfind('@');
    f32 size = 0;
    if (at == std::string_view::npos ||
        !parseFloat(key.substr(at + 1), size) || size <= 0) {
        log::error("Font key '%.*s' must look like 'path@size'", int(key.size()), key.data());
        return std::nullopt;
    }
    std::string path = assetPath(key.substr(0, at));
    TTF_Font* font = openFont(key.substr(0, at), size * pixelScale_);
    if (!font) {
        log::error("Font '%s' failed to load: %s", path.c_str(), SDL_GetError());
        return std::nullopt;
    }
    FontEntry entry{{}, std::unique_ptr<TTF_Font, FontDeleter>(font), size};
    const LanguageInfo& info = lang_->impl().info();
    if (!lang.empty() && lang == info.tag) {
        // Direction and script are left to HarfBuzz per text: forcing RTL on a font would also
        // reverse Latin text ("Score 10") shown in an Arabic UI. The language tag picks local
        // letter forms.
        std::string tag(lang);
        TTF_SetFontLanguage(font, tag.c_str());
        for (const std::string& fallbackPath : info.fonts) {
            TTF_Font* fallback = openFont(fallbackPath, size * pixelScale_);
            if (!fallback) {
                log::error("Fallback font '%s' for '%s' failed to load: %s", fallbackPath.c_str(), tag.c_str(),
                           SDL_GetError());
                continue;
            }
            TTF_SetFontLanguage(fallback, tag.c_str());
            TTF_AddFallbackFont(font, fallback);
            entry.fallbacks.emplace_back(fallback);
        }
    }
    return impl_->fonts.insert(std::move(entry));
}

TTF_Font* TextBackend::ttfFont(FontHandle font) const {
    const FontEntry* f = impl_->fonts.get(font);
    return f ? f->font.get() : nullptr;
}

void TextBackend::unloadFont(FontHandle font) {
    if (!impl_->fonts.get(font)) return;
    ++impl_->fontGeneration; // labels using it re-check their font before drawing
    impl_->fonts.remove(font);
}

void TextBackend::setPixelScale(f32 scale) {
    if (scale <= 0 || scale == pixelScale_) return;
    pixelScale_ = scale;
    impl_->fonts.forEach([scale](FontHandle, FontEntry& f) {
        TTF_SetFontSize(f.font.get(), f.sizeDesign * scale);
        for (auto& fallback : f.fallbacks) TTF_SetFontSize(fallback.get(), f.sizeDesign * scale);
    });
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
        if (!cache) cache = &registry.emplace<TextCache>(e, *this);

        // Keyed labels render their translation again when the key, a value or the language changes.
        if (!label.key_.empty() && (label.langGen_ != lang_->generation() || cache->revision != label.revision())) {
            char buf[1024];
            std::array<FormatArg, KeyArgs::kMax> args;
            const size_t n = label.args_.view(args);
            const size_t len = lang_->formatArgs(buf, label.key_, std::span<const FormatArg>(args.data(), n));
            label.shown_.assign(buf, len);
            label.langGen_ = lang_->generation();
        }
        cache->revision = label.revision();
        TextBlockParams p;
        p.fonts = std::span<const FontHandle>(&label.font, 1);
        p.text = label.displayed();
        p.wrapWidth = label.wrapWidth;
        p.maxLines = label.maxLines;
        p.lineSpacing = label.lineSpacing;
        p.rtl = lang_->rightToLeft();
        if (!cache->block.set(p) || cache->block.empty()) continue;
        const Vec2 size = cache->block.size();
        const Vec2 pos = t.pos - viewOrigin;
        const f32 x = label.align == TextAlign::Left ? pos.x : label.align == TextAlign::Center ? pos.x - size.x * 0.5f : pos.x - size.x;
        items.push_back({label.layer, order++, e, {x, pos.y - size.y * 0.5f}, size.x, toAlignH(label.align), label.color});
    }

    std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.layer != b.layer ? a.layer < b.layer : a.order < b.order;
    });
    for (const DrawItem& item : items) {
        if (TextCache* cache = registry.try_get<TextCache>(item.entity)) {
            cache->block.draw(item.topLeft, item.width, item.align, fit, item.color, 1);
        }
    }
}

TtfTextPtr TextBackend::createText(TTF_Font* font, std::string_view s) {
    TTF_Text* t = TTF_CreateText(impl_->engine, font, s.empty() ? "" : s.data(), s.size());
    if (t) gLiveTexts.fetch_add(1, std::memory_order_relaxed);
    return TtfTextPtr(t);
}

u32 TextBackend::fontGeneration() const { return impl_->fontGeneration; }

u32 TextBackend::liveTextCount() const { return gLiveTexts.load(std::memory_order_relaxed); }

u32 TextBackend::liveFontCount() const { return impl_->fonts.size(); }

} // namespace nova
