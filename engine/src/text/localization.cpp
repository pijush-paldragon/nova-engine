#include "text/localization_impl.hpp"

#include "text/plural.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/log.hpp>
#include <platform/asset_io.hpp>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <yyjson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace nova {
namespace {

std::string_view str(yyjson_val* v) { return {yyjson_get_str(v), yyjson_get_len(v)}; }

std::string_view primaryTag(std::string_view tag) {
    const size_t dash = tag.find_first_of("-_");
    return dash == std::string_view::npos ? tag : tag.substr(0, dash);
}

void logOnce(std::string_view kind, std::string_view what, const std::string& message) {
#if defined(NOVA_DEV_TOOLS)
    static std::unordered_set<std::string, LangHash, std::equal_to<>> seen;
    std::string key(kind);
    key += ':';
    key += what;
    if (seen.insert(std::move(key)).second) log::error("%s", message.c_str());
#else
    (void)kind;
    (void)what;
    (void)message;
#endif
}

int pluralIndex(std::string_view name) {
    static const char* names[] = {"zero", "one", "two", "few", "many", "other"};
    for (int i = 0; i < 6; ++i) {
        if (name == names[i]) return i;
    }
    return -1;
}

// A JSON object whose keys are all plural categories.
bool isPluralObject(yyjson_val* obj) {
    if (!yyjson_is_obj(obj) || yyjson_obj_size(obj) == 0) return false;
    size_t idx, max;
    yyjson_val *k, *v;
    yyjson_obj_foreach(obj, idx, max, k, v) {
        if (pluralIndex(str(k)) < 0) return false;
    }
    return true;
}

struct Writer {
    std::span<char> out;
    size_t n = 0;
    void put(std::string_view s) {
        if (out.empty()) return;
        const size_t room = out.size() - 1 - n;
        const size_t take = std::min(room, s.size());
        std::memcpy(out.data() + n, s.data(), take);
        n += take;
    }
    void put(char c) { put(std::string_view(&c, 1)); }
    size_t finish() {
        if (!out.empty()) out[n] = '\0';
        return n;
    }
};

void putArg(Writer& w, const FormatArg& a) {
    char num[32];
    if (const i64* i = std::get_if<i64>(&a.value)) {
        std::snprintf(num, sizeof num, "%lld", static_cast<long long>(*i));
        w.put(std::string_view(num));
    } else if (const f64* f = std::get_if<f64>(&a.value)) {
        std::snprintf(num, sizeof num, "%g", *f);
        w.put(std::string_view(num));
    } else {
        w.put(std::get<std::string_view>(a.value));
    }
}

// Fills {name} placeholders in one pass ({{ and }} are literal braces). Values are inserted as
// they are; a value's own braces are never substituted again.
size_t fill(std::span<char> out, std::string_view tmpl, std::initializer_list<FormatArg> args, const FormatArg* extra) {
    Writer w{out};
    for (size_t i = 0; i < tmpl.size(); ++i) {
        const char c = tmpl[i];
        if ((c == '{' || c == '}') && i + 1 < tmpl.size() && tmpl[i + 1] == c) {
            w.put(c);
            ++i;
            continue;
        }
        if (c == '{') {
            const size_t close = tmpl.find('}', i + 1);
            if (close != std::string_view::npos) {
                const std::string_view name = tmpl.substr(i + 1, close - i - 1);
                const FormatArg* found = nullptr;
                for (const FormatArg& a : args) {
                    if (a.name == name) found = &a;
                }
                if (!found && extra && extra->name == name) found = extra;
                if (found) {
                    putArg(w, *found);
                    i = close;
                    continue;
                }
            }
        }
        w.put(c);
    }
    return w.finish();
}

} // namespace

// ---- Impl ------------------------------------------------------------------------------------

void Localization::Impl::setReader(TextFileReader r) { reader = std::move(r); }

const Localization::Impl::Entry* Localization::Impl::find(std::string_view key) const {
    if (auto it = current.find(key); it != current.end()) return &it->second;
    if (auto it = english.find(key); it != english.end()) return &it->second;
    return nullptr;
}

bool Localization::Impl::loadFile(std::string_view lang, Table& out, LanguageInfo* langInfo) {
    memory::LoadingScope loading;
    if (!reader) reader = assetTextReader();
    const std::string path = "text/" + std::string(lang) + ".json";
    std::optional<std::string> text = reader(path);
    if (!text) return false;
    yyjson_read_err err{};
    yyjson_doc* doc = yyjson_read_opts(text->data(), text->size(),
                                       YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS, nullptr, &err);
    if (!doc) {
        logOnce("syntax", path, path + ": " + err.msg + " at byte " + std::to_string(err.pos));
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    out.clear();
    if (langInfo) {
        *langInfo = LanguageInfo{};
        langInfo->tag = std::string(lang);
        const std::string_view p = primaryTag(lang);
        langInfo->rtl = p == "ar" || p == "he" || p == "fa" || p == "ur";
        for (const char* key : {"fonts", "boldFonts"}) {
            yyjson_val* list = yyjson_obj_get(root, key);
            size_t i, max;
            yyjson_val* v;
            yyjson_arr_foreach(list, i, max, v) {
                if (yyjson_is_str(v)) (key[0] == 'f' ? langInfo->fonts : langInfo->boldFonts).emplace_back(str(v));
            }
        }
    }
    yyjson_val* strings = yyjson_obj_get(root, "strings");
    if (!yyjson_is_obj(strings)) {
        logOnce("shape", path, path + ": \"strings\" must be an object");
    } else {
        // Depth-first walk, building dotted keys.
        struct Item {
            yyjson_val* obj;
            std::string prefix;
        };
        std::vector<Item> stack{{strings, ""}};
        while (!stack.empty()) {
            Item item = std::move(stack.back());
            stack.pop_back();
            size_t idx, max;
            yyjson_val *k, *v;
            yyjson_obj_foreach(item.obj, idx, max, k, v) {
                const std::string key = item.prefix + std::string(str(k));
                if (yyjson_is_str(v)) {
                    out[key].text = std::string(str(v));
                } else if (isPluralObject(v)) {
                    Entry e;
                    e.plural = true;
                    size_t j, jmax;
                    yyjson_val *pk, *pv;
                    bool ok = true;
                    yyjson_obj_foreach(v, j, jmax, pk, pv) {
                        if (!yyjson_is_str(pv)) ok = false;
                        else {
                            const int i = pluralIndex(str(pk));
                            e.forms[size_t(i)] = std::string(str(pv));
                            e.formMask |= u8(1u << i);
                        }
                    }
                    if (!(e.formMask & (1u << u32(PluralCategory::Other))) || !ok) {
                        logOnce("plural", path + key, path + ": \"" + key + "\" plural needs text for \"other\"");
                    } else {
                        out[key] = std::move(e);
                    }
                } else if (yyjson_is_obj(v)) {
                    stack.push_back({v, key + "."});
                } else {
                    logOnce("type", path + key, path + ": \"" + key + "\" must be text, a plural object or a group");
                }
            }
        }
    }
    yyjson_doc_free(doc);
    return true;
}

TextFileReader assetTextReader() {
    return [](std::string_view path) -> std::optional<std::string> {
        SDL_IOStream* io = openAsset(path);
        if (!io) return std::nullopt;
        size_t size = 0;
        void* data = SDL_LoadFile_IO(io, &size, true);
        if (!data) return std::nullopt;
        std::string text(static_cast<const char*>(data), size);
        SDL_free(data);
        return text;
    };
}

// ---- Localization ----------------------------------------------------------------------------

Localization::Localization() : impl_(std::make_unique<Impl>()) {}
Localization::~Localization() = default;

std::string_view Localization::get(std::string_view key) const {
    const Impl::Entry* e = impl_->find(key);
    if (e && !e->plural) return e->text;
    if (e) return e->forms[size_t(PluralCategory::Other)];
    // "[key]", built (and logged) once per key, so a missing key looked up every frame is free.
    static std::unordered_map<std::string, std::string, LangHash, std::equal_to<>> missing;
    if (auto it = missing.find(key); it != missing.end()) return it->second;
    memory::LoadingScope once;
    logOnce("missing", key, "text: missing key \"" + std::string(key) + "\"");
    return missing.emplace(std::string(key), "[" + std::string(key) + "]").first->second;
}

size_t Localization::format(std::span<char> out, std::string_view key, std::initializer_list<FormatArg> args) const {
    return fill(out, get(key), args, nullptr);
}

size_t Localization::plural(std::span<char> out, std::string_view key, i64 count,
                            std::initializer_list<FormatArg> args) const {
    const FormatArg countArg{"count", count};
    auto pick = [&](const Impl::Table& table, std::string_view lang) -> const std::string* {
        auto it = table.find(key);
        if (it == table.end() || !it->second.plural) return nullptr;
        const PluralCategory c = pluralCategory(lang, count);
        const auto& e = it->second;
        return (e.formMask & (1u << u32(c))) ? &e.forms[size_t(c)] : &e.forms[size_t(PluralCategory::Other)];
    };
    if (const std::string* s = pick(impl_->current, impl_->info_.tag)) return fill(out, *s, args, &countArg);
    if (const std::string* s = pick(impl_->english, "en")) return fill(out, *s, args, &countArg);
    return fill(out, get(key), args, &countArg);
}

bool Localization::setLanguage(std::string_view lang) {
    memory::LoadingScope loading;
    Impl& impl = *impl_;
    Impl::Table table;
    LanguageInfo langInfo;
    if (!impl.loadFile(lang, table, &langInfo)) return false;
    if (impl.english.empty() && lang != "en") impl.loadFile("en", impl.english, nullptr);
    if (lang == "en") impl.english = table;
    impl.current = std::move(table);
    impl.info_ = std::move(langInfo);
    if (impl.available.empty()) {
        // text/languages.json lists the languages (SDL can't list APK asset folders).
        if (std::optional<std::string> list = impl.reader("text/languages.json")) {
            if (yyjson_doc* doc = yyjson_read(list->data(), list->size(), 0)) {
                size_t i, max;
                yyjson_val* v;
                yyjson_arr_foreach(yyjson_doc_get_root(doc), i, max, v) {
                    if (yyjson_is_str(v)) impl.available.emplace_back(str(v));
                }
                yyjson_doc_free(doc);
            }
        }
        if (impl.available.empty()) impl.available.emplace_back("en");
    }
    ++impl.generation;
    if (impl.onChange) impl.onChange(impl.info_.tag);
    return true;
}

std::string_view Localization::language() const { return impl_->info_.tag; }
std::span<const std::string> Localization::available() const { return impl_->available; }
bool Localization::rightToLeft() const { return impl_->info_.rtl; }
u32 Localization::generation() const { return impl_->generation; }

} // namespace nova
