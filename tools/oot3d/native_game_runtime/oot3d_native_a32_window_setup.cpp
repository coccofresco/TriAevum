// Native A32 window session: setup (process image, host services, renderer, UI, initial state).
#include "oot3d_native_a32_window_session.h"
#include "oot3d_native_a32_romfs_overlay.h"

namespace Oot3dA32Window {

namespace {

std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion>
BuildPicaMemoryRegions(
    const Oot3dNativeGame::NativeA32ProcessImageManifest &manifest) {
  std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion> regions{
      {0x20000000U, manifest.LinearHeapBaseAddress, manifest.LinearHeapSize}};
  for (const auto &region : manifest.SystemRegions) {
    if (region.Name == "ctr_vram") {
      regions.push_back({0x18000000U, region.Address, region.MappedSize});
      break;
    }
  }
  return regions;
}

} // namespace

void NativeA32WindowSession::Setup() {
  if (launch.Host.Renderer != "opengl" &&
      launch.Host.Renderer != "nri" &&
      launch.Host.Renderer != "vulkan") {
    throw std::runtime_error(
        "the native process requires the OpenGL or NRI/Vulkan backend");
  }

  topScreenConfigRuntime =
      std::make_shared<Oot3dNativeGame::TopScreenUiConfigRuntime>(
          launch.TopScreenConfigPath, launch.TopScreenConfig);
  controlConfigRuntime =
      std::make_shared<Oot3dNativeGame::NativeControlConfigRuntime>(
          launch.ControlConfigPath, launch.ControlConfig);
  activeTopScreenConfig =
      topScreenConfigRuntime->Snapshot().Config;
#if defined(__SWITCH__)
  gSwitchUiProfile = Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile);
  gSwitchGameplayTiming =
      Oot3dNativeGame::GameplayTimingModeName(launch.GameplayTiming);
  gSwitchControlProfile = Oot3dNativeGame::NativeControlProfileName(
      controlConfigRuntime->Snapshot().Config.Profile);
  gSwitchPresentationRateHz = launch.PresentationRateHz;
  gSwitchWholeAotBlockBudget = launch.WholeAotBlockBudget;
  gSwitchPicaGeometryCacheEnabled =
      !launch.DisableOpenGlPicaGeometryCache;
#endif

  manifest = Oot3dNativeGame::LoadNativeA32ProcessImageManifest(
      launch.A32ProcessManifestPath, &error);
  if (!manifest.has_value()) {
    throw std::runtime_error(error);
  }
  titleAdapter = Oot3dNativeGame::ResolveTitleAdapter(manifest->CodeBinSha256);
  if (titleAdapter != Oot3dNativeGame::TitleAdapter::Oot3dEur &&
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    throw std::runtime_error(
        "the TopScreen UI profile belongs to OoT3D EUR; run this title with --ui-profile oot3d");
  }
  std::cerr << "TRIAEVUM_TITLE_ADAPTER " << Oot3dNativeGame::TitleAdapterName(titleAdapter)
            << " code_sha256=" << manifest->CodeBinSha256 << '\n';
#if defined(__SWITCH__)
  WriteSwitchBootStage("manifest_loaded");
#endif

  if (!launch.ScenarioId.empty()) {
    scenarioCatalog =
        Oot3dNativeGame::LoadNativeScenarioCatalog(launch.ScenarioCatalogPath);
    const auto *recipe = Oot3dNativeGame::FindNativeScenarioRecipe(
        *scenarioCatalog, launch.ScenarioId);
    if (recipe == nullptr) {
      throw std::runtime_error("unknown structural scenario: " +
                               launch.ScenarioId);
    }
    scenarioBootstrap.emplace(scenarioCatalog->Runtime, *recipe);
    std::cout << "oot3d_native_game: structural scenario " << recipe->Id
              << " selects " << recipe->ScenePath << " entrance "
              << recipe->EntranceIndex << '\n';
  }

  if (!launch.TopScreenTextureOverridePackPath.empty()) {
    const auto fontPath = launch.TopScreenTextureOverridePackPath.parent_path() / "font_coverage.zip";
    if (std::filesystem::is_regular_file(fontPath)) {
      hdFontsLoaded = hdFonts.Load(fontPath, &error);
      if (!hdFontsLoaded) std::cerr << "HD font coverage unavailable; re-run Forge: " << error << '\n';
    }
  }
  if (!launch.TopScreenTextureOverridePackPath.empty()) {
    if (!topScreenTextureOverridePack.Load(
            launch.TopScreenTextureOverridePackPath, &error)) {
      throw std::runtime_error("cannot load TopScreen texture overrides: " +
                               error);
    }
    topScreenTextureOverridePackPointer = &topScreenTextureOverridePack;
#if defined(__SWITCH__)
    gSwitchTopScreen211Assets =
        topScreenTextureOverridePack.FindProfileTexture(
            Oot3dNativeGame::kTopScreen211MenuAtlasSemantic) != nullptr &&
        topScreenTextureOverridePack.FindProfileTexture(
            "oot3d/topscreen/2.1.1/font_atlas") != nullptr;
#endif
  }

  picaFrontend.SetDiagnosticHistoryEnabled(false);
  picaCompositionDomain =
      Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
  languageSettings = std::make_shared<Oot3dNativeGame::GameLanguageSettings>(
      std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "game_language.json",
      Oot3dNativeGame::DetectGameLanguages(manifest->RomFsImagePath,
          manifest->RomFsImageOffset, manifest->RomFsImageSize, titleAdapter));
  hostConfig.SystemLanguage = languageSettings->SystemId();
  hostConfig.ResourceLimitValues = manifest->ResourceLimitValues;
  hostConfig.ResourceCurrentValues = manifest->ResourceCurrentValues;
  hostConfig.LinearHeapBaseAddress = manifest->LinearHeapBaseAddress;
  hostConfig.LinearHeapSize = manifest->LinearHeapSize;
  hostConfig.HeapBaseAddress = manifest->HeapBaseAddress;
  hostConfig.HeapSize = manifest->HeapSize;
  hostConfig.RomFsImagePath = manifest->RomFsImagePath;
  hostConfig.RomFsImageOffset = manifest->RomFsImageOffset;
  hostConfig.RomFsImageSize = manifest->RomFsImageSize;
  if (const char *overlay = std::getenv("TRIAEVUM_ROMFS_OVERLAY")) {
    // Development: user-supplied RomFS files laid over the title's (a
    // directory per layer mirroring RomFS paths, ';'-separated, later
    // layers winning), until the launcher owns the user's mod folders.
    std::vector<std::filesystem::path> layers;
    for (std::string_view rest = overlay; !rest.empty();) {
      const size_t separator = rest.find(';');
      if (const auto layer = rest.substr(0, separator); !layer.empty()) {
        layers.emplace_back(std::filesystem::u8path(layer));
      }
      rest = separator == std::string_view::npos ? std::string_view{} : rest.substr(separator + 1U);
    }
    std::string error;
    auto base = Oot3dNativeGame::OpenNativeA32HostFileRange(
        manifest->RomFsImagePath, manifest->RomFsImageOffset, manifest->RomFsImageSize, &error);
    auto built = base != nullptr
                     ? Oot3dNativeGame::BuildNativeA32RomFsOverlay(base, layers, &error)
                     : std::nullopt;
    if (!built.has_value()) {
      throw std::runtime_error("RomFS overlay: " + error);
    }
    hostConfig.RomFsOverlay = built->File;
    std::fprintf(stderr, "RomFS overlay: %zu layer(s), %zu file(s) replaced, %zu added, %zu directories added\n",
                 layers.size(), built->ReplacedFiles, built->AddedFiles, built->AddedDirectories);
  }
  hostConfig.PicaFrontend = &picaFrontend;
  hostConfig.PicaCompositionDomain = &picaCompositionDomain;
  hostConfig.PicaCompositionProvider =
      titleAdapter == Oot3dNativeGame::TitleAdapter::Mm3d
          ? static_cast<Oot3dNativeGame::Oot3dPicaCommandListCompositionProvider *>(
                &mm3dCompositionTracker)
          : &picaCompositionTracker;
  hostConfig.SaveDataDirectory =
      launch.SaveDataDirectory.empty()
          ? launch.A32ProcessManifestPath.parent_path() /
                "oot3d_native_savedata"
          : launch.SaveDataDirectory;
  saveDataDirectory = hostConfig.SaveDataDirectory;
  // Save states default to a folder beside the executable; --quick-state pins one file for scripted runs.
  savestateLibrary = std::make_shared<Oot3dNativeGame::SavestateLibrary>(
      std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "savestates.json",
      std::filesystem::path(std::u8string(
          reinterpret_cast<const char8_t *>(Ship::Context::GetAppBundlePath().c_str()))) /
          "savestates",
      launch.QuickStatePath);
  quickStatePath = launch.QuickStatePath.empty() ? savestateLibrary->Directory()
                                                 : launch.QuickStatePath;
  savestateCompatibility = Oot3dNativeGame::NativeA32SavestateCompatibility{
      manifest->CodeBinSha256,
      manifest->ProcessName,
      std::string(oot3d::recomp::kA32SourceSnapshotId),
  };
  hostConfig.DetailedSvcDiagnostics = launch.ExtendedDiagnostics;
  // Extended diagnostics keep every SVC event; runtime.json then lists the
  // last 4096 instead of 32 (bring-up of a title's boot sequence).
  hostConfig.RetainFullSvcHistory = launch.ExtendedDiagnostics;
  hostConfig.ProfileRuntime = launch.ProfileA32Runtime;
  auto &hostServices = hostServicesStorage.emplace(std::move(hostConfig));
  // Development: TRIAEVUM_IR_NO_EXTRA_HID=1 presents an Old 3DS without a
  // Circle Pad Pro (titles then run their accessory prompts).
  if (const char *noExtraHid = std::getenv("TRIAEVUM_IR_NO_EXTRA_HID");
      noExtraHid != nullptr && std::string_view(noExtraHid) == "1") {
    hostServices.SetIrExtraHidAttached(false);
  }
  // Development: TRIAEVUM_SWKBD_TEXT sets the text the system software
  // keyboard returns (ASCII; default "Link").
  if (const char *keyboardText = std::getenv("TRIAEVUM_SWKBD_TEXT");
      keyboardText != nullptr && *keyboardText != '\0') {
    std::u16string text;
    for (const char *character = keyboardText; *character != '\0'; ++character) {
      text.push_back(static_cast<char16_t>(static_cast<unsigned char>(*character)));
    }
    hostServices.SetSoftwareKeyboardText(std::move(text));
  }
  frameRateContract =
      Oot3dNativeGame::ResolveNativeFrameRateContract(
          launch.GameplayTiming, launch.PresentationRateHz);
  savestateCompatibility.GameplayTiming = frameRateContract.Mode;
#if defined(__SWITCH__)
  gSwitchVisualInterpolation = frameRateContract.VisualInterpolationAllowed;
#endif
  auto &frameRatePolicy = frameRatePolicyStorage.emplace(frameRateContract);
  widescreenProjection.HdFonts = hdFontsLoaded ? &hdFonts : nullptr;
  widescreenProjection.FrameRatePolicy = &frameRatePolicy;
  widescreenProjection.SceneViewProbe = &sceneViewProbe;
  widescreenProjection.PlayerTimingProbe = &playerTimingProbe;
  widescreenProjection.TemporalEventLedger = &temporalEventLedger;
  if (frameRateContract.Mode ==
      Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    widescreenProjection.PlayerTemporalBridge = &playerTemporalBridge;
  }
  widescreenProjection.CollectExtendedDiagnostics = launch.ExtendedDiagnostics;
  widescreenProjection.ProfileA32Blocks = launch.ProfileA32Blocks;
  widescreenProjection.TraceA32Blocks = !launch.A32BlockTracePath.empty();
  std::vector<uint32_t> callTraceEntries;
  if (const char *callTrace = std::getenv("TRIAEVUM_A32_CALL_TRACE")) {
    char path[512] = {};
    char entries[512] = {};
    unsigned first = 0, last = 0, word = 0;
    const int fields = std::sscanf(callTrace, "%511[^,],%u,%u,%x,%511[^,]", path,
                                   &first, &last, &word, entries);
    // Optional fifth field: a file of hex code addresses (function entries)
    // to observe as hooks, for code that runs compiled; without it, run with
    // --trace-a32-blocks.
    if (fields == 5) {
      std::ifstream list(entries);
      std::string line;
      while (std::getline(list, line))
        if (!line.empty()) callTraceEntries.push_back(static_cast<uint32_t>(std::stoul(line, nullptr, 16)));
    }
    if (fields >= 3) {
      widescreenProjection.CallTrace = std::fopen(path, "wb");
      widescreenProjection.CallTraceFirstFrame = first;
      widescreenProjection.CallTraceLastFrame = last;
      widescreenProjection.CallTraceWordAddress = word;
    }
    if (widescreenProjection.CallTrace == nullptr)
      throw std::runtime_error("TRIAEVUM_A32_CALL_TRACE: expected PATH,FIRST,LAST[,WORD]");
  }
  auto &blockTraceWriter = blockTraceWriterStorage.emplace(
      launch.A32BlockTracePath, widescreenProjection);
  blockEntryHooks = std::vector<uint32_t>{
      kMtx4x4BuildFrustum,
      kMtx4x4BuildOrthographicProjectionRotated,
      kGameStateUpdate,
      Oot3dNativeGame::kOot3dPlayerUpdateEntry,
      Oot3dNativeGame::kOot3dFramePacingBeginEntry,
      Oot3dNativeGame::kOot3dFramePacingDecisionEntry,
      Oot3dNativeGame::kOot3dFramePacingCommitEntry,
      Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry,
  };
  picaCompositionHooks =
      Oot3dNativeGame::Oot3dNativePicaCompositionHookPcs();
  blockEntryHooks.insert(blockEntryHooks.end(),
                         picaCompositionHooks.begin(),
                         picaCompositionHooks.end());
  if (widescreenProjection.PlayerTemporalBridge != nullptr) {
    const auto temporalHooks =
        Oot3dNativeGame::NativeA32PlayerTemporalHookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), temporalHooks.begin(),
                           temporalHooks.end());
  }
  typedGameplayObservableExitHooks =
      Oot3dNativeGame::Oot3dTypedGameplayObservableExitPoints();
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  blockEntryHooks.insert(blockEntryHooks.end(),
                         typedGameplayObservableExitHooks.begin(),
                         typedGameplayObservableExitHooks.end());
#endif
  const auto &uiLifecycleEntries =
      Oot3dNativeGame::Oot3dNativeUiLifecycleBridge::GuestEntryPoints();
  blockEntryHooks.insert(blockEntryHooks.end(), uiLifecycleEntries.begin(),
                         uiLifecycleEntries.end());
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    const std::array topScreenHooks{
        Oot3dNativeGame::kTopScreenQuestMaterializedHook,
        Oot3dNativeGame::kTopScreenQuestSubmitModelsHook,
        Oot3dNativeGame::kTopScreenQuestDrawHook,
        0x00300588U,
        0x002FEABCU,
        0x00311364U,
        kPauseProjectionPrepare,
        kPauseUiDraw,
        kPauseUiDrawReturn,
        kPauseRendererVisibilityNativeDraw,
        kPauseIconBuild};
    // Texture payloads are patched when the native pause owner has loaded
    // them, before its update can submit HUD or pause draws.
    blockEntryHooks.push_back(kPauseUiUpdate);
    blockEntryHooks.insert(blockEntryHooks.end(), topScreenHooks.begin(),
                           topScreenHooks.end());
    const std::array topScreenSourcePortHooks{
        kPauseTouchButtonsDrawCall,
        kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,
        kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,
        kPauseProjectionPrepareCall,
        kPauseAlternateViewportDrawCall,
        kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,
        kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,
        kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,
        kPauseSceneViewportDrawCall,
        kPauseTouchCoordinateUpdateCall,
        kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraFovScalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenCameraUpdatePatchSite,
        kTopScreenGameplayCompositionPrefix,
    };
    blockEntryHooks.insert(blockEntryHooks.end(),
                           topScreenSourcePortHooks.begin(),
                           topScreenSourcePortHooks.end());
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    const auto itemHooks = Oot3dNativeGame::TopScreenItemDispatchEntries();
    blockEntryHooks.insert(blockEntryHooks.end(), itemHooks.begin(), itemHooks.end());
    blockEntryHooks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
  }
  if (launch.ExtendedDiagnostics) {
    const std::array diagnosticHooks{
        kEnMagInit,
        kEnMagUpdate,
        kEnMagDraw,
        kPauseUiUpdate,
        kPauseUiDraw,
        kFileSelectUpdate,
        kFileSelectInit,
        kFileChooseInit,
        kPlayTransitionUpdate,
        kFileSelectActivate,
        kBlockingQueuePop,
        kBlockingQueuePush,
        kRendererCommandQueueProcessPending,
        kBlockingQueueTryPush,
        kTaskQueueTakeFirstFromPriority,
        kTaskQueueEnqueuePriorityNode,
        kTaskQueueWorkerDrainPriorityNodes,
        kNngxP3dCallback,
        kNngxCommonInterruptHandler,
        kNngxCmdlistCompletionDispatch,
        kRendererGlobalCallbackAfterLookup,
    };
    blockEntryHooks.insert(blockEntryHooks.end(), diagnosticHooks.begin(),
                           diagnosticHooks.end());
  }
  std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
  if (hdFontsLoaded) blockEntryHooks.push_back(Oot3dNativeGame::kNativeQbfA4Blit);
  std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
  blockEntryHooks.erase(
      std::unique(blockEntryHooks.begin(), blockEntryHooks.end()),
      blockEntryHooks.end());
  if (titleAdapter != Oot3dNativeGame::TitleAdapter::Oot3dEur) {
    // Every hook above names OoT3D EUR code: another title has none.
    blockEntryHooks.clear();
    picaCompositionHooks = {};
    typedGameplayObservableExitHooks = {};
  }
  if (titleAdapter == Oot3dNativeGame::TitleAdapter::Mm3d) {
    // MM3D's perspective frustum builder: the scene widens to the output
    // and takes the scene FOV. Its frusta also serve views other than the
    // scene camera, so they are not published as the scene view.
    blockEntryHooks.push_back(Oot3dNativeGame::kMm3dMtx44Frustum);
    const auto compositionHooks = Oot3dNativeGame::Mm3dNativePicaCompositionHookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), compositionHooks.begin(),
                           compositionHooks.end());
    std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
    blockEntryHooks.erase(std::unique(blockEntryHooks.begin(), blockEntryHooks.end()),
                          blockEntryHooks.end());
    widescreenProjection.Projection.PerspectiveFrustumEntry =
        Oot3dNativeGame::kMm3dMtx44Frustum;
    widescreenProjection.Projection.FrustumIsSceneView = false;
    widescreenProjection.Mm3dCompositionTracker = &mm3dCompositionTracker;
    if (mm3dGuestPatches.Empty()) {
      // dekui's settings: its shipped defaults, or (development) a
      // presentation.json given by TRIAEVUM_MM3D_PRESENTATION, until the F1
      // menu owns and persists them.
      const char *presentation = std::getenv("TRIAEVUM_MM3D_PRESENTATION");
      if (!Oot3dNativeGame::Mm3dSingleScreen::LoadSettings(
              presentation != nullptr ? std::filesystem::path(presentation) : std::filesystem::path{}))
        std::fprintf(stderr, "MM3D single-screen: %s is unreadable, using dekui's defaults\n",
                     presentation != nullptr ? presentation : "dekui's settings");
      if (const char *multiplier = std::getenv("TRIAEVUM_MM3D_DAMAGE_MULTIPLIER")) {
        // Development: dekui's damage multiplier until the F1 owns it.
        Oot3dNativeGame::Mm3dSingleScreen::SetDamageMultiplier(std::atoi(multiplier));
      }
      // dekui animates with the emulated clock: here the deterministic
      // presentation clock (a 60 Hz frame each).
      mm3dSingleScreen.emplace([this] {
        return static_cast<std::int64_t>(presentationFrameCount) * 1000000 / 60;
      });
      mm3dSingleScreen->Register(mm3dGuestPatches);
      mm3dGuestCalls.Install(mm3dGuestPatches);
      mm3dRestoration.emplace(mm3dGuestCalls);
      mm3dRestoration->Register(mm3dGuestPatches);
      mm3dSingleScreen->BindRestoration(*mm3dRestoration);
    }
  }
  if (const char *pcDump = std::getenv("TRIAEVUM_A32_PC_DUMP")) {
    // PC,FRAME,DIR,ADDR:SIZE[;ADDR:SIZE...]
    const std::string spec(pcDump);
    const auto first = spec.find(',');
    const auto second = first == std::string::npos ? first : spec.find(',', first + 1);
    const auto third = second == std::string::npos ? second : spec.find(',', second + 1);
    if (third == std::string::npos)
      throw std::runtime_error("TRIAEVUM_A32_PC_DUMP: expected PC,FRAME,DIR,ADDR:SIZE[;...]");
    widescreenProjection.PcDumpAddress =
        static_cast<uint32_t>(std::stoul(spec.substr(0, first), nullptr, 16));
    widescreenProjection.PcDumpFrame =
        static_cast<uint32_t>(std::stoul(spec.substr(first + 1, second - first - 1)));
    widescreenProjection.PcDumpDirectory = spec.substr(second + 1, third - second - 1);
    std::string ranges = spec.substr(third + 1);
    for (std::size_t start = 0; start <= ranges.size();) {
      const auto end = std::min(ranges.find(';', start), ranges.size());
      const auto range = ranges.substr(start, end - start);
      const auto colon = range.find(':');
      if (colon == std::string::npos)
        throw std::runtime_error("TRIAEVUM_A32_PC_DUMP: range without ADDR:SIZE");
      widescreenProjection.PcDumpRanges.emplace_back(
          static_cast<uint32_t>(std::stoul(range.substr(0, colon), nullptr, 16)),
          static_cast<uint32_t>(std::stoul(range.substr(colon + 1), nullptr, 16)));
      start = end + 1;
    }
    widescreenProjection.PcDumpDone = false;
    callTraceEntries.push_back(widescreenProjection.PcDumpAddress);
  }
  if (!callTraceEntries.empty()) {
    blockEntryHooks.insert(blockEntryHooks.end(), callTraceEntries.begin(), callTraceEntries.end());
    std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
    blockEntryHooks.erase(std::unique(blockEntryHooks.begin(), blockEntryHooks.end()),
                          blockEntryHooks.end());
  }
#if defined(__SWITCH__)
  gSwitchBlockHookCount = blockEntryHooks.size();
  gSwitchBlockHooksSorted =
      std::is_sorted(blockEntryHooks.begin(), blockEntryHooks.end());
  gSwitchBlockHooksContainProcessEntry = std::binary_search(
      blockEntryHooks.begin(), blockEntryHooks.end(), 0x00100000U);
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  enableCompiledFunctions = true;
#else
  enableCompiledFunctions = !launch.DisableCompiledFunctions;
#endif
  wholeAotAvailable =
      Oot3dNativeGame::Oot3dWholeAotAvailable();
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (!wholeAotAvailable) {
    throw std::runtime_error(
        "whole-AOT product mode requires the audited generated backend");
  }
  enableWholeAot = true;
#else
  enableWholeAot = enableCompiledFunctions &&
                              !launch.DisableWholeAot &&
                              wholeAotAvailable;
#endif
  if (enableCompiledFunctions && !launch.DisableWholeAot &&
      !wholeAotAvailable) {
    std::cerr
        << "Warning: whole-AOT was requested but is not linked. "
           "Guest execution will use substantially slower fallback paths.\n";
  }
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  enableMassAot = false;
#else
  enableMassAot = enableCompiledFunctions &&
                             !launch.DisableMassAot &&
                             Oot3dNativeGame::Oot3dMassAotAvailable();
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  enableTrueAot = false;
#else
  enableTrueAot =
      enableCompiledFunctions && !launch.DisableTrueAotBlocks;
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  enableManualCompiledFunctions = true;
#else
  enableManualCompiledFunctions =
      !launch.DisableManualCompiledFunctions;
#endif
  const bool oot3dAdapter = titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur;
  if (!oot3dAdapter) {
    // Hand-written replacements, typed gameplay and the mass/true-AOT
    // block sets are OoT3D code; another title runs its own whole AOT.
    enableManualCompiledFunctions = false;
    enableMassAot = false;
    enableTrueAot = false;
  }
  trueAotEntries = oot3dAdapter ? Oot3dNativeGame::Oot3dTrueAotBlockEntryPoints()
                                : std::span<const uint32_t>{};
  compiledFunctionEntries =
      Oot3dNativeGame::Oot3dCompiledFunctionEntryPoints(
          enableWholeAot,
          enableCompiledFunctions && enableManualCompiledFunctions);
  typedGameplayEntries = oot3dAdapter ? Oot3dNativeGame::Oot3dTypedGameplayEntryPoints()
                                      : std::span<const uint32_t>{};
  if (enableMassAot) {
    const auto massAotEntries = Oot3dNativeGame::Oot3dMassAotBlockEntryPoints();
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  massAotEntries.begin(), massAotEntries.end());
    Oot3dNativeGame::ResetOot3dMassAotStats();
  }
  if (enableCompiledFunctions) {
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  typedGameplayEntries.begin(),
                                  typedGameplayEntries.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  compiledFunctionEntries.begin(),
                                  compiledFunctionEntries.end());
    Oot3dNativeGame::ResetOot3dTypedGameplayStats();
    Oot3dNativeGame::ResetOot3dCompiledFunctionStats();
    Oot3dNativeGame::SetOot3dCompiledFunctionProfilingEnabled(
        launch.ProfileA32Runtime);
  }
  if (enableTrueAot) {
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  trueAotEntries.begin(), trueAotEntries.end());
    Oot3dNativeGame::ResetOot3dTrueAotBlockStats();
  }
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  wholeAotObservableExitBlocks.push_back(
      Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry);
  wholeAotObservableExitBlocks.insert(
      wholeAotObservableExitBlocks.end(), picaCompositionHooks.begin(),
      picaCompositionHooks.end());
#endif
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (enableCompiledFunctions && !launch.DisableTypedGameplay) {
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        typedGameplayObservableExitHooks.begin(),
        typedGameplayObservableExitHooks.end());
  }
#endif
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    topScreenSourcePortBlocks = {
        kPauseTouchButtonsDrawCall,       kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,           kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,         kPauseProjectionPrepareCall,
        kPauseAlternateViewportDrawCall,  kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,         kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,    kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,         kPauseSceneViewportDrawCall,
        kPauseTouchCoordinateUpdateCall,  kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,         kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,      kTopScreenCameraUpdatePatchSite,
        kTopScreenTitleLogoFadeHoldPrefix};
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    const auto itemEntries = Oot3dNativeGame::TopScreenItemDispatchEntries();
    topScreenSourcePortBlocks.insert(topScreenSourcePortBlocks.end(),
                                    itemEntries.begin(), itemEntries.end());
    topScreenSourcePortBlocks.push_back(kTopScreenGameplayCompositionPrefix);
    topScreenSourcePortBlocks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    // These source ports are reached from inside large AOT regions. Make
    // their native entries observable so execution returns to the dispatcher
    // before applying the typed replacement. Keep this list sorted because
    // the whole-AOT block-entry callback uses binary_search.
    const std::array topScreenObservableExitBlocks{
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenGameplayCompositionPrefix,
        kTopScreenTitleLogoFadeHoldPrefix,
    };
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        topScreenObservableExitBlocks.begin(),
        topScreenObservableExitBlocks.end());
#else
    wholeAotObservableExitBlocks.push_back(kTopScreenCameraUpdateEntry);
    wholeAotObservableExitBlocks.push_back(kTopScreenCameraNormal1Scalar);
#endif
    wholeAotObservableExitBlocks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    wholeAotObservableExitBlocks.push_back(kPauseRendererVisibilityNativeDraw);
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(),
                                       itemEntries.begin(), itemEntries.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  topScreenSourcePortBlocks.begin(),
                                  topScreenSourcePortBlocks.end());
  }
  std::sort(wholeAotObservableExitBlocks.begin(),
            wholeAotObservableExitBlocks.end());
  wholeAotObservableExitBlocks.erase(
      std::unique(wholeAotObservableExitBlocks.begin(),
                  wholeAotObservableExitBlocks.end()),
      wholeAotObservableExitBlocks.end());
  std::sort(nativeCandidateEntries.begin(), nativeCandidateEntries.end());
  nativeCandidateEntries.erase(
      std::unique(nativeCandidateEntries.begin(), nativeCandidateEntries.end()),
      nativeCandidateEntries.end());
  Oot3dNativeGame::ConfigureOot3dNativeA32Candidates(
      nativeCandidateEntries);
  auto &process = processStorage.emplace(
      Oot3dNativeGame::Oot3dNativeA32Registry(), hostServices);
  widescreenProjection.TraceMemory = &process.Memory();
  process.Memory().EnableWriteTraceFingerprint(
      widescreenProjection.TraceA32Blocks);
  auto &uiLifecycleBridge = uiLifecycleBridgeStorage.emplace(
      process.Memory());
  uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
  uiLifecycleBridge.SetVersionChoiceRevealRequired(
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen);
  widescreenProjection.UiLifecycleBridge = &uiLifecycleBridge;
  widescreenProjection.PicaCompositionDomain = &picaCompositionDomain;
  widescreenProjection.PicaCompositionTracker = &picaCompositionTracker;
  widescreenProjection.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  nativeBlockEntryPcs =
      launch.ProfileA32Blocks || widescreenProjection.TraceA32Blocks
          ? std::span<const uint32_t>{}
          : std::span<const uint32_t>{blockEntryHooks};
  nativeCandidateDispatch = NativeCandidateDispatchState{
      &ApplyNativeWidescreenProjectionPolicy,
      &widescreenProjection,
      nativeBlockEntryPcs,
      Oot3dNativeGame::BuildOot3dMassAotPcFilter(nativeBlockEntryPcs,
                                                 nativeBlockEntryPcs.empty()),
      Oot3dNativeGame::BuildOot3dMassAotPcFilter(
          wholeAotObservableExitBlocks, false),
      &process.Memory(),
      {static_cast<float>(frameRateContract.NativeUpdateRate),
       &frameRatePolicy.GameplayClock().Context()},
      enableCompiledFunctions && !launch.DisableTypedGameplay,
      enableMassAot,
      enableCompiledFunctions,
      enableWholeAot,
      enableManualCompiledFunctions,
      enableTrueAot,
      launch.ProfileA32Runtime,
  };
  nativeCandidateDispatch.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
  widescreenProjection.TopScreenInput =
      &nativeCandidateDispatch.TopScreenInput;
  widescreenProjection.TopScreenInputClock =
      &nativeCandidateDispatch.TopScreenInputClock;
  widescreenProjection.TopScreenItems = &nativeCandidateDispatch.TopScreenItems;
  nativeCandidateDispatch.TopScreenItems.TraceEnabled = launch.ExtendedDiagnostics;
  nativeCandidateDispatch.UiLifecycleBridge = &uiLifecycleBridge;
  nativeCandidateDispatch.PicaCompositionDomain = &picaCompositionDomain;
  nativeCandidateDispatch.TopScreenPauseProjection =
      &uiLifecycleBridge.TopScreenPauseProjection();
  nativeCandidateDispatch.TopScreenConfig = &activeTopScreenConfig;
  Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
      nativeCandidateDispatch.TopScreenCamera.Camera, activeTopScreenConfig);
  appliedTopScreenConfigRevision =
      topScreenConfigRuntime->Snapshot().Revision;
  widescreenProjection.TopScreenPauseTargetRoute =
      &nativeCandidateDispatch.TopScreenPauseRoute;
  widescreenProjection.TopScreenTouchCoordinateRoute =
      &nativeCandidateDispatch.TopScreenTouchCoordinateRoute;
  widescreenProjection.WholeAotObservableExitPcs =
      wholeAotObservableExitBlocks;
  process.SetTimingEnabled(launch.ProfileA32Runtime);
  process.SetManualCompiledFunctionsEnabled(enableManualCompiledFunctions);
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  // Native candidates are experimental reconstruction aids.  The product
  // executes the audited whole-AOT graph and never substitutes source-native
  // or typed gameplay implementations for guest functions.
  if (!nativeCandidateEntries.empty()) {
    process.SetNativeBlockCallback(&ExecuteNativeCandidate,
                                   &nativeCandidateDispatch);
  }
#else
  if (nativeCandidateDispatch.TopScreenUiProfile) {
    process.SetNativeBlockCallback(&ExecuteProductTopScreenHook,
                                   &nativeCandidateDispatch);
  }
#endif
  if (!Oot3dNativeGame::MountNativeA32ProcessImage(
          process, *manifest, std::filesystem::path{}, &error)) {
    throw std::runtime_error(error);
  }
  if (titleAdapter == Oot3dNativeGame::TitleAdapter::Mm3d) {
    uint32_t word = 0;
    if (!process.Memory().Read32(Oot3dNativeGame::kMm3dMtx44Frustum, &word) ||
        word != Oot3dNativeGame::kMm3dMtx44FrustumFirstWord) {
      throw std::runtime_error("MM3D adapter: the perspective frustum builder is not at its address");
    }
    if (!mm3dGuestPatches.Empty()) {
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
      throw std::runtime_error("MM3D adapter: guest patch sites need the A32 registry dispatch");
#else
      // Each site whose code matches the mounted image replaces its
      // instruction; the registry starts a block at every site.
      // dekui runs on the image Project Restoration patched: its data
      // patches first, then every site's check.
      if (mm3dRestoration.has_value()) {
        mm3dRestoration->ApplyDataPatches(process.Memory());
      }
      mm3dGuestPatches.Verify(process.Memory(), &Oot3dNativeGame::Oot3dNativeA32Registry());
      process.SetNativeFunctionCallback(&Oot3dNativeGame::Mm3dGuestPatches::Execute,
                                        &mm3dGuestPatches, mm3dGuestPatches.EnabledPcs());
#endif
    }
    widescreenProjection.Mm3dGuestPatches = &mm3dGuestPatches;
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("process_image_mounted");
#endif

  picaMemoryRegions = BuildPicaMemoryRegions(*manifest);
  const auto &picaMemoryView = picaMemoryViewStorage.emplace(
      process.Memory(), picaMemoryRegions);
  if (topScreenTextureOverridePackPointer != nullptr) {
    topScreenTextureOverrideRuntime.emplace(topScreenTextureOverridePack,
                                            picaMemoryView);
    widescreenProjection.TopScreenTextureOverrides =
        &*topScreenTextureOverrideRuntime;
  }
  auto &submissionQueue = submissionQueueStorage.emplace(
      picaMemoryView, true);
  // Generated title code links its own checked memory helpers. Only a module
  // advertising the observer contract can preserve GPU/CPU byte ownership.
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
  copyWritesObservable =
      Oot3dNativeGame::Oot3dWholeAotPluginObservesMemoryWrites();
#else
  copyWritesObservable = true;
#endif
  if (copyWritesObservable) {
    process.Memory().SetWriteObserver(
        [&](uint32_t address, size_t size) {
          if (!picaMemoryView.SynchronizeGuestWrite(process.Memory(), address, size))
            throw std::runtime_error("cannot make a guest write visible through its physical aliases");
          submissionQueue.NotifyGuestWrite(address, size);
        });
    submissionQueue.BindGuestWriteObservation(
        [&](uint32_t address, size_t size) {
          return process.Memory().ObserveWrites(address, size);
        });
  }
  copyWriteObserverLifetime.emplace(process.Memory());
  if (hdFontsLoaded) submissionQueue.SetTextureSnapshotTransform(
      [&](const Oot3dNativeGame::Oot3dPicaTextureState& texture,
          Oot3dNativeGame::Oot3dPicaResourceSnapshot& resource) {
        hdFonts.Resolve(picaMemoryView, texture, resource);
      });
  if (topScreenTextureOverrideRuntime.has_value()) {
    submissionQueue.SetTexturePayloadTransform(
        [&](const Oot3dNativeGame::Oot3dPicaTextureState &texture,
            std::span<std::uint8_t> payload) {
          topScreenTextureOverrideRuntime->Transform(texture, payload);
        });
  }
  submissionQueue.SetRuntimeProfilingEnabled(launch.ProfileA32Runtime);
  picaFrontend.SetPacketSink(&submissionQueue);
  picaFrontend.SetCommandBufferReader(
      [&picaMemoryView](uint32_t physicalAddress, uint32_t byteSize,
                        uint32_t *guestAddress, std::vector<uint32_t> *words) {
        return picaMemoryView.ReadCommandBuffer(physicalAddress, byteSize,
                                                guestAddress, words);
      });

  hostArgs = launch.Host;
  if (const char* productName = Oot3dNativeGame::TitleProductName(titleAdapter)) {
    hostArgs.ApplicationName = productName;
  }
  hostArgs.AudioSampleRate = Oot3dNativeGame::NativeA32DspHle::NativeSampleRate;
  hostArgs.AudioSampleLength =
      Oot3dNativeGame::NativeA32DspHle::SamplesPerFrame;
  hostArgs.AudioDesiredBuffered = std::max<int32_t>(
      hostArgs.AudioDesiredBuffered,
      static_cast<int32_t>((static_cast<uint64_t>(hostArgs.AudioSampleRate) *
                                kMinimumHostAudioPrebufferMilliseconds +
                            999U) /
                           1000U));
  sceneEnvironment = std::make_shared<Oot3dNativeGame::SceneEnvironmentRuntime>(
      std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "environments",
      [](const Fast::Renderer3ds::PicaEnvironmentLighting& environment) {
        Fast::Oot3d::GraphicsSettingsRuntime::Instance().PublishSceneEnvironment(environment);
      });
  // F1 order: the scene lights join Lighting > Lights & Shadows; then
  // Controls, HUD and Game (Save states, Language) after the graphics tabs.
  // Scene lights are profiles of OoT3D's scenes: another title has none.
  if (titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur) {
    applicationSettingsTabs.push_back(Oot3dNativeGame::CreateSceneEnvironmentPanel(sceneEnvironment));
  }
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreateNativeControlsSettingsPanel(
          controlConfigRuntime,
          launch.UiProfile ==
                  Oot3dNativeGame::Oot3dUiProfile::TopScreen
              ? topScreenConfigRuntime
              : nullptr));
  if (launch.UiProfile ==
      Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    applicationSettingsTabs.push_back(
        Oot3dNativeGame::CreateTopScreenSettingsPanel(
            topScreenConfigRuntime));
  }
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreateSavestatePanel(savestateLibrary));
  applicationSettingsTabs.push_back(Oot3dNativeGame::CreateGameLanguagePanel(languageSettings));
  Fast::Oot3d::InstallGraphicsSettingsPanelTabs(
      std::move(applicationSettingsTabs));
  if (const char* title = Oot3dNativeGame::TitleGraphicsWindowTitle(titleAdapter)) {
    Fast::Oot3d::InstallGraphicsSettingsWindowTitle(title);
  }
  InitContextForDemo(hostArgs);
  // What the title itself provides to the graphics settings: the effects
  // that need a scene view, and the scene FOV, stay inactive without it.
  // After the context: it installs the settings' persistence first.
  {
    auto &settingsRuntime = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
    const std::string titleName = titleAdapter == Oot3dNativeGame::TitleAdapter::Mm3d
                                      ? "Majora's Mask 3D"
                                      : "This game";
    settingsRuntime.SetCapability(
        Fast::Oot3d::GraphicsCapability::TitleSceneView,
        Oot3dNativeGame::TitlePublishesSceneView(titleAdapter),
        titleName + " does not publish its scene view yet.");
    settingsRuntime.SetCapability(
        Fast::Oot3d::GraphicsCapability::TitleSceneFov,
        Oot3dNativeGame::TitleAppliesSceneFov(titleAdapter),
        titleName + " has no scene projection hooks yet; the FOV has no effect.");
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("host_context_initialized");
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE) && defined(_WIN32)
  std::string crashDiagnosticsError;
  if (!Oot3dNativeGame::InstallWholeAotCrashDiagnostics(
          saveDataDirectory / "crashes", &crashDiagnosticsError)) {
    throw std::runtime_error("whole-AOT crash diagnostics failed: " +
                             crashDiagnosticsError);
  }
#endif
  windowPointer = &GetActiveFast3dWindowForDemo();
  auto &window = *windowPointer;
  // Interactive presentation is paced by NativeRealtimeRefreshPacer, not by a
  // second software limiter in SDL. Bounded probes retain their existing policy.
  if (hostArgs.ThroughputBenchmark || hostArgs.FrameLimit == 0U) {
    window.SetTargetFps(0);
  }
  apiPointer = &GetActiveRenderingApiForDemo(window);
  auto &api = *apiPointer;
  programPreparation =
      dynamic_cast<Fast::Renderer3ds::PicaProgramPreparationBackend *>(&api);
  alwaysAvailablePrograms =
      programPreparation != nullptr &&
      programPreparation->UsesAlwaysAvailablePrograms();
  if (programPreparation != nullptr)
    programPreparation->RegisterTitlePassPrograms(
        Oot3dNativeGame::NativePicaTitlePassPrograms());
  // The startup program family (vertex programs, effect supersets) is
  // OoT3D's: another title's draws never match it, so preparing it would
  // only hold that guest at startup. Its draws prepare their own programs.
  if (programPreparation != nullptr &&
      titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur &&
      (launch.PicaParametricTev || alwaysAvailablePrograms)) {
    programPreparation->PrepareNativePicaPrograms(
        Oot3dNativeGame::NativePicaVertexProgramFamily());
  }
  // Initial preparation: from here until the renderer's startup work is ready
  // and the first frames run within budget, the window shows its progress.
  auto &preparationScreen = preparationScreenStorage.emplace(
      std::chrono::steady_clock::now());
  api.SetNativePicaGeometryCacheEnabled(
      !launch.DisableOpenGlPicaGeometryCache);
  // A title whose adapter widens the scene projection to the output aspect
  // fills the output; another title's top screen keeps its native view,
  // letterboxed.
  api.SetNativeTopScreenExtendsToOutput(
      Oot3dNativeGame::TitleAppliesSceneFov(titleAdapter));
  titleRenderBackend =
      dynamic_cast<Fast::Oot3d::TitleRenderBackend *>(&api);
  gui =
      Ship::Context::GetRawInstance()->GetWindow()->GetGui();
  if (gui == nullptr) {
    throw std::runtime_error("native PICA host has no renderer GUI");
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("opengl_renderer_initialized");
#endif
  auto &nativeUiTextureProvider = nativeUiTextureProviderStorage.emplace(
      picaMemoryView, topScreenTextureOverridePackPointer);
  if (hdFontsLoaded) nativeUiTextureProvider.SetGeneratedTextureTransform(
      [&](const Oot3dNativeGame::Oot3dPicaTextureState& texture,
          Oot3dNativeGame::Oot3dPicaResourceSnapshot& resource) {
        hdFonts.Resolve(picaMemoryView, texture, resource);
      });
  auto &ocarinaText = ocarinaTextStorage.emplace(process.Memory(), nativeUiTextureProvider);
  nativeCandidateDispatch.OcarinaText = &ocarinaText;
  widescreenProjection.OcarinaText = &ocarinaText;
  auto &uiRenderBackend = uiRenderBackendStorage.emplace(api);
  auto &n64UiRenderer = n64UiRendererStorage.emplace(uiRenderBackend,
                                               &ocarinaText);
  window.SetTextureFilter(Fast::FILTER_LINEAR);
  inputTimeline = Oot3dNativeGame::NativeA32InputTimeline::LoadFile(
      launch.Host.InputTimelinePath);
  graphicsSettingsSchedule =
      Oot3dNativeGame::GraphicsSettingsSchedule::LoadFile(
          launch.Host.GraphicsSettingsSchedulePath);
#if defined(__SWITCH__)
  WriteSwitchBootStage("ui_runtime_initialized");
#endif

  UpdateScenePresentation();
  process.SetBlockEntryCallback(
      &ApplyNativeWidescreenProjectionPolicy, &widescreenProjection,
      launch.ProfileA32Blocks || widescreenProjection.TraceA32Blocks
          ? std::vector<uint32_t>{}
          : blockEntryHooks);
#if defined(__SWITCH__)
  WriteSwitchBootStage("guest_callbacks_initialized");
#endif

  shaderSourceCache.TevMode = launch.PicaParametricTev
      ? Oot3dNativeGame::Oot3dPicaTevMode::Parametric
      : Oot3dNativeGame::Oot3dPicaTevMode::Specialized;
  shaderSourceCache.UberFragments = alwaysAvailablePrograms;
  auto &picaSemanticTrace = picaSemanticTraceStorage.emplace(
      launch.PicaSemanticTracePath);
  picaSemanticTrace.BeginSession(
      launch.Host.Renderer,
      Oot3dNativeGame::GameplayTimingModeName(launch.GameplayTiming),
      Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile));
  auto &dspHle = dspHleStorage.emplace(
      Oot3dNativeGame::BuildNativeA32DspPhysicalRegions(*manifest));
  context = Ship::Context::GetRawInstance();
  audio = context != nullptr ? context->GetAudio() : nullptr;
  audioPlayer = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
#if defined(__SWITCH__)
  WriteSwitchBootStage("audio_runtime_initialized");
#endif
  phaseStart = std::chrono::steady_clock::now();
  auto &presentationScheduler = presentationSchedulerStorage.emplace(
      frameRateContract.SimulationRateHz,
      frameRateContract.Mode ==
              Oot3dNativeGame::GameplayTimingMode::Enhanced60
          ? Oot3dNativeGame::NativeSimulationCatchUpPolicy::
                PreserveBoundedDebt
          : Oot3dNativeGame::NativeSimulationCatchUpPolicy::DropExcess);
  if (const char* watchRange = std::getenv("TRIAEVUM_WATCH_GUEST_RANGE")) {
    unsigned long long start = 0, size = 0;
    const int parsed = std::sscanf(watchRange, "%llx:%llx", &start, &size);
    std::fprintf(stderr,
                 "TRIAEVUM_WATCH_GUEST_RANGE raw='%s' parsed=%d start=0x%llX size=0x%llX\n",
                 watchRange, parsed, start, size);
    if (parsed == 2 && size != 0) {
      rawCopyWatchRanges.emplace_back(static_cast<uint32_t>(start),
                                      static_cast<uint32_t>(start + size));
      process.Memory().SetRangeWatch(rawCopyWatchRanges);
      watchReportRequested = true;
      std::fprintf(stderr, "TRIAEVUM_WATCH_GUEST_RANGE active=%d ranges=%zu\n",
                   process.Memory().RangeWatchActive() ? 1 : 0,
                   process.Memory().WatchedRangeCount());
    }
  }
  if (!launch.LoadStatePath.empty()) {
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_savestate_loading");
#endif
    Oot3dNativeGame::NativeA32SavestateRuntimeState restoredRuntime;
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::LoadNativeA32State(
            launch.LoadStatePath, savestateCompatibility, restoredRuntime,
            process, hostServices, picaFrontend, submissionQueue, dspHle,
            &ioResult, &error)) {
      throw std::runtime_error("native savestate load failed: " + error);
    }
    ApplyLoadedSavestateSettings(restoredRuntime, launch.LoadStateWithSettings);
    if (restoredRuntime.HostSettingsAvailable)
      initialStateHostSettings = restoredRuntime.HostSettingsJson;
    Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
    hdFonts.Reset();
    const auto restoredGravity = hostServices.HidRuntimeProfile().LastAccelerometer;
    nativeControlPollingState.VirtualMotion.RestoreGravity(
        {static_cast<float>(restoredGravity[0]), static_cast<float>(restoredGravity[1]),
         static_cast<float>(restoredGravity[2])});
    nativeControlPollingState.PendingMouseDeltaX = 0;
    nativeControlPollingState.PendingMouseDeltaY = 0;
    nativeControlPollingState.PendingInputSeconds = 0.0;
    picaCompositionTracker.Reset();
    mm3dCompositionTracker.Reset();
    frameCount = restoredRuntime.FrameCount;
    refreshTickRemainder = restoredRuntime.RefreshTickRemainder;
    nextVblankTick = restoredRuntime.NextVblankTick;
    processResult.Kind = static_cast<Oot3dNativeGame::NativeA32ProcessRunKind>(
        restoredRuntime.ProcessRunKind);
    RestoreSavestateTiming(restoredRuntime);
    ApplySelectedUiProfile();
    if (!api.ResetPicaState(&error)) {
      throw std::runtime_error(
          "native PICA reset before initial savestate restore failed: " +
          error);
    }
    if (titleRenderBackend != nullptr &&
        !titleRenderBackend->ResetTitleState(&error)) {
      throw std::runtime_error(
          "OOT3D renderer reset before initial savestate restore failed: " +
          error);
    }
    RestoreSavestatePicaTextureCache(restoredRuntime);
    RestoreSavestatePicaColorTargets(restoredRuntime);
    RestoreSavestatePicaPresentationState(restoredRuntime);
    RestoreSavestatePicaVisualReplayState(restoredRuntime);
    RestoreSavestateTopScreenTemporalState(restoredRuntime);
    ++savestateStats.LoadCount;
    savestateStats.LoadBytes += ioResult.FileBytes;
    savestateStats.LoadSeconds += SecondsSince(phaseStart);
    savestateStats.ReadSeconds += ioResult.IoSeconds;
    savestateStats.DecodeSeconds += ioResult.DecodeSeconds;
    savestateStats.RestoreSeconds += ioResult.RestoreSeconds;
    std::cout << "oot3d_native_game: loaded initial state at frame "
              << frameCount << " from " << launch.LoadStatePath.string()
              << '\n';
  } else {
    ApplySelectedUiProfile();
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_guest_dispatch");
#endif
    processResult =
        RunUntilGuestWait(process, launch.WholeAotBlockBudget);
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_guest_wait");
#endif
    phaseTiming.GuestSeconds += SecondsSince(phaseStart);
    RequireRunnableGuest(processResult);
  }
  timing = WindowDemoTimingState{};  // its default members read the clock here
  presentationPacingPolicy =
      Fast::Oot3d::ResolvePresentationPacingPolicy(
          Fast::Oot3d::GraphicsSettingsRuntime::Instance()
              .Snapshot()
              .FrameRate);
  frameCompositionPolicy =
      ResolveFrameCompositionPolicy(presentationPacingPolicy);
  visualSampleCadence.Configure(
      frameCompositionPolicy.FixedMultiplier()
          ? frameCompositionPolicy.FixedSampleMultiplier
          : 0U);
  auto &realtimePacer = realtimePacerStorage.emplace(
      hostArgs.FrameLimit == 0U && !hostArgs.ThroughputBenchmark,
      presentationPacingPolicy.TargetRateHz);
  realtimePacer.Configure(presentationPacingPolicy.Enabled &&
                              !hostArgs.ThroughputBenchmark,
                          presentationPacingPolicy.TargetRateHz);
  visualInterpolationEnabled =
      Oot3dNativeGame::ShouldUseNativeVisualInterpolation(
          frameRateContract,
          frameCompositionPolicy.InterpolationEnabled());
  // The two values above belong to the renderer. The guest detects policy
  // changes against its own copies.
  guestCompositionPolicy = frameCompositionPolicy;
  guestInterpolationEnabled = visualInterpolationEnabled;
  lastPresentationTime = std::chrono::steady_clock::now();
  picaPresentationScheduler.SetTimingEnabled(launch.ExtendedDiagnostics);
  api.SetPicaPhysicalMemoryAccess(
      [&](uint32_t address, std::span<uint8_t> bytes) {
        RequireStoppedGuest();
        return picaMemoryView.Read(address, bytes);
      },
      [&](std::span<const Fast::Renderer3ds::PicaPhysicalMemoryWrite> writes) {
        RequireStoppedGuest();
        // The writeback is the renderer writing the copy itself, not a guest or
        // service access; keep it out of the guest-access counters. Scope, not
        // a pair of calls: an early return must not leave the watch suppressed.
        {
        const Oot3dNativeGame::NativeA32Memory::RangeWatchSuppression suppressed(
            process.Memory());
        for (const auto& write : writes) {
          if (!picaMemoryView.ForEachGuestAlias(write.Address, write.Bytes.size(),
              [&](uint32_t address, size_t size) { return process.Memory().IsWritable(address, size); }))
            return false;
        }
        for (const auto& write : writes) {
          if (!picaMemoryView.ForEachGuestAlias(write.Address, write.Bytes.size(),
              [&](uint32_t address, size_t) { return process.Memory().WriteBytes(address, write.Bytes); }))
            return false;
        }
        }
        return true;
      },
      [&](std::span<const Fast::Renderer3ds::PicaRawCopySpan> spans,
          Fast::Renderer3ds::PicaRenderBackend::PhysicalMemoryRead reader) {
        RequireStoppedGuest();
        std::vector<std::pair<uint32_t, size_t>> ranges;
        for (const auto& span : spans) {
          if (!picaMemoryView.ForEachGuestAlias(span.DestinationAddress, span.ByteCount,
              [&](uint32_t address, size_t size) { ranges.emplace_back(address, size); return true; }))
            return false;
        }
        return process.Memory().DeferWrites(ranges,
            [&, reader = std::move(reader)](uint32_t address, std::span<uint8_t> bytes) {
              const auto physical = picaMemoryView.TranslateGuest(address, bytes.size());
              if (!physical) return false;
              // The bytes live in a renderer-owned GPU copy: a guest read
              // reaches it after every renderer command before it.
              if (renderStream.InCommand() ||
                  renderStream.CurrentMode() ==
                      Oot3dNativeGame::NativeRenderCommandStream::Mode::Inline) {
                return reader(*physical, bytes);
              }
              bool read = false;
              renderStream.Run([&] { read = reader(*physical, bytes); });
              return read;
            });
      },
      [&](uint64_t) {
        RequireStoppedGuest();
        if (!hostServices.SignalPicaInterrupt(process.Memory(), Oot3dNativeGame::Oot3dPicaInterruptId::Ppf)) return false;
        ++picaStats.CompletedPpfCount;
        ++picaStats.RecordedCompletionCount;
        return true;
      },
      [&]() {
        RequireStoppedGuest();
        return process.Memory().MaterializeDeferredWrites();
      });
  physicalMemoryAccessLifetime.emplace(api);
  rendererTiming = &phaseTiming;
  // Declared after every renderer-owned local the commands reference: leaving
  // the loop (normally or by exception) stops the worker before they die.
  renderStreamStop.emplace(renderStream);

  benchmark = NativeA32BenchmarkWindow{};  // its default members read the clock here
  pacingTrace = std::getenv("TRIAEVUM_PACING_TRACE");
  measurePacing = pacingTrace && std::string_view(pacingTrace) == "1";
  benchmarkFrameTimes = (hostArgs.ThroughputBenchmark || measurePacing)
      ? std::make_unique<Fast::Renderer::FrameTimeDistribution>() : nullptr;
  lastCompletedFrameTime = std::chrono::steady_clock::now();
  cpuProbeEnvironment = std::getenv("TRIAEVUM_PROFILE_CPU_LEDGER");
  cpuProbeRequested = CpuProbe::BuildEnabled && cpuProbeEnvironment &&
      std::string_view(cpuProbeEnvironment) == "1";

  parallelPresentationEnvironment =
      std::getenv("TRIAEVUM_PARALLEL_PRESENTATION");
  // Harnesses override the launch choice: "0" serial, "1" parallel.
  parallelPresentation =
      parallelPresentationEnvironment != nullptr &&
              (std::string_view(parallelPresentationEnvironment) == "0" ||
               std::string_view(parallelPresentationEnvironment) == "1")
          ? std::string_view(parallelPresentationEnvironment) == "1"
          : launch.ParallelPresentation;
  fast3dGui = dynamic_cast<Fast::Fast3dGui*>(gui.get());
  if (parallelPresentation) {
    // Renderer timings accumulate apart and merge once the stream drains.
    rendererTiming = &workerPhaseTiming;
    renderStream.SetMode(
        Oot3dNativeGame::NativeRenderCommandStream::Mode::Worker);
    // The ImGui overlay (and the display composition it triggers) renders
    // in renderer order, from a copy of each frame's draw data.
    if (fast3dGui != nullptr) {
      fast3dGui->SetRendererRouting(
          {[&](std::function<void()> work) { renderStream.Run(std::move(work)); },
           [&](std::function<void()> work) {
             renderStream.Submit(std::move(work));
           }});
    }
  }
  // The routing refers to this loop's stream: removed on every exit.
  guiRendererRoutingReset.emplace(parallelPresentation ? fast3dGui : nullptr);

#if defined(__SWITCH__)
  WriteSwitchBootStage("runtime_loop");
#endif
}

void NativeA32WindowSession::ApplyTopScreenConfigSnapshot() {
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  const auto snapshot = topScreenConfigRuntime->Snapshot();
  if (snapshot.Revision == appliedTopScreenConfigRevision) {
    return;
  }
  activeTopScreenConfig = snapshot.Config;
  appliedTopScreenConfigRevision = snapshot.Revision;
  uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
      nativeCandidateDispatch.TopScreenCamera.Camera,
      activeTopScreenConfig);
  if (!activeTopScreenConfig.FreeCameraEnabled) {
    nativeCandidateDispatch.TopScreenCameraInput.Reset();
  }
}

void NativeA32WindowSession::SetTopScreenSessionMinimapVisible(bool visible) {
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  activeTopScreenConfig.MinimapVisible = visible;
  uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
}

void NativeA32WindowSession::ApplySelectedUiProfile() {
  auto &process = *processStorage;
  if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    return;
  }
  Oot3dNativeGame::TopScreenLayoutApplyStats layoutStats;
  if (!Oot3dNativeGame::ApplyTopScreenVerifiedLayout(process.Memory(),
                                                     &layoutStats, &error)) {
    throw std::runtime_error(
        "TopScreen source-port layout rejected the process image: " + error);
  }
  Oot3dNativeGame::TopScreenRuntimeGeometryApplyStats geometryStats;
  if (layoutStats.WordsChanged != 0U &&
      !Oot3dNativeGame::RebindTopScreenVerifiedRuntimeGeometry(
          process.Memory(), &geometryStats, &error)) {
    throw std::runtime_error(
        "TopScreen source-port runtime geometry rejected the process "
        "image: " +
        error);
  }
  std::cout << "TopScreen source-port layout: contracts="
            << layoutStats.ContractsChecked
            << " words=" << layoutStats.WordsWritten
            << " changed_words=" << layoutStats.WordsChanged
            << " rebound_streams=" << geometryStats.StreamsRebound
            << " rebound_quads=" << geometryStats.QuadsRebound << '\n';
}

void NativeA32WindowSession::UpdateScenePresentation() {
  auto &window = *windowPointer;
  widescreenProjection.Projection.Presentation =
      Fast::Oot3d::ResolveScenePresentationPolicy(
          std::max<uint32_t>(1, window.GetWidth()),
          std::max<uint32_t>(1, window.GetHeight()));
  widescreenProjection.Projection.FovMultiplier =
      Fast::Oot3d::GraphicsSettingsRuntime::Instance()
          .Snapshot()
          .FovMultiplier;
}

Fast::Renderer3ds::PicaFrameCompositionPolicy NativeA32WindowSession::ResolveFrameCompositionPolicy(const Fast::Oot3d::PresentationPacingPolicy& pacing) {
  auto policy = pacing.Composition;
  policy.NativeRateHz = frameRateContract.SimulationRateHz;
  if (!frameRateContract.VisualInterpolationAllowed) {
    policy.Interpolation =
        Fast::Oot3d::NativeVisualInterpolationMode::Disabled;
    policy.FixedSampleMultiplier = 1U;
  }
  return policy;
}

} // namespace Oot3dA32Window
