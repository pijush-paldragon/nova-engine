#include "settings.hpp"

using namespace nova;

namespace {

void saveNow(SaveStore& save) {
    if (auto r = save.flush(); !r) log::error("%s", r.error().message.c_str());
}

} // namespace

void applyAudioSettings(Audio& audio, SaveStore& save) {
    audio.setVolume(Bus::Music, float(save.getFloat("music_volume", 0.6)));
    const bool soundOn = save.getBool("sound_on", true);
    audio.setVolume(Bus::Sfx, soundOn ? float(save.getFloat("sfx_volume", 1.0)) : 0.f);
}

ui::PopupRef openSettings(ui::Canvas& ui, Audio& audio, SaveStore& save) {
    ui::PopupRef popup = ui.push("ui/settings.json", {.closeOnBackdrop = true});
    popup.slider("music").setValue(float(save.getFloat("music_volume", 0.6)));
    popup.slider("sfx").setValue(float(save.getFloat("sfx_volume", 1.0)));
    popup.toggle("sound").setOn(save.getBool("sound_on", true));

    popup.slider("music").onChange([&audio](float v) { audio.setVolume(Bus::Music, v); });
    popup.slider("music").onCommit([&save](float v) {
        save.setFloat("music_volume", v);
        saveNow(save);
    });
    popup.slider("sfx").onChange([&audio, &save](float v) {
        if (save.getBool("sound_on", true)) audio.setVolume(Bus::Sfx, v);
    });
    popup.slider("sfx").onCommit([&save](float v) {
        save.setFloat("sfx_volume", v);
        saveNow(save);
    });
    popup.toggle("sound").onChange([&audio, &save](bool on) {
        save.setBool("sound_on", on);
        applyAudioSettings(audio, save);
        saveNow(save);
    });
    popup.button("close").onClick([popup]() mutable { popup.close(); });
    return popup;
}
