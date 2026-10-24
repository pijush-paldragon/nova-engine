#include "ui/style.hpp"

#include <nova/core/log.hpp>

#include <unordered_set>

namespace nova::ui {
namespace {

constexpr int kMaxExtends = 8;

// Properties `over` sets replace `base`'s; states merge override by override.
Style mergeOnto(const Style& base, const Style& over) {
    Style s = base;
    auto take = [](auto& into, const auto& from) {
        if (from.isSet()) into = *from;
    };
    take(s.background, over.background);
    take(s.outlineColor, over.outlineColor);
    take(s.outlineWidth, over.outlineWidth);
    take(s.tint, over.tint);
    take(s.opacity, over.opacity);
    take(s.scale, over.scale);
    take(s.padding, over.padding);
    take(s.font, over.font);
    take(s.fontSize, over.fontSize);
    take(s.textColor, over.textColor);
    take(s.clickSound, over.clickSound);
    auto mergeState = [](StateStyle& into, const StateStyle& from) {
        if (from.background) into.background = from.background;
        if (from.tint) into.tint = from.tint;
        if (from.textColor) into.textColor = from.textColor;
        if (from.opacity) into.opacity = from.opacity;
        if (from.scale) into.scale = from.scale;
    };
    mergeState(s.pressed, over.pressed);
    mergeState(s.disabled, over.disabled);
    mergeState(s.checked, over.checked);
    s.extends.clear();
    return s;
}

void applyState(Style& s, const StateStyle& st) {
    if (st.background) s.background = *st.background;
    if (st.tint) s.tint = *st.tint;
    if (st.textColor) s.textColor = *st.textColor;
    if (st.opacity) s.opacity = *st.opacity;
    if (st.scale) s.scale = *st.scale;
}

} // namespace

bool firstReport(LogKey key, std::string_view name) {
    static std::unordered_set<std::string, IdHashSv, std::equal_to<>> seen[size_t(LogKey::Count)];
    auto& set = seen[size_t(key)];
    if (set.find(name) != set.end()) return false;
    set.emplace(name);
    return true;
}

void logOnce(const std::string& message) {
#if defined(NOVA_DEV_TOOLS)
    static thread_local std::unordered_map<std::string, bool> seen;
    if (seen.emplace(message, true).second) log::error("%s", message.c_str());
#else
    (void)message;
#endif
}

void Theme::patch(const std::string& name, const Style& style) {
    auto it = entries_.find(std::string_view(name));
    if (it == entries_.end()) {
        set(name, style);
        return;
    }
    Style merged = mergeOnto(it->second.raw, style);
    merged.extends = style.extends.empty() ? it->second.raw.extends : style.extends;
    set(name, std::move(merged));
}

void Theme::set(std::string name, Style style) {
    Entry& e = entries_[std::move(name)];
    e.raw = std::move(style);
    for (auto& [_, entry] : entries_) {
        for (bool& d : entry.done) d = false;
    }
    ++generation_;
}

const Style& Theme::resolveRaw(std::string_view name, int depth) const {
    static const Style empty;
    auto it = entries_.find(name);
    if (it == entries_.end()) return empty;
    const Style& raw = it->second.raw;
    // A style without `extends` builds on "default", so every style shares its state behaviour.
    if (raw.extends.empty()) {
        if (name == "default" || !has("default")) return raw;
        merged_ = mergeOnto(entries_.find(std::string_view("default"))->second.raw, raw);
        return merged_;
    }
    if (depth >= kMaxExtends) {
        logOnceFor(LogKey::StyleExtends, name, [&] {
            return "ui: style extends cycle (or chain deeper than 8) at \"" + std::string(name) + "\"";
        });
        return raw;
    }
    if (!has(raw.extends)) {
        logOnceFor(LogKey::UnknownStyle, raw.extends, [&] {
            return "ui: unknown style \"" + raw.extends + "\" (extended by \"" + std::string(name) + "\")";
        });
    }
    Style base = resolveRaw(raw.extends, depth + 1);
    merged_ = mergeOnto(base, raw);
    return merged_;
}

const Style& Theme::resolve(std::string_view name, VisualState state) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        logOnceFor(LogKey::UnknownStyle, name, [&] { return "ui: unknown style \"" + std::string(name) + "\""; });
        it = entries_.find(std::string_view("default"));
        if (it == entries_.end()) {
            static const Style fallback;
            return fallback;
        }
        name = "default";
    }
    const Entry& entry = it->second;
    const int i = int(state);
    if (!entry.done[i]) {
        Style s = resolveRaw(name, 0);
        if (state == VisualState::Pressed) applyState(s, s.pressed);
        else if (state == VisualState::Disabled) applyState(s, s.disabled);
        else if (state == VisualState::Checked) applyState(s, s.checked);
        entry.resolved[i] = std::move(s);
        entry.done[i] = true;
    }
    return entry.resolved[i];
}

const char* defaultStyleName(Kind kind) {
    switch (kind) {
        case Kind::Button: return "button";
        case Kind::Toggle: return "toggle";
        case Kind::Slider: return "slider";
        case Kind::ProgressBar: return "progress";
        default: return "default";
    }
}

Theme Theme::builtin() {
    Theme t;
    auto shape = [](u8 radius, u32 rgba) {
        Background b;
        b.kind = Background::Kind::Shape;
        b.radius = radius;
        b.color = Color::fromHex(rgba);
        return b;
    };
    Style def;
    def.disabled.opacity = 0.5f;
    def.pressed.tint = Color::fromHex(0xD9D9D9FF);
    def.pressed.scale = 0.95f;
    t.set("default", def);

    Style primary;
    primary.extends = "default";
    primary.background = shape(16, 0x3B82F6FF);
    primary.padding = {16, 32, 16, 32};
    t.set("primary", primary);
    t.set("button", primary);

    Style secondary;
    secondary.extends = "primary";
    secondary.background = shape(16, 0x00000000);
    secondary.outlineColor = Color::fromHex(0x3B82F6FF);
    t.set("secondary", secondary);

    Style danger;
    danger.extends = "primary";
    danger.background = shape(16, 0xEF4444FF);
    t.set("danger", danger);

    Style panel;
    panel.extends = "default";
    panel.background = shape(24, 0x1F2937F0);
    panel.padding = Insets::all(24);
    t.set("panel", panel);

    Style hud;
    hud.extends = "default";
    hud.fontSize = 40;
    t.set("hud", hud);

    Style toggle;
    toggle.extends = "default";
    toggle.background = shape(16, 0x4B5563FF);
    toggle.checked.background = shape(16, 0x22C55EFF);
    t.set("toggle", toggle);

    Style slider;
    slider.extends = "default";
    slider.background = shape(8, 0x4B5563FF);
    slider.tint = Color::fromHex(0x3B82F6FF); // fill and knob colour
    t.set("slider", slider);

    Style progress;
    progress.extends = "default";
    progress.background = shape(8, 0x374151FF);
    progress.tint = Color::fromHex(0x22C55EFF);
    t.set("progress", progress);
    // Popup backdrops: a black sheet whose opacity is the popup's dim; no pressed look.
    Style backdrop;
    backdrop.extends = "default";
    backdrop.background = shape(0, 0x000000FF);
    backdrop.pressed.scale = 1.f;
    backdrop.pressed.tint = Color::White;
    t.set("ui-backdrop", backdrop);
    return t;
}

} // namespace nova::ui
