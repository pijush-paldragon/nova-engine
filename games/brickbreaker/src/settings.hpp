#pragma once

#include <nova/nova.hpp>

// Saved audio settings: "music_volume", "sfx_volume" and "sound_on" (mutes sound effects).
void applyAudioSettings(nova::Audio& audio, nova::SaveStore& save);

// Opens ui/settings.json as a popup on `ui`. Slider changes apply at once and are saved when
// the finger lifts; the toggle saves immediately.
nova::ui::PopupRef openSettings(nova::ui::Canvas& ui, nova::Audio& audio, nova::SaveStore& save);
