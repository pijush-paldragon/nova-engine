#pragma once

#include "ui/element_store.hpp"

#include <nova/ui/canvas.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace nova {
struct Services;
}

namespace nova::ui {

const char* kindName(Kind kind);

struct Canvas::Impl {
    ElementStore store;
    Services* services = nullptr;
    std::vector<std::string> layerSources{"code"}; // what built each layer, for messages
    std::unordered_set<std::string> reported;      // "kind:id" already logged as missing

    // Dev builds log a missing id or wrong kind once per (kind, id).
    void reportMissing(std::string_view id, const char* what, u16 layer);
};

} // namespace nova::ui
