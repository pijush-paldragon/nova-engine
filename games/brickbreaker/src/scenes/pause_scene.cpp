#include "scenes/pause_scene.hpp"

#include "scenes/menu_scene.hpp"
#include "settings.hpp"

using namespace nova;

void PauseScene::onEnter() {
    // The popup's backdrop dims the frozen game below; closing it (Resume, back, P) resumes.
    menu_ = ui().push("ui/pause.json", {.dim = 0.7f, .enter = ui::Transition::Pop});
    menu_.onClose([this] {
        if (!quitting_) scenes().pop();
    });
    ui().onClick("resume", [this] { menu_.close(); });
    ui().onClick("settings", [this] { openSettings(ui(), audio(), save()); });
    ui().onClick("quit", [this] {
        quitting_ = true;
        scenes().pop();                   // this pause scene...
        scenes().replace<MenuScene>();    // ...then the game below it
    });

    if (options().autoplay) timers().after(0.5f, [this] { menu_.close(); });
}

void PauseScene::onUpdate(float) {
    if (ui().popupCount() == 1 && (input().keyPressed(Key::P) || input().keyPressed(Key::Space))) menu_.close();
}
