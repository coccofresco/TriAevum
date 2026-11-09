// Which title starts the renderer with prepared shaders and in which pipeline
// cache files, and what the renderer's startup then does
// (fast/renderer3ds/title_shader_preparation.h).
#include "triaevum_title_session.h"

#include "fast/renderer3ds/title_shader_preparation.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Oot3dNativeGame;

// Renderer3ds::kNriPipelineCacheFilename (its header needs Vulkan).
constexpr const char *kNriCacheFile = "nri_pipeline_cache.bin";

namespace {
void Check(bool value, const char *message) {
  if (!value) {
    std::cerr << "triaevum_title_shader_preparation_tests: " << message << '\n';
    std::exit(1);
  }
}

// A title session that declares no prepared shaders (as MM3D does until its
// corpus exists).
class UnpreparedSession final : public TitleSession {
public:
  const char *GameName() const override { return "Unprepared"; }
  std::string_view PipelineCacheNamespace() const override { return "unprepared"; }
};
// A title session whose shader corpus is prepared (MM3D after P7.1/P7.2).
class PreparedSession final : public TitleSession {
public:
  const char *GameName() const override { return "Prepared"; }
  bool PreparedShaders() const override { return true; }
  std::string_view PipelineCacheNamespace() const override { return "prepared"; }
};

Fast::Renderer3ds::TitleShaderPreparationPlan PlanFor(TitleAdapter adapter, const TitleSession *session) {
  return Fast::Renderer3ds::ResolveTitleShaderPreparationPlan({TitlePreparedShaders(adapter, session), {}});
}
std::string NriCacheFileFor(TitleAdapter adapter, const TitleSession *session) {
  return Fast::Renderer3ds::TitlePipelineCacheFilename(kNriCacheFile,
                                                       TitlePipelineCacheNamespace(adapter, session));
}
} // namespace

int main() {
  // OoT3D (no session): always-available programs and the original cache
  // file, the one Forge prepares.
  Check(PlanFor(TitleAdapter::Oot3dEur, nullptr).AlwaysAvailablePrograms, "OoT3D lost always-available programs");
  Check(NriCacheFileFor(TitleAdapter::Oot3dEur, nullptr) == kNriCacheFile,
        "OoT3D's pipeline cache file changed");
  // A generic title has no prepared shaders and its own cache files.
  Check(!TitlePreparedShaders(TitleAdapter::Generic, nullptr), "a generic title claims prepared shaders");
  Check(NriCacheFileFor(TitleAdapter::Generic, nullptr) == "nri_pipeline_cache.generic.bin",
        "a generic title shares OoT3D's pipeline cache file");
  // A title session declares no prepared shaders unless it says so, and its
  // pipelines persist in its own files either way.
  const UnpreparedSession unprepared;
  Check(!PlanFor(TitleAdapter::Mm3d, &unprepared).AlwaysAvailablePrograms,
        "a title without prepared shaders keeps always-available programs");
  Check(NriCacheFileFor(TitleAdapter::Mm3d, &unprepared) == "nri_pipeline_cache.unprepared.bin",
        "a title session does not get its own pipeline cache file");
  // Flipping the capability restores the work, for the same adapter.
  const PreparedSession prepared;
  Check(PlanFor(TitleAdapter::Mm3d, &prepared).AlwaysAvailablePrograms,
        "a prepared title session does not restore always-available programs");
  // The session, not the adapter, decides once a title has one.
  Check(!TitlePreparedShaders(TitleAdapter::Oot3dEur, &unprepared), "the adapter overrode its session");
  // Owner decision 2b (2026-11-09, release decision D4 reverted): the window
  // session selects the frame-reproducible program only for a fixed-step
  // session (SetFrameReproducibleProgramSelection(hostArgs.FrameReproducible())).
  // Interactive OoT3D play may render the always-available program while a
  // draw's specialized program compiles; a fixed-step session still renders
  // only the bit-exact specialized program (c24bd592).
  using Fast::Renderer3ds::DrawsUseAlwaysAvailablePrograms;
  const bool ootAlwaysAvailable = PlanFor(TitleAdapter::Oot3dEur, nullptr).AlwaysAvailablePrograms;
#if defined(TRIAEVUM_SHADER_PREPARATION_TEST_MUTANT_D4)
  // Recorded negative: the reverted D4 selection (every OoT3D session forced).
  const bool interactiveSelection = true;
  const bool fixedStepSelection = true;
#elif defined(TRIAEVUM_SHADER_PREPARATION_TEST_MUTANT_FIXED_STEP)
  // Recorded negative: a fixed-step session that lost its forcing.
  const bool interactiveSelection = false;
  const bool fixedStepSelection = false;
#else
  const bool interactiveSelection = false; // FrameReproducible() of an interactive session
  const bool fixedStepSelection = true;    // FrameReproducible() of a fixed-step session
#endif
  Check(DrawsUseAlwaysAvailablePrograms(ootAlwaysAvailable, interactiveSelection),
        "interactive OoT3D play no longer admits the always-available (uber) program");
  Check(!DrawsUseAlwaysAvailablePrograms(ootAlwaysAvailable, fixedStepSelection),
        "a fixed-step OoT3D session renders the always-available (uber) program");
  std::cout << "triaevum_title_shader_preparation_tests: ok\n";
  return 0;
}
