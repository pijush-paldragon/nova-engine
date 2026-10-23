#pragma once

#include <nova/core/math.hpp>

#include <vector>

inline constexpr nova::Vec2 kBrickSize{80, 36};
inline constexpr float kBrickGap = 8;
inline constexpr float kBricksTop = 220;

struct LevelLayout {
    int cols = 8;
    int rows = 0;
    std::vector<int> hp; // row-major, row 0 at the top
};

// Level 1 has 5 rows; one more per level up to 10. Top rows take the most hits (3).
LevelLayout makeLevel(int level);

// Brick rectangle in design units (720 wide), grid centred horizontally.
nova::Rect brickRect(int col, int row, float top);

nova::Color brickColor(int hp);
