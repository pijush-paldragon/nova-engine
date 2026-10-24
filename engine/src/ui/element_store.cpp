#include "ui/element_store.hpp"

namespace nova::ui {

ElementStore::ElementStore() {
    scratch_.reserve(64);
    addLayer();
}

UiHandle ElementStore::addLayer() {
    Element root;
    root.kind = Kind::Panel;
    root.spec.width = Size::fill();
    root.spec.height = Size::fill();
    root.layer = u16(layers_.size());
    UiHandle h = pool_.insert(std::move(root));
    layers_.push_back(h);
    ids_.emplace_back();
    layerDirty_.push_back(1);
    return h;
}

UiHandle ElementStore::add(UiHandle parent, Kind kind, const UiSpec& spec) {
    const Element* p = live(parent);
    if (!p) return {};
    const u16 layer = p->layer;
    Element e;
    e.kind = kind;
    e.spec = spec;
    e.padding = spec.padding;
    e.shown = spec.value; // progress bars start at their value
    e.parent = parent;
    e.layer = layer;
    UiHandle h = pool_.insert(std::move(e)); // may move storage: re-fetch pointers below
    Element& parentEl = *get(parent);
    Element& child = *get(h);
    child.prev = parentEl.lastChild;
    if (Element* last = get(parentEl.lastChild)) last->next = h;
    else parentEl.firstChild = h;
    parentEl.lastChild = h;
    indexId(h, child);
    markDirty(layer);
    return h;
}

void ElementStore::indexId(UiHandle h, const Element& e) {
    if (e.spec.id.empty()) return;
    ids_[e.layer].try_emplace(e.spec.id, h); // first one wins; the loader reports duplicates
}

void ElementStore::remove(UiHandle h) {
    Element* e = live(h);
    if (!e) return;
    for (UiHandle root : layers_) {
        if (root == h) return; // layer roots live as long as their layer
    }
    // Walk the subtree: mark removed and drop ids.
    scratch_.clear();
    scratch_.push_back(h);
    while (!scratch_.empty()) {
        UiHandle cur = scratch_.back();
        scratch_.pop_back();
        Element* el = get(cur);
        el->removed = true;
        if (!el->spec.id.empty()) {
            IdIndex& ids = ids_[el->layer];
            if (auto it = ids.find(std::string_view(el->spec.id)); it != ids.end() && it->second == cur) ids.erase(it);
        }
        for (UiHandle c = el->firstChild; c.valid(); c = get(c)->next) scratch_.push_back(c);
    }
    pending_.push_back(h);
    markDirty(get(h)->layer);
}

void ElementStore::unlink(UiHandle h) {
    Element& e = *get(h);
    if (Element* prev = get(e.prev)) prev->next = e.next;
    else if (Element* parent = get(e.parent)) parent->firstChild = e.next;
    if (Element* next = get(e.next)) next->prev = e.prev;
    else if (Element* parent = get(e.parent)) parent->lastChild = e.prev;
}

void ElementStore::flush() {
    for (UiHandle h : pending_) {
        if (!get(h)) continue; // already freed with an ancestor
        unlink(h);
        scratch_.clear();
        scratch_.push_back(h);
        while (!scratch_.empty()) {
            UiHandle cur = scratch_.back();
            scratch_.pop_back();
            for (UiHandle c = get(cur)->firstChild; c.valid(); c = get(c)->next) scratch_.push_back(c);
            pool_.remove(cur);
        }
    }
    pending_.clear();
}

Element* ElementStore::get(UiHandle h) { return pool_.get(h); }
const Element* ElementStore::get(UiHandle h) const { return pool_.get(h); }

Element* ElementStore::live(UiHandle h) {
    Element* e = pool_.get(h);
    return e && !e->removed ? e : nullptr;
}
const Element* ElementStore::live(UiHandle h) const { return const_cast<ElementStore*>(this)->live(h); }

UiHandle ElementStore::find(std::string_view id) const {
    for (size_t i = ids_.size(); i-- > 0;) {
        if (auto it = ids_[i].find(id); it != ids_[i].end()) return it->second;
    }
    return {};
}

} // namespace nova::ui
