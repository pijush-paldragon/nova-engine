#pragma once

#include <nova/nova.hpp>

// Translucent overlay above the play scene; tap (or back) resumes.
class PauseScene final : public nova::Scene {
public:
    PauseScene() { opaque = false; }
    void onEnter() override;
    void onUpdate(float dt) override;
};
