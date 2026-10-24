#include "text/markup.hpp"

#include "core/parse.hpp"

#include <algorithm>

namespace nova {
namespace {

struct Token {
    enum class Type : u8 { Text, Open, Close, Icon };
    Type type = Type::Text;
    u32 begin = 0, end = 0; // bytes of src
    std::string_view name, value;
    f32 scale = 1;
    std::optional<Color> color;
    bool matched = false;
};

bool isStyleTag(std::string_view n) { return n == "b" || n == "i" || n == "color" || n == "size" || n == "style"; }

bool parseHexColor(std::string_view s, Color& out) {
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return false;
    u32 v = 0;
    for (char c : s.substr(1)) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= u32(c - '0');
        else if (c >= 'a' && c <= 'f') v |= u32(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= u32(c - 'A' + 10);
        else return false;
    }
    if (s.size() == 7) v = (v << 8) | 0xFF;
    out = {u8(v >> 24), u8(v >> 16), u8(v >> 8), u8(v)};
    return true;
}

bool isName(std::string_view s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

// Reads one tag's inside ("b", "/color", "icon=sprites/coin scale=1.5"). False if it isn't a
// valid tag; `why` then says what is wrong.
bool readTag(std::string_view tag, Token& t, std::string& why) {
    if (!tag.empty() && tag[0] == '/') {
        t.type = Token::Type::Close;
        t.name = tag.substr(1);
        if (isStyleTag(t.name)) return true;
        why = "unknown tag";
        return false;
    }
    const size_t eq = tag.find('=');
    t.name = tag.substr(0, std::min(eq, tag.find(' ')));
    std::string_view rest = eq == std::string_view::npos ? std::string_view{} : tag.substr(eq + 1);
    const size_t space = rest.find(' ');
    t.value = rest.substr(0, space);
    const std::string_view extra = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    if (t.name == "b" || t.name == "i") {
        t.type = Token::Type::Open;
        if (eq == std::string_view::npos && tag.size() == 1) return true;
        why = "takes no value";
        return false;
    }
    if (!extra.empty() && t.name != "icon") {
        why = "unexpected \"" + std::string(extra) + "\"";
        return false;
    }
    if (t.name == "color") {
        t.type = Token::Type::Open;
        Color c;
        if (!t.value.empty() && t.value[0] == '#') {
            if (parseHexColor(t.value, c)) {
                t.color = c;
                return true;
            }
        } else if (isName(t.value)) {
            return true;
        }
        why = "needs #rrggbb, #rrggbbaa or a colour name";
        return false;
    }
    if (t.name == "size") {
        t.type = Token::Type::Open;
        if (parseFloat(t.value, t.scale) && t.scale > 0) return true;
        why = "needs a positive scale";
        return false;
    }
    if (t.name == "style") {
        t.type = Token::Type::Open;
        if (isName(t.value)) return true;
        why = "needs a style name";
        return false;
    }
    if (t.name == "icon") {
        t.type = Token::Type::Icon;
        const size_t slash = t.value.find('/');
        if (slash == std::string_view::npos || slash == 0 || slash + 1 == t.value.size()) {
            why = "needs \"atlas/region\"";
            return false;
        }
        if (extra.empty()) return true;
        if (extra.substr(0, 6) == "scale=" && parseFloat(extra.substr(6), t.scale) && t.scale > 0) return true;
        why = "takes only scale=<number>";
        return false;
    }
    why = "unknown tag";
    return false;
}

bool sameStyle(const RunStyle& a, const RunStyle& b) {
    return a.bold == b.bold && a.italic == b.italic && a.sizeScale == b.sizeScale && a.color == b.color &&
           a.colorName == b.colorName && a.styleName == b.styleName;
}

} // namespace

bool parseMarkup(std::string_view src, std::string& storage, std::vector<MarkupRun>& out,
                 std::vector<std::string>* errors) {
    storage.clear();
    out.clear();
    if (src.find('[') == std::string_view::npos) return false;
    auto report = [&](std::string message) {
        if (errors) errors->push_back(std::move(message));
    };

    // 1. Tokens: text, and tags that read correctly (anything else stays text).
    std::vector<Token> tokens;
    u32 textStart = 0;
    auto flushText = [&](u32 end) {
        if (end > textStart) tokens.push_back({Token::Type::Text, textStart, end, {}, {}, 1, {}, false});
    };
    for (size_t i = 0; i < src.size();) {
        if (src[i] != '[') {
            ++i;
            continue;
        }
        if (i + 1 < src.size() && src[i + 1] == '[') {
            i += 2;
            continue;
        }
        const size_t close = src.find(']', i + 1);
        if (close == std::string_view::npos) {
            report("markup: \"[\" without \"]\" in \"" + std::string(src) + "\"");
            break;
        }
        Token t;
        std::string why;
        const std::string_view tag = src.substr(i + 1, close - i - 1);
        if (readTag(tag, t, why)) {
            flushText(u32(i));
            t.begin = u32(i);
            t.end = u32(close + 1);
            tokens.push_back(t);
            textStart = u32(close + 1);
        } else {
            report("markup: [" + std::string(tag) + "] " + why);
        }
        i = close + 1;
    }
    flushText(u32(src.size()));

    // 2. Pair opening and closing tags; unpaired ones become text.
    std::vector<size_t> open;
    for (size_t k = 0; k < tokens.size(); ++k) {
        Token& t = tokens[k];
        if (t.type == Token::Type::Open) {
            open.push_back(k);
        } else if (t.type == Token::Type::Close) {
            if (!open.empty() && tokens[open.back()].name == t.name) {
                t.matched = tokens[open.back()].matched = true;
                open.pop_back();
            } else {
                report("markup: unexpected [/" + std::string(t.name) + "]");
            }
        } else if (t.type == Token::Type::Icon) {
            t.matched = true;
        }
    }
    for (size_t k : open) report("markup: unclosed [" + std::string(tokens[k].name) + "]");

    // 3. Runs of visible text (offsets into storage until it stops growing) and icons.
    struct Pending {
        MarkupRun run;
        u32 begin = 0, end = 0;
    };
    std::vector<Pending> pending;
    std::vector<RunStyle> styles{RunStyle{}};
    for (const Token& t : tokens) {
        if (t.matched && t.type == Token::Type::Open) {
            RunStyle s = styles.back();
            if (t.name == "b") s.bold = true;
            else if (t.name == "i") s.italic = true;
            else if (t.name == "size") s.sizeScale *= t.scale;
            else if (t.name == "style") s.styleName = t.value;
            else if (t.color) {
                s.color = t.color;
                s.colorName = {};
            } else {
                s.colorName = t.value;
                s.color.reset();
            }
            styles.push_back(s);
            continue;
        }
        if (t.matched && t.type == Token::Type::Close) {
            styles.pop_back();
            continue;
        }
        if (t.matched && t.type == Token::Type::Icon) {
            Pending p;
            p.run.kind = MarkupRun::Kind::Icon;
            p.run.style = styles.back();
            p.run.icon = t.value;
            p.run.iconScale = t.scale;
            p.begin = p.end = u32(storage.size());
            pending.push_back(p);
            continue;
        }
        // Text, or a tag that stays as written.
        const u32 begin = u32(storage.size());
        const std::string_view text = src.substr(t.begin, t.end - t.begin);
        for (size_t i = 0; i < text.size(); ++i) {
            storage.push_back(text[i]);
            if (text[i] == '[' && i + 1 < text.size() && text[i + 1] == '[' && t.type == Token::Type::Text) ++i;
        }
        const u32 end = u32(storage.size());
        if (!pending.empty() && pending.back().run.kind == MarkupRun::Kind::Text && pending.back().end == begin &&
            sameStyle(pending.back().run.style, styles.back())) {
            pending.back().end = end;
        } else if (end > begin) {
            Pending p;
            p.run.style = styles.back();
            p.begin = begin;
            p.end = end;
            pending.push_back(p);
        }
    }
    out.reserve(pending.size());
    for (Pending& p : pending) {
        p.run.text = std::string_view(storage).substr(p.begin, p.end - p.begin);
        out.push_back(p.run);
    }
    return true;
}

std::string_view escapeMarkup(std::string_view value, std::span<char> buf) {
    size_t n = 0;
    for (char c : value) {
        const size_t need = c == '[' ? 2 : 1;
        if (n + need > buf.size()) break;
        buf[n++] = c;
        if (c == '[') buf[n++] = '[';
    }
    return {buf.data(), n};
}

} // namespace nova
