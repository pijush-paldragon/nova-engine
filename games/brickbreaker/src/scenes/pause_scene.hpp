#pragma once

#include <nova/nova.hpp>

// Pause menu above the play scene (which stays frozen below): resume, settings, quit.
class PauseScene final : public nova::Scene {
public:
    PauseScene() { opaque = false; }
    void onEnter() override;
    void onUpdate(float dt) override;

private:
    nova::ui::PopupRef menu_;
    bool quitting_ = false;
};
