#pragma once

#include <nova/nova.hpp>

class MenuScene final : public nova::Scene {
public:
    void onEnter() override;
    void onUpdate(float dt) override;

private:
    nova::Entity ball_ = nova::kNullEntity;
};
