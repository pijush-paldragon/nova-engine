#pragma once

#include <nova/core/result.hpp>
#include <nova/core/types.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace nova {

// Persistent key-value store in the platform's app-data folder.
// Keys match [A-Za-z0-9_.-]+. Writes are atomic (temp file + rename).
class SaveStore {
public:
    explicit SaveStore(std::string directory);

    // Returns false when the file is missing or corrupt; the store is then empty.
    bool load();
    // Writes only when something changed since the last load/flush.
    Result<void> flush();
    bool dirty() const { return dirty_; }

    void setInt(std::string_view key, i64 value);
    void setFloat(std::string_view key, f64 value);
    void setBool(std::string_view key, bool value);
    void setString(std::string_view key, std::string_view value);

    // Each getter returns `fallback` when the key is missing or holds another type.
    i64 getInt(std::string_view key, i64 fallback = 0) const;
    f64 getFloat(std::string_view key, f64 fallback = 0) const;
    bool getBool(std::string_view key, bool fallback = false) const;
    std::string getString(std::string_view key, std::string_view fallback = {}) const;

private:
    using Value = std::variant<i64, f64, bool, std::string>;

    void set(std::string_view key, Value value);
    template <class T>
    const T* find(std::string_view key) const;

    std::string path_;
    std::map<std::string, Value, std::less<>> values_;
    bool dirty_ = false;
};

} // namespace nova
