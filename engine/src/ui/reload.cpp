#include "ui/reload.hpp"

#include "ui/draw.hpp"

#include <nova/core/alloc_stats.hpp>
#include <scene/services.hpp>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <string>

namespace nova::detail {
namespace {
std::string& sourceDir() {
    static std::string dir;
    return dir;
}
} // namespace

void setDevAssetSourceDir(const char* dir) { sourceDir() = dir ? dir : ""; }
const char* devAssetSourceDir() { return sourceDir().c_str(); }

} // namespace nova::detail

namespace nova::ui {
namespace {

bool reloadEnabled() {
#if defined(NOVA_DEV_TOOLS)
    return detail::devAssetSourceDir()[0] != '\0';
#else
    return false;
#endif
}

i64 stampOf(const std::string& rel) {
    std::string path = std::string(detail::devAssetSourceDir()) + rel;
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) ? i64(info.modify_time) : -1;
}

std::vector<i64> stampsOf(const std::vector<std::string>& files) {
    std::vector<i64> stamps;
    stamps.reserve(files.size());
    for (const std::string& f : files) stamps.push_back(stampOf(f));
    return stamps;
}

bool changed(const std::vector<std::string>& files, const std::vector<i64>& stamps) {
    if (files.size() != stamps.size()) return !files.empty();
    for (size_t i = 0; i < files.size(); ++i) {
        if (stampOf(files[i]) != stamps[i]) return true;
    }
    return false;
}

void reloadLayer(Canvas::Impl& impl, u16 layer) {
    const std::string file = impl.layerSources[layer];
    const Theme* theme = impl.services && impl.services->ui ? &impl.services->ui->theme : nullptr;
    std::vector<NodeSpec> nodes;
    LoadResult result = loadLayout(file, sourceFileReader(), theme, nodes);
    if (!result.ok || !result.errors.empty()) {
        impl.devError = result.firstError;
        // Watch what was read (plus the layer's previous files), so the next save retries.
        std::vector<std::string> watch = impl.layerFiles[layer];
        for (const std::string& f : result.files) {
            if (std::find(watch.begin(), watch.end(), f) == watch.end()) watch.push_back(f);
        }
        impl.layerFiles[layer] = watch;
        recordLayerStamps(impl, layer);
        return;
    }
    impl.buildLayer(layer, nodes, file, result);
    reapplyRemembered(impl, layer);
    impl.devError.clear();
}

} // namespace

FileReader sourceFileReader() {
    return [](std::string_view rel) -> std::optional<std::string> {
        std::string path = std::string(detail::devAssetSourceDir()) + std::string(rel);
        size_t size = 0;
        void* data = SDL_LoadFile(path.c_str(), &size);
        if (!data) return std::nullopt;
        std::string text(static_cast<const char*>(data), size);
        SDL_free(data);
        return text;
    };
}

void recordLayerStamps(Canvas::Impl& impl, u16 layer) {
    if (!reloadEnabled()) return;
    if (impl.layerStamps.size() <= layer) impl.layerStamps.resize(layer + 1u);
    impl.layerStamps[layer] = stampsOf(impl.layerFiles[layer]);
}

void recordThemeStamps(UiShared& shared, const std::vector<std::string>& files) {
    if (!reloadEnabled()) return;
    shared.themeFiles = files;
    shared.themeStamps = stampsOf(files);
}

void reapplyRemembered(Canvas::Impl& impl, u16 layer) {
    impl.store.forEach([&](UiHandle, Element& e) {
        if (e.layer != layer || e.spec.id.empty()) return;
        auto it = impl.remembered.find(std::string_view(e.spec.id));
        if (it == impl.remembered.end()) return;
        const Remembered& r = it->second;
        if (r.text) e.spec.text = *r.text;
        if (r.sprite) e.spec.sprite = *r.sprite;
        if (r.style) e.spec.style = *r.style;
        if (r.value) e.spec.value = e.shown = *r.value;
        if (r.on) e.spec.on = *r.on;
        if (r.visible) e.spec.visible = *r.visible;
        if (r.enabled) e.spec.enabled = *r.enabled;
        e.resolveDirty = true;
    });
    impl.store.markDirty(layer);
}

void pollReload(Canvas::Impl& impl, f32 dt) {
    if (!reloadEnabled()) return;
    impl.reloadTimer += dt;
    if (impl.reloadTimer < kReloadInterval) return;
    impl.reloadTimer = 0;
    memory::LoadingScope loading; // a dev-only file check, never part of a shipped frame
    if (impl.services && impl.services->ui) {
        UiShared& shared = *impl.services->ui;
        if (!shared.themeFiles.empty() && changed(shared.themeFiles, shared.themeStamps)) {
            LoadResult r = loadTheme(shared.themeFiles.front(), sourceFileReader(), shared.theme);
            recordThemeStamps(shared, shared.themeFiles);
            impl.devError = r.ok && r.errors.empty() ? std::string() : r.firstError;
        }
    }
    for (u16 layer = 0; layer < impl.store.layerCount() && layer < impl.layerFiles.size(); ++layer) {
        if (impl.layerFiles[layer].empty()) continue; // built in code
        const std::vector<i64> none;
        const std::vector<i64>& stamps = layer < impl.layerStamps.size() ? impl.layerStamps[layer] : none;
        if (changed(impl.layerFiles[layer], stamps)) reloadLayer(impl, layer);
    }
}

} // namespace nova::ui
