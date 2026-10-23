#include <nova/render/sprite_batch.hpp>

#include <algorithm>

namespace nova {
namespace {

constexpr size_t kReservedSprites = 4096;

FColor toFColor(Color c) {
    constexpr f32 k = 1.0f / 255.0f;
    return {c.r * k, c.g * k, c.b * k, c.a * k};
}

} // namespace

SpriteBatch::SpriteBatch() {
    cmds_.reserve(kReservedSprites);
    vertices_.reserve(kReservedSprites * 4);
    indices_.reserve(kReservedSprites * 6);
    runs_.reserve(256);
}

void SpriteBatch::begin() {
    cmds_.clear();
    vertices_.clear();
    indices_.clear();
    runs_.clear();
}

void SpriteBatch::build(const std::function<Vec2(TextureHandle)>& textureSize) {
    std::sort(cmds_.begin(), cmds_.end(), [](const SpriteCmd& a, const SpriteCmd& b) { return a.key < b.key; });

    for (const SpriteCmd& c : cmds_) {
        if (runs_.empty() || !(runs_.back().texture == c.texture) || runs_.back().blend != c.blend) {
            runs_.push_back({c.texture, c.blend, u32(indices_.size()), 0});
        }

        Vec2 texSize = textureSize(c.texture);
        Rect src = c.src.w > 0 ? c.src : Rect{0, 0, texSize.x, texSize.y};
        f32 u0 = src.x / texSize.x, v0 = src.y / texSize.y;
        f32 u1 = (src.x + src.w) / texSize.x, v1 = (src.y + src.h) / texSize.y;

        f32 x0 = -c.pivot.x * c.size.x, y0 = -c.pivot.y * c.size.y;
        f32 x1 = x0 + c.size.x, y1 = y0 + c.size.y;
        FColor color = toFColor(c.color);

        auto base = i32(vertices_.size());
        vertices_.push_back({c.xform.apply({x0, y0}), color, {u0, v0}}); // TL
        vertices_.push_back({c.xform.apply({x1, y0}), color, {u1, v0}}); // TR
        vertices_.push_back({c.xform.apply({x1, y1}), color, {u1, v1}}); // BR
        vertices_.push_back({c.xform.apply({x0, y1}), color, {u0, v1}}); // BL
        for (i32 i : {0, 1, 2, 0, 2, 3}) indices_.push_back(base + i);
        runs_.back().indexCount += 6;
    }
}

void SpriteBatch::toPixels(f32 scale, Vec2 visibleOrigin) {
    for (Vertex& v : vertices_) v.pos = (v.pos - visibleOrigin) * scale;
}

} // namespace nova
