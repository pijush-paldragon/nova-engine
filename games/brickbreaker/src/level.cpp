#include "level.hpp"

#include <algorithm>

LevelLayout makeLevel(int level) {
    LevelLayout layout;
    layout.rows = std::clamp(4 + level, 1, 10);
    layout.hp.reserve(size_t(layout.cols * layout.rows));
    for (int row = 0; row < layout.rows; ++row) {
        int hp = 3 - (row * 3) / layout.rows; // 3 at the top down to 1 at the bottom
        for (int col = 0; col < layout.cols; ++col) layout.hp.push_back(std::clamp(hp, 1, 3));
    }
    return layout;
}

nova::Rect brickRect(int col, int row, float top) {
    constexpr int kCols = 8;
    constexpr float gridWidth = kCols * kBrickSize.x + (kCols - 1) * kBrickGap;
    constexpr float left = (720.f - gridWidth) * 0.5f;
    return {left + float(col) * (kBrickSize.x + kBrickGap), top + float(row) * (kBrickSize.y + kBrickGap),
            kBrickSize.x, kBrickSize.y};
}

nova::Color brickColor(int hp) {
    switch (hp) {
        case 3: return nova::Color::fromHex(0xFFD166FF);
        case 2: return nova::Color::fromHex(0xF72585FF);
        default: return nova::Color::fromHex(0x4CC9F0FF);
    }
}
