#pragma once

#include <nova/core/handle.hpp>
#include <nova/core/types.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nova {

using ScopeId = u32;
inline constexpr ScopeId kGlobalScope = 0;

// Path-keyed cache with scope-based lifetime. Each acquire tags the entry with a scope
// (a scene, or kGlobalScope). releaseScope() drops a scope's references; entries no scope
// references stay cached until purgeUnreferenced() unloads them. Failed loads are cached
// too, so a missing file is reported once rather than retried every frame.
template <class T, class Tag>
class AssetCache {
public:
    using Loader = std::function<std::optional<T>(std::string_view key)>;
    using Unloader = std::function<void(T&)>;

    AssetCache(Loader loader, Unloader unloader)
        : loader_(std::move(loader)), unloader_(std::move(unloader)) {}
    AssetCache(const AssetCache&) = delete;
    AssetCache& operator=(const AssetCache&) = delete;
    ~AssetCache() { clear(); }

    Handle<Tag> acquire(std::string_view key, ScopeId scope) {
        auto it = index_.find(std::string(key));
        if (it != index_.end()) {
            Entry* entry = pool_.get(it->second);
            if (entry->failed) return {};
            if (std::find(entry->scopes.begin(), entry->scopes.end(), scope) == entry->scopes.end()) {
                entry->scopes.push_back(scope);
            }
            return it->second;
        }
        std::optional<T> value = loader_(key);
        Entry entry{std::move(value), {}, false};
        entry.failed = !entry.value.has_value();
        if (!entry.failed) entry.scopes.push_back(scope);
        Handle<Tag> handle = pool_.insert(std::move(entry));
        index_.emplace(std::string(key), handle);
        return pool_.get(handle)->failed ? Handle<Tag>{} : handle;
    }

    T* get(Handle<Tag> handle) {
        Entry* entry = pool_.get(handle);
        return (entry && entry->value) ? &*entry->value : nullptr;
    }

    void releaseScope(ScopeId scope) {
        pool_.forEach([scope](Handle<Tag>, Entry& e) {
            e.scopes.erase(std::remove(e.scopes.begin(), e.scopes.end(), scope), e.scopes.end());
        });
    }

    void purgeUnreferenced() {
        for (auto it = index_.begin(); it != index_.end();) {
            Entry* entry = pool_.get(it->second);
            if (!entry->failed && entry->scopes.empty()) {
                unloader_(*entry->value);
                pool_.remove(it->second);
                it = index_.erase(it);
            } else {
                ++it;
            }
        }
    }

    u32 loadedCount() const {
        u32 n = 0;
        const_cast<AssetCache*>(this)->pool_.forEach([&n](Handle<Tag>, Entry& e) { n += e.value ? 1 : 0; });
        return n;
    }

    void clear() {
        pool_.forEach([this](Handle<Tag>, Entry& e) {
            if (e.value) unloader_(*e.value);
        });
        pool_.clear();
        index_.clear();
    }

    // Calls fn(T&) for every loaded entry.
    template <class F>
    void forEachLoaded(F&& fn) {
        pool_.forEach([&fn](Handle<Tag>, Entry& e) {
            if (e.value) fn(*e.value);
        });
    }

private:
    struct Entry {
        std::optional<T> value;
        std::vector<ScopeId> scopes;
        bool failed;
    };

    Loader loader_;
    Unloader unloader_;
    HandlePool<Entry, Tag> pool_;
    std::unordered_map<std::string, Handle<Tag>> index_;
};

} // namespace nova
