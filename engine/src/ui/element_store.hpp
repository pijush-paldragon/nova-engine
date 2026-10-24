#pragma once

#include <nova/audio/audio.hpp>
#include <nova/core/handle.hpp>
#include <nova/render/types.hpp>
#include <nova/text/text.hpp>
#include <nova/ui/types.hpp>

#include <memory>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nova {
class TextRun;
}

namespace nova::ui {

struct TextRunDeleter {
    void operator()(TextRun* run) const; // draw.cpp
};

// One UI element. Tree links are intrusive handles, so the tree never allocates per node.
struct Element {
    Kind kind = Kind::Panel;
    UiSpec spec;
    UiHandle parent, firstChild, lastChild, next, prev;
    u16 layer = 0;
    bool removed = false; // queued for removal: no longer drawn, hit or found

    Rect box{};           // laid-out box, design units (before offset/scale, which are visual)
    Vec2 measured{};      // content size for fit axes, padding included
    Insets padding;       // effective padding: the spec's, else the style's

    // Visual state and caches resolved from the style (draw.cpp).
    f32 scale = 1;        // animated scale; the style's scale applies on top at draw
    bool pressed = false;
    f32 pressAmount = 0;      // eases toward pressed (0..1) over 0.08 s
    f32 shown = 0;            // ProgressBar: displayed value
    f32 shownFrom = 0, shownT = 1; // ProgressBar animation start value and progress
    bool resolveDirty = true; // style, font or sprite must be looked up again
    u32 themeGen = 0;
    FontHandle font;
    SpriteRegion region;      // Image sprite, or Button icon
    SoundHandle clickSound;   // from the style
    std::unique_ptr<TextRun, TextRunDeleter> textRun;
};

// Screen metrics layout depends on.
struct LayoutEnv {
    Rect root;      // every layer's root box: the visible screen, design units
    Rect safeArea;  // design units
    f32 pixelScale = 1;
    bool operator==(const LayoutEnv&) const = default;
};

// Transparent hashing, so lookups by string_view never build a std::string.
struct IdHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using IdIndex = std::unordered_map<std::string, UiHandle, IdHash, std::equal_to<>>;

class ElementStore {
public:
    ElementStore();

    // A new layer's root (a full-screen stack panel); layer 0 exists from construction.
    UiHandle addLayer();
    UiHandle layerRoot(u16 layer) const { return layers_[layer]; }
    u16 layerCount() const { return u16(layers_.size()); }

    // Appends a child. An invalid or removed parent gives an invalid handle.
    UiHandle add(UiHandle parent, Kind kind, const UiSpec& spec);
    // Marks the subtree removed and drops its ids; storage is freed by flush().
    void remove(UiHandle h);
    void flush();

    Element* get(UiHandle h);
    const Element* get(UiHandle h) const;
    // A live (not removed) element, or null.
    Element* live(UiHandle h);
    const Element* live(UiHandle h) const;

    // Layout must re-run for this layer (size-affecting change, add/remove).
    void markDirty(u16 layer) { layerDirty_[layer] = 1; }
    bool dirty(u16 layer) const { return layerDirty_[layer] != 0; }
    void clearDirty(u16 layer) { layerDirty_[layer] = 0; }

    // Screen metrics of the last layout pass; a change re-lays out every layer.
    const LayoutEnv& lastEnv() const { return lastEnv_; }
    void setLastEnv(const LayoutEnv& env) { lastEnv_ = env; }

    // Searches layers from the top down.
    UiHandle find(std::string_view id) const;
    u32 size() const { return pool_.size(); }

    template <class F>
    void forEach(F&& fn) {
        pool_.forEach([&](UiHandle h, Element& e) {
            if (!e.removed) fn(h, e);
        });
    }

    template <class F>
    void forEachChild(UiHandle parent, F&& fn) const {
        const Element* p = get(parent);
        for (UiHandle c = p ? p->firstChild : UiHandle{}; c.valid();) {
            const Element* e = get(c);
            UiHandle next = e->next;
            fn(c, *e);
            c = next;
        }
    }

private:
    void indexId(UiHandle h, const Element& e);
    void unlink(UiHandle h);

    HandlePool<Element, ElementTag> pool_;
    std::vector<UiHandle> layers_;
    std::vector<IdIndex> ids_;      // per layer
    std::vector<u8> layerDirty_;    // per layer
    std::vector<UiHandle> pending_; // removed subtree roots, freed at flush
    std::vector<UiHandle> scratch_;
    LayoutEnv lastEnv_{};
};

} // namespace nova::ui
