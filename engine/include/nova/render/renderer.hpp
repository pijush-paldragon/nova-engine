#pragma once

#include <nova/render/camera.hpp>
#include <nova/render/types.hpp>

namespace nova {

// Game-facing view of the renderer. Games draw through components (Sprite, TextLabel,
// ParticleEmitter); this exposes screen metrics and settings.
class Renderer2D {
public:
    // Active backend as reported by SDL: "metal", "vulkan", "opengles2", "opengl", ...
    const char* backendName() const { return backendName_; }
    // The visible world area in design units; always contains the whole design area.
    Rect visibleRect() const { return fit_.visible; }
    // Area free of notches, rounded corners and home indicators, in design units.
    Rect safeArea() const { return safeArea_; }
    u32 drawCallsLastFrame() const { return drawCallsLastFrame_; }
    void setClearColor(Color color) { clearColor_ = color; }
    // Window pixels per design unit.
    f32 pixelScale() const { return fit_.scale; }
    const ScreenFit& fit() const { return fit_; }
    // Size in texels; an unknown or stale handle reports the checkerboard's size.
    Vec2 textureSize(TextureHandle texture) const;

protected:
    Renderer2D() = default;
    ~Renderer2D() = default;
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    const char* backendName_ = "none";
    ScreenFit fit_{};
    Rect safeArea_{};
    u32 drawCallsLastFrame_ = 0;
    Color clearColor_ = Color::Black;
};

} // namespace nova
