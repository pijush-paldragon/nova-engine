#include "core/parse.hpp"

#include <nova/core/assert.hpp>
#include <nova/core/log.hpp>
#include <nova/save/save_store.hpp>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <charconv>
#include <cstdio>

namespace nova {
namespace {

constexpr std::string_view kHeader = "NOVA_SAVE 1";

bool validKey(std::string_view key) {
    if (key.empty()) return false;
    for (char c : key) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}

std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool parseInt(std::string_view text, i64& out) {
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

} // namespace

SaveStore::SaveStore(std::string directory) : path_(std::move(directory) + "save.dat") {}

bool SaveStore::load() {
    values_.clear();
    dirty_ = false;

    size_t size = 0;
    void* data = SDL_LoadFile(path_.c_str(), &size);
    if (!data) return false;
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);

    std::string_view rest = text;
    auto nextLine = [&rest]() {
        size_t nl = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        return line;
    };

    if (nextLine() != kHeader) {
        log::error("Save file %s is corrupt (bad header); starting empty", path_.c_str());
        return false;
    }

    while (!rest.empty()) {
        std::string_view line = nextLine();
        if (line.empty()) continue;
        // "<type> <key> <value>"
        size_t s1 = line.find(' ');
        size_t s2 = s1 == std::string_view::npos ? s1 : line.find(' ', s1 + 1);
        if (s1 != 1 || s2 == std::string_view::npos) {
            log::warn("Save: skipping malformed line");
            continue;
        }
        std::string key(line.substr(2, s2 - 2));
        std::string_view value = line.substr(s2 + 1);
        bool ok = validKey(key);
        switch (line[0]) {
            case 'i': { i64 v; ok = ok && parseInt(value, v); if (ok) values_[key] = v; break; }
            case 'f': { f64 v; ok = ok && parseFloat(value, v); if (ok) values_[key] = v; break; }
            case 'b': ok = ok && (value == "0" || value == "1"); if (ok) values_[key] = value == "1"; break;
            case 's': if (ok) values_[key] = unescape(value); break;
            default: ok = false;
        }
        if (!ok) log::warn("Save: skipping malformed entry '%s'", key.c_str());
    }
    return true;
}

Result<void> SaveStore::flush() {
    if (!dirty_) return Result<void>::success();

    std::string text{kHeader};
    text += '\n';
    char number[64];
    for (const auto& [key, value] : values_) {
        if (auto* v = std::get_if<i64>(&value)) {
            std::snprintf(number, sizeof number, "%lld", static_cast<long long>(*v));
            text += "i " + key + ' ' + number;
        } else if (auto* f = std::get_if<f64>(&value)) {
            std::snprintf(number, sizeof number, "%.17g", *f);
            text += "f " + key + ' ' + number;
        } else if (auto* b = std::get_if<bool>(&value)) {
            text += "b " + key + (*b ? " 1" : " 0");
        } else {
            text += "s " + key + ' ' + escape(std::get<std::string>(value));
        }
        text += '\n';
    }

    std::string tmp = path_ + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), text.data(), text.size())) {
        return Result<void>::fail(std::string("Save write failed: ") + SDL_GetError());
    }
    if (!SDL_RenamePath(tmp.c_str(), path_.c_str())) {
        SDL_RemovePath(tmp.c_str());
        return Result<void>::fail(std::string("Save rename failed: ") + SDL_GetError());
    }
    dirty_ = false;
    return Result<void>::success();
}

void SaveStore::set(std::string_view key, Value value) {
    if (!NOVA_VERIFY(validKey(key), "save keys must match [A-Za-z0-9_.-]+")) return;
    auto it = values_.find(key);
    if (it != values_.end()) {
        if (it->second == value) return;
        it->second = std::move(value);
    } else {
        values_.emplace(std::string(key), std::move(value));
    }
    dirty_ = true;
}

void SaveStore::setInt(std::string_view key, i64 value) { set(key, value); }
void SaveStore::setFloat(std::string_view key, f64 value) { set(key, value); }
void SaveStore::setBool(std::string_view key, bool value) { set(key, value); }
void SaveStore::setString(std::string_view key, std::string_view value) { set(key, std::string(value)); }

template <class T>
const T* SaveStore::find(std::string_view key) const {
    auto it = values_.find(key);
    return it == values_.end() ? nullptr : std::get_if<T>(&it->second);
}

i64 SaveStore::getInt(std::string_view key, i64 fallback) const {
    auto* v = find<i64>(key);
    return v ? *v : fallback;
}
f64 SaveStore::getFloat(std::string_view key, f64 fallback) const {
    auto* v = find<f64>(key);
    return v ? *v : fallback;
}
bool SaveStore::getBool(std::string_view key, bool fallback) const {
    auto* v = find<bool>(key);
    return v ? *v : fallback;
}
std::string SaveStore::getString(std::string_view key, std::string_view fallback) const {
    auto* v = find<std::string>(key);
    return v ? *v : std::string(fallback);
}

} // namespace nova
