#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace ThreeDsRecomp::Oot3d {
namespace {

std::string RequiredString(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks string field: ") + key);
    }
    return it->get<std::string>();
}

int32_t RequiredInt(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks integer field: ") + key);
    }
    return it->get<int32_t>();
}

const nlohmann::json& RequiredObject(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_object()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks object field: ") + key);
    }
    return *it;
}

std::vector<int32_t> RequiredIntArray(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks array field: ") + key);
    }
    std::vector<int32_t> result;
    result.reserve(it->size());
    for (const auto& value : *it) {
        if (!value.is_number_integer()) {
            throw std::runtime_error(std::string("OOT3D semantic route has non-integer array field: ") + key);
        }
        result.push_back(value.get<int32_t>());
    }
    return result;
}

std::vector<std::string> RequiredStringArray(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks array field: ") + key);
    }
    std::vector<std::string> result;
    result.reserve(it->size());
    for (const auto& value : *it) {
        if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
            throw std::runtime_error(std::string("OOT3D semantic route has invalid string array field: ") + key);
        }
        result.push_back(value.get<std::string>());
    }
    return result;
}

std::vector<SemanticRouteCondition> RequiredConditions(const nlohmann::json& object,
                                                       const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->empty()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks non-empty condition array: ") + key);
    }
    std::vector<SemanticRouteCondition> result;
    std::unordered_set<std::string> facts;
    result.reserve(it->size());
    facts.reserve(it->size());
    for (const auto& value : *it) {
        if (!value.is_object()) {
            throw std::runtime_error(std::string("OOT3D semantic route has invalid condition: ") + key);
        }
        SemanticRouteCondition condition{
            RequiredString(value, "fact"),
            RequiredString(value, "equals"),
        };
        if (!facts.emplace(condition.Fact).second) {
            throw std::runtime_error("OOT3D semantic route condition repeats fact: " + condition.Fact);
        }
        result.push_back(std::move(condition));
    }
    return result;
}

std::vector<SemanticSceneRoomBinding> RequiredRoomBindings(const nlohmann::json& object,
                                                           const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->empty()) {
        throw std::runtime_error(std::string("OOT3D semantic route lacks non-empty room binding array: ") + key);
    }
    std::vector<SemanticSceneRoomBinding> result;
    result.reserve(it->size());
    for (const auto& value : *it) {
        if (!value.is_object()) {
            throw std::runtime_error(std::string("OOT3D semantic route has invalid room binding: ") + key);
        }
        result.push_back({
            RequiredInt(value, "scaffold_room_index"),
            RequiredInt(value, "native_room_index"),
        });
    }
    return result;
}

std::string SemanticRequestKey(std::string_view kind, std::string_view semanticKey,
                               std::string_view variantKey = {}) {
    auto key = std::string(kind) + ":" + std::string(semanticKey);
    if (!variantKey.empty()) {
        key += ":" + std::string(variantKey);
    }
    return key;
}

std::string SceneEntryVariantKey(int32_t entranceIndex, std::string_view variantKey) {
    return std::to_string(entranceIndex) + ":" + std::string(variantKey);
}

std::string SceneEntrySelectorKey(int32_t entranceIndex,
                                  const std::vector<SemanticRouteCondition>& conditions) {
    std::vector<const SemanticRouteCondition*> sorted;
    sorted.reserve(conditions.size());
    for (const auto& condition : conditions) {
        sorted.push_back(&condition);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) {
        return left->Fact < right->Fact || (left->Fact == right->Fact && left->Equals < right->Equals);
    });

    auto key = std::to_string(entranceIndex) + ":";
    for (const auto* condition : sorted) {
        key += condition->Fact + "=" + condition->Equals + ";";
    }
    return key;
}

bool ConditionsMatch(const std::vector<SemanticRouteCondition>& conditions,
                     std::span<const SemanticGameplayFact> facts) {
    return std::all_of(conditions.begin(), conditions.end(), [&facts](const auto& condition) {
        return std::any_of(facts.begin(), facts.end(), [&condition](const auto& fact) {
            return fact.Key == condition.Fact && fact.Value == condition.Equals;
        });
    });
}

std::string CutsceneRequestKey(std::string_view gameState, int32_t sceneId, int32_t sceneSetupIndex,
                               int32_t cutsceneIndex) {
    return std::string(gameState) + ":" + std::to_string(sceneId) + ":" +
           std::to_string(sceneSetupIndex) + ":" + std::to_string(cutsceneIndex);
}

void RequireValue(std::string_view actual, std::string_view expected, std::string_view field) {
    if (actual != expected) {
        throw std::runtime_error("Unsupported OOT3D semantic route " + std::string(field) + ": " +
                                 std::string(actual));
    }
}

} // namespace

SemanticRouteCatalog SemanticRouteCatalog::Parse(const nlohmann::json& document, const AssetCatalog& assets) {
    if (!document.is_object() || document.value("format", "") != "oot3d_semantic_route_catalog_v1" ||
        document.value("status", "") != "complete") {
        throw std::runtime_error("Unsupported or incomplete OOT3D semantic route catalog");
    }
    const auto recordsIt = document.find("records");
    if (recordsIt == document.end() || !recordsIt->is_array()) {
        throw std::runtime_error("OOT3D semantic route catalog has no records array");
    }

    SemanticRouteCatalog catalog;
    catalog.mRecords.reserve(recordsIt->size());
    for (const auto& source : *recordsIt) {
        if (!source.is_object()) {
            throw std::runtime_error("OOT3D semantic route catalog contains a non-object record");
        }
        SemanticRouteRecord record;
        record.RouteId = RequiredString(source, "route_id");
        record.Kind = RequiredString(source, "kind");
        record.CompletionEvent = RequiredString(source, "completion_event");
        record.Status = RequiredString(source, "status");

        const auto& scaffold = RequiredObject(source, "scaffold");
        record.Scaffold.System = RequiredString(scaffold, "system");
        record.Scaffold.RequestKind = RequiredString(scaffold, "request_kind");
        record.Scaffold.SemanticKey = RequiredString(scaffold, "semantic_key");
        record.Scaffold.SceneId = RequiredInt(scaffold, "scene_id");
        if (record.Kind == "cutscene") {
            record.Scaffold.GameState = RequiredString(scaffold, "game_state");
            record.Scaffold.SceneSetupIndex = RequiredInt(scaffold, "scene_setup_index");
            record.Scaffold.CutsceneIndex = RequiredInt(scaffold, "cutscene_index");
        } else if (record.Kind == "scene_entry") {
            record.Scaffold.EntranceIndex = RequiredInt(scaffold, "entrance_index");
            record.Scaffold.EntranceSymbol = RequiredString(scaffold, "entrance_symbol");
            record.Scaffold.OriginSemanticKey = RequiredString(scaffold, "origin_semantic_key");
            record.Scaffold.VariantKey = RequiredString(scaffold, "variant_key");
            record.Scaffold.VariantSource = RequiredString(scaffold, "variant_source");
            record.Scaffold.VariantConditions = RequiredConditions(scaffold, "variant_conditions");
        }

        const auto& native = RequiredObject(source, "native");
        record.Native.System = RequiredString(native, "system");
        record.Native.AssetId = RequiredString(native, "asset_id");
        record.Native.SceneId = RequiredInt(native, "scene_id");
        record.Native.ScenePath = RequiredString(native, "scene_path");
        record.Native.SceneStem = RequiredString(native, "scene_stem");
        record.Native.SetupIndices = RequiredIntArray(native, "setup_indices");
        if (record.Kind == "cutscene") {
            record.Native.SequenceAssetIds = RequiredStringArray(native, "sequence_asset_ids");
        } else if (record.Kind == "scene_entry") {
            record.Native.SetupSelection = RequiredString(native, "setup_selection");
            record.Native.RoomBindings = RequiredRoomBindings(native, "room_bindings");
            record.Native.GlobalEntranceIndex = RequiredInt(native, "global_entrance_index");
            record.Native.LocalEntranceIndex = RequiredInt(native, "local_entrance_index");
            record.Native.SpatialSource = RequiredString(native, "spatial_source");
            record.Native.CameraSource = RequiredString(native, "camera_source");
            record.Native.PlayerEntryStateSource = RequiredString(native, "player_entry_state_source");
            record.Native.PopulationSource = RequiredString(native, "population_source");
            record.Native.ActorConfigurationSource = RequiredString(native, "actor_configuration_source");
        }

        const auto& authority = RequiredObject(source, "authority");
        record.Authority.ControlFlow = RequiredString(authority, "control_flow");
        record.Authority.Content = RequiredString(authority, "content");
        record.Authority.Timing = RequiredString(authority, "timing");
        record.Authority.Rendering = RequiredString(authority, "rendering");
        if (record.Kind == "scene_entry") {
            record.Authority.SpatialState = RequiredString(authority, "spatial_state");
            record.Authority.PlayerEntryState = RequiredString(authority, "player_entry_state");
            record.Authority.Population = RequiredString(authority, "population");
            record.Authority.ActorConfiguration = RequiredString(authority, "actor_configuration");
            record.Authority.BehaviorSemantics = RequiredString(authority, "behavior_semantics");
        }
        catalog.mRecords.push_back(std::move(record));
    }
    catalog.BuildIndices(assets);
    return catalog;
}

SemanticRouteCatalog SemanticRouteCatalog::LoadFile(const std::filesystem::path& path, const AssetCatalog& assets) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Unable to open OOT3D semantic route catalog: " + path.string());
    }
    return Parse(nlohmann::json::parse(stream), assets);
}

void SemanticRouteCatalog::BuildIndices(const AssetCatalog& assets) {
    mById.reserve(mRecords.size());
    mBySemanticRequest.reserve(mRecords.size());
    mSceneEntriesByScaffoldVariant.reserve(mRecords.size());
    mSceneEntriesByScaffoldEntrance.reserve(mRecords.size());
    mCutscenesByScaffoldRequest.reserve(mRecords.size());
    std::unordered_map<std::string, const SemanticRouteRecord*> sceneEntrySelectors;
    sceneEntrySelectors.reserve(mRecords.size());
    for (const auto& record : mRecords) {
        RequireValue(record.Status, "resolved", "status");
        RequireValue(record.Scaffold.System, "oot_n64_gameplay", "scaffold.system");
        RequireValue(record.Native.System, "oot3d", "native.system");
        RequireValue(record.Authority.ControlFlow, "oot_n64_gameplay_scaffold", "authority.control_flow");
        RequireValue(record.Authority.Content, "oot3d_native", "authority.content");
        RequireValue(record.Authority.Timing, "oot3d_native", "authority.timing");
        RequireValue(record.Authority.Rendering, "oot3d_native", "authority.rendering");
        if (!mById.emplace(record.RouteId, &record).second) {
            throw std::runtime_error("Duplicate OOT3D semantic route id: " + record.RouteId);
        }
        const auto semanticKey = SemanticRequestKey(record.Kind, record.Scaffold.SemanticKey,
                                                     record.Scaffold.VariantKey);
        if (!mBySemanticRequest.emplace(semanticKey, &record).second) {
            throw std::runtime_error("Duplicate OOT3D semantic request: " + semanticKey);
        }

        const auto* asset = assets.Find(record.Native.AssetId);
        if (asset == nullptr) {
            throw std::runtime_error("OOT3D semantic route target is absent from asset catalog: " +
                                     record.Native.AssetId);
        }
        if (record.Kind == "scene") {
            RequireValue(record.Scaffold.RequestKind, "scene", "scaffold.request_kind");
            RequireValue(record.CompletionEvent, "scene_exit_requested", "completion_event");
            if (asset->Family != "scene_profile" || asset->SceneId != record.Native.SceneId ||
                asset->SceneStem != record.Native.SceneStem || asset->SetupIndices != record.Native.SetupIndices) {
                throw std::runtime_error("OOT3D semantic scene route does not match its native asset ownership: " +
                                         record.RouteId);
            }
            if (!mScenesByScaffoldId.emplace(record.Scaffold.SceneId, &record).second) {
                throw std::runtime_error("Duplicate OOT3D scaffold scene id: " +
                                         std::to_string(record.Scaffold.SceneId));
            }
        } else if (record.Kind == "scene_entry") {
            RequireValue(record.Scaffold.RequestKind, "scene_entry", "scaffold.request_kind");
            RequireValue(record.CompletionEvent, "scene_entry_finished", "completion_event");
            RequireValue(record.Native.SpatialSource, "oot3d_zsi_spawn_entry", "native.spatial_source");
            RequireValue(record.Native.CameraSource,
                         "oot3d_zsi_player_params_then_collision_or_default_camera",
                         "native.camera_source");
            RequireValue(record.Native.PlayerEntryStateSource, "oot3d_player_entry_state",
                         "native.player_entry_state_source");
            RequireValue(record.Scaffold.VariantSource, "oot_n64_semantic_source_offline",
                         "scaffold.variant_source");
            if (record.Scaffold.VariantConditions.empty()) {
                throw std::runtime_error("OOT3D semantic scene-entry has no gameplay conditions: " +
                                         record.RouteId);
            }
            std::unordered_set<int32_t> scaffoldRoomIndices;
            scaffoldRoomIndices.reserve(record.Native.RoomBindings.size());
            for (const auto& roomBinding : record.Native.RoomBindings) {
                if (roomBinding.ScaffoldRoomIndex < 0 || roomBinding.NativeRoomIndex < 0 ||
                    !scaffoldRoomIndices.emplace(roomBinding.ScaffoldRoomIndex).second ||
                    assets.FindRooms(record.Native.SceneId, roomBinding.NativeRoomIndex).empty()) {
                    throw std::runtime_error("OOT3D semantic scene-entry room binding is invalid: " +
                                             record.RouteId);
                }
            }
            RequireValue(record.Native.PopulationSource, "oot3d_zsi_actor_and_object_lists",
                         "native.population_source");
            RequireValue(record.Native.ActorConfigurationSource, "oot3d_zsi_actor_params",
                         "native.actor_configuration_source");
            RequireValue(record.Authority.SpatialState, "oot3d_native", "authority.spatial_state");
            RequireValue(record.Authority.PlayerEntryState, "oot3d_native",
                         "authority.player_entry_state");
            RequireValue(record.Authority.Population, "oot3d_native", "authority.population");
            RequireValue(record.Authority.ActorConfiguration, "oot3d_native",
                         "authority.actor_configuration");
            RequireValue(record.Authority.BehaviorSemantics,
                         "oot_n64_semantic_scaffold_with_verified_oot3d_deltas",
                         "authority.behavior_semantics");
            if (record.Scaffold.EntranceIndex < 0 || record.Native.GlobalEntranceIndex < 0 ||
                record.Native.LocalEntranceIndex < 0 || record.Native.SetupIndices.empty() ||
                asset->Family != "scene_profile" || asset->SceneId != record.Native.SceneId ||
                asset->SceneStem != record.Native.SceneStem ||
                std::any_of(record.Native.SetupIndices.begin(), record.Native.SetupIndices.end(),
                            [asset](int32_t setupIndex) {
                                return std::find(asset->SetupIndices.begin(), asset->SetupIndices.end(), setupIndex) ==
                                       asset->SetupIndices.end();
                            })) {
                throw std::runtime_error("OOT3D semantic scene-entry ownership mismatch: " + record.RouteId);
            }
            const auto requestKey = SceneEntryVariantKey(record.Scaffold.EntranceIndex,
                                                         record.Scaffold.VariantKey);
            if (!mSceneEntriesByScaffoldVariant.emplace(requestKey, &record).second) {
                throw std::runtime_error("Duplicate OOT3D scaffold scene-entry request: " + requestKey);
            }
            const auto selectorKey = SceneEntrySelectorKey(record.Scaffold.EntranceIndex,
                                                           record.Scaffold.VariantConditions);
            if (!sceneEntrySelectors.emplace(selectorKey, &record).second) {
                throw std::runtime_error("Duplicate OOT3D scaffold scene-entry selector: " + selectorKey);
            }
            mSceneEntriesByScaffoldEntrance[record.Scaffold.EntranceIndex].push_back(&record);
        } else if (record.Kind == "cutscene") {
            RequireValue(record.Scaffold.RequestKind, "cutscene", "scaffold.request_kind");
            RequireValue(record.CompletionEvent, "cutscene_finished", "completion_event");
            if (asset->Family != "scene_profile" || asset->SceneId != record.Native.SceneId ||
                asset->SceneStem != record.Native.SceneStem || record.Native.SetupIndices.size() != 1 ||
                std::find(asset->SetupIndices.begin(), asset->SetupIndices.end(),
                          record.Native.SetupIndices.front()) == asset->SetupIndices.end()) {
                throw std::runtime_error("OOT3D semantic cutscene scene ownership mismatch: " + record.RouteId);
            }
            if (record.Native.SequenceAssetIds.empty()) {
                throw std::runtime_error("OOT3D semantic cutscene has no native QDB sequence: " + record.RouteId);
            }
            for (const auto& sequenceAssetId : record.Native.SequenceAssetIds) {
                const auto* sequenceAsset = assets.Find(sequenceAssetId);
                if (sequenceAsset == nullptr || sequenceAsset->Family != "cutscene_timeline" ||
                    sequenceAsset->SceneId != record.Native.SceneId) {
                    throw std::runtime_error("OOT3D semantic cutscene QDB ownership mismatch: " + sequenceAssetId);
                }
            }
            const auto requestKey = CutsceneRequestKey(
                record.Scaffold.GameState, record.Scaffold.SceneId, record.Scaffold.SceneSetupIndex,
                record.Scaffold.CutsceneIndex);
            if (!mCutscenesByScaffoldRequest.emplace(requestKey, &record).second) {
                throw std::runtime_error("Duplicate OOT3D scaffold cutscene request: " + requestKey);
            }
        } else {
            throw std::runtime_error("Unsupported OOT3D semantic route kind: " + record.Kind);
        }
    }
}

const SemanticRouteRecord* SemanticRouteCatalog::Find(std::string_view routeId) const {
    const auto it = mById.find(std::string(routeId));
    return it == mById.end() ? nullptr : it->second;
}

const SemanticRouteRecord* SemanticRouteCatalog::FindSemantic(std::string_view kind,
                                                               std::string_view semanticKey,
                                                               std::string_view variantKey) const {
    const auto it = mBySemanticRequest.find(SemanticRequestKey(kind, semanticKey, variantKey));
    return it == mBySemanticRequest.end() ? nullptr : it->second;
}

const SemanticRouteRecord* SemanticRouteCatalog::FindSceneRequest(int32_t scaffoldSceneId) const {
    const auto it = mScenesByScaffoldId.find(scaffoldSceneId);
    return it == mScenesByScaffoldId.end() ? nullptr : it->second;
}

const SemanticRouteRecord* SemanticRouteCatalog::FindSceneEntryRequest(
    int32_t scaffoldEntranceIndex, std::string_view variantKey) const {
    const auto it = mSceneEntriesByScaffoldVariant.find(
        SceneEntryVariantKey(scaffoldEntranceIndex, variantKey));
    return it == mSceneEntriesByScaffoldVariant.end() ? nullptr : it->second;
}

bool SemanticSceneEntryRouteMatch::Ready() const {
    return Route != nullptr && Status == "resolved";
}

const SemanticSceneRoomBinding* FindSceneRoomBinding(const SemanticRouteRecord& route,
                                                     int32_t scaffoldRoomIndex) {
    const auto binding = std::find_if(
        route.Native.RoomBindings.begin(), route.Native.RoomBindings.end(),
        [scaffoldRoomIndex](const auto& candidate) {
            return candidate.ScaffoldRoomIndex == scaffoldRoomIndex;
        });
    return binding == route.Native.RoomBindings.end() ? nullptr : &*binding;
}

SemanticSceneEntryRouteMatch SemanticRouteCatalog::MatchSceneEntryRequest(
    int32_t scaffoldEntranceIndex, std::span<const SemanticGameplayFact> facts) const {
    const auto routes = mSceneEntriesByScaffoldEntrance.find(scaffoldEntranceIndex);
    if (routes == mSceneEntriesByScaffoldEntrance.end()) {
        return { nullptr, "semantic_scene_entry_route_missing" };
    }

    const SemanticRouteRecord* best = nullptr;
    size_t bestSpecificity = 0;
    bool ambiguous = false;
    for (const auto* route : routes->second) {
        if (!ConditionsMatch(route->Scaffold.VariantConditions, facts)) {
            continue;
        }
        const auto specificity = route->Scaffold.VariantConditions.size();
        if (best == nullptr || specificity > bestSpecificity) {
            best = route;
            bestSpecificity = specificity;
            ambiguous = false;
        } else if (specificity == bestSpecificity) {
            ambiguous = true;
        }
    }
    if (best == nullptr) {
        return { nullptr, "semantic_scene_entry_variant_missing" };
    }
    if (ambiguous) {
        return { nullptr, "semantic_scene_entry_variant_ambiguous" };
    }
    return { best, "resolved" };
}

const SemanticRouteRecord* SemanticRouteCatalog::FindCutsceneRequest(std::string_view gameState,
                                                                      int32_t scaffoldSceneId,
                                                                      int32_t sceneSetupIndex,
                                                                      int32_t cutsceneIndex) const {
    const auto it = mCutscenesByScaffoldRequest.find(
        CutsceneRequestKey(gameState, scaffoldSceneId, sceneSetupIndex, cutsceneIndex));
    return it == mCutscenesByScaffoldRequest.end() ? nullptr : it->second;
}

const std::vector<SemanticRouteRecord>& SemanticRouteCatalog::Records() const {
    return mRecords;
}

bool SemanticRouteCatalog::Empty() const {
    return mRecords.empty();
}

} // namespace ThreeDsRecomp::Oot3d
