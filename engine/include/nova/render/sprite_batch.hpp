#pragma once

#include <nova/render/types.hpp>

#include <functional>
#include <span>
#include <vector>

namespace nova {

struct SpriteCmd {
    u64 key;           // see makeSortKey
    TextureHandle texture;
    Rect src;          // texels; w == 0 -> whole texture
    Affine2 xform;     // local quad space -> screen (design units)
    Vec2 size;         // quad size in design units (before xform scale)
    Vec2 pivot;        // 0..1 within the quad
    Color color;
    BlendMode blend;
};

// Sort order: layer, then blend mode, then texture, then submission order.
constexpr u64 makeSortKey(u8 layer, BlendMode blend, u16 textureIndex, u16 order) {
    return (u64(layer) << 56) | (u64(blend) << 48) | (u64(textureIndex) << 32) | (u64(order) << 16);
}

struct FColor {
    f32 r, g, b, a;
};

// Layout-identical to SDL_Vertex (checked in renderer.cpp).
struct Vertex {
    Vec2 pos;
    FColor color;
    Vec2 uv;
};

struct BatchRun {
    TextureHandle texture;
    BlendMode blend;
    u32 firstIndex;
    u32 indexCount;
};

// Collects sprite commands for a frame, sorts them, and expands them into one vertex/index
// stream with one run per (texture, blend) change. Storage is reused across frames.
class SpriteBatch {
public:
    SpriteBatch();

    void begin();
    void push(const SpriteCmd& cmd) { cmds_.push_back(cmd); }
    void build(const std::function<Vec2(TextureHandle)>& textureSize);

    // Converts vertex positions from design units to window pixels in place.
    void toPixels(f32 scale, Vec2 visibleOrigin);

    std::span<const Vertex> vertices() const { return vertices_; }
    std::span<const i32> indices() const { return indices_; }
    std::span<const BatchRun> runs() const { return runs_; }
    size_t spriteCount() const { return cmds_.size(); }

private:
    std::vector<SpriteCmd> cmds_;
    std::vector<Vertex> vertices_;
    std::vector<i32> indices_;
    std::vector<BatchRun> runs_;
};

} // namespace nova
