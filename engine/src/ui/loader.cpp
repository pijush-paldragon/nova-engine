#include "ui/loader.hpp"

#include "ui/canvas_impl.hpp"
#include "ui/draw.hpp"
#include "ui/reload.hpp"

#include <nova/core/alloc_stats.hpp>
#include <nova/core/log.hpp>
#include <platform/asset_io.hpp>
#include <scene/services.hpp>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <yyjson.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_set>

namespace nova::ui {
namespace {

struct DocDeleter {
    void operator()(yyjson_doc* d) const { yyjson_doc_free(d); }
};
using Doc = std::unique_ptr<yyjson_doc, DocDeleter>;

template <class E>
struct EnumName {
    const char* name;
    E value;
};

constexpr EnumName<Kind> kKinds[] = {{"panel", Kind::Panel},   {"image", Kind::Image},   {"label", Kind::Label},
                                     {"button", Kind::Button}, {"toggle", Kind::Toggle}, {"slider", Kind::Slider},
                                     {"progress", Kind::ProgressBar}};
constexpr EnumName<Anchor> kAnchors[] = {
    {"top-left", Anchor::TopLeft}, {"top", Anchor::Top},       {"top-right", Anchor::TopRight},
    {"left", Anchor::Left},        {"center", Anchor::Center}, {"right", Anchor::Right},
    {"bottom-left", Anchor::BottomLeft}, {"bottom", Anchor::Bottom}, {"bottom-right", Anchor::BottomRight}};
constexpr EnumName<LayoutMode> kLayouts[] = {{"stack", LayoutMode::Stack}, {"row", LayoutMode::Row}, {"column", LayoutMode::Column}};
constexpr EnumName<Justify> kJustify[] = {{"start", Justify::Start}, {"center", Justify::Center}, {"end", Justify::End},
                                          {"space-between", Justify::SpaceBetween}};
constexpr EnumName<Align> kAlign[] = {{"start", Align::Start}, {"center", Align::Center}, {"end", Align::End},
                                      {"stretch", Align::Stretch}};
constexpr EnumName<TextAlignH> kTextAlign[] = {{"left", TextAlignH::Left}, {"center", TextAlignH::Center}, {"right", TextAlignH::Right}};
constexpr EnumName<ImageFit> kFit[] = {{"stretch", ImageFit::Stretch}, {"contain", ImageFit::Contain}, {"cover", ImageFit::Cover}};
constexpr EnumName<ToggleLook> kLooks[] = {{"switch", ToggleLook::Switch}, {"checkbox", ToggleLook::Checkbox}};
constexpr EnumName<FillDirection> kDirections[] = {{"right", FillDirection::Right}, {"left", FillDirection::Left},
                                                   {"up", FillDirection::Up}, {"down", FillDirection::Down}};
constexpr EnumName<Transition> kTransitions[] = {
    {"none", Transition::None},          {"fade", Transition::Fade},           {"pop", Transition::Pop},
    {"slide-up", Transition::SlideUp},   {"slide-down", Transition::SlideDown}, {"slide-left", Transition::SlideLeft},
    {"slide-right", Transition::SlideRight}};

constexpr const char* kElementKeys[] = {
    "type",    "id",      "style",   "text",       "sprite",   "icon",      "width",     "height",    "minWidth",
    "maxWidth", "minHeight", "maxHeight", "anchor", "offset", "layout",   "gap",       "padding",   "justify",
    "align",   "safeArea", "visible", "enabled",   "opacity",  "blocksInput", "textAlign", "fit",     "slice",
    "tint",    "look",    "on",      "min",        "max",      "step",      "value",     "direction", "animate",
    "children", "include", "enter",  "exit"};

bool isKnown(std::string_view key, const char* const* begin, const char* const* end) {
    return std::any_of(begin, end, [&](const char* k) { return key == k; });
}

std::string_view str(yyjson_val* v) { return {yyjson_get_str(v), yyjson_get_len(v)}; }

bool parseHex(std::string_view s, Color& out) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    u32 v = 0;
    for (char c : s.substr(1)) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= u32(c - '0');
        else if (c >= 'a' && c <= 'f') v |= u32(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= u32(c - 'A' + 10);
        else return false;
    }
    out = s.size() == 7 ? Color::fromHex((v << 8) | 0xFF) : Color::fromHex(v);
    return true;
}

// Collects messages; each value reader reports a wrong type and leaves the target unchanged.
struct Reader {
    std::vector<std::string>& errors;
    std::string where; // "<file>: <path>"

    void error(const std::string& message) { errors.push_back(where + ": " + message); }
    void wrong(std::string_view key, const char* expected) {
        error("\"" + std::string(key) + "\" must be " + expected);
    }

    void number(yyjson_val* v, std::string_view key, f32& out) {
        if (yyjson_is_num(v)) out = f32(yyjson_get_num(v));
        else wrong(key, "a number");
    }
    void boolean(yyjson_val* v, std::string_view key, bool& out) {
        if (yyjson_is_bool(v)) out = yyjson_get_bool(v);
        else wrong(key, "true or false");
    }
    void string(yyjson_val* v, std::string_view key, std::string& out) {
        if (yyjson_is_str(v)) out = str(v);
        else wrong(key, "a string");
    }
    void color(yyjson_val* v, std::string_view key, Color& out) {
        if (!yyjson_is_str(v) || !parseHex(str(v), out)) wrong(key, "a colour like \"#rrggbb\" or \"#rrggbbaa\"");
    }
    void insets(yyjson_val* v, std::string_view key, Insets& out) {
        if (yyjson_is_num(v)) {
            out = Insets::all(f32(yyjson_get_num(v)));
        } else if (yyjson_is_arr(v) && yyjson_arr_size(v) == 4 && yyjson_is_num(yyjson_arr_get(v, 0)) &&
                   yyjson_is_num(yyjson_arr_get(v, 1)) && yyjson_is_num(yyjson_arr_get(v, 2)) &&
                   yyjson_is_num(yyjson_arr_get(v, 3))) {
            out = {f32(yyjson_get_num(yyjson_arr_get(v, 0))), f32(yyjson_get_num(yyjson_arr_get(v, 1))),
                   f32(yyjson_get_num(yyjson_arr_get(v, 2))), f32(yyjson_get_num(yyjson_arr_get(v, 3)))};
        } else {
            wrong(key, "a number or [top, right, bottom, left]");
        }
    }
    void vec2(yyjson_val* v, std::string_view key, Vec2& out) {
        if (yyjson_is_arr(v) && yyjson_arr_size(v) == 2 && yyjson_is_num(yyjson_arr_get(v, 0)) &&
            yyjson_is_num(yyjson_arr_get(v, 1))) {
            out = {f32(yyjson_get_num(yyjson_arr_get(v, 0))), f32(yyjson_get_num(yyjson_arr_get(v, 1)))};
        } else {
            wrong(key, "[x, y]");
        }
    }
    void size(yyjson_val* v, std::string_view key, Size& out) {
        if (yyjson_is_num(v)) {
            out = Size::px(f32(yyjson_get_num(v)));
            return;
        }
        if (yyjson_is_str(v)) {
            std::string_view s = str(v);
            if (s == "fit") return void(out = Size::fit());
            if (s == "fill") return void(out = Size::fill());
            if (s.rfind("fill:", 0) == 0) {
                char* end = nullptr;
                std::string weight(s.substr(5));
                f32 w = f32(SDL_strtod(weight.c_str(), &end));
                if (end && *end == '\0' && w > 0) return void(out = Size::fill(w));
            }
        }
        wrong(key, "a number, \"fill\", \"fill:N\" or \"fit\"");
    }
    template <class E, size_t N>
    void choice(yyjson_val* v, std::string_view key, const EnumName<E> (&names)[N], E& out) {
        if (yyjson_is_str(v)) {
            for (const auto& n : names) {
                if (str(v) == n.name) return void(out = n.value);
            }
        }
        std::string expected = "one of";
        for (size_t i = 0; i < N; ++i) expected += std::string(i ? ", " : " ") + names[i].name;
        wrong(key, expected.c_str());
    }
};

const char* kindLabel(Kind k) { return kindName(k); }

struct LayoutParser {
    const FileReader& read;
    const Theme* theme;
    std::vector<NodeSpec>& out;
    LoadResult& result;
    std::vector<std::string> openFiles; // include stack, for cycle detection
    std::vector<Doc> docs;              // kept alive while their values are referenced

    bool fatal(const std::string& message) {
        if (result.firstError.empty()) result.firstError = message;
        result.ok = false;
        return false;
    }

    // Reads and parses a file; null (with a fatal error) when unreadable.
    yyjson_val* open(const std::string& path) {
        std::optional<std::string> text = read(path);
        if (!text) {
            fatal(path + ": file not found");
            return nullptr;
        }
        result.files.push_back(path);
        yyjson_read_err err{};
        const yyjson_read_flag flags = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;
        yyjson_doc* doc = yyjson_read_opts(text->data(), text->size(), flags, nullptr, &err);
        if (!doc) {
            size_t line = 1, col = 1;
            for (size_t i = 0; i < err.pos && i < text->size(); ++i) {
                if ((*text)[i] == '\n') {
                    ++line;
                    col = 1;
                } else {
                    ++col;
                }
            }
            fatal(path + ":" + std::to_string(line) + ":" + std::to_string(col) + ": " + err.msg);
            return nullptr;
        }
        docs.emplace_back(doc);
        return yyjson_doc_get_root(doc);
    }

    // Applies every key of `obj` (except include/children/type) onto `node`.
    void applyKeys(yyjson_val* obj, NodeSpec& node, Reader& r) {
        UiSpec& s = node.spec;
        size_t idx, max;
        yyjson_val *k, *v;
        yyjson_obj_foreach(obj, idx, max, k, v) {
            const std::string_view key = str(k);
            if (key == "type" || key == "include" || key == "children") continue;
            if (key == "enter" || key == "exit") {
                if (node.parent == -1 && out.size() == 1) r.choice(v, key, kTransitions, key == "enter" ? result.enter : result.exit);
                continue;
            }
            if (!isKnown(key, std::begin(kElementKeys), std::end(kElementKeys))) {
                r.error("unknown key \"" + std::string(key) + "\"");
                continue;
            }
            if (key == "id") r.string(v, key, s.id);
            else if (key == "style") r.string(v, key, s.style);
            else if (key == "text") r.string(v, key, s.text);
            else if (key == "sprite") r.string(v, key, s.sprite);
            else if (key == "icon") r.string(v, key, s.icon);
            else if (key == "width") r.size(v, key, s.width);
            else if (key == "height") r.size(v, key, s.height);
            else if (key == "minWidth") r.number(v, key, s.minWidth);
            else if (key == "maxWidth") r.number(v, key, s.maxWidth);
            else if (key == "minHeight") r.number(v, key, s.minHeight);
            else if (key == "maxHeight") r.number(v, key, s.maxHeight);
            else if (key == "anchor") r.choice(v, key, kAnchors, s.anchor);
            else if (key == "offset") r.vec2(v, key, s.offset);
            else if (key == "layout") r.choice(v, key, kLayouts, s.layout);
            else if (key == "gap") r.number(v, key, s.gap);
            else if (key == "padding") r.insets(v, key, s.padding);
            else if (key == "justify") r.choice(v, key, kJustify, s.justify);
            else if (key == "align") r.choice(v, key, kAlign, s.align);
            else if (key == "safeArea") r.boolean(v, key, s.safeArea);
            else if (key == "visible") r.boolean(v, key, s.visible);
            else if (key == "enabled") r.boolean(v, key, s.enabled);
            else if (key == "opacity") r.number(v, key, s.opacity);
            else if (key == "blocksInput") r.boolean(v, key, s.blocksInput);
            else if (key == "textAlign") r.choice(v, key, kTextAlign, s.textAlign);
            else if (key == "fit") r.choice(v, key, kFit, s.imageFit);
            else if (key == "slice") r.insets(v, key, s.slice);
            else if (key == "tint") r.color(v, key, s.tint);
            else if (key == "look") r.choice(v, key, kLooks, s.look);
            else if (key == "on") r.boolean(v, key, s.on);
            else if (key == "min") r.number(v, key, s.min);
            else if (key == "max") r.number(v, key, s.max);
            else if (key == "step") r.number(v, key, s.step);
            else if (key == "value") r.number(v, key, s.value);
            else if (key == "direction") r.choice(v, key, kDirections, s.direction);
            else if (key == "animate") r.number(v, key, s.animate);
        }
        if (theme && !s.style.empty() && !theme->has(s.style)) r.error("unknown style \"" + s.style + "\"");
    }

    static std::string childPath(const std::string& parent, size_t i) {
        return (parent.empty() ? "" : parent + ".") + "children[" + std::to_string(i) + "]";
    }

    static std::string describe(const std::string& path, Kind kind, bool known, const std::string& id) {
        std::string d = path.empty() ? "root" : path;
        if (!known) return d;
        d += std::string(" (") + kindLabel(kind);
        if (!id.empty()) d += " \"" + id + "\"";
        return d + ")";
    }

    void children(yyjson_val* obj, i32 parent, const std::string& file, const std::string& path) {
        yyjson_val* kids = yyjson_obj_get(obj, "children");
        if (!kids) return;
        if (!yyjson_is_arr(kids)) {
            result.errors.push_back(file + ": " + describe(path, out[size_t(parent)].kind, true, out[size_t(parent)].spec.id) +
                                    ": \"children\" must be an array");
            return;
        }
        size_t i, max;
        yyjson_val* child;
        yyjson_arr_foreach(kids, i, max, child) {
            if (!result.ok && !result.firstError.empty()) return;
            node(child, parent, file, childPath(path, i));
        }
    }

    // Parses one element (and its subtree) under `parent`. Returns its index, or -1.
    i32 node(yyjson_val* v, i32 parent, const std::string& file, const std::string& path) {
        if (!yyjson_is_obj(v)) {
            result.errors.push_back(file + ": " + (path.empty() ? std::string("root") : path) + " must be an object");
            return -1;
        }
        if (yyjson_val* inc = yyjson_obj_get(v, "include")) {
            if (!yyjson_is_str(inc)) {
                result.errors.push_back(file + ": " + (path.empty() ? std::string("root") : path) + ": \"include\" must be a string");
                return -1;
            }
            const std::string target(str(inc));
            if (std::find(openFiles.begin(), openFiles.end(), target) != openFiles.end() ||
                openFiles.size() >= 8) {
                std::string chain;
                for (const std::string& f : openFiles) chain += f + " -> ";
                fatal(openFiles.front() + ": include cycle: " + chain + target);
                return -1;
            }
            yyjson_val* root = open(target);
            if (!root) return -1;
            openFiles.push_back(target);
            const i32 index = node(root, parent, target, path);
            openFiles.pop_back();
            if (index < 0) return -1;
            // The include element's own keys override the included root.
            NodeSpec& n = out[size_t(index)];
            Reader r{result.errors, file + ": " + describe(path, n.kind, true, n.spec.id)};
            applyKeys(v, n, r);
            children(v, index, file, path);
            return index;
        }

        NodeSpec n;
        n.parent = parent;
        bool known = true;
        yyjson_val* type = yyjson_obj_get(v, "type");
        std::string id;
        if (yyjson_val* idv = yyjson_obj_get(v, "id"); yyjson_is_str(idv)) id = str(idv);
        if (!yyjson_is_str(type)) {
            result.errors.push_back(file + ": " + (path.empty() ? std::string("root") : path) + ": missing \"type\"");
            known = false;
        } else {
            known = false;
            for (const auto& k : kKinds) {
                if (str(type) == k.name) {
                    n.kind = k.value;
                    known = true;
                }
            }
            if (!known) {
                result.errors.push_back(file + ": " + (path.empty() ? std::string("root") : path) + ": unknown type \"" +
                                        std::string(str(type)) + "\"");
                return -1; // an element of an unknown type is skipped with its children
            }
        }
        out.push_back(std::move(n));
        const i32 index = i32(out.size() - 1);
        Reader r{result.errors, file + ": " + describe(path, out.back().kind, known, id)};
        applyKeys(v, out.back(), r);
        children(v, index, file, path);
        return index;
    }
};

// ---- Theme ----------------------------------------------------------------------------------

constexpr const char* kStyleKeys[] = {"extends",  "background", "outline",   "tint",       "opacity", "scale",
                                      "padding",  "font",       "fontSize",  "textColor", "clickSound", "states"};
constexpr const char* kStateKeys[] = {"background", "tint", "textColor", "opacity", "scale"};

void readBackground(yyjson_val* v, Reader& r, Background& out) {
    if (yyjson_is_str(v) && str(v) == "none") {
        out = Background{};
        return;
    }
    if (!yyjson_is_obj(v)) {
        r.wrong("background", "an object or \"none\"");
        return;
    }
    Background b;
    if (yyjson_val* shape = yyjson_obj_get(v, "shape")) {
        b.kind = Background::Kind::Shape;
        if (!yyjson_is_str(shape) || (str(shape) != "rounded" && str(shape) != "rect")) r.wrong("shape", "\"rounded\" or \"rect\"");
    }
    if (yyjson_val* sprite = yyjson_obj_get(v, "sprite")) {
        b.kind = Background::Kind::Sprite;
        r.string(sprite, "sprite", b.sprite);
    }
    if (yyjson_val* radius = yyjson_obj_get(v, "radius")) {
        f32 rad = 0;
        r.number(radius, "radius", rad);
        b.radius = u8(std::clamp(rad, 0.f, 255.f));
    }
    if (yyjson_val* color = yyjson_obj_get(v, "color")) r.color(color, "color", b.color);
    if (yyjson_val* slice = yyjson_obj_get(v, "slice")) r.insets(slice, "slice", b.slice);
    if (b.kind == Background::Kind::None) r.error("background needs \"shape\" or \"sprite\"");
    out = b;
}

void readState(yyjson_val* v, Reader& r, StateStyle& out) {
    if (!yyjson_is_obj(v)) return r.wrong("state", "an object");
    size_t idx, max;
    yyjson_val *k, *val;
    yyjson_obj_foreach(v, idx, max, k, val) {
        const std::string_view key = str(k);
        if (!isKnown(key, std::begin(kStateKeys), std::end(kStateKeys))) {
            r.error("unknown key \"" + std::string(key) + "\"");
        } else if (key == "background") {
            Background b;
            readBackground(val, r, b);
            out.background = b;
        } else if (key == "tint" || key == "textColor") {
            Color c;
            r.color(val, key, c);
            (key == "tint" ? out.tint : out.textColor) = c;
        } else {
            f32 f = 1;
            r.number(val, key, f);
            (key == "opacity" ? out.opacity : out.scale) = f;
        }
    }
}

Style readStyle(yyjson_val* v, Reader& r) {
    Style s;
    size_t idx, max;
    yyjson_val *k, *val;
    yyjson_obj_foreach(v, idx, max, k, val) {
        const std::string_view key = str(k);
        if (!isKnown(key, std::begin(kStyleKeys), std::end(kStyleKeys))) r.error("unknown key \"" + std::string(key) + "\"");
        else if (key == "extends") r.string(val, key, s.extends);
        else if (key == "background") readBackground(val, r, s.background);
        else if (key == "outline") {
            if (!yyjson_is_obj(val)) {
                r.wrong(key, "an object like {\"color\": \"#000000\"}");
                continue;
            }
            Color c = Color::Black;
            if (yyjson_val* color = yyjson_obj_get(val, "color")) r.color(color, "color", c);
            s.outlineColor = c;
            if (yyjson_val* width = yyjson_obj_get(val, "width")) r.number(width, "width", s.outlineWidth);
        } else if (key == "tint") r.color(val, key, s.tint);
        else if (key == "opacity") r.number(val, key, s.opacity);
        else if (key == "scale") r.number(val, key, s.scale);
        else if (key == "padding") r.insets(val, key, s.padding);
        else if (key == "font") r.string(val, key, s.font);
        else if (key == "fontSize") r.number(val, key, s.fontSize);
        else if (key == "textColor") r.color(val, key, s.textColor);
        else if (key == "clickSound") r.string(val, key, s.clickSound);
        else if (key == "states") {
            if (!yyjson_is_obj(val)) {
                r.wrong(key, "an object");
                continue;
            }
            size_t j, jmax;
            yyjson_val *sk, *sv;
            yyjson_obj_foreach(val, j, jmax, sk, sv) {
                const std::string_view state = str(sk);
                if (state == "pressed") readState(sv, r, s.pressed);
                else if (state == "disabled") readState(sv, r, s.disabled);
                else if (state == "checked") readState(sv, r, s.checked);
                else r.error("unknown state \"" + std::string(state) + "\"");
            }
        }
    }
    return s;
}

void logAll(const LoadResult& r) {
#if defined(NOVA_DEV_TOOLS)
    if (!r.ok && !r.firstError.empty() && std::find(r.errors.begin(), r.errors.end(), r.firstError) == r.errors.end()) {
        log::error("%s", r.firstError.c_str());
    }
    for (const std::string& e : r.errors) log::error("%s", e.c_str());
#else
    (void)r;
#endif
}

} // namespace

FileReader assetFileReader() {
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

LoadResult loadLayout(std::string_view path, const FileReader& read, const Theme* theme, std::vector<NodeSpec>& out) {
    memory::LoadingScope loading;
    out.clear();
    LoadResult result;
    result.ok = true;
    LayoutParser parser{read, theme, out, result, {}, {}};
    const std::string file(path);
    yyjson_val* root = parser.open(file);
    if (!root) {
        out.clear();
        return result;
    }
    parser.openFiles.push_back(file);
    if (!yyjson_is_obj(root)) {
        parser.fatal(file + ": the root must be an object");
    } else {
        parser.node(root, -1, file, "");
    }
    if (!result.ok) {
        out.clear();
        return result;
    }
    if (out.empty()) parser.fatal(file + ": no elements");
    // Duplicate ids: the first wins at lookup, so later ones are unreachable by id.
    std::unordered_set<std::string_view> seen;
    for (const NodeSpec& n : out) {
        if (!n.spec.id.empty() && !seen.insert(n.spec.id).second) {
            result.errors.push_back(file + ": duplicate id \"" + n.spec.id + "\"");
        }
    }
    if (result.firstError.empty() && !result.errors.empty()) result.firstError = result.errors.front();
    return result;
}

LoadResult loadTheme(std::string_view path, const FileReader& read, Theme& theme) {
    memory::LoadingScope loading;
    LoadResult result;
    result.ok = true;
    std::vector<NodeSpec> unused;
    LayoutParser parser{read, nullptr, unused, result, {}, {}};
    const std::string file(path);
    yyjson_val* root = parser.open(file);
    if (!root) return result;
    yyjson_val* styles = yyjson_obj_get(root, "styles");
    if (!yyjson_is_obj(root) || !yyjson_is_obj(styles)) {
        parser.fatal(file + ": expected {\"styles\": {...}}");
        return result;
    }
    size_t idx, max;
    yyjson_val *k, *v;
    yyjson_obj_foreach(styles, idx, max, k, v) {
        const std::string name(str(k));
        Reader r{result.errors, file + ": styles." + name};
        if (!yyjson_is_obj(v)) {
            r.error("must be an object");
            continue;
        }
        theme.set(name, readStyle(v, r));
    }
    if (!result.errors.empty()) result.firstError = result.errors.front();
    return result;
}

void loadGameTheme(UiShared& shared, const FileReader& read) {
    if (!read("ui/theme.json")) return; // optional
    LoadResult r = loadTheme("ui/theme.json", read, shared.theme);
    recordThemeStamps(shared, r.files);
    logAll(r);
}

void uiSetFileReader(Canvas& canvas, FileReader read) { canvas.impl().reader = std::move(read); }

// ---- Canvas::load ----------------------------------------------------------------------------

void Canvas::load(std::string_view file) {
    memory::LoadingScope loading;
    Impl& impl = *impl_;
    if (!impl.reader) impl.reader = assetFileReader();
    const Theme* theme = impl.services && impl.services->ui ? &impl.services->ui->theme : nullptr;
    std::vector<NodeSpec> nodes;
    LoadResult result = loadLayout(file, impl.reader, theme, nodes);
    logAll(result);
    if (!result.ok) return;
    impl.buildLayer(0, nodes, std::string(file), result);
}

} // namespace nova::ui
