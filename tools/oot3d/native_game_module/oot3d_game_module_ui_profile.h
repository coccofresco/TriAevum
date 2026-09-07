#pragma once

#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;

enum class Oot3dGameModuleUiProfile : std::uint8_t {
  Native = 0U,
  TopScreen = 1U,
};

inline constexpr Oot3dGameModuleUiProfile kDefaultOot3dGameModuleUiProfile =
    Oot3dGameModuleUiProfile::TopScreen;

struct Oot3dGameModuleUiProfileStats {
  std::uint32_t LayoutContractsChecked = 0U;
  std::uint32_t LayoutWordsWritten = 0U;
  std::uint32_t LayoutWordsChanged = 0U;
  std::uint32_t RuntimeStreamsRebound = 0U;
  std::uint32_t RuntimeQuadsRebound = 0U;
};

// Title-owned adapter for UI behavior reconstructed from TopScreen. The
// common 3DS runtime and renderer remain unaware of OOT3D guest addresses.
class Oot3dGameModuleUiProfileRuntime final {
public:
  explicit Oot3dGameModuleUiProfileRuntime(
      Oot3dGameModuleUiProfile profile = kDefaultOot3dGameModuleUiProfile)
      : mProfile(profile) {}

  [[nodiscard]] bool ApplyAfterMount(NativeA32Process &process,
                                     std::string *error = nullptr);
  [[nodiscard]] Oot3dGameModuleUiProfile Profile() const noexcept;
  [[nodiscard]] const Oot3dGameModuleUiProfileStats &Stats() const noexcept;

private:
  Oot3dGameModuleUiProfile mProfile;
  Oot3dGameModuleUiProfileStats mStats;
};

} // namespace Oot3dNativeGame
