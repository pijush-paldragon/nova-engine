#pragma once

#include <nova/core/types.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace nova {

// A generational reference. Generation 0 is never issued, so a default handle is invalid,
// and a handle whose slot has been reused no longer matches and resolves to null.
template <class Tag>
struct Handle {
    u32 index = 0;
    u32 generation = 0;

    bool valid() const { return generation != 0; }
    bool operator==(const Handle&) const = default;
};

// Owns values of T and hands out generational handles to them.
template <class T, class Tag = T>
class HandlePool {
public:
    using HandleType = Handle<Tag>;

    HandleType insert(T&& value) {
        u32 index;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else {
            index = u32(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.value.emplace(std::move(value));
        ++size_;
        return {index, slot.generation};
    }

    T* get(HandleType h) {
        if (!h.valid() || h.index >= slots_.size()) return nullptr;
        Slot& slot = slots_[h.index];
        return (slot.generation == h.generation && slot.value) ? &*slot.value : nullptr;
    }
    const T* get(HandleType h) const { return const_cast<HandlePool*>(this)->get(h); }

    bool remove(HandleType h) {
        if (!get(h)) return false;
        Slot& slot = slots_[h.index];
        slot.value.reset();
        slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
        freeList_.push_back(h.index);
        --size_;
        return true;
    }

    template <class F>
    void forEach(F&& fn) {
        for (u32 i = 0; i < slots_.size(); ++i) {
            if (slots_[i].value) fn(HandleType{i, slots_[i].generation}, *slots_[i].value);
        }
    }

    void clear() {
        for (u32 i = 0; i < slots_.size(); ++i) {
            if (slots_[i].value) remove(HandleType{i, slots_[i].generation});
        }
    }

    u32 size() const { return size_; }

#if defined(NOVA_TESTING)
    void debugSetGeneration(u32 index, u32 generation) { slots_[index].generation = generation; }
#endif

private:
    struct Slot {
        std::optional<T> value;
        u32 generation = 1;
    };

    std::vector<Slot> slots_;
    std::vector<u32> freeList_;
    u32 size_ = 0;
};

} // namespace nova
