#pragma once

#include <nova/core/handle.hpp>
#include <nova/ui/types.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nova::ui {

// One UI element. Tree links are intrusive handles, so the tree never allocates per node.
struct Element {
    Kind kind = Kind::Panel;
    UiSpec spec;
    UiHandle parent, firstChild, lastChild, next, prev;
    u16 layer = 0;
    bool removed = false; // queued for removal: no longer drawn, hit or found

    Rect box{};           // laid-out box, design units
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

    // Searches layers from the top down.
    UiHandle find(std::string_view id) const;
    u32 size() const { return pool_.size(); }

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
    std::vector<UiHandle> pending_; // removed subtree roots, freed at flush
    std::vector<UiHandle> scratch_;
};

} // namespace nova::ui
