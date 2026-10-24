#pragma once

#include "ui/draw.hpp"
#include "ui/element_store.hpp"
#include "ui/loader.hpp"

#include <nova/assets/asset_cache.hpp>

#include <nova/ui/canvas.hpp>

#include <nova/ecs/tween.hpp>

#include <array>
#include <optional>
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

// One running animation track.
struct Anim {
    enum Property : u8 { Scale, Opacity, Offset };
    UiHandle h;
    u8 property = Scale;
    Vec2 from{}, to{};
    f32 duration = 0, elapsed = 0, delay = 0;
    Ease ease = Ease::OutQuad;
    bool yoyo = false, loop = false, backward = false;
    bool active = false;
    u64 started = 0; // for reusing the oldest slot when all are busy
    std::function<void()> onDone;
};
inline constexpr size_t kAnimSlots = 64;

// Values set through references (and changed by the player), re-applied after a reload.
struct Remembered {
    std::optional<std::string> text, sprite, style;
    std::optional<f32> value;
    std::optional<bool> on, visible, enabled;
};

// Per-layer popup state (layer 0, the screen, uses none of it).
struct LayerInfo {
    enum class Phase : u8 { Open, Entering, Exiting };
    PopupOptions options;
    Transition enter = Transition::None, exit = Transition::None; // resolved from options / file
    u32 token = 0;                                                 // stable id (0: the screen)
    UiHandle backdrop, content;
    Vec2 rootOffset{};                                             // the root's own values, which
    f32 rootOpacity = 1, rootScale = 1;                            // transitions build on
    Phase phase = Phase::Open;
    f32 t = 0;
    std::function<void()> onClose;
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
    std::vector<std::vector<std::string>> layerFiles{{}}; // files each layer was loaded from (reload)
    FileReader reader;                              // where load() reads files (assets by default)
    std::vector<std::vector<i64>> layerStamps;      // modification times of layerFiles (live reload)
    f32 reloadTimer = 0;
    std::string devError;                           // live reload: the edit that failed, until fixed
    std::vector<LayerInfo> layerInfo{LayerInfo{}};  // per layer; index 0 is the screen

    // Popups (canvas.cpp).
    u16 createPopupLayer(const PopupOptions& options, Transition enter, Transition exit);
    // Records a layer's root element and the values its transitions start from.
    void setLayerContent(u16 layer, UiHandle content);
    void closeLayer(u16 layer);
    void finishLayer(u16 layer); // removes it and runs onClose
    u16 layerOfToken(u32 token) const; // 0 when that popup is gone
    u32 nextToken = 1;
    // Sizes scratch space and lays out after a structural change, so the frames that follow
    // don't allocate (called inside the change's LoadingScope).
    void prepareNow();
    std::unordered_set<std::string> reported;      // "kind:id" already logged as missing

    using IdBindings = std::unordered_map<std::string, BindingPtr, IdHash, std::equal_to<>>;
    IdBindings globalBindings;                         // Canvas::onClick(id): any layer
    std::unordered_map<u32, IdBindings> scopedBindings; // through references: per layer token
    std::unordered_map<u64, BindingPtr> handleBindings;
    std::array<Touch, 10> touches{};
    std::vector<UiEvent> events;
    std::vector<UiHandle> hitScratch, hitStack;
    std::array<Anim, kAnimSlots> anims{};
    u64 animCounter = 0;
    std::unordered_map<std::string, Remembered, IdHash, std::equal_to<>> remembered;

    // Replaces a layer's content with parsed nodes.
    void buildLayer(u16 layer, const std::vector<NodeSpec>& nodes, std::string source, const LoadResult& result);

    // The remembered values for an element's id (null for elements without one).
    Remembered* rememberFor(const Element& e);

    // Replaces the binding of `h` (by its id when it has one) with a copy changed by `edit`.
    void bind(UiHandle h, const std::function<void(Binding&)>& edit);
    void bindId(std::string_view id, const std::function<void(Binding&)>& edit);
    BindingPtr bindingOf(UiHandle h) const;       // the element's own (layer) binding, else canvas-wide
    BindingPtr globalBindingOf(UiHandle h) const; // canvas-wide only

    // Dev builds log a missing id or wrong kind once per (kind, id).
    void reportMissing(std::string_view id, const char* what, u16 layer);
};

} // namespace nova::ui
