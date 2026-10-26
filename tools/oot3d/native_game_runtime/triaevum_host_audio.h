#pragma once

// The host audio service's output policy (docs/TRIAEVUM_AUDIO_SETTINGS.md):
// the owner of the master volume and mute, held by Ship::Audio. Null before
// the host context exists.

#include "ship/Context.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioOutputPolicy.h"

#include <memory>

namespace Oot3dNativeGame {

[[nodiscard]] inline std::shared_ptr<Ship::AudioOutputPolicy> HostAudioOutputPolicy() {
  auto *context = Ship::Context::GetRawInstance();
  if (context == nullptr) return nullptr;
  const auto audio = context->GetAudio();
  return audio != nullptr ? audio->GetOutputPolicy() : nullptr;
}

} // namespace Oot3dNativeGame
