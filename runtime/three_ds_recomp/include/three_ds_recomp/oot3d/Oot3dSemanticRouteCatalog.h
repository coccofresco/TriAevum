#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"

namespace ThreeDsRecomp::Oot3d {

struct SemanticGameplayFact {
    std::string Key;
    std::string Value;
};

struct SemanticRouteCondition {
    std::string Fact;
    std::string Equals;
};

struct SemanticSceneRoomBinding {
    int32_t ScaffoldRoomIndex = -1;
    int32_t NativeRoomIndex = -1;
};

struct SemanticRouteRequest {
    std::string System;
    std::string RequestKind;
    std::string SemanticKey;
    int32_t SceneId = -1;
    std::string GameState;
    int32_t SceneSetupIndex = -1;
    int32_t CutsceneIndex = -1;
    int32_t EntranceIndex = -1;
    std::string EntranceSymbol;
    std::string OriginSemanticKey;
    std::string VariantKey;
    std::string VariantSource;
    std::vector<SemanticRouteCondition> VariantConditions;
};

struct SemanticRouteNativeTarget {
    std::string System;
    std::string AssetId;
    int32_t SceneId = -1;
    std::string ScenePath;
    std::string SceneStem;
    std::vector<int32_t> SetupIndices;
    std::vector<SemanticSceneRoomBinding> RoomBindings;
    std::vector<std::string> SequenceAssetIds;
    std::string SetupSelection;
    int32_t GlobalEntranceIndex = -1;
    int32_t LocalEntranceIndex = -1;
    std::string SpatialSource;
    std::string CameraSource;
    std::string PlayerEntryStateSource;
    std::string PopulationSource;
    std::string ActorConfigurationSource;
};

struct SemanticRouteAuthority {
    std::string ControlFlow;
    std::string Content;
    std::string Timing;
    std::string Rendering;
    std::string SpatialState;
    std::string PlayerEntryState;
    std::string Population;
    std::string ActorConfiguration;
    std::string BehaviorSemantics;
};

struct SemanticRouteRecord {
    std::string RouteId;
    std::string Kind;
    SemanticRouteRequest Scaffold;
    SemanticRouteNativeTarget Native;
    SemanticRouteAuthority Authority;
    std::string CompletionEvent;
    std::string Status;
};

struct SemanticSceneEntryRouteMatch {
    const SemanticRouteRecord* Route = nullptr;
    std::string Status;

    bool Ready() const;
};

const SemanticSceneRoomBinding* FindSceneRoomBinding(const SemanticRouteRecord& route,
                                                     int32_t scaffoldRoomIndex);

class SemanticRouteCatalog {
  public:
    SemanticRouteCatalog() = default;
    SemanticRouteCatalog(const SemanticRouteCatalog&) = delete;
    SemanticRouteCatalog& operator=(const SemanticRouteCatalog&) = delete;
    SemanticRouteCatalog(SemanticRouteCatalog&&) = default;
    SemanticRouteCatalog& operator=(SemanticRouteCatalog&&) = default;

    static SemanticRouteCatalog Parse(const nlohmann::json& document, const AssetCatalog& assets);
    static SemanticRouteCatalog LoadFile(const std::filesystem::path& path, const AssetCatalog& assets);

    const SemanticRouteRecord* Find(std::string_view routeId) const;
    const SemanticRouteRecord* FindSemantic(std::string_view kind, std::string_view semanticKey,
                                            std::string_view variantKey = {}) const;
    const SemanticRouteRecord* FindSceneRequest(int32_t scaffoldSceneId) const;
    const SemanticRouteRecord* FindSceneEntryRequest(int32_t scaffoldEntranceIndex,
                                                     std::string_view variantKey) const;
    SemanticSceneEntryRouteMatch MatchSceneEntryRequest(
        int32_t scaffoldEntranceIndex, std::span<const SemanticGameplayFact> facts) const;
    const SemanticRouteRecord* FindCutsceneRequest(std::string_view gameState, int32_t scaffoldSceneId,
                                                   int32_t sceneSetupIndex, int32_t cutsceneIndex) const;
    const std::vector<SemanticRouteRecord>& Records() const;
    bool Empty() const;

  private:
    void BuildIndices(const AssetCatalog& assets);

    std::vector<SemanticRouteRecord> mRecords;
    std::unordered_map<std::string, const SemanticRouteRecord*> mById;
    std::unordered_map<std::string, const SemanticRouteRecord*> mBySemanticRequest;
    std::unordered_map<int32_t, const SemanticRouteRecord*> mScenesByScaffoldId;
    std::unordered_map<std::string, const SemanticRouteRecord*> mSceneEntriesByScaffoldVariant;
    std::unordered_map<int32_t, std::vector<const SemanticRouteRecord*>> mSceneEntriesByScaffoldEntrance;
    std::unordered_map<std::string, const SemanticRouteRecord*> mCutscenesByScaffoldRequest;
};

} // namespace ThreeDsRecomp::Oot3d
