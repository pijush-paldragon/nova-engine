#pragma once

#include <nova/core/types.hpp>

#include <cmath>

namespace nova {

inline constexpr f32 kPi = 3.14159265358979323846f;

struct Vec2 {
    f32 x = 0;
    f32 y = 0;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(Vec2 o) const { return {x * o.x, y * o.y}; }
    constexpr Vec2 operator/(Vec2 o) const { return {x / o.x, y / o.y}; }
    constexpr Vec2 operator*(f32 s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(f32 s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(f32 s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;

    f32 length() const { return std::sqrt(x * x + y * y); }
    // Returns {0,0} for a zero vector instead of NaN.
    Vec2 normalized() const {
        f32 len = length();
        return len > 0 ? Vec2{x / len, y / len} : Vec2{};
    }
};

constexpr f32 dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
constexpr Vec2 lerp(Vec2 a, Vec2 b, f32 t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

template <class T>
constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Rect {
    f32 x = 0;
    f32 y = 0;
    f32 w = 0;
    f32 h = 0;

    constexpr bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    constexpr bool intersects(const Rect& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    constexpr Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    constexpr f32 right() const { return x + w; }
    constexpr f32 bottom() const { return y + h; }
    constexpr bool operator==(const Rect&) const = default;
};

struct Color {
    u8 r = 255;
    u8 g = 255;
    u8 b = 255;
    u8 a = 255;

    // 0xRRGGBBAA
    static constexpr Color fromHex(u32 rgba) {
        return {u8(rgba >> 24), u8(rgba >> 16), u8(rgba >> 8), u8(rgba)};
    }
    constexpr Color withAlpha(u8 alpha) const { return {r, g, b, alpha}; }
    constexpr bool operator==(const Color&) const = default;

    static const Color White;
    static const Color Black;
    static const Color Magenta;
};

inline constexpr Color Color::White{255, 255, 255, 255};
inline constexpr Color Color::Black{0, 0, 0, 255};
inline constexpr Color Color::Magenta{255, 0, 255, 255};

inline Color lerp(Color a, Color b, f32 t) {
    auto ch = [t](u8 x, u8 y) { return u8(std::lround(lerp(f32(x), f32(y), t))); };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

// 2x3 affine transform. y-down convention: a positive angle rotates +x toward +y.
struct Affine2 {
    f32 a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    // Scale, then rotate, then translate.
    static Affine2 trs(Vec2 pos, f32 radians, Vec2 scale) {
        f32 cs = std::cos(radians), sn = std::sin(radians);
        return {cs * scale.x, sn * scale.x, -sn * scale.y, cs * scale.y, pos.x, pos.y};
    }
    constexpr Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
};

} // namespace nova
