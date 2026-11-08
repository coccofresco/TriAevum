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
#include "../dekui/button_emission.h"
#include "../dekui/button_glyph_emission.h"
#include "mm3d_title_contracts.h"
#include "../gameplay/mm3d_actor_interactions.h"
#include "../gameplay/mm3d_rumble_events.h"
#include "../gameplay/mm3d_gameplay_options.h"
#include "../gameplay/mm3d_light_arena.h"
#include "../gameplay/mm3d_actor_visibility.h"
#include "../gameplay/mm3d_foliage_distance.h"
#include "../dekui/hud_adjustments.h"
#include "oot3d_dev_warp.h"
#include "oot3d_native_a32_process.h"
#include "oot3d_native_a32_registry.h"
#include "oot3d_native_a32_scene_view.h"
#include "oot3d_native_pica_draw_state.h"
#include "mm3d_photo_camera_hook.h"
#include "mm3d_presentation_settings.h"
#include "mm3d_timing_observer.h"
#include "mm3d_frame_pacing.h"
#include "mm3d_ui_subsystems.h"
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "mm3d_dev_tools.h"
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

class Mm3dTitleSession final : public TitleSession, public Oot3dPicaCommandListCompositionProvider {
public:
  const char *GameName() const override { return "Majora's Mask 3D"; }
  Oot3dPicaCommandListCompositionProvider *CompositionProvider() override { return this; }
  bool TakeCommandListCompositionSpans(uint32_t address,uint32_t bytes,
      std::vector<Oot3dPicaCommandListCompositionSpan>& spans,std::string* error) override {
    if(!mComposition.TakeCommandListCompositionSpans(address,bytes,spans,error))return false;
    if(mSingleScreen)mSingleScreen->SubmitButtonOverlayCommandList(address,bytes);
    return true;
  }
  bool NativeFragmentLighting() const override { return true; }
  std::string_view CompanionLightName() const override { return "Tatl"; }
  // dekui's settings (Restoration's options and the damage multiplier
  // among them) travel with "state + settings", as TopScreen's do for OoT3D.
  std::optional<nlohmann::json> CaptureSavestateTitleSettings() const override {
    auto settings = nlohmann::json::parse(Mm3dSingleScreen::ActiveSettingsJson(), nullptr, false);
    if (!settings.is_object()) return std::nullopt;
    return settings;
  }
  bool ApplySavestateTitleSettings(const nlohmann::json& settings, std::string* error) override {
    return Mm3dSingleScreen::ApplySettingsJson(settings.dump(), error);
  }
  bool HasButtonOverlay() const override { return true; }
  void ConfigureButtonOverlay(const NativeControlConfig& config, NativePromptSet prompts) override {
    if (mSingleScreen) mSingleScreen->ConfigureButtonOverlay(config, prompts);
  }
  void ObserveButtonOverlayDraw(uint64_t draw) override { if (mSingleScreen) mSingleScreen->ObserveButtonOverlayDraw(draw); }
  void SelectButtonOverlay(std::span<const uint64_t> draws) override { if (mSingleScreen) mSingleScreen->SelectButtonOverlay(draws); }
  std::vector<oot3d::ui::UiPrimitive> ButtonOverlay(float scale, bool gameplayHudVisible) const override {
    return mSingleScreen ? mSingleScreen->ButtonOverlay(scale, gameplayHudVisible) : std::vector<oot3d::ui::UiPrimitive>{};
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
  // No MM3D shader corpus yet: no vertex-program family in the built-in uber
  // set, no fragment-family pack, no scenario corpus or Forge preparation
  // (PARITY_PLAN P7.1/P7.2). Return true once those exist: that restores
  // always-available programs for MM3D (its startup program family must then
  // be prepared by the window session next to OoT3D's).
  bool PreparedShaders() const override { return false; }
  // MM3D's own pipeline cache files, apart from OoT3D's.
  std::string_view PipelineCacheNamespace() const override { return "mm3d"; }

  bool IsGameplayHud(uint32_t sourcePc, uint32_t nativeValue) const override {
    return IsMm3dGameplayHud(sourcePc, nativeValue);
  }
  bool OwnsSceneProjection() const override { return true; }
  // dekui draws the single-screen minimap by rewriting the native quad
  // queue (native_minimap.h); photo Hide HUD drops it there.
  void SetGameplayHudVisible(bool visible) override {
    Mm3dDekui::HudAdjustments::photo_hud_hidden.store(!visible);
  }
  void SetPresentation(const TitlePresentationSettings& settings) override {
    mPresentationSettings=settings;
    ApplyMm3dPresentationSettings(settings);
    mLightArena.SetEnabled(settings.ExtendedDrawDistance);
    mVisibility.SetExtended(settings.ExtendedDrawDistance);
    mFoliage.SetExtended(settings.ExtendedDrawDistance);
  }
  std::vector<ThreeDsRecomp::Input::RumblePulse> RumblePulses(
      oot3d::recomp::a32::MemoryBus& memory, bool enabled) override {
    return mRumble.Poll(memory, enabled);
  }
  // The host's one gameplay settings store (F1 and Esc share it): the rows
  // MM3D implements reach the title-owned sites; the rest stay inert.
  void SetGameplayOptions(const TitleGameplayOptions& options) override {
    mGameplay.Set({options.QuickPutAway, options.RollDash});
  }
  [[nodiscard]] TitleGameplayOptionSet SupportedGameplayOptions() const override {
    return {.QuickPutAway = true, .RollDash = true};
  }

  void InstallHooks(TitleSessionHookSetup &setup) override {
    // MM3D's perspective frustum builder: the scene widens to the output
    // and takes the scene FOV. Its frusta also serve views other than the
    // scene camera, so they are not published as the scene view.
    auto &blockEntryHooks = setup.BlockEntryHooks;
    blockEntryHooks.push_back(kMm3dMtx44Frustum);
    for (const auto& site : Mm3dVisibility::Sites) blockEntryHooks.push_back(site.Pc);
    for (const auto& site : Mm3dFoliage::Sites) blockEntryHooks.push_back(site.Pc);
    for (const auto& site : Mm3dTiming::Sites) blockEntryHooks.push_back(site.Pc);
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
      mSingleScreen->SetQuadQueueObserver([this](auto moves, auto& memory) {
        mComposition.CommitQuadQueueMoves(moves, memory);
      });
      mSingleScreen->Register(mGuestPatches);
      RegisterMm3dLightingHooks(mGuestPatches);
      RegisterMm3dPhotoCamera(mGuestPatches, mPhotoCamera, setup.FreeCamera);
      mGameplay.Register(mGuestPatches);
      mGuestCalls.Install(mGuestPatches);
      mRestoration.emplace(mGuestCalls);
      mRestoration->Register(mGuestPatches);
      mSingleScreen->BindRestoration(*mRestoration);
      auto edits = setup.MessageEditsPath;
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
      if (edits.empty()) {
        if (const char *legacy = std::getenv("TRIAEVUM_MM3D_MESSAGE_EDITS"))
          edits = std::filesystem::u8path(legacy);
      }
#endif
      if (!edits.empty()) {
        // Launcher-owned, user-derived data. Explicit arguments win over the
        // legacy developer override; state restore never changes this choice.
        const auto size = std::filesystem::file_size(edits);
        if (size > 64U * 1024U * 1024U)
          throw std::runtime_error("MM3D message edits exceed the preparation limit");
        std::ifstream stream(edits, std::ios::binary);
        std::string text(static_cast<size_t>(size), '\0');
        stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        const bool complete = stream && stream.peek() == std::char_traits<char>::eof();
        std::string error;
        const auto parsed = Mm3dRestorationPort::ReadMessageEdits(text, &error);
        if (!complete || !parsed.has_value() || !mRestoration->SetMessageEdits(*parsed)) {
          throw std::runtime_error(std::string("MM3D message edits: ") + edits.string() + " is invalid " + error);
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
      if (mSingleScreen) {
        const auto& registry=Oot3dNativeA32Registry();
        for (const auto pc:{Mm3dDekui::ButtonEmission::Pc,Mm3dDekui::ButtonEmission::Pc+4,
                            Mm3dDekui::ButtonGlyphEmission::Pc})
          if (!oot3d::recomp::a32::FindBlock(registry,pc))
            throw std::runtime_error("MM3D glyph emission: missing A32 hook/continuation boundary; regenerate from --block-boundaries");
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
    const bool visibility = mVisibility.Verify(process.Memory(), [](uint32_t pc) {
      return oot3d::recomp::a32::FindBlock(Oot3dNativeA32Registry(), pc) != nullptr;
    });
    Mm3dDekui::ActorDrawDistance::result_policy_owned.store(visibility);
    if (!visibility) std::fprintf(stderr, "MM3D actor visibility disabled: guard or registry block mismatch\n");
    if (!mFoliage.Verify(process.Memory(), [](uint32_t pc) {
          return oot3d::recomp::a32::FindBlock(Oot3dNativeA32Registry(), pc) != nullptr;
        })) std::fprintf(stderr, "MM3D foliage distance disabled: guard or registry block mismatch\n");
    if (!mTiming.Verify(process.Memory(), [](uint32_t pc) {
          return oot3d::recomp::a32::FindBlock(Oot3dNativeA32Registry(), pc) != nullptr;
        })) std::fprintf(stderr, "MM3D timing receipts disabled: guard or registry block mismatch\n");
    if (!mFramePacing.Verify(process.Memory(), [](uint32_t pc) {
          return oot3d::recomp::a32::FindBlock(Oot3dNativeA32Registry(), pc) != nullptr;
        })) std::fprintf(stderr, "MM3D frame pacing disabled: EUR guard or registry mismatch\n");
    mHostMemory = &process.Memory();
    if (!mLightArena.Verify(process.Memory()))
      std::fprintf(stderr, "MM3D light overflow disabled: EUR arena contract mismatch\n");
    mRumble.Verify(process.Memory());
    if (!mRumble.Verified()) std::fprintf(stderr, "MM3D rumble disabled: EUR guard mismatch\n");
    mActorInteractionGuards = Mm3dActorInteractionGuardsHold(process.Memory());
    if (!mActorInteractionGuards)
      std::fprintf(stderr, "MM3D actor interactions disabled: EUR collider guard words do not match\n");
    mImageMounted = true;
  }

  void ObserveBlockEntry(TitleBlockEntry &entry) override {
    mTiming.Observe(entry.Pc, entry.State, entry.Memory);
    mFramePacing.Observe(entry.Pc, entry.State, entry.Memory);
    mVisibility.Observe(entry.Pc, entry.State, entry.Memory, mLightArena.Verified());
    mFoliage.Observe(entry.Pc, entry.State, entry.Memory);
    // Same allocator PC, one dispatch: relocate BEFORE lighting invalidates the
    // destination owner. Never duplicate the existing lighting hook registration.
    if (entry.Pc == kMm3dLightAllocatorSite.Pc && mHostMemory) {
      const auto result = mLightArena.BeforeAllocation(entry.Memory, entry.State.r[1],
          [this](bool create) { return EnsureLightOverflow(create); });
      using R = Mm3dLightArena::Result;
      if (result == R::Unavailable || result == R::Exhausted)
        throw std::runtime_error("MM3D light arena: cannot allocate a distinct overflow record");
    }
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
      // The stereo frustum only proves the native pass boundary. Decode the
      // actual fovy/aspect projection at View+BC, which already includes the
      // renderer multiplier, picture shape and scripted 49 base. View+18 is
      // the game's animated FOV, not the presented FOV.
      const auto &frustum = *entry.PendingSceneFrustum;
      uint32_t currentView = 0;
      bool read = memory.Read32(kMm3dCurrentViewPointer, &currentView);
      const uint32_t base = currentView != 0U ? currentView : kMm3dDefaultViewAddress;
      const auto readFloat = [&](uint32_t address, float &value) {
        uint32_t word = 0;
        read = read && memory.Read32(address, &word);
        value = std::bit_cast<float>(word);
      };
      float view[3] = {}, projectionY = 0;
      std::array<float, 3> eye{}, at{};
      readFloat(base + 0xBCU, view[0]);
      readFloat(base + 0xD0U, projectionY);
      readFloat(base + kMm3dViewNearOffset, view[1]);
      readFloat(base + kMm3dViewFarOffset, view[2]);
      for (uint32_t axis = 0; axis < 3U; ++axis) {
        readFloat(base + kMm3dViewEyeOffset + axis * 4U, eye[axis]);
        readFloat(base + kMm3dViewAtOffset + axis * 4U, at[axis]);
      }
      const auto presented = DecodeSymmetricPerspective(view[0], projectionY, view[1], view[2]);
      if (read && presented) {
        uint32_t nativeFov=0;
        if(memory.Read32(base+0x18,&nativeFov))
          mProjectionHistory.Record(mPresentationSettings,entry.HostFrame,std::bit_cast<float>(nativeFov),*presented);
        const float top = presented->Top;
        const float right = presented->Right;
        if (currentView != 0U) {
          entry.SceneView->ObservePerspectiveCamera(
              frustum.Pc, frustum.ReturnAddress, -right, right, -top, top,
              view[1], view[2], eye, at);
        } else {
          entry.SceneView->ObservePerspective(
              frustum.Pc, frustum.ReturnAddress, -right, right, -top, top,
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

  std::optional<bool> PhotoCameraOwnsInput(oot3d::recomp::a32::MemoryBus& memory) const override {
    GuestByteAccess guest(memory);
    return mPhotoCamera.OwnsInput(guest);
  }
  void Reset() override {
    if (const char* audit = std::getenv("TRIAEVUM_MM3D_COMPOSITION_AUDIT"); audit && std::string_view(audit) == "1")
      std::fprintf(stderr, "TRIAEVUM_MM3D_COMPOSITION_RESET %s
", CompositionSummary().dump().c_str());
    mComposition.Reset();
    mRumble.Reset();
    mPhotoCamera.Reset();
    mVisibility.Reset();
    mProjectionHistory.Reset();
    mTiming.Reset();
    mFramePacing.Reset();
    // dekui's UI/camera host state belongs to the abandoned timeline.
    if (mSingleScreen) mSingleScreen->ResetHostState();
    // Restoration's suspended guest calls too (the restored guest threads
    // never return to them); the save waits for them to finish.
    mGuestCalls.Reset();
    mGuestPatches.ResetPending();
    if (mSingleScreen) mSingleScreen->ResetButtonOverlay();
  }
  bool SavestateCapturePending() const override { return mGuestCalls.Active() != 0U; }

  nlohmann::json CompositionSummary() const override {
    const auto &stats = mComposition.Stats();
    const auto &photo = mPhotoCamera.Stats();
    const auto &pacing = mFramePacing.Stats();
    auto projections=nlohmann::json::array();
    for(unsigned i=0;i<mProjectionHistory.Count();++i) {
      const auto& s=mProjectionHistory.Segments()[i];
      projections.push_back({{"first_frame",s.FirstFrame},{"last_frame",s.LastFrame},{"samples",s.Samples},
          {"native_fov",s.NativeFov},{"presented_fov",s.PresentedFov},{"aspect",s.Aspect},
          {"fov_multiplier",s.Settings.FovMultiplier},{"horizontal_expansion",s.Settings.HorizontalExpansion},
          {"vertical_expansion",s.Settings.VerticalExpansion},{"edd",s.Settings.ExtendedDrawDistance}});
    }
    return nlohmann::json{
        {"frame_pacing", {{"verified",mFramePacing.Verified()}, {"simulation_hz",30},
            {"native_interval_writes",0}, {"begins",pacing.FramePacingFramesObserved},
            {"decisions",pacing.FramePacingDecisionsObserved}, {"commits",pacing.FramePacingCommitsObserved},
            {"interval_writes",pacing.FramePacingWrites}, {"deadline_writes",pacing.FramePacingDeadlineWrites},
            {"read_failures",pacing.ReadFailures}, {"write_failures",pacing.WriteFailures},
            {"rejected",mFramePacing.Rejected}, {"unpaired",mFramePacing.Unpaired},
            {"resets",mFramePacing.Resets}, {"output_samples",mFramePacing.OutputSamples},
            {"output_violations",mFramePacing.OutputViolations}}},
        {"timing", {{"verified",mTiming.Verified()}, {"read_only",true},
            {"game_calls",mTiming.GameCalls}, {"game_samples",mTiming.GameSamples},
            {"player_calls",mTiming.PlayerCalls}, {"player_samples",mTiming.PlayerSamples},
            {"limiter_calls",mTiming.LimiterCalls}, {"limiter_samples",mTiming.LimiterSamples},
            {"commit_input_intervals",mTiming.CommitIntervals}, {"wait_decisions",mTiming.WaitDecisions},
            {"non_native30_samples",mTiming.NonNative30Samples}, {"read_failures",mTiming.ReadFailures},
            {"epoch_changes",mTiming.EpochChanges}, {"resets",mTiming.Resets},
            {"game",{{"valid",mTiming.Game.Valid},{"state",mTiming.Game.State},
                {"frame",mTiming.Game.Frame},{"main",mTiming.Game.Main},
                {"vsync",mTiming.Game.Vsync},{"interval",mTiming.Game.Interval}}},
            {"player",{{"valid",mTiming.Player.Valid},{"address",mTiming.Player.Player},
                {"play",mTiming.Player.Play},{"frame",mTiming.Player.Frame},
                {"form",mTiming.Player.Form},{"speed_xz",mTiming.Player.SpeedXZ},
                {"flags",mTiming.Player.Flags},{"focus_actor",mTiming.Player.FocusActor},
                {"ledge_type",mTiming.Player.Ledge&0xFF},{"ledge_timer",mTiming.Player.Ledge>>8}}},
            {"limiter",{{"valid",mTiming.Limiter.Valid},{"state",mTiming.Limiter.State},
                {"maximum",mTiming.Limiter.Maximum},{"interval",mTiming.Limiter.Interval},
                {"deadline",mTiming.Limiter.Deadline},{"vsync",mTiming.Limiter.Vsync},
                {"native_interval",mTiming.Limiter.Native}}}}},
        {"photo_camera", {{"calls",photo.Calls},{"active_calls",photo.ActiveCalls},
            {"inactive_calls",photo.InactiveCalls},{"rejected",photo.Rejected},{"applied",photo.Applied},
            {"restored",photo.Restored},{"rebased",photo.Rebased},{"resets",photo.Resets},
            {"write_failures",photo.WriteFailures},{"max_eye_distance",photo.MaxEyeDistance},
            {"max_player_distance",photo.MaxPlayerDistance},{"last_eye",photo.LastEye},
            {"last_native_eye",photo.LastNativeEye}}},
        {"layout_owners", {{"draws",stats.LayoutOwners},{"dropped",stats.LayoutOwnersDropped}}},
        {"scene_projections",std::move(projections)},
        {"scene_projection_segments_discarded",mProjectionHistory.DiscardedSegments()},
        {"foliage_distance", {{"verified", mFoliage.Verified()},
             {"table_changes", mFoliage.Stats.TableChanges},
             {"radius_changes", mFoliage.Stats.RadiusChanges},
             {"pressure_native", mFoliage.Stats.PressureNative},
             {"invalid", mFoliage.Stats.Invalid}}},
        {"actor_visibility", {{"verified", mVisibility.Verified()},
             {"calls", mVisibility.mStats.Calls}, {"results", mVisibility.mStats.Results},
             {"native_inside", mVisibility.mStats.NativeInside},
             {"native_mode_results", mVisibility.mStats.NativeModeResults},
             {"extended_mode_results", mVisibility.mStats.ExtendedModeResults},
             {"max_horizontal", mVisibility.mStats.MaxHorizontal},
             {"max_vertical", mVisibility.mStats.MaxVertical},
             {"actor_grants", mVisibility.mStats.ActorGrants},
             {"grass_grants", mVisibility.mStats.GrassGrants},
             {"wood_grants", mVisibility.mStats.WoodGrants},
             {"restored", mVisibility.mStats.Restored},
             {"invalid", mVisibility.mStats.Invalid},
             {"native_mismatch", mVisibility.mStats.NativeMismatch},
             {"unknown_callers", mVisibility.mStats.UnknownCallers},
             {"identity_changes", mVisibility.mStats.IdentityChanges}}},
        {"light_arena", {{"verified", mLightArena.Verified()},
                         {"native_allocations", mLightArena.Stats().NativeAllocations},
                         {"banks_seen", mLightArena.Stats().BanksSeen},
                         {"overflow_allocations", mLightArena.Stats().OverflowAllocations},
                         {"relocations", mLightArena.Stats().Relocations},
                         {"invalid", mLightArena.Stats().Invalid},
                         {"unavailable", mLightArena.Stats().Unavailable},
                         {"exhausted", mLightArena.Stats().Exhausted}}},
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
        {"atmosphere_quad_spans", stats.AtmosphereQuadSpans},
        {"ui_quad_spans", stats.UiQuadSpans},
        {"quad_queue_moves", stats.QuadQueueMoves},
        {"native_ui_quad_moves", stats.NativeUiQuadMoves},
        {"unknown_quad_spans", stats.UnknownQuadSpans},
        {"invalid_quad_owners", stats.InvalidQuadOwners},
        {"primitive_caller_entries", stats.PrimitiveCallerEntries},
        {"primitive_entries", stats.PrimitiveEntries},
        {"primitive_spans", stats.PrimitiveSpans},
        {"temporal_primitive_spans", stats.TemporalPrimitiveSpans},
        {"unowned_primitives", stats.UnownedPrimitives},
        {"invalid_primitives", stats.InvalidPrimitives},
        {"temporal_model_spans", stats.TemporalModelSpans},
        {"isolated_model_spans", stats.IsolatedModelSpans},
        {"atmosphere_model_spans", stats.AtmosphereModelSpans},
        {"lifecycle_invalidations", stats.LifecycleInvalidations},
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
  bool EnsureLightOverflow(bool create) {
    using A = Mm3dLightArena;
    auto& memory = *mHostMemory;
    if (!memory.IsMapped(A::MappingBase, A::MappingSize)) {
      // MapRegion rejects ANY overlap, including partial mappings. Do not unmap
      // or appropriate another subsystem's range on failure.
      if (!create || !memory.MapRegion({"mm3d_light_overflow", A::MappingBase,
                                        A::MappingSize, true, false, {}})) return false;
      if (!memory.Write32(A::MappingBase, A::Magic0) ||
          !memory.Write32(A::MappingBase + 4, A::Magic1)) return false;
    }
    // Revalidate after savestate restore; host-side booleans are not ownership.
    bool exact = false;
    for (const auto& region : memory.RegionInfos())
      if (region.BaseAddress == A::MappingBase && region.Size == A::MappingSize &&
          region.Writable && !region.Executable) exact = true;
    uint32_t magic0=0, magic1=0;
    return exact && memory.Read32(A::MappingBase,&magic0) && magic0==A::Magic0 &&
           memory.Read32(A::MappingBase+4,&magic1) && magic1==A::Magic1;
  }
  NativeA32Memory* mHostMemory = nullptr;
  Mm3dLightArena mLightArena;
  Mm3dVisibility::Source mVisibility;
  Mm3dFoliage::Source mFoliage;
  TitlePresentationSettings mPresentationSettings;
  PresentedProjectionHistory mProjectionHistory;
  Mm3dNativePicaCompositionTracker mComposition;
  // MM3D's single-screen mode: its settings and the typed replacements of
  // guest instructions that read them.
  std::optional<Mm3dSingleScreen> mSingleScreen;
  Mm3dGuestCalls mGuestCalls;
  std::optional<Mm3dRestorationPort> mRestoration;
  Mm3dGuestPatches mGuestPatches;
  Mm3dRumble::Source mRumble;
  Mm3dPhotoCamera mPhotoCamera;
  Mm3dGameplay::GameplayOptions mGameplay;
  Mm3dTiming::Observer mTiming;
  Mm3dTiming::FramePacing mFramePacing;
  bool mImageMounted = false;
  bool mActorInteractionGuards = false;
};

} // namespace

std::unique_ptr<TitleSession> CreateMm3dTitleSession() {
  return std::make_unique<Mm3dTitleSession>();
}

} // namespace Oot3dNativeGame
