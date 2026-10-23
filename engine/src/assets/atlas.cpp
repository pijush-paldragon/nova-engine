#include <nova/assets/atlas.hpp>

#include <charconv>

namespace nova {
namespace {

// Splits a line on spaces/tabs into at most `max` tokens; returns the token count.
size_t tokenize(std::string_view line, std::string_view* out, size_t max) {
    size_t count = 0;
    size_t i = 0;
    while (i < line.size() && count < max + 1) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        if (count < max) out[count] = line.substr(start, i - start);
        ++count;
    }
    return count;
}

bool toFloat(std::string_view s, f32& out) {
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && end == s.data() + s.size();
}

template <class Fn>
bool forEachLine(std::string_view text, Fn&& fn) {
    int lineNo = 0;
    while (!text.empty()) {
        size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++lineNo;
        std::string_view tokens[6];
        size_t n = tokenize(line, tokens, 6);
        if (n == 0 || tokens[0].front() == '#') continue;
        if (!fn(tokens, n, lineNo)) return false;
    }
    return true;
}

} // namespace

Result<std::string> parseAtlasImage(std::string_view text) {
    std::string image;
    forEachLine(text, [&](std::string_view* t, size_t n, int) {
        if (n == 2 && t[0] == "image") image = std::string(t[1]);
        return false; // only the first non-comment line matters
    });
    if (image.empty()) return Result<std::string>::fail("atlas: first line must be 'image <path>'");
    return image;
}

Result<std::vector<AtlasEntry>> parseAtlas(std::string_view text) {
    std::vector<AtlasEntry> entries;
    bool sawImage = false;
    std::string error;
    forEachLine(text, [&](std::string_view* t, size_t n, int lineNo) {
        if (!sawImage) {
            if (n != 2 || t[0] != "image") {
                error = "atlas: first line must be 'image <path>'";
                return false;
            }
            sawImage = true;
            return true;
        }
        AtlasEntry e;
        if (n != 5 || !toFloat(t[1], e.src.x) || !toFloat(t[2], e.src.y) || !toFloat(t[3], e.src.w) ||
            !toFloat(t[4], e.src.h)) {
            error = "atlas: malformed region on line " + std::to_string(lineNo);
            return false;
        }
        e.name = std::string(t[0]);
        entries.push_back(std::move(e));
        return true;
    });
    if (!error.empty()) return Result<std::vector<AtlasEntry>>::fail(error);
    if (!sawImage) return Result<std::vector<AtlasEntry>>::fail("atlas: missing image line");
    return entries;
}

} // namespace nova
