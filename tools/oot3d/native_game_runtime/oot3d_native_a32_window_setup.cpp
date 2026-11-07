// Native A32 window session: setup (process image, host services, renderer, UI, initial state).
#include "oot3d_native_a32_window_session.h"
#include "oot3d_native_a32_romfs_overlay.h"
#include "oot3d_native_a32_romfs_layers.h"
#include "oot3d_native_direct_aot.h"
#include "oot3d_title_grass_rules.h"
#include "triaevum_audio_settings_panel.h"
#include "triaevum_host_audio.h"
#include "oot3d_savestate_migration.h"
#include "fast/extension/graphics_settings_persistence.h"
#include "fast/extension/title_grass_rules_file.h"
#include "fast/renderer3ds/title_shader_preparation.h"
#include "oot3d/renderer/azahar_texture_pack.h"
#include "ship/config/Config.h"

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#if defined(__ANDROID__)
#include "android_host.h"
#endif

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

#if defined(TRIAEVUM_DEVELOPER_TOOLS)
// TRIAEVUM_A32_COVERAGE=PATH: the guest code the A32 registry does not
// translate (interpreted Thumb block entries, A32 PCs without a block), written
// as "pc,kind,count" lines when the process exits. A coverage oracle for
// function discovery: every listed PC is code the title reached in this run.
struct UntranslatedCodeLog {
  std::string Path;
  std::unordered_map<uint32_t, uint64_t> Thumb;
  std::unordered_map<uint32_t, uint64_t> Arm;
  ~UntranslatedCodeLog() {
    std::FILE *file = std::fopen(Path.c_str(), "w");
    if (file == nullptr) return;
    std::fprintf(file, "pc,kind,count\n");
    for (const auto *entries : {&Thumb, &Arm}) {
      std::vector<std::pair<uint32_t, uint64_t>> sorted(entries->begin(), entries->end());
      std::sort(sorted.begin(), sorted.end());
      for (const auto &[pc, count] : sorted)
        std::fprintf(file, "0x%08X,%s,%llu\n", pc, entries == &Thumb ? "thumb" : "arm",
                     static_cast<unsigned long long>(count));
    }
    std::fclose(file);
  }
};

void ObserveUntranslatedCode(uint32_t pc, bool thumb, void *user) {
  auto &log = *static_cast<UntranslatedCodeLog *>(user);
  ++(thumb ? log.Thumb : log.Arm)[pc];
}
#endif

#if !defined(__ANDROID__) && !defined(__SWITCH__)
// The desktop's file browser on a folder: SDL hands a file URL to the
// desktop's handler (Explorer, the Linux file manager, Finder).
bool OpenFolderInFileBrowser(const std::filesystem::path &folder, std::string *error) {
  std::error_code status;
  const auto absolute = std::filesystem::absolute(folder, status).lexically_normal();
  if (status) {
    *error = status.message();
    return false;
  }
  const auto utf8 = absolute.generic_u8string();
  std::string url = "file://";
#if defined(_WIN32)
  url += '/';
#endif
  static constexpr char kHex[] = "0123456789ABCDEF";
  for (const char8_t unit : utf8) {
    const auto c = static_cast<unsigned char>(unit);
    const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
                       c == '-' || c == '_' || c == '.' || c == '~' || c == ':';
    if (plain) {
      url += static_cast<char>(c);
    } else {
      url += '%';
      url += kHex[c >> 4U];
      url += kHex[c & 15U];
    }
  }
  if (SDL_OpenURL(url.c_str()) != 0) {
    *error = SDL_GetError();
    return false;
  }
  return true;
}
#endif

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
  titleSession = Oot3dNativeGame::CreateTitleSession(titleAdapter);
  if (titleSession) picaPresentationScheduler.SetGameplayHudClassifier(
      [title=titleSession.get()](uint32_t pc,uint32_t value) { return title->IsGameplayHud(pc,value); });
  // Before any settings load: configurations older than schema 15 take the
  // title's shipped grass source and mask rules. OoT3D's rules are keyed by
  // its own textures; another title's configuration drops an unedited copy
  // that builds installing them for every title wrote there. The removal is
  // recorded, and OoT3D restores its rules from it when it loads the same
  // file (a developer launch without --config shares one for every title).
  if (titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur) {
    Fast::Extension::InstallTitleGrassRuleDefaults(Oot3dNativeGame::Oot3dTitleGrassRuleDefaults());
  } else {
    Fast::Extension::InstallTitleGrassRuleDefaults(Fast::Extension::TitleGrassRuleDefaults{
        titleSession ? titleSession->GrassRuleDefaults() : std::nullopt,
        {Oot3dNativeGame::Oot3dTitleGrassRuleDefaults()}});
  }
  // Static shadow actor catalogs are keyed by the title's own actor ids.
  picaPresentationScheduler.SetStaticShadowActorPolicy(
      titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur ? Oot3dNativeGame::Oot3dStaticShadowActorPolicy()
      : titleSession                                          ? titleSession->StaticShadowActors()
                                                              : Oot3dNativeGame::StaticShadowActorPolicy{});
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
    gSwitchTopScreen30UiAtlas =
        topScreenTextureOverridePack.FindProfileTexture(
            "oot3d/topscreen/3.0/ui_atlas") != nullptr;
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
  if (!launch.UiTexturePackDirectory.empty()) {
    // Forge's UI texture pack beneath the user's pack: the boot language's
    // folders, then the language-neutral textures.
    std::vector<std::filesystem::path> directories;
    for (const auto &folder :
         Oot3dNativeGame::UiTexturePackLanguageFolders(languageSettings->SystemId())) {
      const auto directory = launch.UiTexturePackDirectory / "languages" / folder;
      if (std::filesystem::is_directory(directory)) directories.push_back(directory);
    }
    directories.push_back(launch.UiTexturePackDirectory / "common");
    Oot3d::Renderer::AzaharTexturePackRuntime::Instance().SetBaseDirectories(std::move(directories));
  }
  // Optional title-owned grass authoring files. OoT3D retains its existing
  // config storage and output; titles choosing a namespace cannot overwrite
  // its sources/masks even when they share the host configuration file.
  {
    auto& file = Fast::Extension::ActiveTitleGrassRulesFile();
    file.SetPath({});
    if (titleSession && !titleSession->GrassRuleNamespace().empty()) {
      const auto config = std::filesystem::absolute(
          launch.Host.ConfigurationPath.empty() ? "oot3d_native_game.json" : launch.Host.ConfigurationPath);
      file.SetPath(config.parent_path() / "grass" / titleSession->GrassRuleNamespace() / "rules.json");
    }
  }
  // The installed game's program ID and the managed custom texture folders
  // (UX plan WP-05C), kept across every settings reconfigure; the
  // environment and the F1 folders keep their precedence. Without them (an
  // older launch profile) the folders resolve as before. The settings file's
  // folder is always named: F1 folders inside the installation are stored
  // relative to it, so a moved installation keeps them (issue #124).
  {
    std::error_code settingsError;
    const auto settingsPath = std::filesystem::path(launch.Host.ConfigurationPath);
    const auto settingsDirectory = settingsPath.empty()
                                       ? std::filesystem::path{}
                                       : std::filesystem::absolute(settingsPath, settingsError).parent_path();
    // Azahar names texture folders by the installed title's program ID:
    // --title-id, else the one the process image's exheader carries.
    Oot3d::Renderer::AzaharTexturePackRuntime::Instance().SetHostDefaults({
        .TitleId = launch.Routing.TitleId ? launch.Routing.TitleId : manifest->ProgramId,
        .LoadDirectory = launch.Routing.TextureLoadDirectory,
        .DumpDirectory = launch.Routing.TextureDumpDirectory,
        .SettingsDirectory = settingsError ? std::filesystem::path{} : settingsDirectory,
    });
  }
  hostConfig.ResourceLimitValues = manifest->ResourceLimitValues;
  hostConfig.ResourceCurrentValues = manifest->ResourceCurrentValues;
  hostConfig.LinearHeapBaseAddress = manifest->LinearHeapBaseAddress;
  hostConfig.LinearHeapSize = manifest->LinearHeapSize;
  hostConfig.HeapBaseAddress = manifest->HeapBaseAddress;
  hostConfig.HeapSize = manifest->HeapSize;
  hostConfig.RomFsImagePath = manifest->RomFsImagePath;
  hostConfig.RomFsImageOffset = manifest->RomFsImageOffset;
  hostConfig.RomFsImageSize = manifest->RomFsImageSize;
  if (!launch.RomFsOverlayPath.empty()) {
    // User-enabled RomFS mods: Forge built the overlay over this exact image.
    std::ifstream image(manifest->RomFsImagePath, std::ios::binary);
    const uint64_t imageOffset = manifest->RomFsImageOffset;
    hostConfig.RomFsOverlay = Oot3dNativeGame::NativeA32RomFsOverlay::Load(
        launch.RomFsOverlayPath, manifest->RomFsImageSize,
        [&](uint64_t offset, std::span<uint8_t> bytes) {
          image.clear();
          image.seekg(static_cast<std::streamoff>(imageOffset + offset), std::ios::beg);
          image.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
          return static_cast<bool>(image);
        },
        &error);
    if (!hostConfig.RomFsOverlay) throw std::runtime_error(error);
    std::cout << "oot3d_native_game: RomFS mods active ("
              << hostConfig.RomFsOverlay->PatchCount() << " metadata patches, "
              << (hostConfig.RomFsOverlay->ViewSize() - manifest->RomFsImageSize)
              << " appended bytes)\n";
  }
  if (const char *overlay = std::getenv("TRIAEVUM_ROMFS_OVERLAY")) {
    // Development: user-supplied RomFS files laid over the title's (a
    // directory per layer mirroring RomFS paths, ';'-separated, later
    // layers winning). Installed RomFS mods (Forge's overlay) are the
    // product path; the two never combine.
    if (hostConfig.RomFsOverlay)
      throw std::runtime_error("TRIAEVUM_ROMFS_OVERLAY cannot combine with installed RomFS mods");
    std::vector<std::filesystem::path> layers;
    for (std::string_view rest = overlay; !rest.empty();) {
      const size_t separator = rest.find(';');
      if (const auto layer = rest.substr(0, separator); !layer.empty()) {
        layers.emplace_back(std::filesystem::u8path(layer));
      }
      rest = separator == std::string_view::npos ? std::string_view{} : rest.substr(separator + 1U);
    }
    std::string layerError;
    auto base = Oot3dNativeGame::OpenNativeA32HostFileRange(
        manifest->RomFsImagePath, manifest->RomFsImageOffset, manifest->RomFsImageSize, &layerError);
    auto built = base != nullptr
                     ? Oot3dNativeGame::BuildNativeA32RomFsLayers(base, layers, &layerError)
                     : std::nullopt;
    if (!built.has_value()) {
      throw std::runtime_error("RomFS layers: " + layerError);
    }
    hostConfig.RomFsLayers = built->File;
    std::fprintf(stderr, "RomFS layers: %zu layer(s), %zu file(s) replaced, %zu added, %zu directories added\n",
                 layers.size(), built->ReplacedFiles, built->AddedFiles, built->AddedDirectories);
  }
  hostConfig.PicaFrontend = &picaFrontend;
  hostConfig.PicaCompositionDomain = &picaCompositionDomain;
  Oot3dNativeGame::Oot3dPicaCommandListCompositionProvider *titleComposition =
      titleSession ? titleSession->CompositionProvider() : nullptr;
  hostConfig.PicaCompositionProvider =
      titleComposition != nullptr ? titleComposition : &picaCompositionTracker;
  hostConfig.SaveDataDirectory =
      launch.SaveDataDirectory.empty()
          ? launch.A32ProcessManifestPath.parent_path() /
                "oot3d_native_savedata"
          : launch.SaveDataDirectory;
  saveDataDirectory = hostConfig.SaveDataDirectory;
  // Save states default to a folder beside the executable on Windows and beside
  // the save data elsewhere; --quick-state pins one file for scripted runs.
#if defined(_WIN32)
  constexpr bool kSavestatesBesideExecutable = true;
#else
  constexpr bool kSavestatesBesideExecutable = false;
#endif
  const auto configurationDirectory = std::filesystem::path(launch.Host.ConfigurationPath).parent_path();
  const auto earlierSavestates = Oot3dNativeGame::DefaultSavestateDirectory(
      kSavestatesBesideExecutable,
      std::filesystem::path(std::u8string(
          reinterpret_cast<const char8_t *>(Ship::Context::GetAppBundlePath().c_str()))),
      hostConfig.SaveDataDirectory);
  auto defaultSavestates = earlierSavestates;
  if (!launch.Routing.SavestateDirectory.empty()) {
    // The managed default (D/savestates, UX plan WP-05C): states saved in the
    // earlier default folder are copied there first, verified, never deleted.
    const auto migration = Oot3dNativeGame::MigrateLegacySavestates({
        .PreferencesPath = configurationDirectory / "savestates.json",
        .MigrationsPath = configurationDirectory / "migrations.json",
        .LegacyDirectory = earlierSavestates,
        .ManagedDirectory = launch.Routing.SavestateDirectory,
        .FixedSlot = !launch.QuickStatePath.empty(),
    });
    defaultSavestates = migration.DefaultDirectory;
    savestateMigrationMessage = std::string("save-state migration ") +
                                Oot3dNativeGame::SavestateMigrationStatusName(migration.Status) + ": " +
                                migration.Message;
    std::cerr << "TRIAEVUM_SAVESTATE_MIGRATION " << savestateMigrationMessage << '\n';
  }
  savestateLibrary = std::make_shared<Oot3dNativeGame::SavestateLibrary>(
      configurationDirectory / "savestates.json", defaultSavestates, launch.QuickStatePath);
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
  // Scripted runs must not depend on the host clock: the RTC starts at
  // 2011-06-16 00:00 (the title's release) and follows the virtual clock.
  if (!launch.Host.InputTimelinePath.empty()) {
    hostConfig.FixedDateTimeMilliseconds = 3517257600000ULL;
  }
  auto &hostServices = hostServicesStorage.emplace(std::move(hostConfig));
  // Development: TRIAEVUM_IR_NO_EXTRA_HID=1 presents an Old 3DS without a
  // Circle Pad Pro (titles then run their accessory prompts).
  if (const char *noExtraHid = std::getenv("TRIAEVUM_IR_NO_EXTRA_HID");
      noExtraHid != nullptr && std::string_view(noExtraHid) == "1") {
    hostServices.SetIrExtraHidAttached(false);
  }
  // Development: TRIAEVUM_TOUCH_ANCHORS lays out a touch overlay's controls
  // on a host without one ("A=l,t,r,b;DPAD=l,t,r,b", window fractions).
  if (const char *anchors = std::getenv("TRIAEVUM_TOUCH_ANCHORS"); anchors != nullptr && *anchors != '\0') {
    if (!ThreeDsRecomp::Input::ApplyTouchAnchorList(anchors, ThreeDsRecomp::Input::OverlayTouchAnchors()))
      std::cerr << "TRIAEVUM_TOUCH_ANCHORS ignored: expected NAME=l,t,r,b;... (A B X Y ZL ZR DPAD)" << std::endl;
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
  const auto actorVisibilityHooks =
      Oot3dNativeGame::NativeActorVisibilityHookPcs();
  blockEntryHooks.insert(blockEntryHooks.end(), actorVisibilityHooks.begin(),
                         actorVisibilityHooks.end());
  // Object bank extension (any UI profile): the block policy raises an
  // observable exit at these entries while the extension is active.
  const auto objectBankHooks = Oot3dNativeGame::ObjectBankExtension::HookPcs();
  blockEntryHooks.insert(blockEntryHooks.end(), objectBankHooks.begin(),
                         objectBankHooks.end());
  const auto rainHooks = Oot3dNativeGame::TopScreenRainAdapter::HookPcs();
  blockEntryHooks.insert(blockEntryHooks.end(), rainHooks.begin(), rainHooks.end());
  widescreenProjection.ActorVisibility.Diagnostics =
      std::getenv("OOT3D_ACTOR_VISIBILITY_DIAGNOSTICS") != nullptr;
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
        kMessageWindowDraw,
        kMessageWindowEntriesDrawn,
        kPauseTouchButtonsDrawCall,
        kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,
        kPauseDungeonMapCursorInput,
        kPauseSceneDimBranch,
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
        kPauseSceneFiveDrawCall,
        Oot3dNativeGame::kTopScreenSystemPageBoundary,
        kPauseTouchButtonsHeldDownQuery,
        kPauseTouchCoordinateUpdateCall,
        kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraFovScalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenCameraUpdatePatchSite,
        kTopScreenGameplayCompositionPrefix,
        Oot3dNativeGame::kTopScreenPageProjectionUploadCall,
        Oot3dNativeGame::kTopScreenItemsPageDrawCall,
        Oot3dNativeGame::kTopScreenItemsCursorDrawCallA,
        Oot3dNativeGame::kTopScreenItemsCursorDrawCallB,
        Oot3dNativeGame::kTopScreenItemsUpdateBoundary,
        Oot3dNativeGame::kTopScreenItemsShortcutCursorBoundary,
        Oot3dNativeGame::kTopScreenGearModelDrawCall,
        Oot3dNativeGame::kTopScreenViewModelTransformBlock,
    };
    blockEntryHooks.insert(blockEntryHooks.end(),
                           topScreenSourcePortHooks.begin(),
                           topScreenSourcePortHooks.end());
    const auto reframedFileHooks = Oot3dNativeGame::TopScreenReframedFileSelect::HookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), reframedFileHooks.begin(),
                           reframedFileHooks.end());
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    for (const auto pc : Oot3dNativeGame::TopScreenDungeonMapIconCoordinateSites())
      blockEntryHooks.push_back(pc);
    const auto itemHooks = Oot3dNativeGame::TopScreenItemDispatchEntries();
    blockEntryHooks.insert(blockEntryHooks.end(), itemHooks.begin(), itemHooks.end());
    blockEntryHooks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    const auto gameplayHooks = Oot3dNativeGame::TopScreenGameplayRuntime::HookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), gameplayHooks.begin(), gameplayHooks.end());
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
  if (titleSession) {
    Oot3dNativeGame::TitleSessionHookSetup titleHooks{
        .BlockEntryHooks = blockEntryHooks,
        .PerspectiveFrustumEntry = widescreenProjection.Projection.PerspectiveFrustumEntry,
        .FrustumIsSceneView = widescreenProjection.Projection.FrustumIsSceneView,
        .SceneView = sceneViewProbe,
        .PresentationClock = [this] {
          return static_cast<std::int64_t>(presentationFrameCount) * 1000000 / 60;
        },
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
        .DevToolsTitle = &devToolsTitle,
#endif
        .FreeCamera = nativeCandidateDispatch.DevFreeCamera,
        .MessageEditsPath = launch.TitleMessageEditsPath};
    titleSession->InstallHooks(titleHooks);
    widescreenProjection.Title = titleSession.get();
  }
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  if (const char *coverage = std::getenv("TRIAEVUM_A32_COVERAGE"); coverage != nullptr && *coverage != '\0') {
    static UntranslatedCodeLog untranslatedCode;
    untranslatedCode.Path = coverage;
    oot3d::recomp::a32::SetUntranslatedCodeObserver(&ObserveUntranslatedCode, &untranslatedCode);
    std::fprintf(stderr, "TRIAEVUM_A32_COVERAGE %s\n", coverage);
  }
#endif
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
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  if (const char *probe = std::getenv("TRIAEVUM_A32_PROBE"); probe != nullptr && *probe != '\0') {
    const std::string spec = probe;
    const auto comma = spec.rfind(',');
    if (comma == std::string::npos)
      throw std::runtime_error("TRIAEVUM_A32_PROBE: expected SPEC.json,OUT.jsonl");
    std::ifstream file(spec.substr(0, comma));
    if (!file) throw std::runtime_error("TRIAEVUM_A32_PROBE: cannot read " + spec.substr(0, comma));
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    readProbes = Oot3dNativeGame::A32ReadProbeSet::Parse(text);
    widescreenProjection.ReadProbeOut = std::fopen(spec.substr(comma + 1).c_str(), "w");
    if (widescreenProjection.ReadProbeOut == nullptr)
      throw std::runtime_error("TRIAEVUM_A32_PROBE: cannot write " + spec.substr(comma + 1));
    widescreenProjection.ReadProbes = &*readProbes;
    for (const uint32_t pc : readProbes->Pcs()) callTraceEntries.push_back(pc);
  }
#endif
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
  // Sorted TopScreen entries the product hook owns (product builds only).
  std::vector<uint32_t> productTopScreenBlocks;
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    topScreenSourcePortBlocks = {
        kMessageWindowDraw,               kMessageWindowEntriesDrawn,
        kPauseTouchButtonsDrawCall,       kPauseTouchButtonsTouchBranch,
        kPauseTouchButtonsHeldDownQuery,
        kPauseLowerScreenBlock,           kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,         kPauseProjectionPrepareCall,
        kPauseAlternateViewportDrawCall,  kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,         kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,    kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,         kPauseSceneViewportDrawCall,
        kPauseSceneFiveDrawCall,          kPauseUiDrawReturn,
        Oot3dNativeGame::kTopScreenSystemPageBoundary,
        kPauseTouchCoordinateUpdateCall,  kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,         kTopScreenCameraNormal1Scalar,
        kTopScreenCameraFovScalar,
        kTopScreenCameraUpdateEntry,      kTopScreenCameraUpdatePatchSite,
        kTopScreenTitleLogoFadeHoldPrefix,
        Oot3dNativeGame::kTopScreenPageProjectionUploadCall,
        Oot3dNativeGame::kTopScreenItemsPageDrawCall,
        Oot3dNativeGame::kTopScreenItemsCursorDrawCallA,
        Oot3dNativeGame::kTopScreenItemsCursorDrawCallB,
        Oot3dNativeGame::kTopScreenItemsUpdateBoundary,
        Oot3dNativeGame::kTopScreenItemsShortcutCursorBoundary,
        Oot3dNativeGame::kTopScreenGearModelDrawCall,
        Oot3dNativeGame::kTopScreenViewModelTransformBlock,
        Oot3dNativeGame::kTopScreenQuestDrawHook,
        kPauseSceneDimBranch,
        Oot3dNativeGame::kTopScreenVisionsUpperDispatch,
        Oot3dNativeGame::kTopScreenVisionsLowerDispatch};
    const auto reframedFileSites = Oot3dNativeGame::TopScreenReframedFileSelect::HookPcs();
    topScreenSourcePortBlocks.insert(topScreenSourcePortBlocks.end(),
                                     reframedFileSites.begin(), reframedFileSites.end());
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    const auto mapIconSites =
        Oot3dNativeGame::TopScreenDungeonMapIconCoordinateSites();
    topScreenSourcePortBlocks.insert(topScreenSourcePortBlocks.end(),
                                    mapIconSites.begin(), mapIconSites.end());
    const auto itemEntries = Oot3dNativeGame::TopScreenItemDispatchEntries();
    topScreenSourcePortBlocks.insert(topScreenSourcePortBlocks.end(),
                                    itemEntries.begin(), itemEntries.end());
    topScreenSourcePortBlocks.push_back(kTopScreenGameplayCompositionPrefix);
    topScreenSourcePortBlocks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    // SingleScreen 3.0 gameplay options leave compiled execution only while
    // enabled (TopScreenGameplayRuntime::ShouldObserve).
    const auto gameplayBlocks = Oot3dNativeGame::TopScreenGameplayRuntime::HookPcs();
    topScreenSourcePortBlocks.insert(topScreenSourcePortBlocks.end(),
                                    gameplayBlocks.begin(), gameplayBlocks.end());
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(),
                                        gameplayBlocks.begin(), gameplayBlocks.end());
    // These source ports are reached from inside large AOT regions. Make
    // their native entries observable so execution returns to the dispatcher
    // before applying the typed replacement. Keep this list sorted because
    // the whole-AOT block-entry callback uses binary_search.
    const std::array topScreenObservableExitBlocks{
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenGameplayCompositionPrefix,
        kTopScreenTitleLogoFadeHoldPrefix,
        // 3.0 0x005C91D0 skips the Quest draw in states the pause barrier
        // does not cover (pause camera, runtime scene latch).
        Oot3dNativeGame::kTopScreenQuestDrawHook,
    };
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        topScreenObservableExitBlocks.begin(),
        topScreenObservableExitBlocks.end());
#else
    // The product runs the same TopScreen source port as developer builds:
    // every block is an observable exit of the whole-AOT graph, because the
    // product keeps whole-AOT execution through pause routes.
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(),
                                       topScreenObservableExitBlocks.begin(),
                                       topScreenObservableExitBlocks.end());
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(),
                                       topScreenSourcePortBlocks.begin(),
                                       topScreenSourcePortBlocks.end());
    productTopScreenBlocks = topScreenSourcePortBlocks;
    productTopScreenBlocks.push_back(kPauseRendererVisibilityNativeDraw);
    productTopScreenBlocks.insert(productTopScreenBlocks.end(), itemEntries.begin(),
                                  itemEntries.end());
    std::sort(productTopScreenBlocks.begin(), productTopScreenBlocks.end());
    // The whole-AOT graph reports only its block starts: a hook or resume pc
    // that is not an entry of the title module would silently never run.
    // tools/oot3d/native_a32_runtime/whole_aot_observed_sites.json makes the
    // generator start a block at each of them.
    {
      std::vector<uint32_t> required = productTopScreenBlocks;
      for (const auto &contract :
           Oot3dNativeGame::TopScreenVerifiedControlFlowContracts())
        required.push_back(contract.Target);
      for (const auto &contract :
           Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts())
        required.push_back(contract.Entry + 4U);
      for (const auto pc : mapIconSites)
        required.push_back(pc + 4U);
      required.push_back(kTopScreenCameraFovScalar + 4U);
      const auto entries = Oot3dNativeGame::Oot3dWholeAotEntryPoints();
      std::vector<uint32_t> sortedEntries(entries.begin(), entries.end());
      std::sort(sortedEntries.begin(), sortedEntries.end());
      std::string missing;
      for (const uint32_t pc : required) {
        if (!std::binary_search(sortedEntries.begin(), sortedEntries.end(), pc)) {
          char text[16];
          std::snprintf(text, sizeof(text), " 0x%08X", pc);
          missing += text;
        }
      }
      if (!missing.empty())
        throw std::runtime_error(
            "the title module does not start a block at the TopScreen sites" + missing +
            "; rebuild it with whole_aot_observed_sites.json");
    }
#endif
    wholeAotObservableExitBlocks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    wholeAotObservableExitBlocks.push_back(kPauseRendererVisibilityNativeDraw);
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(),
                                       itemEntries.begin(), itemEntries.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  topScreenSourcePortBlocks.begin(),
                                  topScreenSourcePortBlocks.end());
  }
  {
    // Object bank extension, in every UI profile: its entries leave compiled
    // execution (while active) and reach the native-block dispatch.
    const auto hooks = Oot3dNativeGame::ObjectBankExtension::HookPcs();
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(), hooks.begin(),
                                        hooks.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(), hooks.begin(), hooks.end());
    // SingleScreen 3.0 rain, observed the same way.
    const auto rainHooks = Oot3dNativeGame::TopScreenRainAdapter::HookPcs();
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(), rainHooks.begin(),
                                        rainHooks.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(), rainHooks.begin(),
                                  rainHooks.end());
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    // Hooks, resumes and native calls must start blocks of the title module.
    std::vector<uint32_t> required(hooks.begin(), hooks.end());
    const auto resumes = Oot3dNativeGame::ObjectBankExtension::ResumePcs();
    const auto calls = Oot3dNativeGame::ObjectBankExtension::NativeCallPcs();
    required.insert(required.end(), resumes.begin(), resumes.end());
    required.insert(required.end(), calls.begin(), calls.end());
    for (const auto pcs : {Oot3dNativeGame::TopScreenRainAdapter::HookPcs(),
                           Oot3dNativeGame::TopScreenRainAdapter::ResumePcs(),
                           Oot3dNativeGame::TopScreenRainAdapter::NativeCallPcs()})
      required.insert(required.end(), pcs.begin(), pcs.end());
    const auto entries = Oot3dNativeGame::Oot3dWholeAotEntryPoints();
    std::vector<uint32_t> sortedEntries(entries.begin(), entries.end());
    std::sort(sortedEntries.begin(), sortedEntries.end());
    std::string missing;
    for (const uint32_t pc : required) {
      if (!std::binary_search(sortedEntries.begin(), sortedEntries.end(), pc)) {
        char text[16];
        std::snprintf(text, sizeof(text), " 0x%08X", pc);
        missing += text;
      }
    }
    if (!missing.empty())
      throw std::runtime_error("the title module does not start a block at the object bank sites" +
                               missing + "; rebuild it with whole_aot_observed_sites.json");
#endif
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
  widescreenProjection.ActorVisibility.HostMemory = &process.Memory();
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
  // The free camera outlives this reset: MM3D's camera site already holds it.
  auto devFreeCamera = std::move(nativeCandidateDispatch.DevFreeCamera);
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
  nativeCandidateDispatch.DevFreeCamera = std::move(devFreeCamera);
  nativeCandidateDispatch.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
  // After the rebuild above, which would discard an earlier assignment.
  nativeCandidateDispatch.ProductTopScreenBlocks = std::move(productTopScreenBlocks);
  widescreenProjection.TopScreenInput =
      &nativeCandidateDispatch.TopScreenInput;
  widescreenProjection.TopScreenInputClock =
      &nativeCandidateDispatch.TopScreenInputClock;
  widescreenProjection.ObservedExitPc = &nativeCandidateDispatch.ObservedExitPc;
  nativeCandidateDispatch.TopScreenPausePageRedrawActive =
      &widescreenProjection.TopScreenPausePageRedrawActive;
  widescreenProjection.TopScreenItems = &nativeCandidateDispatch.TopScreenItems;
  // After the dispatch state is rebuilt: an earlier pointer would name the
  // free camera that assignment destroyed.
  widescreenProjection.DevFreeCamera = nativeCandidateDispatch.DevFreeCamera.get();
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
  // The object bank extension runs in every UI profile; TopScreen blocks
  // are still gated inside the hook.
  process.SetNativeBlockCallback(&ExecuteProductTopScreenHook,
                                 &nativeCandidateDispatch);
#endif
  if (!Oot3dNativeGame::MountNativeA32ProcessImage(
          process, *manifest, std::filesystem::path{}, &error)) {
    throw std::runtime_error(error);
  }
  if (titleSession) titleSession->ImageMounted(process);
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
  // The user's profiles beside the configuration; the release's defaults in
  // the runtime resources.
  // A title without authored tuning keeps its native lighting (D1 of the
  // alignment: OoT3D's base profile is OoT3D's), except in the groups whose
  // base its session supplies.
  Oot3dNativeGame::SceneEnvironmentFallback sceneEnvironmentBase(
      titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur
          ? Oot3dNativeGame::BaseSceneEnvironmentProfile()
          : Oot3dNativeGame::NativePreservingSceneEnvironmentProfile());
  if (titleSession) {
    for (size_t group = 0; group < sceneEnvironmentBase.Groups.size(); ++group) {
      Oot3dNativeGame::SceneEnvironmentProfile profile;
      if (titleSession->BaseSceneEnvironment(Oot3dNativeGame::SceneEnvironmentGroup(group), profile))
        sceneEnvironmentBase.Groups[group] = std::move(profile);
    }
  }
  sceneEnvironment = std::make_shared<Oot3dNativeGame::SceneEnvironmentRuntime>(
      Oot3dNativeGame::TitleSceneEnvironmentDirectory(launch.Host.ConfigurationPath, titleAdapter),
      [](const Fast::Renderer3ds::PicaEnvironmentLighting& environment) {
        Fast::Extension::GraphicsSettingsRuntime::Instance().PublishSceneEnvironment(environment);
      },
      Oot3dNativeGame::TitleReleaseSceneEnvironmentDirectory(launch.Host.ResourceRoot, titleAdapter),
      std::move(sceneEnvironmentBase));
  // F1 order: the scene lights join Lighting > Lights & Shadows; then
  // Controls, HUD and Game (Save states, Language) after the graphics tabs.
  // Only adapters with a typed scene provider expose scene profiles.
  if (titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur ||
      titleAdapter == Oot3dNativeGame::TitleAdapter::Mm3d) {
    applicationSettingsTabs.push_back(Oot3dNativeGame::CreateSceneEnvironmentPanel(
        sceneEnvironment, {.NativeFragmentLighting = titleSession && titleSession->NativeFragmentLighting(),
                           .CompanionLightName = titleSession ? std::string(titleSession->CompanionLightName()) : "Navi"}));
  }
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  // The developer tours, lab and emitter inventory drive OoT3D's guest
  // (scenes, entrances, actors): unavailable for another title.
  const bool oot3dDeveloperTools = titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur;
  if (const char *tour = std::getenv("TRIAEVUM_DEV_FREECAM_TOUR");
      oot3dDeveloperTools && tour != nullptr && *tour != '\0') {
    std::string text = tour;
    size_t start = 0;
    while (start < text.size()) {
      const size_t end = std::min(text.find(';', start), text.size());
      DevFreeCameraTourPose pose;
      std::string entry = text.substr(start, end - start);
      if (!entry.empty() && entry.front() == '@') {
        pose.Absolute = true;
        entry.erase(0, 1);
      }
      if (std::sscanf(entry.c_str(), "%f,%f,%f,%f,%f", &pose.Forward, &pose.Right, &pose.Up, &pose.Yaw,
                      &pose.Pitch) == 5)
        devTourPoses.push_back(pose);
      start = end + 1;
    }
    const char *out = std::getenv("TRIAEVUM_DEV_FREECAM_TOUR_OUT");
    devTourDirectory = out != nullptr ? std::filesystem::path(out) : std::filesystem::current_path();
    std::filesystem::create_directories(devTourDirectory);
    // A boot warp point is noticed when it is requested (UpdateDevFreeCameraTour).
    devTourAfterWarp = std::getenv("TRIAEVUM_DEV_WARP") != nullptr;
    std::fprintf(stderr, "TRIAEVUM_DEV_FREECAM_TOUR poses=%zu out=%s\n", devTourPoses.size(),
                 devTourDirectory.string().c_str());
  }
  // Another title with developer tools gets the lab's title-neutral ops only
  // (its own warp script, captures, the F1 window): UpdateDevLab rejects the rest.
  if (const char *lab = std::getenv("TRIAEVUM_DEV_LAB");
      (oot3dDeveloperTools || devToolsTitle) && lab != nullptr && *lab != '\0') {
    devLab = std::make_unique<Oot3dNativeGame::DevLabChannel>(lab);
    std::fprintf(stderr, "TRIAEVUM_DEV_LAB %s\n", lab);
  }
  if (const char *emitters = std::getenv("TRIAEVUM_DEV_EMITTERS");
      oot3dDeveloperTools && emitters != nullptr && *emitters != '\0') {
    devEmitterLog = std::make_unique<Oot3dNativeGame::NativeEmitterInventoryLog>(emitters);
    std::fprintf(stderr, "TRIAEVUM_DEV_EMITTERS %s\n", emitters);
  }
  if (const char *trace = std::getenv("TRIAEVUM_SURFACE_TRACE"); trace != nullptr && *trace != '\0') {
    // PATH,HOST_FRAME[,FRAMES]: a surface-lighting trace of any title.
    const std::string spec = trace;
    const auto first = spec.find(',');
    if (first == std::string::npos)
      throw std::runtime_error("TRIAEVUM_SURFACE_TRACE must be PATH,HOST_FRAME[,FRAMES]");
    const auto second = spec.find(',', first + 1);
    devSurfaceTracePath = spec.substr(0, first);
    devSurfaceTraceFrame = static_cast<uint32_t>(std::stoul(spec.substr(first + 1, second - first - 1)));
    if (second != std::string::npos)
      devSurfaceTraceFrames = static_cast<uint32_t>(std::stoul(spec.substr(second + 1)));
    devSurfaceTraceFrame = std::max(devSurfaceTraceFrame, 1U);
  }
  // Agents edit scene profiles on disk while the game runs.
  if (const char *reload = std::getenv("TRIAEVUM_ENVIRONMENT_HOT_RELOAD"); reload != nullptr && *reload != '0') {
    sceneEnvironment->SetHotReload(true);
    std::fprintf(stderr, "TRIAEVUM_ENVIRONMENT_HOT_RELOAD enabled\n");
  }
#endif
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
      Oot3dNativeGame::CreateSavestatePanel(
          savestateLibrary, titleSession ? std::string(titleSession->SavestateSettingsNotice())
                                        : std::string{}));
  applicationSettingsTabs.push_back(Oot3dNativeGame::CreateGameLanguagePanel(languageSettings));
#if defined(__ANDROID__)
  constexpr bool kOfferAudioFocusMute = false;
#else
  constexpr bool kOfferAudioFocusMute = true;
#endif
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreateAudioSettingsPanel(Oot3dNativeGame::HostAudioOutputPolicy, kOfferAudioFocusMute));
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreatePhotoModePanel(nativeCandidateDispatch.DevFreeCamera, photoMode));
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  // Shared developer tools; guest ABI and catalog are supplied by the title.
  if (titleAdapter == Oot3dNativeGame::TitleAdapter::Oot3dEur || devToolsTitle) {
  devWarp = std::make_shared<Oot3dNativeGame::DevWarpRuntime>(
      std::filesystem::path(launch.Host.ConfigurationPath).parent_path() /
          (devToolsTitle ? "developer-tools-mm3d.json" : "developer-tools.json"), devToolsTitle);
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreateDevToolsPanel(devWarp, languageSettings->BootLanguage()));
  // Scripted check of the Better list without the menu:
  // TRIAEVUM_DEV_WARP=<scene>:<entrance>:<child|adult>:<day|night> (list
  // indices), requested when gameplay first starts.
  // TRIAEVUM_DEV_FREECAM=<x>,<y>,<z>,<yaw>,<pitch>: the free camera at a fixed
  // pose from the start (degrees; yaw 0 faces +Z, pitch positive up).
  if (const char *pose = std::getenv("TRIAEVUM_DEV_FREECAM")) {
    std::array<float, 3> eye{};
    float yaw = 0.0F, pitch = 0.0F;
    if (std::sscanf(pose, "%f,%f,%f,%f,%f", &eye[0], &eye[1], &eye[2], &yaw, &pitch) != 5)
      throw std::runtime_error("TRIAEVUM_DEV_FREECAM must be <x>,<y>,<z>,<yaw>,<pitch>");
    nativeCandidateDispatch.DevFreeCamera->SetPose(eye, yaw, pitch);
    std::fprintf(stderr, "TRIAEVUM_DEV_FREECAM pose=%.1f,%.1f,%.1f yaw=%.1f pitch=%.1f\n", eye[0], eye[1], eye[2], yaw, pitch);
  }
  if (devToolsTitle) {
    if (const char* script = std::getenv("TRIAEVUM_DEV_WARP"); script && titleSession)
      titleSession->RunDevWarpScript(script, *devWarp);
  } else if (const char *script = std::getenv("TRIAEVUM_DEV_WARP")) {
    int scene = -1, entrance = -1;
    char age[8]{}, time[8]{};
    const auto scenes = Oot3dNativeGame::DevWarpBetterScenes();
    if (std::sscanf(script, "%d:%d:%7[a-z]:%7[a-z]", &scene, &entrance, age, time) != 4 || scene < 0 ||
        scene >= static_cast<int>(scenes.size()) || entrance < 0 ||
        entrance >= static_cast<int>(scenes[static_cast<size_t>(scene)].Count)) {
      throw std::runtime_error("TRIAEVUM_DEV_WARP must be <scene>:<entrance>:<child|adult>:<day|night>");
    }
    devWarp->WarpTo(Oot3dNativeGame::DevWarpBetterEntrances()[scenes[static_cast<size_t>(scene)].First +
                                                               static_cast<size_t>(entrance)],
                    std::string_view(age) == "adult" ? Oot3dNativeGame::DevWarpAge::Adult
                                                     : Oot3dNativeGame::DevWarpAge::Child,
                    std::string_view(time) == "night");
  }
  // TRIAEVUM_DEV_WARP_ENTRANCE=<entrance>:<scene>:<child|adult>:<day|night>:
  // any native entrance, for oracle fixtures outside the Better list (0x28A,
  // the Lon Lon fence jump, spawns the adult player mounted on Epona).
  // OoT3D's entrance table: another title's dev tools ignore it.
  if (const char *script = devToolsTitle ? nullptr : std::getenv("TRIAEVUM_DEV_WARP_ENTRANCE")) {
    int entrance = -1, scene = -1;
    char age[8]{}, time[8]{};
    if (std::sscanf(script, "%i:%i:%7[a-z]:%7[a-z]", &entrance, &scene, age, time) != 4 || entrance < 0 ||
        entrance > 0xFFFF || scene < 0 || scene > 0xFF) {
      throw std::runtime_error("TRIAEVUM_DEV_WARP_ENTRANCE must be <entrance>:<scene>:<child|adult>:<day|night>");
    }
    const Oot3dNativeGame::DevWarpEntrance target{script, script, script, static_cast<uint16_t>(entrance),
                                                  static_cast<uint8_t>(scene), 0, -1};
    devWarp->WarpTo(target,
                    std::string_view(age) == "adult" ? Oot3dNativeGame::DevWarpAge::Adult
                                                     : Oot3dNativeGame::DevWarpAge::Child,
                    std::string_view(time) == "night");
  }
  }
#endif
  Fast::Extension::InstallGraphicsSettingsPanelTabs(
      std::move(applicationSettingsTabs));
  if (const char* title = Oot3dNativeGame::TitleGraphicsWindowTitle(titleAdapter)) {
    Fast::Extension::InstallGraphicsSettingsWindowTitle(title);
  }
  // The renderer reads it when the context starts it.
  Fast::Renderer3ds::InstallTitleShaderPreparation(
      {Oot3dNativeGame::TitlePreparedShaders(titleAdapter, titleSession.get()),
       std::string(Oot3dNativeGame::TitlePipelineCacheNamespace(titleAdapter, titleSession.get()))});
  InitContextForDemo(hostArgs);
  // What the title itself provides to the graphics settings: the effects
  // that need a scene view, and the scene FOV, stay inactive without it.
  // After the context: it installs the settings' persistence first.
  {
    auto &settingsRuntime = Fast::Extension::GraphicsSettingsRuntime::Instance();
    const std::string titleName = titleSession ? titleSession->GameName() : "This game";
    settingsRuntime.SetCapability(
        Fast::Extension::GraphicsCapability::TitleSceneView,
        Oot3dNativeGame::TitlePublishesSceneView(titleAdapter),
        titleName + " does not publish its scene view yet.");
    settingsRuntime.SetCapability(
        Fast::Extension::GraphicsCapability::TitleSceneFov,
        Oot3dNativeGame::TitleAppliesSceneFov(titleAdapter),
        titleName + " has no scene projection hooks yet; the FOV has no effect.");
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("host_context_initialized");
#endif
  if (!savestateMigrationMessage.empty())
    SPDLOG_INFO("oot3d_native_game: {}", savestateMigrationMessage);
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE) && defined(_WIN32)
  // --crash-directory (D/logs/crashes), else the earlier <save data>/crashes.
  std::string crashDiagnosticsError;
  if (!Oot3dNativeGame::InstallWholeAotCrashDiagnostics(
          launch.Routing.CrashDirectory.empty() ? saveDataDirectory / "crashes" : launch.Routing.CrashDirectory,
          &crashDiagnosticsError)) {
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
  const bool titleFragmentLighting = titleSession && titleSession->NativeFragmentLighting();
  api.SetNativePicaFragmentLightingOperand(titleFragmentLighting);
  api.SetNativePicaFragmentLitShadowReceivers(titleFragmentLighting);
  programPreparation =
      dynamic_cast<Fast::Renderer3ds::PicaProgramPreparationBackend *>(&api);
  alwaysAvailablePrograms =
      programPreparation != nullptr &&
      programPreparation->UsesAlwaysAvailablePrograms();
  // The title pass programs serve the shared renderer passes (shadow
  // casters among them) of every title: registering them is a lookup table,
  // and MM3D's draws hit them.
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
      dynamic_cast<Fast::Extension::TitleRenderBackend *>(&api);
  gui =
      Ship::Context::GetRawInstance()->GetWindow()->GetGui();
  if (gui == nullptr) {
    throw std::runtime_error("native PICA host has no renderer GUI");
  }
  // The in-game menu (Esc / R3): the controller-friendly subset, through the
  // same owners as the F1 tabs above. Built only now, after the renderer
  // window: its first snapshot creates the graphics settings runtime, which
  // reads the settings file through the persistence port the window's GUI
  // installs. Built earlier, the runtime would start without the port, run the
  // launch on the defaults ("session only") and never load or save the file.
  // The window host's part: its folders, the help card's record in the
  // runtime configuration, F1 and the normal exit.
  Oot3dNativeGame::InGameMenuHost menuHost;
  menuHost.SaveDataDirectory = saveDataDirectory;
  menuHost.LogsDirectory = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(
      Ship::Context::GetLogDirectory().c_str())));
#if defined(__ANDROID__)
  menuHost.Controls = Oot3dNativeGame::Menu::HelpControls::Touch;
  menuHost.InstallerRoute = "Close the game, then the TriAevum installer: Saves, Diagnostics";
  // "Export saves" / "Share diagnostics": the activity opens the installer's
  // page once the game has quit through the normal exit path (as Quit game).
  menuHost.OpenInstallerPage = [this](std::string_view page, std::string *error) {
    if (!RequestAndroidInstallerPage(page, error)) return false;
    menuQuitRequested = true;
    return true;
  };
#elif !defined(__SWITCH__)
  menuHost.OpenFolder = OpenFolderInFileBrowser;
#endif
  menuHost.HelpCardShown = [] {
    auto *context = Ship::Context::GetRawInstance();
    return context != nullptr && context->GetConfig() != nullptr && context->GetConfig()->GetBool("Help.Shown", false);
  };
  menuHost.RememberHelpCardShown = [](std::string *error) {
    auto *context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetConfig() == nullptr) {
      *error = "no runtime configuration";
      return false;
    }
    context->GetConfig()->SetBool("Help.Shown", true);
    if (!context->GetConfig()->Save()) {
      *error = "the configuration file could not be written";
      return false;
    }
    return true;
  };
  menuHost.OpenAdvancedSettings = [this] {
    if (auto f1 = gui->GetGuiWindow("OOT3D Graphics"); f1 != nullptr) f1->Show();
  };
  menuHost.QuitGame = [this] { menuQuitRequested = true; };
  inGameMenuStorage.emplace(Oot3dNativeGame::InGameMenuOwners{
      controlConfigRuntime,
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen ? topScreenConfigRuntime : nullptr,
      languageSettings, savestateLibrary, nativeCandidateDispatch.DevFreeCamera, photoMode, std::move(menuHost),
      Oot3dNativeGame::HostAudioOutputPolicy()});
  // The first-play help card belongs to sessions a person plays: scripted
  // runs (captures, fixed steps, replays, the developer lab) never see it.
  // TRIAEVUM_HELP_CARD=1 or 0 overrides that, for qualification.
  helpCardEligible = !hostArgs.Scripted();
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  if (std::getenv("TRIAEVUM_DEV_LAB") != nullptr) helpCardEligible = false;
#endif
  if (const char *override = std::getenv("TRIAEVUM_HELP_CARD"); override != nullptr && *override != '\0')
    helpCardEligible = std::string_view(override) == "1";
#if defined(__SWITCH__)
  WriteSwitchBootStage("opengl_renderer_initialized");
#endif
  auto &nativeUiTextureProvider = nativeUiTextureProviderStorage.emplace(
      picaMemoryView, topScreenTextureOverridePackPointer);
  // SingleScreen 3.0 HD UI atlas (Forge import): the 3.0 atlas at a higher
  // resolution, which also draws elements the native atlas lacks. It applies
  // to the exact native atlas bytes only, and a user's texture pack that
  // replaces the atlas wins. UI coordinates are normalized.
  std::shared_ptr<const std::vector<uint8_t>> uiAtlasHdPixels;
  uint32_t uiAtlasHdWidth = 0, uiAtlasHdHeight = 0;
  std::span<const uint8_t> uiAtlasNativePayload;
  if (!launch.TopScreenUiAtlasHdPath.empty()) {
    const auto *atlas = topScreenTextureOverridePack.FindProfileTexture("oot3d/topscreen/3.0/ui_atlas");
    if (atlas == nullptr)
      throw std::runtime_error("--topscreen-ui-atlas-hd needs the SingleScreen 3.0 atlas in the TopScreen pack");
    int width = 0, height = 0, channels = 0;
    stbi_uc *decoded = stbi_load(launch.TopScreenUiAtlasHdPath.string().c_str(), &width, &height, &channels, 4);
    if (decoded == nullptr)
      throw std::runtime_error("cannot decode the TopScreen HD UI atlas " + launch.TopScreenUiAtlasHdPath.string());
    const bool scaled = width > 0 && width % atlas->Width == 0 && height > 0 &&
                        static_cast<uint64_t>(width) * atlas->Height == static_cast<uint64_t>(height) * atlas->Width;
    if (scaled)
      uiAtlasHdPixels = std::make_shared<const std::vector<uint8_t>>(
          decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);
    stbi_image_free(decoded);
    if (!scaled)
      throw std::runtime_error("the TopScreen HD UI atlas is not a multiple of the native atlas size");
    uiAtlasHdWidth = static_cast<uint32_t>(width);
    uiAtlasHdHeight = static_cast<uint32_t>(height);
    uiAtlasNativePayload = atlas->EncodedPayload;
  }
  // The HUD the UI renderer draws resolves the user's texture pack exactly as
  // native PICA uploads do (same identity, same dumping, same async decode).
  nativeUiTextureProvider.SetCustomTextureSource({
      [uiAtlasHdPixels, uiAtlasHdWidth, uiAtlasHdHeight, uiAtlasNativePayload](
          const Oot3dNativeGame::Oot3dNativeUiCustomTextureRequest &request) {
        using Oot3d::Renderer::AzaharTextureResolveState;
        const auto result = Oot3d::Renderer::AzaharTexturePackRuntime::Instance().ResolveOrQueue({
            .Width = request.width,
            .Height = request.height,
            .NativeFormat = request.native_pica_format,
            .MipLevel = 0U,
            .NativeBytes = request.encoded_base_level,
            .NativeRgba8 = request.rgba8,
        });
        Oot3dNativeGame::Oot3dNativeUiCustomTexture custom;
        if (result.State == AzaharTextureResolveState::Pending) {
          custom.state = Oot3dNativeGame::Oot3dNativeUiCustomTexture::State::Pending;
        } else if (result.State == AzaharTextureResolveState::Ready && result.Replacement &&
                   result.Replacement->Rgba8) {
          custom.state = Oot3dNativeGame::Oot3dNativeUiCustomTexture::State::Ready;
          custom.width = result.Replacement->Width;
          custom.height = result.Replacement->Height;
          custom.rgba8 = result.Replacement->Rgba8;
        } else if (uiAtlasHdPixels != nullptr &&
                   std::ranges::equal(request.encoded_base_level, uiAtlasNativePayload)) {
          custom.state = Oot3dNativeGame::Oot3dNativeUiCustomTexture::State::Ready;
          custom.width = uiAtlasHdWidth;
          custom.height = uiAtlasHdHeight;
          custom.rgba8 = uiAtlasHdPixels;
        }
        return custom;
      },
      [] { return Oot3d::Renderer::AzaharTexturePackRuntime::Instance().Generation(); },
  });
  if (hdFontsLoaded) nativeUiTextureProvider.SetGeneratedTextureTransform(
      [&](const Oot3dNativeGame::Oot3dPicaTextureState& texture,
          Oot3dNativeGame::Oot3dPicaResourceSnapshot& resource) {
        hdFonts.Resolve(picaMemoryView, texture, resource);
      });
  auto &ocarinaText = ocarinaTextStorage.emplace(process.Memory(), nativeUiTextureProvider);
  nativeCandidateDispatch.OcarinaText = &ocarinaText;
  widescreenProjection.OcarinaText = &ocarinaText;
  auto &pauseTransaction = pauseTransactionStorage.emplace(process.Memory());
  nativeCandidateDispatch.PauseTransaction = &pauseTransaction;
  widescreenProjection.PauseTransaction = &pauseTransaction;
  auto &systemPage = systemPageStorage.emplace(process.Memory());
  nativeCandidateDispatch.SystemPage = &systemPage;
  widescreenProjection.SystemPage = &systemPage;
  auto &itemDescription = itemDescriptionStorage.emplace(process.Memory());
  nativeCandidateDispatch.ItemDescription = &itemDescription;
  widescreenProjection.ItemDescription = &itemDescription;
  auto &wideItems = wideItemsStorage.emplace(process.Memory());
  nativeCandidateDispatch.WideItems = &wideItems;
  nativeCandidateDispatch.Visions = &visionsStorage.emplace(process.Memory());
  nativeCandidateDispatch.ReframedFileSelect = &reframedFileSelectStorage.emplace(process.Memory());
  widescreenProjection.WideItems = &wideItems;
  // SingleScreen 3.0 shortcut column: reordering previews the host config;
  // saving stays an explicit F1 action.
  wideItems.SetShortcutOrder(activeTopScreenConfig.ItemsShortcutOrder);
  wideItems.SetShortcutOrderChanged(
      [this](const Oot3dNativeGame::TopScreenItemsShortcutOrder &order) {
        auto config = topScreenConfigRuntime->Snapshot().Config;
        config.ItemsShortcutOrder = order;
        topScreenConfigRuntime->Preview(config);
      });
  // Hold A on a shortcut row: 3.0's in-page D-pad picker previews the
  // age's mapping; saving stays an explicit F1 action.
  wideItems.SetDpadAssignments(activeTopScreenConfig.ChildDpad,
                               activeTopScreenConfig.AdultDpad);
  wideItems.SetDpadAssignmentChanged(
      [this](bool child, const Oot3dNativeGame::TopScreenDpadAssignments &mapping) {
        auto config = topScreenConfigRuntime->Snapshot().Config;
        (child ? config.ChildDpad : config.AdultDpad) = mapping;
        topScreenConfigRuntime->Preview(config);
      });
  uiLifecycleBridge.SetTopScreenItemsShortcutSource(&wideItems);
  auto &gameplay = gameplayStorage.emplace(process.Memory(),
                                           launch.TopScreenLinkAnimationsPath);
  gameplay.SetConfig(activeTopScreenConfig);
  nativeCandidateDispatch.Gameplay = &gameplay;
  widescreenProjection.TopScreenGameplay = &gameplay;
  // Extended draw distance enables it (UpdateScenePresentation); its region
  // maps on first use.
  auto &objectBank = objectBankStorage.emplace(process.Memory());
  nativeCandidateDispatch.ObjectBank = &objectBank;
  widescreenProjection.ObjectBank = &objectBank;
  auto &rain = rainStorage.emplace(process.Memory());
  nativeCandidateDispatch.Rain = &rain;
  widescreenProjection.Rain = &rain;
  ApplyTopScreenRainConfig();
  auto &uiRenderBackend = uiRenderBackendStorage.emplace(api);
  // The in-game menu's baked fonts answer their own texture names; everything
  // else still resolves through the ocarina text and native providers.
  auto &menuFontProvider = menuFontProviderStorage.emplace(&ocarinaText);
  auto &n64UiRenderer = n64UiRendererStorage.emplace(uiRenderBackend,
                                               &menuFontProvider);
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
  if (const char *path = std::getenv("OOT3D_PICA_RAW_DRAWS"); path != nullptr && path[0] != '\0') {
    std::string recordError;
    if (!picaRawDrawRecorder.Configure(path, launch.PicaParametricTev, alwaysAvailablePrograms, &recordError))
      throw std::runtime_error(recordError);
  }
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
    if (titleSession) titleSession->Reset();
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
      Fast::Extension::ResolvePresentationPacingPolicy(
          Fast::Extension::GraphicsSettingsRuntime::Instance()
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
        RequireStoppedGuest("memory read");
        return picaMemoryView.Read(address, bytes);
      },
      [&](std::span<const Fast::Renderer3ds::PicaPhysicalMemoryWrite> writes) {
        RequireStoppedGuest("memory write");
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
        RequireStoppedGuest("raw copy");
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
        RequireStoppedGuest("PPF interrupt");
        if (!hostServices.SignalPicaInterrupt(process.Memory(), Oot3dNativeGame::Oot3dPicaInterruptId::Ppf)) return false;
        ++picaStats.CompletedPpfCount;
        ++picaStats.RecordedCompletionCount;
        return true;
      },
      [&]() {
        RequireStoppedGuest("deferred writes");
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

  {
    // The launch profile chooses the admission policy; harnesses override it:
    // TRIAEVUM_INPUT_ADMISSION=0 never admits (early sampling), =1 admits
    // without late sampling, on|off|auto select the policy itself.
    auto admissionMode = launch.InputAdmission;
    inputAdmissionLateSampling = true;
    if (const char *admission = std::getenv("TRIAEVUM_INPUT_ADMISSION")) {
      const std::string_view value(admission);
      if (value == "0") {
        admissionMode = Oot3dNativeGame::NativeInputAdmissionMode::Off;
      } else if (value == "1") {
        admissionMode = Oot3dNativeGame::NativeInputAdmissionMode::On;
        inputAdmissionLateSampling = false;
      } else {
        (void)Oot3dNativeGame::ParseNativeInputAdmissionMode(value, &admissionMode);
      }
    }
    inputAdmission = Oot3dNativeGame::NativeInputAdmissionController(admissionMode);
  }

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

// Rain is a TopScreen gameplay option: other UI profiles keep native rain.
void NativeA32WindowSession::ApplyTopScreenRainConfig() {
  if (!rainStorage) return;
  rainStorage->SetConfig(nativeCandidateDispatch.TopScreenUiProfile
                             ? Oot3dNativeGame::TopScreenRainConfig{
                                   activeTopScreenConfig.Rain3d,
                                   activeTopScreenConfig.Rain3dDensityPercent}
                             : Oot3dNativeGame::TopScreenRainConfig{});
}

void NativeA32WindowSession::ApplyTopScreenConfigSnapshot() {
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  const auto snapshot = topScreenConfigRuntime->Snapshot();
  if (snapshot.Revision == appliedTopScreenConfigRevision) {
    return;
  }
  activeTopScreenConfig = snapshot.Config;
  appliedTopScreenConfigRevision = snapshot.Revision;
  PublishTopScreenUiConfig();
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  if (gameplayStorage) gameplayStorage->SetConfig(activeTopScreenConfig);
  ApplyTopScreenRainConfig();
  if (wideItemsStorage) {
    wideItemsStorage->SetShortcutOrder(activeTopScreenConfig.ItemsShortcutOrder);
    wideItemsStorage->SetDpadAssignments(activeTopScreenConfig.ChildDpad,
                                         activeTopScreenConfig.AdultDpad);
  }
  Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
      nativeCandidateDispatch.TopScreenCamera.Camera,
      activeTopScreenConfig);
  if (!activeTopScreenConfig.FreeCameraEnabled) {
    nativeCandidateDispatch.TopScreenCameraInput.Reset();
  }
}

void NativeA32WindowSession::SetTopScreenSessionMinimapVisible(bool visible) {
  activeTopScreenConfig.MinimapVisible = visible;
  PublishTopScreenUiConfig();
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
}

void NativeA32WindowSession::PublishTopScreenUiConfig() {
  // Photo mode's Hide HUD is a session state over the saved configuration:
  // the TopScreen compositor sees render_hud off, the configuration keeps it.
  auto shown = activeTopScreenConfig;
  if (photoModeHudHidden) shown.RenderHud = false;
  uiLifecycleBridgeStorage->SetTopScreenConfig(shown);
}

void NativeA32WindowSession::ApplyPhotoModeHud() {
  const bool hidden = photoMode->HideHud.load(std::memory_order_relaxed);
  if (hidden != photoModeHudHidden) {
    photoModeHudHidden = hidden;
    PublishTopScreenUiConfig();
  }
  // The game's own gameplay HUD draws (the action icon, the minimap), which
  // the TopScreen compositor does not draw, follow render_hud as well.
  picaPresentationScheduler.SetNativeGameplayHudVisible(
      !hidden && (!nativeCandidateDispatch.TopScreenUiProfile || activeTopScreenConfig.RenderHud));
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
  const auto graphics = Fast::Extension::GraphicsSettingsRuntime::Instance().Snapshot();
  // The picture is the aspect ratio's letterbox in the window; the scene's
  // FOV policy follows the picture's shape (the renderer sizes its targets
  // to the same picture), never the window's.
  presentationPicture = Fast::Extension::ResolvePresentationPicture(
      std::max<uint32_t>(1, window.GetWidth()),
      std::max<uint32_t>(1, window.GetHeight()), graphics.Aspect);
  widescreenProjection.Projection.Presentation =
      Fast::Extension::ResolveScenePresentationPolicy(
          std::max<uint32_t>(1, presentationPicture.Width),
          std::max<uint32_t>(1, presentationPicture.Height));
  widescreenProjection.Projection.FovMultiplier = graphics.FovMultiplier;
  widescreenProjection.Projection.TitleOwnsSceneProjection = titleSession && titleSession->OwnsSceneProjection();
  if (titleSession) {
    const auto& presentation = widescreenProjection.Projection.Presentation;
    titleSession->SetPresentation({graphics.FovMultiplier, presentation.HorizontalFovExpansion,
                                   presentation.VerticalFovExpansion, graphics.ExtendedDrawDistance});
  }
  auto &visibility = widescreenProjection.ActorVisibility;
  visibility.ExtendedDrawDistance = graphics.ExtendedDrawDistance;
  visibility.Presentation = widescreenProjection.Projection.Presentation;
  visibility.FovMultiplier = graphics.FovMultiplier;
  // Off stops new extra loads; resident slots keep their hooks until they
  // retire (ObjectBankExtension::ShouldObserve).
  if (objectBankStorage) objectBankStorage->SetEnabled(graphics.ExtendedDrawDistance);
}

Fast::Renderer3ds::PicaFrameCompositionPolicy NativeA32WindowSession::ResolveFrameCompositionPolicy(const Fast::Extension::PresentationPacingPolicy& pacing) {
  auto policy = pacing.Composition;
  policy.NativeRateHz = frameRateContract.SimulationRateHz;
  if (!frameRateContract.VisualInterpolationAllowed) {
    policy.Interpolation =
        Fast::Extension::NativeVisualInterpolationMode::Disabled;
    policy.FixedSampleMultiplier = 1U;
  }
  return policy;
}

} // namespace Oot3dA32Window
