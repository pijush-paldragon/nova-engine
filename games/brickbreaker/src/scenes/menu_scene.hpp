#pragma once

#include <nova/nova.hpp>

// Title screen: best score, Play and Settings. Starts the music on first entry.
class MenuScene final : public nova::Scene {
public:
    void onEnter() override;
    void onUpdate(float dt) override;
};
