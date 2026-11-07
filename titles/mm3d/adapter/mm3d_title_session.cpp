// MM3D's title session: its hooks and guest patches (mm3d-dekui, Project
// Restoration, lighting), its scene view and environment, its interface frame
// and its developer tools, at the points the native A32 window session calls.
#include "triaevum_title_session.h"

#include "a32_runtime.h"
#include "mm3d_guest_calls.h"
#include "mm3d_guest_patches.h"
#include "mm3d_lighting_hook_sites.h"
#include "mm3d_pica_composition.h"
#include "mm3d_restoration.h"
#include "mm3d_scene_environment.h"
#include "mm3d_scene_environment_profile.h"
#include "mm3d_single_screen.h"
#include "mm3d_title_contracts.h"
#include "../gameplay/mm3d_actor_interactions.h"
#include "oot3d_dev_warp.h"
#include "oot3d_native_a32_process.h"
#include "oot3d_native_a32_registry.h"
#include "oot3d_native_a32_scene_view.h"
#include "oot3d_native_pica_draw_state.h"
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "mm3d_dev_camera_hook.h"
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace Oot3dNativeGame {
namespace {

class Mm3dTitleSession final : public TitleSession {
public:
  const char *GameName() const override { return "Majora's Mask 3D"; }
  Oot3dPicaCommandListCompositionProvider *CompositionProvider() override { return &mComposition; }
  bool NativeFragmentLighting() const override { return true; }
  bool HasButtonOverlay() const override { return true; }
  void ConfigureButtonOverlay(const NativeControlConfig& config, NativePromptSet prompts) override {
    if (mSingleScreen) mSingleScreen->ConfigureButtonOverlay(config, prompts);
  }
  void RecordButtonOverlay(uint64_t draw) override { if (mSingleScreen) mSingleScreen->RecordButtonOverlay(draw); }
  void SelectButtonOverlay(uint64_t draw) override { if (mSingleScreen) mSingleScreen->SelectButtonOverlay(draw); }
  std::vector<oot3d::ui::UiPrimitive> ButtonOverlay(float scale) const override {
    return mSingleScreen ? mSingleScreen->ButtonOverlay(scale) : std::vector<oot3d::ui::UiPrimitive>{};
  }
  std::string_view GrassRuleNamespace() const override { return "mm3d"; }
  std::vector<Fast::Extension::GrassInteractor> ActorInteractions(
      oot3d::recomp::a32::MemoryBus& memory, uint64_t sourceFrame) const override {
    auto contacts = CollectMm3dActorInteractions(memory, sourceFrame, mActorInteractionGuards);
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
    if (std::getenv("OOT3D_GRASS_DIAGNOSTICS") && sourceFrame % 60 == 0) {
      std::fprintf(stderr, "MM3D grass colliders: frame=%llu count=%zu guarded=%d\n",
                   static_cast<unsigned long long>(sourceFrame), contacts.size(), mActorInteractionGuards);
      for (const auto& contact : contacts)
        if (contact.Type != Fast::Extension::GrassInteractor::Kind::SceneActor)
          std::fprintf(stderr, "MM3D grass collider: role=%u radius=%.3f halfHeight=%.3f y=%.3f\n",
                       unsigned(contact.Type), contact.Radius, contact.HalfHeight, contact.Position[1]);
    }
#endif
    return contacts;
  }

  void InstallHooks(TitleSessionHookSetup &setup) override {
    // MM3D's perspective frustum builder: the scene widens to the output
    // and takes the scene FOV. Its frusta also serve views other than the
    // scene camera, so they are not published as the scene view.
    auto &blockEntryHooks = setup.BlockEntryHooks;
    blockEntryHooks.push_back(kMm3dMtx44Frustum);
    const auto compositionHooks = Mm3dNativePicaCompositionHookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), compositionHooks.begin(),
                           compositionHooks.end());
    std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
    blockEntryHooks.erase(std::unique(blockEntryHooks.begin(), blockEntryHooks.end()),
                          blockEntryHooks.end());
    setup.PerspectiveFrustumEntry = kMm3dMtx44Frustum;
    setup.FrustumIsSceneView = false;
    setup.SceneView.UseExternalSceneEnvironment();
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
    if (!*setup.DevToolsTitle) *setup.DevToolsTitle = std::make_shared<Mm3dDevTools>();
#endif
    if (mGuestPatches.Empty()) {
      // dekui's settings: its shipped defaults, or (development) a
      // presentation.json given by TRIAEVUM_MM3D_PRESENTATION, until the F1
      // menu owns and persists them.
      const char *presentation = std::getenv("TRIAEVUM_MM3D_PRESENTATION");
      if (!Mm3dSingleScreen::LoadSettings(
              presentation != nullptr ? std::filesystem::path(presentation) : std::filesystem::path{}))
        std::fprintf(stderr, "MM3D single-screen: %s is unreadable, using dekui's defaults\n",
                     presentation != nullptr ? presentation : "dekui's settings");
      if (const char *multiplier = std::getenv("TRIAEVUM_MM3D_DAMAGE_MULTIPLIER")) {
        // Development: dekui's damage multiplier until the F1 owns it.
        Mm3dSingleScreen::SetDamageMultiplier(std::atoi(multiplier));
      }
      // dekui animates with the emulated clock: here the deterministic
      // presentation clock (a 60 Hz frame each).
      mSingleScreen.emplace(setup.PresentationClock);
      mSingleScreen->Register(mGuestPatches);
      RegisterMm3dLightingHooks(mGuestPatches);
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
      RegisterMm3dDevCamera(mGuestPatches, *setup.DevToolsTitle, setup.FreeCamera);
#endif
      mGuestCalls.Install(mGuestPatches);
      mRestoration.emplace(mGuestCalls);
      mRestoration->Register(mGuestPatches);
      mSingleScreen->BindRestoration(*mRestoration);
      if (const char *edits = std::getenv("TRIAEVUM_MM3D_MESSAGE_EDITS")) {
        // Development: dekui's single-screen wording compiled against the
        // user's own messages (titles/mm3d/dekui/setup/prepare_message_edits.py),
        // until the launcher prepares and owns them.
        std::ifstream stream(std::filesystem::u8path(edits), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::string error;
        const auto parsed = Mm3dRestorationPort::ReadMessageEdits(text, &error);
        if (!stream || !parsed.has_value() || !mRestoration->SetMessageEdits(*parsed)) {
          throw std::runtime_error(std::string("MM3D message edits: ") + edits + " is invalid " + error);
        }
        std::fprintf(stderr, "MM3D message edits: %zu message(s)\n", parsed->size());
      }
    }
  }

  void ImageMounted(NativeA32Process &process) override {
    uint32_t word = 0;
    if (!process.Memory().Read32(kMm3dMtx44Frustum, &word) ||
        word != kMm3dMtx44FrustumFirstWord) {
      throw std::runtime_error("MM3D adapter: the perspective frustum builder is not at its address");
    }
    if (!mGuestPatches.Empty()) {
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
      throw std::runtime_error("MM3D adapter: guest patch sites need the A32 registry dispatch");
#else
      // Each site whose code matches the mounted image replaces its
      // instruction; the registry starts a block at every site.
      // dekui runs on the image Project Restoration patched: its data
      // patches first, then every site's check.
      if (mRestoration.has_value()) {
        mRestoration->ApplyDataPatches(process.Memory());
      }
      mGuestPatches.Verify(process.Memory(), &Oot3dNativeA32Registry());
      if (!mComposition.VerifyHooks(process.Memory(), &Oot3dNativeA32Registry()))
        throw std::runtime_error("MM3D lighting: wrong image or missing observer block; regenerate registry boundaries");
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
      const auto adapterProofFlag = [](const char* name) {
        const char* value = std::getenv(name);
        return value && *value && *value != '0';
      };
      mComposition.SetSupplementalPublication(
          !adapterProofFlag("TRIAEVUM_MM3D_ADAPTER_NO_POINTS"), !adapterProofFlag("TRIAEVUM_MM3D_ADAPTER_NO_WATER"));
#endif
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
      // Which sites stayed off, for agents reading the log.
      for (const auto &site : mGuestPatches.Statistics())
        if (!site.Enabled)
          std::fprintf(stderr, "TRIAEVUM_MM3D_PATCH_DISABLED %s pc=0x%08X mismatch=0x%08X missing_block=%d prepared=%d\n",
                       site.Name.c_str(), site.Pc, site.MismatchAddress.value_or(0U), site.MissingBlock ? 1 : 0,
                       site.Prepared ? 1 : 0);
#endif
      process.SetNativeFunctionCallback(&Mm3dGuestPatches::Execute,
                                        &mGuestPatches, mGuestPatches.EnabledPcs());
#endif
    }
    mActorInteractionGuards = Mm3dActorInteractionGuardsHold(process.Memory());
    if (!mActorInteractionGuards)
      std::fprintf(stderr, "MM3D actor interactions disabled: EUR collider guard words do not match\n");
    mImageMounted = true;
  }

  void ObserveBlockEntry(TitleBlockEntry &entry) override {
    if (mComposition.ObserveBlockEntry(entry.Pc, entry.State, entry.Memory) !=
            Mm3dNativePicaCompositionTracker::Event::OpaqueWorldBegins ||
        entry.SceneView == nullptr)
      return;
    auto &memory = entry.Memory;
    entry.SceneView->BeginExternalSceneView();
    // Capture once at the same native opaque boundary as the view/lights,
    // before the guest advances. The renderer later receives values only.
    const auto environment = CaptureMm3dSceneEnvironment(memory);
    TraceMm3dSceneEnvironment(environment, entry.HostFrame, entry.Pc, entry.State.r[14]);
    if (entry.PendingSceneFrustum.has_value()) {
      // The scene's camera for its opaque pass: the current View's field of
      // view and clip planes, at the aspect (and widening) of the frustum the
      // pass draws with, and its eye and target. The default View (no current
      // one) has a placeholder eye and target, so it publishes no camera.
      const auto &frustum = *entry.PendingSceneFrustum;
      uint32_t currentView = 0;
      bool read = memory.Read32(kMm3dCurrentViewPointer, &currentView);
      const uint32_t base = currentView != 0U ? currentView : kMm3dDefaultViewAddress;
      const auto readFloat = [&](uint32_t address, float &value) {
        uint32_t word = 0;
        read = read && memory.Read32(address, &word);
        value = std::bit_cast<float>(word);
      };
      float view[3] = {};
      std::array<float, 3> eye{}, at{};
      readFloat(base + kMm3dViewFovyOffset, view[0]);
      readFloat(base + kMm3dViewNearOffset, view[1]);
      readFloat(base + kMm3dViewFarOffset, view[2]);
      for (uint32_t axis = 0; axis < 3U; ++axis) {
        readFloat(base + kMm3dViewEyeOffset + axis * 4U, eye[axis]);
        readFloat(base + kMm3dViewAtOffset + axis * 4U, at[axis]);
      }
      const float frustumHeight = frustum.Top - frustum.Bottom;
      if (read && view[0] > 1.0F && view[0] < 179.0F && view[1] > 0.0F &&
          view[2] > view[1] && frustumHeight > 0.0F) {
        const float top = view[1] * std::tan(view[0] * 0.5F * 3.14159265F / 180.0F);
        const float scale = 2.0F * top / frustumHeight;
        if (currentView != 0U) {
          entry.SceneView->ObservePerspectiveCamera(
              frustum.Pc, frustum.ReturnAddress, frustum.Left * scale,
              frustum.Right * scale, frustum.Bottom * scale, frustum.Top * scale,
              view[1], view[2], eye, at);
        } else {
          entry.SceneView->ObservePerspective(
              frustum.Pc, frustum.ReturnAddress, frustum.Left * scale,
              frustum.Right * scale, frustum.Bottom * scale, frustum.Top * scale,
              view[1], view[2]);
        }
      }
    }
    entry.SceneView->ObserveExternalSceneEnvironment(environment.State);
  }

  std::optional<float> UiFrameExtensionY(const Oot3dPicaViewportState &viewport) const override {
    // MM3D's interface keeps the 16:9 frame mm3d-dekui lays it out for,
    // whatever the output's aspect; the scene fills the output.
    if (!IsMm3dTopScreenCanvas(viewport)) return std::nullopt;
    return kMm3dUiFrameExtensionY;
  }

  bool SceneEnvironmentStillCurrent(oot3d::recomp::a32::MemoryBus &memory,
                                    const NativeSceneEnvironmentState &state) const override {
    return Mm3dSceneEnvironmentStillCurrent(memory, state);
  }

  bool SceneEnvironmentInputs(const NativeA32SceneViewSnapshot &snapshot,
                              std::optional<SceneEnvironmentTime> &environmentTime,
                              std::optional<ScenePlayerPlacement> &player) const override {
    if (!snapshot.Environment) return false;
    // MM3D's adapter: its play context owns the time, the scene view's world
    // identity stands for the authored scene (a restore is another world) and
    // its clock drives the day/night blend once its night flag is read.
    const auto& state = *snapshot.Environment;
    SceneEnvironmentTime time;
    time.Owner = state.ContextIdentity;
    time.AuthoredScene = snapshot.WorldIdentity;
    // The visit's environment group, latched by the lighting probe at the
    // same opaque boundary (mm3d_scene_environment.h). Profile day/night
    // selection also covers TimeAtEntry: its native clock is stopped, but
    // entering at night must select the group's night file. This is not the
    // lighting probe's continuous-sun flag (which remains TimeFlows only).
    time.Group = mComposition.EnvironmentGroup(state.ContextIdentity, state.SceneId);
    if (state.Night) {
      time.TimeDriven = time.Group == SceneEnvironmentGroup::TimeFlows ||
                        time.Group == SceneEnvironmentGroup::TimeAtEntry;
      time.Night = *state.Night;
      time.Clock = state.Clock.value_or(*state.Night ? 0x0000U : 0x8000U);
    }
    environmentTime = time;
    player = state.Player;
    return true;
  }

  // Owner decision Q-P1 (b): the base profile per group, applied to the
  // Scene lights only (mm3d_scene_environment_profile.h).
  bool BaseSceneEnvironment(SceneEnvironmentGroup group, SceneEnvironmentProfile &profile) const override {
    auto base = Mm3dBaseSceneEnvironmentProfile(group);
    if (!base) return false;
    profile = std::move(*base);
    return true;
  }

#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  void RunDevWarpScript(std::string_view script, DevWarpRuntime &devWarp) override {
    // mm3d:<encoded entrance>[:day=..][:time=HHMM][:form=..] or
    // mm3d:zone=<id>[:...]: stable encodings, no list-index ambiguity.
    const auto parsed = ParseMm3dDevWarpScript(script, devWarp.Entrances(), devWarp.Zones());
    std::string error;
    if (!parsed.Zone.empty()) {
      if (!devWarp.WarpToZone(parsed.Zone, parsed.Arrival, &error))
        throw std::runtime_error("TRIAEVUM_DEV_WARP: " + error);
      return;
    }
    devWarp.WarpTo(*devWarp.FindEntrance(*parsed.Entrance), parsed.Arrival);
  }
#endif

  void Reset() override {
    mComposition.Reset();
    if (mSingleScreen) mSingleScreen->ResetButtonOverlay();
  }

  nlohmann::json CompositionSummary() const override {
    const auto &stats = mComposition.Stats();
    return nlohmann::json{
        {"button_overlay", mSingleScreen ? nlohmann::json(mSingleScreen->ButtonOverlaySummary()) : nlohmann::json::object()},
        {"scope_entries", stats.ScopeEntries},
        {"scope_exits", stats.ScopeExits},
        {"recorded_spans", stats.RecordedSpans},
        {"empty_scopes", stats.EmptyScopes},
        {"invalid_ranges", stats.InvalidRanges},
        {"nested_scope_entries", stats.NestedScopeEntries},
        {"replaced_spans", stats.ReplacedSpans},
        {"expired_spans", stats.ExpiredSpans},
        {"scene_frames", stats.SceneFrames},
        {"read_failures", stats.ReadFailures},
        {"light_bindings", stats.LightBindings},
        {"scene_directional_lights", stats.SceneDirectionalLights}};
  }

  nlohmann::json GuestPatchSummary() const override {
    if (!mImageMounted) return nullptr;
    auto sites = nlohmann::json::array();
    for (const auto &site : mGuestPatches.Statistics()) {
      sites.push_back(
          {{"name", site.Name},
           {"pc", site.Pc},
           {"enabled", site.Enabled},
           {"mismatch_address",
            site.MismatchAddress.has_value()
                ? nlohmann::json(*site.MismatchAddress)
                : nlohmann::json(nullptr)},
           {"missing_block", site.MissingBlock},
           {"prepared", site.Prepared},
           {"replacements", site.Replacements},
           {"declined", site.Declined}});
    }
    return sites;
  }

private:
  Mm3dNativePicaCompositionTracker mComposition;
  // MM3D's single-screen mode: its settings and the typed replacements of
  // guest instructions that read them.
  std::optional<Mm3dSingleScreen> mSingleScreen;
  Mm3dGuestCalls mGuestCalls;
  std::optional<Mm3dRestorationPort> mRestoration;
  Mm3dGuestPatches mGuestPatches;
  bool mImageMounted = false;
  bool mActorInteractionGuards = false;
};

} // namespace

std::unique_ptr<TitleSession> CreateMm3dTitleSession() {
  return std::make_unique<Mm3dTitleSession>();
}

} // namespace Oot3dNativeGame
