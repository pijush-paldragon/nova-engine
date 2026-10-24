#include "input/input_internal.hpp"

#include <nova/core/log.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>

#include <optional>

namespace nova {
namespace {

constexpr f32 kTapMaxSeconds = 0.3f;
constexpr f32 kTapMaxTravel = 20.f;      // design units
constexpr f32 kMultiTapWindow = 0.15f;   // seconds between first and third finger

std::optional<Key> mapScancode(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: return Key::Left;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return Key::Right;
        case SDL_SCANCODE_UP: case SDL_SCANCODE_W: return Key::Up;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: return Key::Down;
        case SDL_SCANCODE_SPACE: return Key::Space;
        case SDL_SCANCODE_RETURN: return Key::Enter;
        case SDL_SCANCODE_ESCAPE: return Key::Escape;
        case SDL_SCANCODE_P: return Key::P;
        case SDL_SCANCODE_R: return Key::R;
        case SDL_SCANCODE_F1: return Key::F1;
        default: return std::nullopt;
    }
}

Vec2 pixelsToDesign(Vec2 px, const ScreenFit& fit) {
    return {fit.visible.x + px.x / fit.scale, fit.visible.y + px.y / fit.scale};
}

} // namespace

const Pointer* Input::primary() const {
    for (int i = 0; i < count_; ++i) {
        const Pointer& p = pointers_[size_t(i)];
        if (!p.released && !p.consumedByUi) return &p;
    }
    for (int i = 0; i < count_; ++i) {
        if (!pointers_[size_t(i)].consumedByUi) return &pointers_[size_t(i)];
    }
    return nullptr;
}

bool Input::tapped() const {
    if (!tapped_) return false;
    for (int i = 0; i < count_; ++i) {
        const Pointer& p = pointers_[size_t(i)];
        if (p.id == tapId_ && p.released) return !p.consumedByUi;
    }
    return true;
}

void InputBackend::setConsumed(u64 pointerId) {
    for (int i = 0; i < count_; ++i) {
        if (pointers_[size_t(i)].id == pointerId) pointers_[size_t(i)].consumedByUi = true;
    }
}

void InputBackend::beginFrame(f32 dt) {
    int kept = 0;
    for (int i = 0; i < count_; ++i) {
        Pointer p = pointers_[size_t(i)];
        if (p.released) continue;
        p.pressed = false;
        p.delta = {};
        p.heldTime += dt;
        pointers_[size_t(kept++)] = p; // preserves press order, so primary stays the oldest
    }
    count_ = kept;
    tapped_ = false;
    backPressed_ = false;
    threeFingerTap_ = false;
    keysPressed_.reset();
}

void InputBackend::handleEvent(const SDL_Event& e, const ScreenFit& fit) {
#if defined(NOVA_DEV_TOOLS)
    static const bool logInput = SDL_getenv("NOVA_LOG_INPUT") != nullptr; // device debugging aid
#else
    constexpr bool logInput = false;
#endif
    if (logInput && e.type >= SDL_EVENT_FINGER_DOWN && e.type <= SDL_EVENT_FINGER_CANCELED) {
        log::info("input: finger event 0x%x id=%llu at (%.3f, %.3f)", unsigned(e.type),
                  static_cast<unsigned long long>(e.tfinger.fingerID), double(e.tfinger.x), double(e.tfinger.y));
    } else if (logInput && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
        log::info("input: mouse button 0x%x which=%u at (%.1f, %.1f)", unsigned(e.type), unsigned(e.button.which),
                  double(e.button.x), double(e.button.y));
    }
    switch (e.type) {
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            Vec2 pos = pixelsToDesign({e.tfinger.x * fit.windowPx.x, e.tfinger.y * fit.windowPx.y}, fit);
            u64 id = e.tfinger.fingerID;
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                press(id, pos);
            } else if (e.type == SDL_EVENT_FINGER_MOTION) {
                move(id, pos, {e.tfinger.dx * fit.windowPx.x / fit.scale, e.tfinger.dy * fit.windowPx.y / fit.scale});
            } else {
                // A cancelled touch (system gesture, incoming call) ends the pointer but is never a tap.
                release(id, pos, e.type == SDL_EVENT_FINGER_UP);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                Vec2 pos = pixelsToDesign({e.button.x * fit.pixelDensity, e.button.y * fit.pixelDensity}, fit);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) press(kMousePointerId, pos);
                else release(kMousePointerId, pos, true);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (find(kMousePointerId)) {
                Vec2 pos = pixelsToDesign({e.motion.x * fit.pixelDensity, e.motion.y * fit.pixelDensity}, fit);
                Vec2 delta{e.motion.xrel * fit.pixelDensity / fit.scale, e.motion.yrel * fit.pixelDensity / fit.scale};
                move(kMousePointerId, pos, delta);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            bool down = e.type == SDL_EVENT_KEY_DOWN;
            if (down && (e.key.scancode == SDL_SCANCODE_AC_BACK || e.key.scancode == SDL_SCANCODE_ESCAPE)) {
                backPressed_ = true;
            }
            if (auto k = mapScancode(e.key.scancode)) setKey(*k, down);
            break;
        }
        default:
            break;
    }
}

Pointer* InputBackend::find(u64 id) {
    for (int i = 0; i < count_; ++i) {
        if (pointers_[size_t(i)].id == id && !pointers_[size_t(i)].released) return &pointers_[size_t(i)];
    }
    return nullptr;
}

void InputBackend::press(u64 id, Vec2 pos) {
    if (find(id) || count_ >= kMaxPointers) return;
    Pointer& p = pointers_[size_t(count_++)];
    p = Pointer{};
    p.id = id;
    p.pos = p.startPos = pos;
    p.pressed = p.held = true;

    int recent = 0;
    for (int i = 0; i < count_; ++i) {
        const Pointer& q = pointers_[size_t(i)];
        if (q.held && q.heldTime <= kMultiTapWindow) ++recent;
    }
    if (recent >= 3) threeFingerTap_ = true;
}

void InputBackend::move(u64 id, Vec2 pos, Vec2 delta) {
    if (Pointer* p = find(id)) {
        p->pos = pos;
        p->delta += delta;
    }
}

void InputBackend::release(u64 id, Vec2 pos, bool canTap) {
    Pointer* p = find(id);
    if (!p) return;
    p->pos = pos;
    p->held = false;
    p->released = true;
    p->cancelled = !canTap;
    if (canTap && p->heldTime < kTapMaxSeconds && (p->pos - p->startPos).length() < kTapMaxTravel) {
        tapped_ = true;
        tapId_ = id;
    }
}

void InputBackend::setKey(Key k, bool down) {
    if (down && !keysDown_[size_t(k)]) keysPressed_[size_t(k)] = true;
    keysDown_[size_t(k)] = down;
}

} // namespace nova
