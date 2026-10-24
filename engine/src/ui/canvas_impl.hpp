#pragma once

#include "ui/draw.hpp"
#include "ui/element_store.hpp"

#include <nova/assets/asset_cache.hpp>

#include <nova/ui/canvas.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nova {
struct Services;
}

namespace nova::ui {

const char* kindName(Kind kind);

// Callbacks bound to an element (by id, or by handle for elements without one). Replaced, never
// mutated, so a callback that rebinds itself keeps running safely.
struct Binding {
    std::function<void()> click;
    std::function<void(bool)> toggle;
    std::function<void(f32)> change;
    std::function<void(f32)> commit;
};
using BindingPtr = std::shared_ptr<const Binding>;

struct UiEvent {
    enum class Type : u8 { Click, Toggle, Change, Commit };
    Type type;
    UiHandle h;
    f32 value = 0;
};

// A pointer the UI owns, from press until release.
struct Touch {
    u64 pointer = 0;
    UiHandle owner;
    bool active = false;
    bool cancelled = false;
};

struct Canvas::Impl {
    ElementStore store;
    Services* services = nullptr;
    ScopeId scope = kGlobalScope;                   // the owning scene's asset scope
    DrawList drawList;                              // last frame's draw commands
    u32 themeGen = 0;                               // theme generation the elements were resolved with
    std::vector<std::string> layerSources{"code"}; // what built each layer, for messages
    std::unordered_set<std::string> reported;      // "kind:id" already logged as missing

    std::unordered_map<std::string, BindingPtr, IdHash, std::equal_to<>> idBindings;
    std::unordered_map<u64, BindingPtr> handleBindings;
    std::array<Touch, 10> touches{};
    std::vector<UiEvent> events;
    std::vector<UiHandle> hitScratch, hitStack;

    // Replaces the binding of `h` (by its id when it has one) with a copy changed by `edit`.
    void bind(UiHandle h, const std::function<void(Binding&)>& edit);
    void bindId(std::string_view id, const std::function<void(Binding&)>& edit);
    BindingPtr bindingOf(UiHandle h) const;

    // Dev builds log a missing id or wrong kind once per (kind, id).
    void reportMissing(std::string_view id, const char* what, u16 layer);
};

} // namespace nova::ui
