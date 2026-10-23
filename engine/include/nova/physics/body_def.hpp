#pragma once

#include <nova/core/math.hpp>

namespace nova {

enum class BodyType : u8 { Static, Kinematic, Dynamic };
enum class ShapeType : u8 { Box, Circle };

// Fluent description of a rigid body with one shape, in design pixels:
//   BodyDef::dynamic().circle(16).restitution(1).friction(0).bullet()
struct BodyDef {
    BodyType type = BodyType::Static;
    ShapeType shape = ShapeType::Box;
    Vec2 boxSize{10, 10};
    f32 radius = 5;
    f32 densityValue = 1;
    f32 frictionValue = 0.6f;
    f32 restitutionValue = 0;
    bool isBullet = false;
    bool isSensor = false;
    bool lockRotation = false;

    static BodyDef dynamic() { return withType(BodyType::Dynamic); }
    static BodyDef kinematic() { return withType(BodyType::Kinematic); }
    static BodyDef staticBody() { return withType(BodyType::Static); }

    BodyDef& box(Vec2 sizePx) { shape = ShapeType::Box; boxSize = sizePx; return *this; }
    BodyDef& circle(f32 radiusPx) { shape = ShapeType::Circle; radius = radiusPx; return *this; }
    BodyDef& density(f32 v) { densityValue = v; return *this; }
    BodyDef& friction(f32 v) { frictionValue = v; return *this; }
    BodyDef& restitution(f32 v) { restitutionValue = v; return *this; }
    BodyDef& bullet() { isBullet = true; return *this; }
    BodyDef& sensor() { isSensor = true; return *this; }
    BodyDef& fixedRotation() { lockRotation = true; return *this; }

private:
    static BodyDef withType(BodyType t) {
        BodyDef d;
        d.type = t;
        return d;
    }
};

} // namespace nova
