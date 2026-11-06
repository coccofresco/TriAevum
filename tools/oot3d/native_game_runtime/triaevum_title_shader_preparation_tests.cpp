// Which title starts the renderer with prepared shaders, and what the
// renderer's startup then does (fast/renderer3ds/title_shader_preparation.h).
#include "triaevum_title_session.h"

#include "fast/renderer3ds/title_shader_preparation.h"

#include <cstdlib>
#include <iostream>

using namespace Oot3dNativeGame;

namespace {
void Check(bool value, const char *message) {
  if (!value) {
    std::cerr << "triaevum_title_shader_preparation_tests: " << message << '\n';
    std::exit(1);
  }
}

// A title session that declares nothing (as MM3D does until its corpus exists).
class UnpreparedSession final : public TitleSession {
public:
  const char *GameName() const override { return "Unprepared"; }
};
// A title session whose shader corpus is prepared (MM3D after P7.1/P7.2).
class PreparedSession final : public TitleSession {
public:
  const char *GameName() const override { return "Prepared"; }
  bool PreparedShaders() const override { return true; }
};

Fast::Renderer3ds::TitleShaderPreparationPlan PlanFor(TitleAdapter adapter, const TitleSession *session) {
  return Fast::Renderer3ds::ResolveTitleShaderPreparationPlan({TitlePreparedShaders(adapter, session)});
}
} // namespace

int main() {
  // OoT3D (no session): every cache and preparation path stays.
  const auto oot3d = PlanFor(TitleAdapter::Oot3dEur, nullptr);
  Check(oot3d.PersistentPipelineCache && oot3d.AlwaysAvailablePrograms, "OoT3D lost a cache or preparation path");
  // A generic title has no prepared shaders.
  Check(!TitlePreparedShaders(TitleAdapter::Generic, nullptr), "a generic title claims prepared shaders");
  // A title session declares no prepared shaders unless it says so.
  const UnpreparedSession unprepared;
  const auto mm3d = PlanFor(TitleAdapter::Mm3d, &unprepared);
  Check(!mm3d.PersistentPipelineCache && !mm3d.AlwaysAvailablePrograms,
        "a title without prepared shaders keeps the persistent cache or always-available programs");
  // Flipping the capability restores the work, for the same adapter.
  const PreparedSession prepared;
  const auto restored = PlanFor(TitleAdapter::Mm3d, &prepared);
  Check(restored.PersistentPipelineCache && restored.AlwaysAvailablePrograms,
        "a prepared title session does not restore the caches");
  // The session, not the adapter, decides once a title has one.
  Check(!TitlePreparedShaders(TitleAdapter::Oot3dEur, &unprepared), "the adapter overrode its session");
  std::cout << "triaevum_title_shader_preparation_tests: ok\n";
  return 0;
}
