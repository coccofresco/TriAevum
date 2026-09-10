#include "oot3d_native_game_bootstrap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "ship/Context.h"
#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeActorRenderProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"
#include "ship/resource/File.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"

#include "oot3d_native_abi_catalog.h"
#include "oot3d_native_player_collision_action_contract.h"
#include "oot3d_native_actor_core_contract.h"
#include "oot3d_native_actor_runtime.h"
#include "oot3d_native_closure_catalog.h"
#include "oot3d_native_frame_rate.h"
#include "oot3d_native_player_animation_catalog.h"
#include "oot3d_link_runtime_types.h"

namespace {

ThreeDsRecomp::Oot3d::NativeSourceProvider::FileLoader NativeArchiveSourceLoader(
    std::shared_ptr<Ship::ArchiveManager> archiveManager) {
    return [archiveManager = std::move(archiveManager)](
               const std::string& path)
               -> std::shared_ptr<const std::vector<uint8_t>> {
        const auto file =
            archiveManager == nullptr ? nullptr : archiveManager->LoadFile(path);
        if (file == nullptr || file->Buffer == nullptr ||
            file->BufferOffset > file->Buffer->size()) {
            return nullptr;
        }
        return std::make_shared<const std::vector<uint8_t>>(
            file->Buffer->begin() +
                static_cast<std::ptrdiff_t>(file->BufferOffset),
            file->Buffer->end());
    };
}

uint32_t ParseU32(const std::string& value, const char* name) {
    size_t parsed = 0;
    const unsigned long out = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || out > UINT32_MAX) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<uint32_t>(out);
}

double ParseDouble(const std::string& value, const char* name) {
    size_t parsed = 0;
    const double out = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(out)) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return out;
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open JSON input: " + path.string());
    }
    nlohmann::json document;
    stream >> document;
    return document;
}

std::vector<uint8_t> ReadBinary(const std::filesystem::path& path,
                                const char* role) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error(std::string("could not open ") + role +
                                 ": " + path.string());
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        throw std::runtime_error(std::string(role) + " is empty: " +
                                 path.string());
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error(std::string("could not read ") + role +
                                 ": " + path.string());
    }
    return bytes;
}

std::filesystem::path ResolveManifestPath(const std::filesystem::path& manifestPath,
                                          std::string_view value) {
    std::filesystem::path path(value);
    if (path.is_relative()) {
        path = manifestPath.parent_path() / path;
    }
    return path.lexically_normal();
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

void ValidateNativeAuthority(std::string_view field, std::string_view value) {
    if (!value.empty() && value != "oot3d_native") {
        throw std::runtime_error("route authority " + std::string(field) +
                                 " is not OOT3D-native: " + std::string(value));
    }
}

struct PlayableSceneShard {
    std::string Name;
    std::vector<std::filesystem::path> Archives;
};

std::filesystem::path ResolvePlayableCoreArchive(
    const std::filesystem::path& playablePackPath) {
    const auto pack = ReadJson(playablePackPath);
    if (pack.value("format", "") != "oot3d_playable_pack_v1" ||
        pack.value("status", "") != "complete" ||
        !pack.contains("core_archive") || !pack.at("core_archive").is_object()) {
        throw std::runtime_error("native game playable pack has no core archive");
    }
    const auto& core = pack.at("core_archive");
    if (core.value("status", "") != "ready" || !core.contains("path") ||
        !core.at("path").is_string() ||
        core.value("native_abi_catalog_resource", "") !=
            Oot3d::kNativeAbiCatalogResource) {
        throw std::runtime_error("native game playable core archive metadata is invalid");
    }
    const auto path = ResolveManifestPath(
        playablePackPath, core.at("path").get<std::string>());
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("native game playable core archive is missing: " +
                                 path.string());
    }
    return path;
}

PlayableSceneShard ResolvePlayableSceneShard(
    const std::filesystem::path& playablePackPath,
    const std::vector<std::string>& requiredAssetIds) {
    const auto pack = ReadJson(playablePackPath);
    if (pack.value("format", "") != "oot3d_playable_pack_v1" ||
        pack.value("status", "") != "complete" ||
        !pack.contains("shards") || !pack.at("shards").is_array()) {
        throw std::runtime_error("native game playable-pack manifest is invalid");
    }

    const nlohmann::json* selected = nullptr;
    for (const auto& shard : pack.at("shards")) {
        if (!shard.is_object() || !shard.contains("asset_ids") ||
            !shard.at("asset_ids").is_array()) {
            continue;
        }
        const auto ownsAsset = [&](const std::string& assetId) {
            return std::find(shard.at("asset_ids").begin(),
                             shard.at("asset_ids").end(), assetId) !=
                   shard.at("asset_ids").end();
        };
        if (!std::all_of(requiredAssetIds.begin(), requiredAssetIds.end(),
                         ownsAsset)) {
            continue;
        }
        if (selected != nullptr) {
            throw std::runtime_error(
                "multiple playable-pack shards own the complete native scene");
        }
        selected = &shard;
    }
    if (selected == nullptr || selected->value("status", "") != "ready" ||
        !selected->contains("archives") ||
        !selected->at("archives").is_array() ||
        selected->at("archives").empty()) {
        throw std::runtime_error(
            "playable pack has no ready shard for the native scene and its rooms");
    }

    PlayableSceneShard result;
    result.Name = selected->value("name", "");
    if (result.Name.empty()) {
        throw std::runtime_error("playable scene shard has no name");
    }
    for (const auto& archive : selected->at("archives")) {
        if (!archive.is_object() || !archive.contains("path") ||
            !archive.at("path").is_string()) {
            throw std::runtime_error("playable scene shard has an invalid archive row");
        }
        const auto path = ResolveManifestPath(
            playablePackPath, archive.at("path").get<std::string>());
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("playable scene shard archive is missing: " +
                                     path.string());
        }
        if (archive.contains("byte_length") &&
            archive.at("byte_length").is_number_unsigned() &&
            std::filesystem::file_size(path) !=
                archive.at("byte_length").get<uint64_t>()) {
            throw std::runtime_error(
                "playable scene shard archive size does not match its manifest");
        }
        result.Archives.push_back(path);
    }
    return result;
}

std::vector<std::string> ValidateNativeManifest(const std::filesystem::path& manifestPath,
                                                std::string_view expectedScenePath,
                                                std::filesystem::path& nativeCodeBinPath,
                                                std::filesystem::path& nativeAudioArchivePath,
                                                std::filesystem::path& nativeStreamArchivePath) {
    const auto manifest = ReadJson(manifestPath);
    if (manifest.value("format", "") != "oot3d_standalone_demo_manifest_v1") {
        throw std::runtime_error("native game requires an OOT3D standalone manifest");
    }
    const auto policy = manifest.value("policy", nlohmann::json::object());
    if (policy.value("runtime_asset_policy", "") != "oot3d_native_or_offline_derived_only" ||
        !policy.value("no_shipwright_runtime_replacement", false) ||
        !policy.value("no_n64_runtime_asset_substitution", false)) {
        throw std::runtime_error("manifest does not enforce the native-only runtime asset policy");
    }
    if (!manifest.contains("sources") || !manifest.at("sources").is_object()) {
        throw std::runtime_error("manifest has no native source inventory");
    }

    std::vector<std::string> sourceKinds;
    const auto& sources = manifest.at("sources");
    for (const auto& [sourceName, source] : sources.items()) {
        if (!source.is_object()) {
            throw std::runtime_error("manifest source is not an object: " + sourceName);
        }
        const std::string kind = source.value("kind", "");
        if (!StartsWith(kind, "oot3d_")) {
            throw std::runtime_error("manifest source is not OOT3D-native: " + sourceName + "=" +
                                     kind);
        }
        sourceKinds.push_back(sourceName + "=" + kind);
        const std::string sourcePath = source.value("path", "");
        if (sourcePath.empty()) {
            throw std::runtime_error("manifest source has no path: " + sourceName);
        }
        const auto resolvedPath = ResolveManifestPath(manifestPath, sourcePath);
        if (!std::filesystem::is_regular_file(resolvedPath)) {
            throw std::runtime_error("manifest source file is missing: " + resolvedPath.string());
        }
    }

    const auto collision = sources.value("collision", nlohmann::json::object());
    const auto codeBin = sources.value("native_code_bin", nlohmann::json::object());
    if (collision.value("kind", "") != "oot3d_scene_zsi_native_collision") {
        throw std::runtime_error("manifest collision source is not a native OOT3D ZSI");
    }
    if (codeBin.value("kind", "") != "oot3d_exefs_code_bin") {
        throw std::runtime_error("manifest code source is not OOT3D ExeFS code.bin");
    }
    nativeCodeBinPath = ResolveManifestPath(manifestPath, codeBin.value("path", ""));
    const auto collisionSourcePath = ResolveManifestPath(
        manifestPath, collision.value("path", ""));
    nativeAudioArchivePath = collisionSourcePath.parent_path().parent_path() /
                             "sound" / "QueenSound.bcsar";
    if (!std::filesystem::is_regular_file(nativeAudioArchivePath)) {
        throw std::runtime_error("native OOT3D sound archive is missing: " +
                                 nativeAudioArchivePath.string());
    }
    nativeStreamArchivePath = collisionSourcePath.parent_path().parent_path() /
                              "sound" / "QueenStream.bcsar";
    if (!std::filesystem::is_regular_file(nativeStreamArchivePath)) {
        throw std::runtime_error("native OOT3D stream archive is missing: " +
                                 nativeStreamArchivePath.string());
    }
    sourceKinds.push_back("native_audio=oot3d_romfs_bcsar");
    sourceKinds.push_back("native_stream_audio=oot3d_romfs_bcsar_bcstm");
    const auto collisionPath = std::filesystem::path(collision.value("path", "")).filename();
    if (!expectedScenePath.empty() &&
        collisionPath != std::filesystem::path(expectedScenePath).filename()) {
        throw std::runtime_error("route scene and manifest collision ZSI do not match");
    }
    return sourceKinds;
}

void ValidateShaderResourceRoot(const std::filesystem::path& resourceRoot) {
    if (!std::filesystem::is_directory(resourceRoot)) {
        throw std::runtime_error("runtime/three_ds_recomp shader resource root is not a directory: " +
                                 resourceRoot.string());
    }
    for (const auto& entry : std::filesystem::directory_iterator(resourceRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o2r") {
            throw std::runtime_error("native game resource root must not contain game archives: " +
                                     entry.path().string());
        }
    }
}

std::string ReadManifestPlayerMovementClip(const std::filesystem::path& manifestPath) {
    const auto manifest = ReadJson(manifestPath);
    if (!manifest.contains("sources") || !manifest.at("sources").is_object() ||
        !manifest.at("sources").contains("link_child")) {
        throw std::runtime_error("native manifest has no player source");
    }
    const auto& player = manifest.at("sources").at("link_child");
    const std::string movementClip = player.value("csab_name", "");
    if (movementClip.empty()) {
        throw std::runtime_error("native manifest player source has no movement CSAB binding");
    }
    return movementClip;
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoPlayerClipSelection ResolveManifestPlayerClips(
    const Oot3dNativeGame::PlayerAnimationCatalog& catalog, std::string_view movementClip,
    std::vector<uint32_t>& typeCandidates, uint32_t& uniqueCsabCount) {
    for (uint32_t type = 0; type < catalog.AnimationTypeCount(); ++type) {
        if (catalog.ResolveSemantic("run", type).CsabName == movementClip) {
            typeCandidates.push_back(type);
        }
    }
    if (typeCandidates.empty()) {
        throw std::runtime_error(
            "manifest player movement CSAB is absent from the native run group");
    }

    const auto resolveEquivalent = [&](std::string_view semantic) {
        const std::string selected =
            catalog.ResolveSemantic(semantic, typeCandidates.front()).CsabName;
        for (const uint32_t type : typeCandidates) {
            if (catalog.ResolveSemantic(semantic, type).CsabName != selected) {
                throw std::runtime_error("manifest player movement CSAB leaves an "
                                         "ambiguous native animation variant");
            }
        }
        return selected;
    };
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoPlayerClipSelection selection = {
        resolveEquivalent("idle"),
        resolveEquivalent("walk"),
        resolveEquivalent("run"),
        resolveEquivalent("walk_end_left"),
        resolveEquivalent("walk_end_right"),
    };

    std::unordered_set<std::string> seenCsabNames = {
        selection.Idle, selection.Walk, selection.Run,
        selection.WalkEndLeft, selection.WalkEndRight,
    };
    const auto appendUnique = [&](std::string id, std::string csabName) {
        if (seenCsabNames.insert(csabName).second) {
            selection.AdditionalClips.push_back({ std::move(id), std::move(csabName) });
        }
    };
    const auto appendDirect = [&](std::string_view id, std::string_view semantic) {
        appendUnique(std::string(id), catalog.ResolveDirectSemantic(semantic).CsabName);
    };
    const auto appendSelectedGroup = [&](std::string_view id, std::string_view semantic) {
        appendUnique(std::string(id),
                     catalog.ResolveSemantic(semantic, typeCandidates.front()).CsabName);
    };

    appendDirect("airborne_wait", "landing_wait");
    appendDirect("fall", "fall");
    appendDirect("fall_wait", "fall_wait");
    appendDirect("auto_jump", "auto_jump");
    appendDirect("auto_jump_up", "auto_jump_up");
    appendDirect("run_auto_jump", "run_auto_jump");
    appendDirect("run_auto_jump_end", "run_auto_jump_end");
    appendUnique("landing", resolveEquivalent("landing"));
    appendUnique("short_landing", resolveEquivalent("short_landing"));
    appendSelectedGroup("landing_roll", "landing_roll");
    appendSelectedGroup("defense", "defense");
    appendSelectedGroup("defense_wait", "defense_wait");
    appendSelectedGroup("defense_end", "defense_end");
    appendUnique("ledge_hold", resolveEquivalent("jump_climb_hold"));
    appendUnique("ledge_wait", resolveEquivalent("jump_climb_wait"));
    appendUnique("ledge_climb_up", resolveEquivalent("jump_climb_up"));
    appendDirect("surface_climb_start_back", "free_climb_start_back");
    appendDirect("surface_climb_start_front", "free_climb_start_front");
    appendDirect("surface_climb_up_right", "free_climb_up_right");
    appendDirect("surface_climb_up_left", "free_climb_up_left");
    appendDirect("surface_climb_side_right", "free_climb_side_right");
    appendDirect("surface_climb_side_left", "free_climb_side_left");
    appendDirect("ladder_climb_start_front", "regular_climb_start_front");
    appendDirect("ladder_climb_start_back", "regular_climb_start_back");
    appendDirect("ladder_climb_up_left", "regular_climb_up_left");
    appendDirect("ladder_climb_up_right", "regular_climb_up_right");
    appendDirect("ladder_climb_end_front_left", "regular_climb_end_front_left");
    appendDirect("ladder_climb_end_front_right", "regular_climb_end_front_right");
    appendDirect("ladder_climb_end_back_left", "regular_climb_end_back_left");
    appendDirect("ladder_climb_end_back_right", "regular_climb_end_back_right");

    for (const auto& binding : catalog.ResolveAll()) {
        appendUnique("native_group_" + std::to_string(binding.CsabTypeLocalIndex),
                     binding.CsabName);
    }
    for (const auto& direct : catalog.ResolveAllDirectSemantic()) {
        appendUnique("native_direct_" + direct.Semantic, direct.Binding.CsabName);
    }
    uniqueCsabCount = static_cast<uint32_t>(seenCsabNames.size());
    return selection;
}

void ApplyPlayerPoseSamplingContract(const std::filesystem::path& semanticsPath,
                                     ThreeDsRecomp::Oot3d::Oot3dNativeDemoPlayerClipSelection& playerClips) {
    const auto semantics = ReadJson(semanticsPath);
    if (semantics.value("format", "") != "oot3d_character_runtime_semantics_v1") {
        throw std::runtime_error("unsupported OOT3D player runtime semantics profile");
    }
    const auto binding = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationSamplingBinding(semantics);
    if (!binding.Available || !binding.ContractPresent || !binding.Error.empty()) {
        throw std::runtime_error("OOT3D player pose sampling contract is unavailable: " +
                                 binding.Error);
    }
    playerClips.PoseSamplingPolicyAvailable = true;
    playerClips.PoseSamplingPolicy = binding.Policy;
    playerClips.RootBaseTranslation = binding.BaseTranslation;
}

} // namespace

void PrintOot3dNativeGameUsage() {
    std::cerr << "usage: oot3d_native_game [--launch-profile <profile.json>] "
                 "[overrides]\n"
                 "       oot3d_native_game --manifest <demo_manifest.json> "
                 "--resource-root <three_ds_recomp/legacy/src/fast> --asset-catalog "
                 "<oot3d_asset_catalog.json> "
                 "--playable-pack <oot3d_playable_pack.json> "
                 "--route-catalog <oot3d_semantic_route_catalog.json> "
                 "--player-animation-contract "
                 "<player_animation_group_native_contract.json> "
                 "--player-runtime-semantics "
                 "<link_child_native_runtime_semantics.json> "
                 "--player-collision-action-contract "
                 "<player_collision_action_native_contract.json> "
                 "--actor-core-contract <actor_core_native_contract.json> "
                 "--actor-shard <oot3d-actors-native.o2r> "
                 "--room-compilation-unit <oot3d_room_compilation_unit_v1.json> "
                 "--native-closure-manifest <oot3d_native_corpus_manifest.json> "
                 "--route-id <resolved scene_entry route> "
                 "[--a32-process-manifest <manifest.json>] "
                 "[--ui-profile <oot3d|topscreen>] "
                 "[--topscreen-texture-overrides <textures.o3tu>] "
                 "[--topscreen-config <topscreen_ui.json>] "
                 "[--controls-config <oot3d_controls.json>] "
                 "[--save-data <directory>] "
                 "[--validate-only] [--output <summary.json>] [--screenshot "
                 "<capture.bmp>] "
                 "[--screenshot-sequence] [--screenshot-interval <frames>] "
                 "[--screenshot-start-frame <frame>] "
                 "[--room-request-smoke <room-index>] "
                 "[--extended-diagnostics] "
                 "[--gameplay-timing <native30_interpolated|"
                 "native30_no_interpolation|enhanced60>] "
                 "[--simulation-rate <hz>] "
                 "[--presentation-rate <hz|free>] "
                 "[--disable-visual-interpolation] "
                 "[--profile-a32-blocks] "
                 "[--trace-a32-blocks <trace.jsonl>] "
                 "[--pica-semantic-trace <trace.jsonl>] "
                 "[--pica-aot-shader-pack <pack.o3ps>] "
                 "[--renderer-cache-directory <directory>] "
                 "[--pica-aot-shader-strict] "
                 "[--pica-effective-shader-inventory <inventory.json>] "
                 "[--pica-pipeline-inventory <inventory.json>] "
                 "[--pica-pipeline-manifest <manifest.json>] "
                 "[--pica-pipeline-prewarm] "
                 "[--scenario-catalog <catalog.json> --scenario <id>] "
                 "[--scenario-strict] [--scenario-auto-exit] "
                  "[--profile-a32-runtime] "
                  "[--disable-compiled-functions] "
                  "[--disable-typed-gameplay] "
                  "[--enable-source-gameplay-profile] "
                  "[--enable-source-actor-init-context] "
                  "[--enable-source-actor-update-all] "
                  "[--enable-source-cutscene-update-frame] "
                  "[--enable-source-cutscene-process-commands] "
                  "[--enable-source-camera-update] "
                  "[--enable-source-player-update] "
                  "[--enable-source-player-update-common] "
                  "[--enable-source-csab-curves] "
                  "[--disable-manual-compiled-functions] "
                 "[--disable-true-aot-blocks] "
                 "[--disable-whole-aot] "
                 "[--whole-aot-block-budget <blocks>] "
                 "[--disable-opengl-pica-geometry-cache] "
                 "[--enable-mass-aot|--disable-mass-aot] "
                 "[--disable-audio] "
                 "[--audio-pcm-dump <capture.wav>] "
                 "[--load-state <checkpoint.oot3dsav>] "
                 "[--quick-state <checkpoint.oot3dsav>] "
                 "[--save-state <checkpoint.oot3dsav> "
                 "--save-state-frame <frame>] "
                 "[--renderer <nri|opengl>] "
                 "[--config <oot3d_native_game.json>] "
                 "[--frames <count>] [--benchmark-warmup-frames <count>] "
                 "[--throughput-benchmark] "
                 "[--max-seconds <seconds>] "
                 "[--fixed-delta-seconds <seconds>] "
                 "[--input-timeline <timeline.json>] [--width "
                 "<pixels>] [--height <pixels>]\n";
}

bool ParseOot3dNativeGameArgs(int argc, char** argv, Oot3dNativeGameLaunch& launch) {
    launch.Host.ApplicationName = "OOT3D Native Game";
    launch.Host.ApplicationId = "oot3d_native_game";
    launch.Host.ConfigurationPath = "oot3d_native_game.json";
    launch.Host.BackendId = 2;
    launch.Host.RenderMode = "native_texture";
    bool gameplayTimingExplicit = false;
    bool simulationRateExplicit = false;
    bool disableVisualInterpolationExplicit = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg(argv[index]);
        if (arg == "--manifest" && index + 1 < argc) {
            launch.Host.ManifestPath = argv[++index];
        } else if (arg == "--config" && index + 1 < argc) {
            launch.Host.ConfigurationPath =
                std::filesystem::absolute(argv[++index]).string();
        } else if (arg == "--resource-root" && index + 1 < argc) {
            launch.Host.ResourceRoot = argv[++index];
        } else if (arg == "--asset-catalog" && index + 1 < argc) {
            launch.AssetCatalogPath = argv[++index];
        } else if (arg == "--playable-pack" && index + 1 < argc) {
            launch.PlayablePackPath = argv[++index];
        } else if (arg == "--route-catalog" && index + 1 < argc) {
            launch.RouteCatalogPath = argv[++index];
        } else if (arg == "--player-animation-contract" && index + 1 < argc) {
            launch.PlayerAnimationContractPath = argv[++index];
        } else if (arg == "--player-runtime-semantics" && index + 1 < argc) {
            launch.PlayerRuntimeSemanticsPath = argv[++index];
        } else if (arg == "--player-collision-action-contract" && index + 1 < argc) {
            launch.PlayerCollisionActionContractPath = argv[++index];
        } else if (arg == "--actor-core-contract" && index + 1 < argc) {
            launch.ActorCoreContractPath = argv[++index];
        } else if (arg == "--actor-shard" && index + 1 < argc) {
            launch.ActorShardPath = argv[++index];
        } else if (arg == "--room-compilation-unit" && index + 1 < argc) {
            launch.RoomCompilationUnitPath = argv[++index];
        } else if (arg == "--native-closure-manifest" && index + 1 < argc) {
            launch.NativeClosureManifestPath = argv[++index];
        } else if (arg == "--route-id" && index + 1 < argc) {
            launch.RouteId = argv[++index];
        } else if (arg == "--a32-process-manifest" && index + 1 < argc) {
            launch.A32ProcessManifestPath = argv[++index];
        } else if (arg == "--ui-profile" && index + 1 < argc) {
            if (!Oot3dNativeGame::ParseOot3dUiProfile(
                    argv[++index], &launch.UiProfile)) {
                return false;
            }
        } else if (arg == "--topscreen-texture-overrides" &&
                   index + 1 < argc) {
            launch.TopScreenTextureOverridePackPath = argv[++index];
        } else if (arg == "--topscreen-config" && index + 1 < argc) {
            launch.TopScreenConfigPath = argv[++index];
        } else if (arg == "--controls-config" && index + 1 < argc) {
#if defined(__SWITCH__)
            // devoptab paths (sdmc:/...) are absolute to Horizon even though
            // std::filesystem classifies them as relative POSIX paths.
            launch.ControlConfigPath =
                std::filesystem::path(argv[++index]).lexically_normal();
#else
            launch.ControlConfigPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
#endif
        } else if (arg == "--save-data" && index + 1 < argc) {
            launch.SaveDataDirectory = argv[++index];
        } else if (arg == "--renderer" && index + 1 < argc) {
            launch.Host.Renderer = argv[++index];
        } else if (arg == "--validate-only") {
            launch.ValidateOnly = true;
        } else if (arg == "--extended-diagnostics") {
            launch.ExtendedDiagnostics = true;
        } else if (arg == "--gameplay-timing" && index + 1 < argc) {
            if (!Oot3dNativeGame::ParseGameplayTimingMode(
                    argv[++index], &launch.GameplayTiming)) {
                throw std::runtime_error(
                    "unknown OOT3D gameplay timing mode");
            }
            gameplayTimingExplicit = true;
        } else if (arg == "--simulation-rate" && index + 1 < argc) {
            launch.SimulationRateHz =
                ParseU32(argv[++index], "simulation rate");
            if (launch.SimulationRateHz == 0U) {
                throw std::runtime_error("simulation rate must be positive");
            }
            simulationRateExplicit = true;
        } else if (arg == "--presentation-rate" && index + 1 < argc) {
            const std::string value(argv[++index]);
            launch.PresentationRateHz =
                value == "free" ? 0U
                                : ParseU32(value, "presentation rate");
            if (launch.PresentationRateHz != 0U &&
                launch.PresentationRateHz <
                    Oot3dNativeGame::kOot3dOriginalSimulationRateHz) {
                throw std::runtime_error(
                    "presentation rates below the native 30 Hz simulation "
                    "clock are not supported yet");
            }
        } else if (arg == "--disable-visual-interpolation") {
            launch.DisableVisualInterpolation = true;
            disableVisualInterpolationExplicit = true;
        } else if (arg == "--profile-a32-blocks") {
            launch.ProfileA32Blocks = true;
        } else if (arg == "--trace-a32-blocks" && index + 1 < argc) {
            launch.A32BlockTracePath = argv[++index];
        } else if (arg == "--pica-semantic-trace" && index + 1 < argc) {
            launch.PicaSemanticTracePath = argv[++index];
        } else if (arg == "--renderer-cache-directory" && index + 1 < argc) {
            launch.RendererCacheDirectory =
                std::filesystem::absolute(std::filesystem::u8path(argv[++index])).lexically_normal();
        } else if (arg == "--pica-aot-shader-pack" &&
                   index + 1 < argc) {
            launch.PicaAotShaderPackPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-aot-shader-strict") {
            launch.PicaAotShaderStrict = true;
        } else if (arg == "--pica-effective-shader-inventory" &&
                   index + 1 < argc) {
            launch.PicaEffectiveShaderInventoryPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-pipeline-inventory" &&
                   index + 1 < argc) {
            launch.PicaPipelineInventoryPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-pipeline-manifest" &&
                   index + 1 < argc) {
            launch.PicaPipelineManifestPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-pipeline-prewarm") {
            launch.PicaPipelinePrewarm = true;
        } else if (arg == "--scenario-catalog" && index + 1 < argc) {
            launch.ScenarioCatalogPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--scenario" && index + 1 < argc) {
            launch.ScenarioId = argv[++index];
        } else if (arg == "--scenario-strict") {
            launch.ScenarioStrict = true;
        } else if (arg == "--scenario-auto-exit") {
            launch.ScenarioAutoExit = true;
        } else if (arg == "--profile-a32-runtime") {
            launch.ProfileA32Runtime = true;
        } else if (arg == "--disable-compiled-functions") {
            launch.DisableCompiledFunctions = true;
        } else if (arg == "--disable-typed-gameplay") {
            launch.DisableTypedGameplay = true;
        } else if (arg == "--enable-source-gameplay-profile") {
            launch.EnableSourceGameplayProfile = true;
        } else if (arg == "--enable-source-actor-init-context") {
            launch.EnableSourceActorInitContext = true;
        } else if (arg == "--enable-source-actor-update-all") {
            launch.EnableSourceActorUpdateAll = true;
        } else if (arg == "--enable-source-cutscene-update-frame") {
            launch.EnableSourceCutsceneUpdateFrame = true;
        } else if (
            arg == "--enable-source-cutscene-process-commands") {
            launch.EnableSourceCutsceneProcessCommands = true;
        } else if (arg == "--enable-source-camera-update") {
            launch.EnableSourceCameraUpdate = true;
        } else if (arg == "--enable-source-player-update") {
            launch.EnableSourcePlayerUpdate = true;
        } else if (arg == "--enable-source-player-update-common") {
            launch.EnableSourcePlayerUpdateCommon = true;
        } else if (arg == "--enable-source-csab-curves") {
            launch.EnableSourceCsabCurves = true;
        } else if (arg == "--disable-manual-compiled-functions") {
            launch.DisableManualCompiledFunctions = true;
        } else if (arg == "--disable-true-aot-blocks") {
            launch.DisableTrueAotBlocks = true;
        } else if (arg == "--disable-whole-aot") {
            launch.DisableWholeAot = true;
        } else if (arg == "--whole-aot-block-budget" &&
                   index + 1 < argc) {
            launch.WholeAotBlockBudget =
                ParseU32(argv[++index], "whole-AOT block budget");
            if (launch.WholeAotBlockBudget == 0U) {
                throw std::runtime_error(
                    "whole-AOT block budget must be positive");
            }
        } else if (arg == "--disable-opengl-pica-geometry-cache") {
            launch.DisableOpenGlPicaGeometryCache = true;
        } else if (arg == "--disable-mass-aot") {
            launch.DisableMassAot = true;
        } else if (arg == "--enable-mass-aot") {
            launch.DisableMassAot = false;
        } else if (arg == "--disable-audio") {
            launch.DisableAudio = true;
        } else if (arg == "--audio-pcm-dump" && index + 1 < argc) {
            launch.AudioPcmDumpPath = argv[++index];
        } else if (arg == "--load-state" && index + 1 < argc) {
            launch.LoadStatePath = argv[++index];
        } else if (arg == "--quick-state" && index + 1 < argc) {
            launch.QuickStatePath = argv[++index];
        } else if (arg == "--save-state" && index + 1 < argc) {
            launch.SaveStatePath = argv[++index];
        } else if (arg == "--save-state-frame" && index + 1 < argc) {
            launch.SaveStateFrame =
                ParseU32(argv[++index], "savestate frame");
            launch.SaveStateFrameAvailable = true;
        } else if (arg == "--output" && index + 1 < argc) {
            launch.Host.OutputPath = argv[++index];
        } else if (arg == "--screenshot" && index + 1 < argc) {
            launch.Host.ScreenshotPath = argv[++index];
        } else if (arg == "--screenshot-sequence") {
            launch.Host.ScreenshotSequence = true;
        } else if (arg == "--screenshot-start-frame" && index + 1 < argc) {
            launch.Host.ScreenshotStartFrame =
                ParseU32(argv[++index], "screenshot start frame");
        } else if (arg == "--screenshot-interval" && index + 1 < argc) {
            launch.Host.ScreenshotSequenceInterval =
                std::max<uint32_t>(1, ParseU32(argv[++index], "screenshot interval"));
        } else if (arg == "--room-request-smoke" && index + 1 < argc) {
            const uint32_t roomIndex =
                ParseU32(argv[++index], "room-request smoke index");
            if (roomIndex > INT32_MAX) {
                throw std::runtime_error("room-request smoke index is too large");
            }
            launch.RoomRequestSmokeSequence.push_back(static_cast<int32_t>(roomIndex));
        } else if (arg == "--frames" && index + 1 < argc) {
            launch.Host.FrameLimit = ParseU32(argv[++index], "frame count");
        } else if (arg == "--benchmark-warmup-frames" && index + 1 < argc) {
            launch.Host.BenchmarkWarmupFrames =
                ParseU32(argv[++index], "benchmark warm-up frame count");
        } else if (arg == "--throughput-benchmark") {
            launch.Host.ThroughputBenchmark = true;
        } else if (arg == "--max-seconds" && index + 1 < argc) {
            launch.Host.MaxSeconds = std::max(0.0, ParseDouble(argv[++index], "maximum seconds"));
        } else if (arg == "--fixed-delta-seconds" && index + 1 < argc) {
            launch.Host.FixedDeltaSeconds =
                ParseDouble(argv[++index], "fixed delta seconds");
            if (launch.Host.FixedDeltaSeconds <= 0.0 ||
                launch.Host.FixedDeltaSeconds > 0.05) {
                throw std::runtime_error("fixed delta seconds must be in (0, 0.05]");
            }
        } else if (arg == "--input-timeline" && index + 1 < argc) {
            launch.Host.InputTimelinePath = argv[++index];
        } else if (arg == "--width" && index + 1 < argc) {
            launch.Host.Width = std::max<uint32_t>(1, ParseU32(argv[++index], "width"));
        } else if (arg == "--height" && index + 1 < argc) {
            launch.Host.Height = std::max<uint32_t>(1, ParseU32(argv[++index], "height"));
        } else {
            return false;
        }
    }

    if (launch.ScenarioCatalogPath.empty() != launch.ScenarioId.empty()) {
        throw std::runtime_error(
            "--scenario-catalog and --scenario must be specified together");
    }
    if (launch.ScenarioStrict && launch.ScenarioId.empty()) {
        throw std::runtime_error(
            "--scenario-strict requires a structural scenario");
    }
    if (launch.ScenarioAutoExit && launch.ScenarioId.empty()) {
        throw std::runtime_error(
            "--scenario-auto-exit requires a structural scenario");
    }

    Oot3dNativeGame::GameplayTimingMode configuredGameplayTiming =
        Oot3dNativeGame::GameplayTimingMode::Native30Interpolated;
    bool configuredGameplayTimingAvailable = false;
    const std::filesystem::path runtimeConfigPath(
        launch.Host.ConfigurationPath);
    if (std::filesystem::is_regular_file(runtimeConfigPath)) {
        const auto runtimeConfig = ReadJson(runtimeConfigPath);
        const auto timing = runtimeConfig.find("gameplay_timing");
        if (timing != runtimeConfig.end()) {
            if (!timing->is_object() || !timing->contains("mode") ||
                !timing->at("mode").is_string() ||
                !Oot3dNativeGame::ParseGameplayTimingMode(
                    timing->at("mode").get_ref<const std::string&>(),
                    &configuredGameplayTiming)) {
                throw std::runtime_error(
                    "gameplay_timing.mode in the runtime config is invalid");
            }
            configuredGameplayTimingAvailable = true;
        }
    }
    if (!gameplayTimingExplicit && configuredGameplayTimingAvailable) {
        launch.GameplayTiming = configuredGameplayTiming;
    }

    if (simulationRateExplicit) {
        Oot3dNativeGame::GameplayTimingMode legacyMode{};
        if (launch.SimulationRateHz ==
            Oot3dNativeGame::kOot3dOriginalSimulationRateHz) {
            legacyMode = disableVisualInterpolationExplicit
                             ? Oot3dNativeGame::GameplayTimingMode::
                                   Native30NoInterpolation
                             : Oot3dNativeGame::GameplayTimingMode::
                                   Native30Interpolated;
        } else if (launch.SimulationRateHz ==
                   Oot3dNativeGame::kOot3dNativeTimeUnitsPerSecond) {
            legacyMode = Oot3dNativeGame::GameplayTimingMode::Enhanced60;
        } else {
            throw std::runtime_error(
                "--simulation-rate supports only 30 or 60; use "
                "--gameplay-timing");
        }
        if (gameplayTimingExplicit &&
            launch.GameplayTiming != legacyMode) {
            throw std::runtime_error(
                "--simulation-rate conflicts with --gameplay-timing");
        }
        launch.GameplayTiming = legacyMode;
    } else if (disableVisualInterpolationExplicit &&
               launch.GameplayTiming ==
                   Oot3dNativeGame::GameplayTimingMode::
                       Native30Interpolated) {
        launch.GameplayTiming =
            Oot3dNativeGame::GameplayTimingMode::Native30NoInterpolation;
    }

    if (launch.Host.ThroughputBenchmark) {
        if (launch.Host.FrameLimit == 0U && launch.Host.MaxSeconds <= 0.0) {
            throw std::runtime_error(
                "--throughput-benchmark requires --frames or --max-seconds");
        }
        // This is a throughput test, not an accelerated duplicate-present
        // test: every measured host frame advances exactly one complete guest
        // simulation/render refresh and presentation itself remains free.
        launch.PresentationRateHz = 0U;
    }

    const auto timingContract =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            launch.GameplayTiming, launch.PresentationRateHz);
    if (launch.Host.ThroughputBenchmark) {
        if (launch.Host.FixedDeltaSeconds > 0.0 &&
            std::abs(launch.Host.FixedDeltaSeconds -
                     timingContract.StepSeconds) > 1.0e-9) {
            throw std::runtime_error(
                "--throughput-benchmark fixed delta must equal one native "
                "simulation step");
        }
        launch.Host.FixedDeltaSeconds = timingContract.StepSeconds;
    }
    launch.SimulationRateHz = timingContract.SimulationRateHz;
    launch.DisableVisualInterpolation =
        launch.GameplayTiming !=
        Oot3dNativeGame::GameplayTimingMode::Native30Interpolated;

    if (!launch.TopScreenTextureOverridePackPath.empty()) {
        if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
            throw std::runtime_error(
                "--topscreen-texture-overrides requires --ui-profile "
                "topscreen");
        }
        if (!std::filesystem::is_regular_file(
                launch.TopScreenTextureOverridePackPath)) {
            throw std::runtime_error(
                "TopScreen texture override pack does not exist: " +
                launch.TopScreenTextureOverridePackPath.string());
        }
    }

    if (!launch.TopScreenConfigPath.empty()) {
        if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
            throw std::runtime_error(
                "--topscreen-config requires --ui-profile topscreen");
        }
        if (!std::filesystem::is_regular_file(launch.TopScreenConfigPath)) {
            throw std::runtime_error(
                "TopScreen UI config does not exist: " +
                launch.TopScreenConfigPath.string());
        }
        std::string configError;
        if (!Oot3dNativeGame::LoadTopScreenUiConfig(
                launch.TopScreenConfigPath, &launch.TopScreenConfig,
                &configError)) {
            throw std::runtime_error(configError);
        }
    }

    if (launch.ControlConfigPath.empty()) {
        std::filesystem::path runtimePath(launch.Host.ConfigurationPath);
        if (runtimePath.empty()) {
            runtimePath = "oot3d_native_game.json";
        }
        runtimePath = std::filesystem::absolute(runtimePath).lexically_normal();
        launch.ControlConfigPath =
            runtimePath.parent_path() / "oot3d_controls.json";
    }
    if (std::filesystem::is_regular_file(launch.ControlConfigPath)) {
        std::string configError;
        if (!Oot3dNativeGame::LoadNativeControlConfig(
                launch.ControlConfigPath, &launch.ControlConfig,
                &configError)) {
            throw std::runtime_error(configError);
        }
    }

    if (launch.Host.Renderer == "opengl") {
        launch.Host.BackendId = Fast::WindowBackend::FAST3D_SDL_OPENGL;
    } else if (launch.Host.Renderer == "nri" || launch.Host.Renderer == "vulkan") {
        launch.Host.BackendId = Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN;
    } else {
        return false;
    }

    if (launch.SaveStatePath.empty() !=
        !launch.SaveStateFrameAvailable) {
        throw std::runtime_error(
            "--save-state and --save-state-frame must be specified together");
    }

    if (!launch.A32ProcessManifestPath.empty()) {
        return !launch.Host.ResourceRoot.empty();
    }

    return !launch.Host.ManifestPath.empty() && !launch.Host.ResourceRoot.empty() &&
           !launch.AssetCatalogPath.empty() && !launch.PlayablePackPath.empty() &&
           !launch.RouteCatalogPath.empty() &&
           !launch.PlayerAnimationContractPath.empty() &&
           !launch.PlayerRuntimeSemanticsPath.empty() &&
           !launch.PlayerCollisionActionContractPath.empty() &&
           !launch.ActorCoreContractPath.empty() &&
           !launch.ActorShardPath.empty() && !launch.RoomCompilationUnitPath.empty() &&
           !launch.NativeClosureManifestPath.empty() &&
           !launch.RouteId.empty();
}

Oot3dNativeGameBootstrap ResolveOot3dNativeGameBootstrap(Oot3dNativeGameLaunch& launch) {
    if (!std::filesystem::is_regular_file(launch.Host.ManifestPath) ||
        !std::filesystem::is_regular_file(launch.AssetCatalogPath) ||
        !std::filesystem::is_regular_file(launch.PlayablePackPath) ||
        !std::filesystem::is_regular_file(launch.RouteCatalogPath) ||
        !std::filesystem::is_regular_file(launch.PlayerAnimationContractPath) ||
        !std::filesystem::is_regular_file(launch.PlayerRuntimeSemanticsPath) ||
        !std::filesystem::is_regular_file(launch.PlayerCollisionActionContractPath) ||
        !std::filesystem::is_regular_file(launch.ActorCoreContractPath) ||
        !std::filesystem::is_regular_file(launch.ActorShardPath) ||
        !std::filesystem::is_regular_file(launch.RoomCompilationUnitPath) ||
        !std::filesystem::is_regular_file(launch.NativeClosureManifestPath)) {
        throw std::runtime_error("native game bootstrap input is missing");
    }
    ValidateShaderResourceRoot(launch.Host.ResourceRoot);

    auto assets = std::make_shared<ThreeDsRecomp::Oot3d::AssetCatalog>(
        ThreeDsRecomp::Oot3d::AssetCatalog::LoadFile(launch.AssetCatalogPath));
    const auto routes =
        ThreeDsRecomp::Oot3d::SemanticRouteCatalog::LoadFile(launch.RouteCatalogPath, *assets);
    const auto* route = routes.Find(launch.RouteId);
    if (route == nullptr) {
        throw std::runtime_error("semantic route was not found: " + launch.RouteId);
    }
    if (route->Kind != "scene_entry" || route->Status != "resolved") {
        throw std::runtime_error("native game bootstrap currently requires a "
                                 "resolved scene_entry route");
    }
    if (route->Native.System != "oot3d" || route->Native.AssetId.empty() ||
        route->Native.ScenePath.empty()) {
        throw std::runtime_error("route has no OOT3D native scene target");
    }
    if (route->Native.SetupIndices.size() != 1 || route->Native.GlobalEntranceIndex < 0 ||
        route->Native.LocalEntranceIndex < 0) {
        throw std::runtime_error(
            "scene_entry route does not resolve one native setup and entrance");
    }
    ValidateNativeAuthority("content", route->Authority.Content);
    ValidateNativeAuthority("timing", route->Authority.Timing);
    ValidateNativeAuthority("rendering", route->Authority.Rendering);
    ValidateNativeAuthority("spatial_state", route->Authority.SpatialState);
    ValidateNativeAuthority("player_entry_state", route->Authority.PlayerEntryState);
    ValidateNativeAuthority("population", route->Authority.Population);
    ValidateNativeAuthority("actor_configuration", route->Authority.ActorConfiguration);

    Oot3dNativeGameBootstrap result;
    result.RouteId = route->RouteId;
    result.SemanticKey = route->Scaffold.SemanticKey;
    result.VariantKey = route->Scaffold.VariantKey;
    result.NativeAssetId = route->Native.AssetId;
    result.NativeScenePath = route->Native.ScenePath;
    result.NativeSceneId = route->Native.SceneId;
    result.NativeSetupIndex = route->Native.SetupIndices.front();
    result.NativeGlobalEntranceIndex = route->Native.GlobalEntranceIndex;
    result.NativeLocalEntranceIndex = route->Native.LocalEntranceIndex;
    result.AssetCatalogRecordCount = static_cast<uint32_t>(assets->Records().size());
    result.RouteCatalogRecordCount = static_cast<uint32_t>(routes.Records().size());
    result.ManifestSourceKinds = ValidateNativeManifest(
        launch.Host.ManifestPath, result.NativeScenePath, result.NativeCodeBinPath,
        result.NativeAudioArchivePath, result.NativeStreamArchivePath);

    const auto nativeReverbLayout =
        Oot3dNativeGame::Oot3dEurRev0AudioReverbLayout();
    const auto nativeReverb = Oot3dNativeGame::ParseNativeAudioReverbEffect(
        ReadBinary(result.NativeCodeBinPath, "native code.bin"),
        nativeReverbLayout);
    if (!nativeReverb.Enabled || nativeReverb.NativeSampleRate == 0) {
        throw std::runtime_error(
            "native code.bin did not resolve the OOT3D DSP sample rate");
    }
    result.NativeAudioSampleRate = nativeReverb.NativeSampleRate;
    result.NativeAudioFrameSamples = static_cast<uint32_t>(
        Oot3dNativeGame::kNativeAudioDspFrameSize);
    result.NativeAudioClockSource =
        "oot3d_code_bin_native_reverb_sample_rate_and_ctr_dsp_frame_size";
    launch.Host.AudioSampleRate = result.NativeAudioSampleRate;
    launch.Host.AudioSampleLength = result.NativeAudioFrameSamples;
    launch.Host.AudioClockSource = result.NativeAudioClockSource;

    auto roomUnit = std::make_shared<Oot3d::RoomCompilationUnit>(
        Oot3d::LoadRoomCompilationUnitFile(launch.RoomCompilationUnitPath));
    if (roomUnit->routeId != result.RouteId || roomUnit->sceneId != result.NativeSceneId ||
        roomUnit->scenePath != result.NativeScenePath ||
        roomUnit->setupIndex != result.NativeSetupIndex ||
        roomUnit->entrypoint.globalEntranceIndex != result.NativeGlobalEntranceIndex ||
        roomUnit->entrypoint.localEntranceIndex != result.NativeLocalEntranceIndex) {
        throw std::runtime_error("room compilation unit does not match the resolved native route");
    }
    if (!roomUnit->roomLifecycle.available) {
        throw std::runtime_error("room compilation unit has no native room lifecycle contract");
    }
    result.RoomCompilationUnitId = roomUnit->unitId;
    result.RoomCompilationPayloadSha256 = roomUnit->payloadSha256;
    result.RoomCompilationCodeBinSha256 = roomUnit->sourceCodeBinSha256;
    result.RoomCompilationRoomCount = static_cast<uint32_t>(roomUnit->rooms.size());
    result.RoomCompilationActorInstanceCount =
        static_cast<uint32_t>(roomUnit->actorInstances.size());
    result.RoomCompilationActorProfileCount = static_cast<uint32_t>(roomUnit->actorProfiles.size());
    result.RoomCompilationObjectDependencyCount =
        static_cast<uint32_t>(roomUnit->objectDependencies.size());
    result.RoomLifecycleStatus = roomUnit->roomLifecycle.status;
    result.RoomLifecycleSourceSnapshotId = roomUnit->roomLifecycle.sourceSnapshotId;

    const auto nativeClosure =
        Oot3dNativeGame::NativeClosureCatalog::LoadFile(
            launch.NativeClosureManifestPath);
    if (nativeClosure.RoomCompilationUnitId() != result.RoomCompilationUnitId ||
        nativeClosure.RoomCompilationPayloadSha256() !=
            result.RoomCompilationPayloadSha256) {
        throw std::runtime_error(
            "native closure corpus was promoted from a different room compilation unit");
    }
    result.NativeClosureSourceRevision = nativeClosure.SourceRevision();
    result.NativeClosureFunctionCount =
        static_cast<uint32_t>(nativeClosure.FunctionCount());
    result.NativeClosureExtractedBodyCount =
        static_cast<uint32_t>(nativeClosure.ExtractedBodyCount());
    result.NativeClosureAotBoundFunctionCount =
        static_cast<uint32_t>(nativeClosure.AotBoundFunctionCount());
    result.NativeClosureAotAvailable = nativeClosure.AotAvailable();
    if (result.NativeClosureAotAvailable &&
        result.NativeClosureAotBoundFunctionCount !=
            result.NativeClosureFunctionCount) {
        throw std::runtime_error(
            "native closure corpus is not completely bound to the pinned A32 AOT image");
    }

    std::vector<std::string> requiredSceneAssets = {result.NativeAssetId};
    for (const int32_t roomIndex : roomUnit->nativeRoomIndices) {
        const auto& candidates = assets->FindRooms(result.NativeSceneId, roomIndex);
        std::vector<const ThreeDsRecomp::Oot3d::AssetCatalogRecord*> setupCandidates;
        std::copy_if(candidates.begin(), candidates.end(),
                     std::back_inserter(setupCandidates),
                     [&](const auto* record) {
                         return std::find(record->SetupIndices.begin(),
                                          record->SetupIndices.end(),
                                          result.NativeSetupIndex) !=
                                record->SetupIndices.end();
                     });
        if (setupCandidates.size() != 1) {
            throw std::runtime_error(
                "room compilation unit does not resolve one cataloged room source");
        }
        requiredSceneAssets.push_back(setupCandidates.front()->AssetId);
    }
    const auto sceneShard = ResolvePlayableSceneShard(
        launch.PlayablePackPath, requiredSceneAssets);
    result.CoreArchive = ResolvePlayableCoreArchive(launch.PlayablePackPath);
    result.SceneShardName = sceneShard.Name;
    result.SceneShardArchives = sceneShard.Archives;
    launch.Host.ResourceArchives.push_back(result.CoreArchive);
    launch.Host.ResourceArchives.insert(launch.Host.ResourceArchives.end(),
                                        sceneShard.Archives.begin(),
                                        sceneShard.Archives.end());

    const auto playerAnimations =
        Oot3dNativeGame::PlayerAnimationCatalog::LoadFile(launch.PlayerAnimationContractPath);
    if (!std::filesystem::equivalent(result.NativeCodeBinPath, playerAnimations.CodeBinPath())) {
        throw std::runtime_error("player animation contract was decoded from a different code.bin");
    }
    result.PlayerAnimationGroupCount = playerAnimations.GroupCount();
    result.PlayerAnimationTypeCount = playerAnimations.AnimationTypeCount();
    launch.Host.NativePlayerClips = ResolveManifestPlayerClips(
        playerAnimations, ReadManifestPlayerMovementClip(launch.Host.ManifestPath),
        result.PlayerAnimationTypeCandidates,
        result.PlayerAnimationCatalogUniqueCsabCount);
    result.PlayerAnimationResidentBindingCount = static_cast<uint32_t>(
        5 + launch.Host.NativePlayerClips->AdditionalClips.size());
    ApplyPlayerPoseSamplingContract(launch.PlayerRuntimeSemanticsPath,
                                    *launch.Host.NativePlayerClips);
    result.PlayerAnimationSelectionSource = "oot3d_manifest_csab_exact_native_table_equivalence";
    result.PlayerPoseSamplingSource = "oot3d_code_bin_skel_anime_sampling_contract";

    const auto playerCollisionAction =
        Oot3dNativeGame::PlayerCollisionActionContract::LoadFile(
            launch.PlayerCollisionActionContractPath);
    if (!std::filesystem::equivalent(result.NativeCodeBinPath,
                                     playerCollisionAction.CodeBinPath())) {
        throw std::runtime_error(
            "player collision/action contract was decoded from a different code.bin");
    }
    launch.PlayerCollisionActionConfig = playerCollisionAction.Config();
    launch.PlayerCollisionActionConfigAvailable = true;
    launch.PlayerActionConfig = playerCollisionAction.ActionConfig();
    launch.PlayerActionConfigAvailable = true;
    result.PlayerCollisionActionCodeBinSha256 = playerCollisionAction.CodeBinSha256();
    if (result.PlayerCollisionActionCodeBinSha256 != roomUnit->sourceCodeBinSha256) {
        throw std::runtime_error(
            "player collision/action contract and room compilation unit were decoded "
            "from different code.bin data");
    }

    auto actorCore = Oot3dNativeGame::ActorCoreContract::LoadFile(launch.ActorCoreContractPath);
    if (actorCore.CodeBinSha256() != roomUnit->sourceCodeBinSha256) {
        throw std::runtime_error("actor core and room compilation unit were "
                                 "decoded from different code.bin data");
    }
    result.ActorCoreSnapshotId = actorCore.SnapshotId();
    result.ActorCoreSourceRevision = actorCore.SourceRevision();
    result.ActorCategoryListCount = actorCore.CategoryListCount();
    result.ActorSpawnTotalGuardValue = actorCore.SpawnTotalGuardValue();
    Oot3dNativeGame::NativeActorRuntimeConfig actorRuntimeConfig;
    actorRuntimeConfig.Assets = std::move(assets);
    actorRuntimeConfig.CompilationUnit = std::move(roomUnit);
    actorRuntimeConfig.NativeSourceLoaderFactory = [] {
        auto* context = Ship::Context::GetRawInstance();
        if (context == nullptr || context->GetResourceManager() == nullptr) {
            throw std::runtime_error(
                "native game source loader requires an initialized resource manager");
        }
        return NativeArchiveSourceLoader(
            context->GetResourceManager()->GetArchiveManager());
    };
    for (const auto& condition : route->Scaffold.VariantConditions) {
        actorRuntimeConfig.GameplayFacts.push_back({condition.Fact, condition.Equals});
    }
    actorRuntimeConfig.GameplayFacts.push_back(
        {"scene.native_id", std::to_string(result.NativeSceneId)});
    actorRuntimeConfig.GameplayFacts.push_back(
        {"scene.native_setup", std::to_string(result.NativeSetupIndex)});
    actorRuntimeConfig.RoomRequestSmokeSequence = launch.RoomRequestSmokeSequence;
    actorRuntimeConfig.MaterialAnimationTicksPerSecond =
        kOot3dNativePlayerTickRate;
    actorRuntimeConfig.MaterialAnimationClockSource =
        "oot3d_native_update_tick_rate_and_MaterialAnimation_Update_0x00373BEC";
    actorRuntimeConfig.NativeClosureManifestPath = launch.NativeClosureManifestPath;
    actorRuntimeConfig.NativeCodeBinPath = result.NativeCodeBinPath;
    actorRuntimeConfig.NativeAudioArchivePath = result.NativeAudioArchivePath;
    actorRuntimeConfig.NativeStreamArchivePath = result.NativeStreamArchivePath;
    actorRuntimeConfig.NativeAudioOutputSampleRate = result.NativeAudioSampleRate;
    actorRuntimeConfig.NativeAudioBehavior =
        Oot3dNativeGame::Oot3dEurRev0AudioBehaviorLayout();
    actorRuntimeConfig.NativeAudioEnvelope =
        Oot3dNativeGame::Oot3dEurRev0AudioEnvelopeLayout();
    actorRuntimeConfig.NativeAudioModulation =
        Oot3dNativeGame::Oot3dEurRev0AudioModulationLayout();
    actorRuntimeConfig.NativeAudioSpatial =
        Oot3dNativeGame::Oot3dEurRev0AudioSpatialLayout();
    actorRuntimeConfig.NativeAudioMix =
        Oot3dNativeGame::Oot3dEurRev0AudioMixLayout();
    actorRuntimeConfig.NativeAudioFilter =
        Oot3dNativeGame::Oot3dEurRev0AudioFilterLayout();
    actorRuntimeConfig.NativeAudioReverb = nativeReverbLayout;
    actorRuntimeConfig.NativeAudioScene =
        Oot3dNativeGame::Oot3dEurRev0AudioSceneLayout();
    actorRuntimeConfig.NativeAudioSoundSpec =
        Oot3dNativeGame::Oot3dEurRev0AudioSoundSpecLayout();
    actorRuntimeConfig.NativeCodeBaseAddress = 0x00100000;
    actorRuntimeConfig.EnRiverSoundIdTableAddress = 0x0052e344;
    actorRuntimeConfig.EnRiverSoundIdTableSource =
        "EnRiverSound_Draw_0x002a5960_native_arm_literal_and_code_bin_table";
    launch.Host.ActorRuntime = std::make_shared<Oot3dNativeGame::NativeActorRuntime>(
        std::move(actorCore), std::move(actorRuntimeConfig));
    launch.Host.ActorRuntimeId = "oot3d_native_actor_core";
    launch.Host.ActorRuntimeStatus = "room_compilation_unit_population_with_native_room_"
                                     "lifecycle_callbacks_and_explicit_gaps";
    launch.Host.ResourceArchives.push_back(launch.ActorShardPath);

    // Preserve the global entrance as the source of truth. The scene loader then
    // decodes its native code.bin row and resolves the local entrance itself;
    // passing the local index here would discard that provenance.
    launch.Host.EntranceIndex = -1;
    launch.Host.NativeSceneSetupOverride = result.NativeSetupIndex;
    launch.Host.NativeSceneSetupSource = "oot3d_native_game_semantic_route";
    return result;
}

nlohmann::json Oot3dNativeGameBootstrapToJson(const Oot3dNativeGameLaunch& launch,
                                              const Oot3dNativeGameBootstrap& bootstrap) {
    nlohmann::json additionalPlayerClips = nlohmann::json::array();
    if (launch.Host.NativePlayerClips) {
        for (const auto& clip : launch.Host.NativePlayerClips->AdditionalClips) {
            additionalPlayerClips.push_back({ { "id", clip.Id }, { "csab", clip.CsabName } });
        }
    }
    return {
        { "format", "oot3d_native_game_bootstrap_v1" },
        { "status", "ready" },
        { "runtime", "dedicated_oot3d_native_game" },
        { "backend", launch.Host.Renderer },
        { "pica_semantic_trace",
          launch.PicaSemanticTracePath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(launch.PicaSemanticTracePath.string()) },
        { "pica_aot_shader_pack",
          launch.PicaAotShaderPackPath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(launch.PicaAotShaderPackPath.string()) },
        { "pica_aot_shader_strict", launch.PicaAotShaderStrict },
        { "renderer_cache_directory", launch.RendererCacheDirectory.string() },
        { "pica_effective_shader_inventory",
          launch.PicaEffectiveShaderInventoryPath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(
                    launch.PicaEffectiveShaderInventoryPath.string()) },
        { "pica_pipeline_inventory",
          launch.PicaPipelineInventoryPath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(
                    launch.PicaPipelineInventoryPath.string()) },
        { "pica_pipeline_manifest",
          launch.PicaPipelineManifestPath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(
                    launch.PicaPipelineManifestPath.string()) },
        { "pica_pipeline_prewarm", launch.PicaPipelinePrewarm },
        { "ui_profile",
          Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile) },
        { "topscreen_texture_override_pack",
          launch.TopScreenTextureOverridePackPath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(
                    launch.TopScreenTextureOverridePackPath.string()) },
        { "topscreen_config",
          launch.TopScreenConfigPath.empty()
              ? nlohmann::json(nullptr)
              : nlohmann::json(launch.TopScreenConfigPath.string()) },
        { "topscreen_settings", {
            { "source", launch.TopScreenConfigPath.empty()
                            ? "built_in_defaults"
                            : "external_json" },
            { "hud_layout",
              Oot3dNativeGame::TopScreenHudLayoutName(
                  launch.TopScreenConfig.HudLayout) },
            { "hud_scale",
              std::round(static_cast<double>(
                             launch.TopScreenConfig.HudScale) *
                         100.0) /
                  100.0 },
            { "minimap_visible",
              launch.TopScreenConfig.MinimapVisible },
            { "camera_zoom_percent",
              launch.TopScreenConfig.CameraZoomPercent },
            { "camera_fov_percent",
              launch.TopScreenConfig.CameraFovPercent },
            { "free_camera_enabled",
              launch.TopScreenConfig.FreeCameraEnabled },
            { "free_camera_speed_level",
              launch.TopScreenConfig.FreeCameraSpeedLevel },
            { "free_camera_invert_x",
              launch.TopScreenConfig.FreeCameraInvertX },
            { "free_camera_invert_y",
              launch.TopScreenConfig.FreeCameraInvertY },
            { "guest_save_preferences_used", false },
            { "gameplay_preference_chords_enabled", false },
          } },
        { "controls_config", launch.ControlConfigPath.string() },
        { "controls", {
            { "schema",
              std::string(Oot3dNativeGame::kNativeControlConfigSchema) },
            { "profile",
              Oot3dNativeGame::NativeControlProfileName(
                  launch.ControlConfig.Profile) },
            { "native_aim_source",
              Oot3dNativeGame::NativeMotionSourceName(
                  launch.ControlConfig.NativeAimSource) },
            { "free_camera_source",
              Oot3dNativeGame::NativeMotionSourceName(
                  launch.ControlConfig.FreeCameraSource) },
            { "preferred_controller_guid",
              launch.ControlConfig.PreferredControllerGuid },
          } },
        { "gameplay_timing_mode",
          Oot3dNativeGame::GameplayTimingModeName(
              launch.GameplayTiming) },
        { "simulation_rate_hz", launch.SimulationRateHz },
        { "presentation_rate_hz",
          launch.PresentationRateHz == 0U
              ? nlohmann::json(nullptr)
              : nlohmann::json(launch.PresentationRateHz) },
        { "presentation_rate_unlimited",
          launch.PresentationRateHz == 0U },
        { "n64_runtime_content_mounted", false },
        { "ship_gameplay_runtime_initialized", false },
        { "player_controller_id", launch.Host.PlayerControllerId },
        { "player_controller_status", launch.Host.PlayerControllerStatus },
        { "manifest", launch.Host.ManifestPath.string() },
        { "asset_catalog", launch.AssetCatalogPath.string() },
        { "playable_pack", launch.PlayablePackPath.string() },
        { "scene_shard_name", bootstrap.SceneShardName },
        { "core_archive", bootstrap.CoreArchive.string() },
        { "scene_shard_archives", bootstrap.SceneShardArchives },
        { "route_catalog", launch.RouteCatalogPath.string() },
        { "player_animation_contract", launch.PlayerAnimationContractPath.string() },
        { "player_runtime_semantics", launch.PlayerRuntimeSemanticsPath.string() },
        { "player_collision_action_contract",
          launch.PlayerCollisionActionContractPath.string() },
        { "player_collision_action_code_bin_sha256",
          bootstrap.PlayerCollisionActionCodeBinSha256 },
        { "player_scene_entrance_action", {
            { "status", launch.PlayerActionConfigAvailable ? "ready" : "unavailable" },
            { "source", "oot3d_code_bin_player_init_start_mode_table" },
            { "start_mode_mask", launch.PlayerActionConfig.StartModeMask },
            { "start_mode_shift", launch.PlayerActionConfig.StartModeShift },
          } },
        { "actor_core_contract", launch.ActorCoreContractPath.string() },
        { "actor_shard", launch.ActorShardPath.string() },
        { "room_compilation_unit", launch.RoomCompilationUnitPath.string() },
        { "native_closure_manifest", launch.NativeClosureManifestPath.string() },
        { "actor_runtime_id", launch.Host.ActorRuntimeId },
        { "actor_runtime_status", launch.Host.ActorRuntimeStatus },
        { "actor_core_snapshot_id", bootstrap.ActorCoreSnapshotId },
        { "actor_core_source_revision", bootstrap.ActorCoreSourceRevision },
        { "actor_category_list_count", bootstrap.ActorCategoryListCount },
        { "actor_spawn_total_guard_value", bootstrap.ActorSpawnTotalGuardValue },
        { "room_compilation_unit_id", bootstrap.RoomCompilationUnitId },
        { "room_compilation_payload_sha256", bootstrap.RoomCompilationPayloadSha256 },
        { "room_compilation_code_bin_sha256", bootstrap.RoomCompilationCodeBinSha256 },
        { "room_compilation_room_count", bootstrap.RoomCompilationRoomCount },
        { "room_compilation_actor_instance_count", bootstrap.RoomCompilationActorInstanceCount },
        { "room_compilation_actor_profile_count", bootstrap.RoomCompilationActorProfileCount },
        { "room_compilation_object_dependency_count", bootstrap.RoomCompilationObjectDependencyCount },
        { "room_lifecycle_status", bootstrap.RoomLifecycleStatus },
        { "room_lifecycle_source_snapshot_id", bootstrap.RoomLifecycleSourceSnapshotId },
        { "native_closure", {
            { "source_revision", bootstrap.NativeClosureSourceRevision },
            { "function_count", bootstrap.NativeClosureFunctionCount },
            { "extracted_body_count", bootstrap.NativeClosureExtractedBodyCount },
            { "aot_available", bootstrap.NativeClosureAotAvailable },
            { "aot_bound_function_count",
              bootstrap.NativeClosureAotBoundFunctionCount },
            { "all_functions_aot_bound",
              bootstrap.NativeClosureAotAvailable &&
                  bootstrap.NativeClosureAotBoundFunctionCount ==
                      bootstrap.NativeClosureFunctionCount },
          } },
        { "route_id", bootstrap.RouteId },
        { "semantic_key", bootstrap.SemanticKey },
        { "variant_key", bootstrap.VariantKey },
        { "native_asset_id", bootstrap.NativeAssetId },
        { "native_scene_path", bootstrap.NativeScenePath },
        { "native_code_bin", bootstrap.NativeCodeBinPath.string() },
        { "native_audio_archive", bootstrap.NativeAudioArchivePath.string() },
        { "native_stream_archive", bootstrap.NativeStreamArchivePath.string() },
        { "native_audio_sample_rate", bootstrap.NativeAudioSampleRate },
        { "native_audio_frame_samples", bootstrap.NativeAudioFrameSamples },
        { "native_audio_clock_source", bootstrap.NativeAudioClockSource },
        { "native_scene_id", bootstrap.NativeSceneId },
        { "native_setup_index", bootstrap.NativeSetupIndex },
        { "native_global_entrance_index", bootstrap.NativeGlobalEntranceIndex },
        { "native_local_entrance_index", bootstrap.NativeLocalEntranceIndex },
        { "manifest_source_kinds", bootstrap.ManifestSourceKinds },
        { "asset_catalog_record_count", bootstrap.AssetCatalogRecordCount },
        { "route_catalog_record_count", bootstrap.RouteCatalogRecordCount },
        { "player_animation_group_count", bootstrap.PlayerAnimationGroupCount },
        { "player_animation_type_count", bootstrap.PlayerAnimationTypeCount },
        { "player_animation_catalog_unique_csab_count",
          bootstrap.PlayerAnimationCatalogUniqueCsabCount },
        { "player_animation_resident_binding_count",
          bootstrap.PlayerAnimationResidentBindingCount },
        { "player_animation_type_candidates", bootstrap.PlayerAnimationTypeCandidates },
        { "player_animation_selection_source", bootstrap.PlayerAnimationSelectionSource },
        { "player_pose_sampling_source", bootstrap.PlayerPoseSamplingSource },
        { "player_pose_sampling", {
            { "available", launch.Host.NativePlayerClips &&
                           launch.Host.NativePlayerClips->PoseSamplingPolicyAvailable },
            { "special_bone", launch.Host.NativePlayerClips
                                  ? launch.Host.NativePlayerClips->PoseSamplingPolicy.SpecialBone : -1 },
            { "default_channel_mask", launch.Host.NativePlayerClips
                                          ? launch.Host.NativePlayerClips->PoseSamplingPolicy.DefaultChannelMask : 0 },
            { "special_bone_channel_mask", launch.Host.NativePlayerClips
                                               ? launch.Host.NativePlayerClips->PoseSamplingPolicy.SpecialBoneChannelMask : 0 },
            { "root_base_translation", launch.Host.NativePlayerClips
                                             ? launch.Host.NativePlayerClips->RootBaseTranslation
                                             : std::array<float, 3>{ 0.0f, 0.0f, 0.0f } },
        } },
        { "player_animation_clips", {
            { "idle", launch.Host.NativePlayerClips ? launch.Host.NativePlayerClips->Idle : "" },
            { "walk", launch.Host.NativePlayerClips ? launch.Host.NativePlayerClips->Walk : "" },
            { "run", launch.Host.NativePlayerClips ? launch.Host.NativePlayerClips->Run : "" },
            { "walk_end_left", launch.Host.NativePlayerClips ? launch.Host.NativePlayerClips->WalkEndLeft : "" },
            { "walk_end_right", launch.Host.NativePlayerClips ? launch.Host.NativePlayerClips->WalkEndRight : "" },
            { "additional", additionalPlayerClips },
        } },
    };
}

void WriteOot3dNativeGameBootstrapJson(const std::filesystem::path& path,
                                       const nlohmann::json& document) {
    if (path.empty()) {
        return;
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not write native game bootstrap output: " + path.string());
    }
    stream << document.dump(2) << '\n';
}
