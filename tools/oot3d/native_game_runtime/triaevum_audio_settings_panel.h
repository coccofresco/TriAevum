#pragma once

// F1 > Game > Audio: the one F1 surface of the host's audio output settings
// (docs/TRIAEVUM_AUDIO_SETTINGS.md). The owner is Ship::AudioOutputPolicy,
// held by the host audio service (Ship::Audio); this section only reads it
// and applies changes through it.

#include "fast/extension/graphics_settings_window.h"

#include <functional>
#include <memory>

namespace Ship {
class AudioOutputPolicy;
}

namespace Oot3dNativeGame {

// Resolves the owner when the section is drawn: F1's tabs are installed
// before the host context (and its audio service) exists.
using AudioOutputPolicySource = std::function<std::shared_ptr<Ship::AudioOutputPolicy>()>;

// `offerFocusMute`: the "Mute when the window loses focus" control (desktop;
// an Android activity pauses in the background instead).
[[nodiscard]] std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab>
CreateAudioSettingsPanel(AudioOutputPolicySource source, bool offerFocusMute);

} // namespace Oot3dNativeGame
