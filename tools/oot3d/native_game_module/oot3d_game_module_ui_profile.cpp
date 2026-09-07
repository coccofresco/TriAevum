#include "oot3d_game_module_ui_profile.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_top_screen_mod_profile.h"

#include <utility>

namespace Oot3dNativeGame {
namespace {

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

} // namespace

bool Oot3dGameModuleUiProfileRuntime::ApplyAfterMount(NativeA32Process &process,
                                                      std::string *error) {
  mStats = {};
  if (mProfile == Oot3dGameModuleUiProfile::Native) {
    return true;
  }

  TopScreenLayoutApplyStats layout;
  std::string detail;
  if (!ApplyTopScreenVerifiedLayout(process.Memory(), &layout, &detail)) {
    SetError(error, "TopScreen layout rejected the mounted title: " + detail);
    return false;
  }

  TopScreenRuntimeGeometryApplyStats geometry;
  if (layout.WordsChanged != 0U && !RebindTopScreenVerifiedRuntimeGeometry(
                                       process.Memory(), &geometry, &detail)) {
    SetError(error, "TopScreen runtime geometry rejected the mounted title: " +
                        detail);
    return false;
  }

  mStats.LayoutContractsChecked = layout.ContractsChecked;
  mStats.LayoutWordsWritten = layout.WordsWritten;
  mStats.LayoutWordsChanged = layout.WordsChanged;
  mStats.RuntimeStreamsRebound = geometry.StreamsRebound;
  mStats.RuntimeQuadsRebound = geometry.QuadsRebound;
  return true;
}

Oot3dGameModuleUiProfile
Oot3dGameModuleUiProfileRuntime::Profile() const noexcept {
  return mProfile;
}

const Oot3dGameModuleUiProfileStats &
Oot3dGameModuleUiProfileRuntime::Stats() const noexcept {
  return mStats;
}

} // namespace Oot3dNativeGame
