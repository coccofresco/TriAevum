#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dSceneProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

nlohmann::json AssetCatalogDocument(int32_t sceneId = 0x55) {
    return {
        { "format", "oot3d_asset_catalog_v1" },
        { "status", "complete" },
        { "records", nlohmann::json::array({ {
            { "asset_id", "scene:spot04_info.zsi" },
            { "family", "scene_profile" },
            { "source_identity", "scene:spot04_info.zsi" },
            { "canonical_resources", nlohmann::json::array({ "oot3d/native/scene/spot04_info.zsi" }) },
            { "dependencies", nlohmann::json::array() },
            { "required_engine_capabilities", nlohmann::json::array() },
            { "support_tier", 2 },
            { "runtime_state", "packaged_not_bound" },
            { "ownership", {
                { "scene_id", sceneId },
                { "scene_stem", "spot04" },
                { "setup_indices", { 0, 3 } },
            } },
        } }) },
    };
}

nlohmann::json SceneEntryAssetCatalogDocument() {
    auto document = AssetCatalogDocument();
    document["records"].push_back({
        { "asset_id", "room:spot04_0_info.zsi" },
        { "family", "scene_room_source" },
        { "source_identity", "room:spot04_0_info.zsi" },
        { "canonical_resources", { "oot3d/native/scene/spot04_0_info.zsi" } },
        { "dependencies", { "scene:spot04_info.zsi" } },
        { "required_engine_capabilities", nlohmann::json::array() },
        { "support_tier", 2 },
        { "runtime_state", "packaged_not_bound" },
        { "ownership", {
            { "scene_id", 0x55 },
            { "room_index", 0 },
            { "scene_stem", "spot04" },
            { "setup_indices", { 0, 3 } },
        } },
    });
    return document;
}

nlohmann::json RouteRecord(int32_t scaffoldSceneId = 0x55, int32_t nativeSceneId = 0x55) {
    return {
        { "route_id", "scene:SCENE_KOKIRI_FOREST" },
        { "kind", "scene" },
        { "scaffold", {
            { "system", "oot_n64_gameplay" },
            { "request_kind", "scene" },
            { "semantic_key", "SCENE_KOKIRI_FOREST" },
            { "scene_id", scaffoldSceneId },
        } },
        { "native", {
            { "system", "oot3d" },
            { "asset_id", "scene:spot04_info.zsi" },
            { "scene_id", nativeSceneId },
            { "scene_path", "spot04_info.zsi" },
            { "scene_stem", "spot04" },
            { "setup_indices", { 0, 3 } },
        } },
        { "authority", {
            { "control_flow", "oot_n64_gameplay_scaffold" },
            { "content", "oot3d_native" },
            { "timing", "oot3d_native" },
            { "rendering", "oot3d_native" },
        } },
        { "completion_event", "scene_exit_requested" },
        { "status", "resolved" },
    };
}

nlohmann::json RouteCatalogDocument(nlohmann::json records) {
    return {
        { "format", "oot3d_semantic_route_catalog_v1" },
        { "status", "complete" },
        { "records", std::move(records) },
    };
}

nlohmann::json CutsceneAssetCatalogDocument() {
    auto document = AssetCatalogDocument(0x51);
    document["records"][0]["asset_id"] = "scene:spot00_info.zsi";
    document["records"][0]["source_identity"] = "scene:spot00_info.zsi";
    document["records"][0]["ownership"]["scene_stem"] = "spot00";
    document["records"][0]["ownership"]["setup_indices"] = { 0, 6 };
    for (int index = 0; index < 2; ++index) {
        const std::string assetId = "qdb:scene/spot00.zar!demo/epona_0" + std::to_string(index) + ".qdb";
        document["records"].push_back({
            { "asset_id", assetId },
            { "family", "cutscene_timeline" },
            { "source_identity", assetId },
            { "canonical_resources", { "oot3d/native/qdb/epona.qdb" } },
            { "dependencies", nlohmann::json::array() },
            { "required_engine_capabilities", { "native_qdb_source", "native_qdb_command_stream" } },
            { "support_tier", 2 },
            { "runtime_state", "packaged_not_bound" },
            { "ownership", {
                { "scene_id", 0x51 },
                { "scene_stem", "spot00" },
                { "setup_indices", nlohmann::json::array() },
            } },
        });
    }
    return document;
}

nlohmann::json CutsceneRouteRecord() {
    return {
        { "route_id", "cutscene:CUTSCENE_OPENING_TITLE" },
        { "kind", "cutscene" },
        { "scaffold", {
            { "system", "oot_n64_gameplay" },
            { "request_kind", "cutscene" },
            { "semantic_key", "CUTSCENE_OPENING_TITLE" },
            { "scene_id", 0x51 },
            { "game_state", "Opening" },
            { "scene_setup_index", 7 },
            { "cutscene_index", 0xFFF3 },
        } },
        { "native", {
            { "system", "oot3d" },
            { "asset_id", "scene:spot00_info.zsi" },
            { "scene_id", 0x51 },
            { "scene_path", "spot00_info.zsi" },
            { "scene_stem", "spot00" },
            { "setup_indices", { 6 } },
            { "sequence_asset_ids", {
                "qdb:scene/spot00.zar!demo/epona_00.qdb",
                "qdb:scene/spot00.zar!demo/epona_01.qdb",
            } },
        } },
        { "authority", {
            { "control_flow", "oot_n64_gameplay_scaffold" },
            { "content", "oot3d_native" },
            { "timing", "oot3d_native" },
            { "rendering", "oot3d_native" },
        } },
        { "completion_event", "cutscene_finished" },
        { "status", "resolved" },
    };
}

nlohmann::json SceneEntryRouteRecord(int32_t scaffoldEntranceIndex = 0x211,
                                     int32_t nativeGlobalEntranceIndex = 900) {
    return {
        { "route_id", "scene_entry:SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE" },
        { "kind", "scene_entry" },
        { "scaffold", {
            { "system", "oot_n64_gameplay" },
            { "request_kind", "scene_entry" },
            { "semantic_key", "SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE" },
            { "scene_id", 0x55 },
            { "entrance_index", scaffoldEntranceIndex },
            { "entrance_symbol", "ENTR_KOKIRI_FOREST_OUTSIDE_LINKS_HOUSE" },
            { "origin_semantic_key", "SCENE_LINKS_HOUSE_EXIT" },
            { "variant_key", "KOKIRI_FOREST_INITIAL_CHILD_DAY" },
            { "variant_source", "oot_n64_semantic_source_offline" },
            { "variant_conditions", nlohmann::json::array({
                { { "fact", "player.age" }, { "equals", "child" } },
                { { "fact", "world.day_phase" }, { "equals", "day" } },
                { { "fact", "gameplay.layer" }, { "equals", "normal" } },
            }) },
        } },
        { "native", {
            { "system", "oot3d" },
            { "asset_id", "scene:spot04_info.zsi" },
            { "scene_id", 0x55 },
            { "scene_path", "spot04_info.zsi" },
            { "scene_stem", "spot04" },
            { "setup_indices", { 0 } },
            { "setup_selection", "oot3d_new_game_child_day_scene_setup" },
            { "room_bindings", nlohmann::json::array({
                { { "scaffold_room_index", 0 }, { "native_room_index", 0 } },
            }) },
            { "global_entrance_index", nativeGlobalEntranceIndex },
            { "local_entrance_index", 3 },
            { "spatial_source", "oot3d_zsi_spawn_entry" },
            { "camera_source", "oot3d_zsi_player_params_then_collision_or_default_camera" },
            { "player_entry_state_source", "oot3d_player_entry_state" },
            { "population_source", "oot3d_zsi_actor_and_object_lists" },
            { "actor_configuration_source", "oot3d_zsi_actor_params" },
        } },
        { "authority", {
            { "control_flow", "oot_n64_gameplay_scaffold" },
            { "content", "oot3d_native" },
            { "timing", "oot3d_native" },
            { "rendering", "oot3d_native" },
            { "spatial_state", "oot3d_native" },
            { "player_entry_state", "oot3d_native" },
            { "population", "oot3d_native" },
            { "actor_configuration", "oot3d_native" },
            { "behavior_semantics", "oot_n64_semantic_scaffold_with_verified_oot3d_deltas" },
        } },
        { "completion_event", "scene_entry_finished" },
        { "status", "resolved" },
    };
}

TEST(Oot3dSemanticRouteCatalog, ResolvesScaffoldSceneToNativeAssetIdentity) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument());
    auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
        RouteCatalogDocument(nlohmann::json::array({ RouteRecord() })), assets);

    const auto* route = routes.FindSceneRequest(0x55);
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->Scaffold.SemanticKey, "SCENE_KOKIRI_FOREST");
    EXPECT_EQ(route->Native.AssetId, "scene:spot04_info.zsi");
    EXPECT_EQ(route->Authority.Content, "oot3d_native");
    EXPECT_EQ(route->Authority.Timing, "oot3d_native");
    EXPECT_EQ(routes.FindSemantic("scene", "SCENE_KOKIRI_FOREST"), route);

    ThreeDsRecomp::Oot3d::SceneProvider provider(assets);
    const auto selection = provider.ResolveSemanticScene(routes, 0x55, 3, 0);
    EXPECT_EQ(selection.Route, route);
    EXPECT_EQ(selection.NativeSceneId, 0x55);
    EXPECT_TRUE(selection.SetupAvailable);
    EXPECT_EQ(selection.RouteStatus, "resolved");
}

TEST(Oot3dSemanticRouteCatalog, KeepsScaffoldAndNativeSceneIdsDistinct) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument(0x155));
    auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
        RouteCatalogDocument(nlohmann::json::array({ RouteRecord(0x55, 0x155) })), assets);

    const auto* route = routes.FindSceneRequest(0x55);
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->Scaffold.SceneId, 0x55);
    EXPECT_EQ(route->Native.SceneId, 0x155);
    ThreeDsRecomp::Oot3d::SceneProvider provider(assets);
    EXPECT_EQ(provider.ResolveSemanticScene(routes, 0x55, 0, 0).NativeSceneId, 0x155);
}

TEST(Oot3dSemanticRouteCatalog, RejectsTargetAbsentFromNativeAssetCatalog) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse({
        { "format", "oot3d_asset_catalog_v1" },
        { "status", "complete" },
        { "records", nlohmann::json::array() },
    });
    EXPECT_THROW(ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
                     RouteCatalogDocument(nlohmann::json::array({ RouteRecord() })), assets),
                 std::runtime_error);
}

TEST(Oot3dSemanticRouteCatalog, RejectsNativeOwnershipMismatch) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument());
    EXPECT_THROW(ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
                     RouteCatalogDocument(nlohmann::json::array({ RouteRecord(0x55, 0x51) })), assets),
                 std::runtime_error);
}

TEST(Oot3dSemanticRouteCatalog, RejectsN64ContentAuthority) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument());
    auto route = RouteRecord();
    route["authority"]["content"] = "oot_n64";
    EXPECT_THROW(ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
                     RouteCatalogDocument(nlohmann::json::array({ route })), assets),
                 std::runtime_error);
}

TEST(Oot3dSemanticRouteCatalog, ResolvesOpeningControlRequestToNativeQdbSequence) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(CutsceneAssetCatalogDocument());
    auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
        RouteCatalogDocument(nlohmann::json::array({ CutsceneRouteRecord() })), assets);

    const auto* route = routes.FindCutsceneRequest("Opening", 0x51, 7, 0xFFF3);

    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->Native.SetupIndices, std::vector<int32_t>({ 6 }));
    EXPECT_EQ(route->Native.SequenceAssetIds.size(), 2);
    EXPECT_EQ(route->Native.SequenceAssetIds.front(), "qdb:scene/spot00.zar!demo/epona_00.qdb");
    EXPECT_EQ(route->CompletionEvent, "cutscene_finished");
    EXPECT_EQ(routes.FindSemantic("cutscene", "CUTSCENE_OPENING_TITLE"), route);
    EXPECT_EQ(routes.FindCutsceneRequest("Opening", 0x51, 6, 0xFFF3), nullptr);
}

TEST(Oot3dSemanticRouteCatalog, RejectsCutsceneSequenceWithN64AssetFamily) {
    auto document = CutsceneAssetCatalogDocument();
    document["records"][1]["family"] = "scene_profile";
    document["records"][1]["ownership"]["scene_id"] = 0x52;
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(document);
    EXPECT_THROW(ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
                     RouteCatalogDocument(nlohmann::json::array({ CutsceneRouteRecord() })), assets),
                 std::runtime_error);
}

TEST(Oot3dSemanticRouteCatalog, ResolvesSceneEntryWithoutAssumingMatchingEntranceIndices) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(SceneEntryAssetCatalogDocument());
    auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
        RouteCatalogDocument(nlohmann::json::array({ SceneEntryRouteRecord(0x211, 900) })), assets);

    const auto* route = routes.FindSceneEntryRequest(0x211, "KOKIRI_FOREST_INITIAL_CHILD_DAY");
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->Scaffold.EntranceIndex, 0x211);
    EXPECT_EQ(route->Native.GlobalEntranceIndex, 900);
    EXPECT_EQ(route->Native.LocalEntranceIndex, 3);
    ASSERT_EQ(route->Native.RoomBindings.size(), 1);
    EXPECT_EQ(route->Native.RoomBindings[0].ScaffoldRoomIndex, 0);
    EXPECT_EQ(route->Native.RoomBindings[0].NativeRoomIndex, 0);
    EXPECT_EQ(ThreeDsRecomp::Oot3d::FindSceneRoomBinding(*route, 0), &route->Native.RoomBindings[0]);
    EXPECT_EQ(route->Native.SpatialSource, "oot3d_zsi_spawn_entry");
    EXPECT_EQ(route->Authority.SpatialState, "oot3d_native");
    EXPECT_EQ(routes.FindSemantic("scene_entry", "SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE",
                                  "KOKIRI_FOREST_INITIAL_CHILD_DAY"), route);

    std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts{
        { "player.age", "child" },
        { "world.day_phase", "day" },
        { "gameplay.layer", "normal" },
    };
    const auto match = routes.MatchSceneEntryRequest(0x211, facts);
    ASSERT_TRUE(match.Ready()) << match.Status;
    EXPECT_EQ(match.Route, route);
    facts[0].Value = "adult";
    EXPECT_EQ(routes.MatchSceneEntryRequest(0x211, facts).Status,
              "semantic_scene_entry_variant_missing");
}

TEST(Oot3dSemanticRouteCatalog, RejectsSceneEntryWhoseSpatialAuthorityIsN64) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(SceneEntryAssetCatalogDocument());
    auto route = SceneEntryRouteRecord();
    route["authority"]["spatial_state"] = "oot_n64";
    EXPECT_THROW(ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
                     RouteCatalogDocument(nlohmann::json::array({ route })), assets),
                 std::runtime_error);
}

TEST(Oot3dSemanticRouteCatalog, RejectsSceneEntryRoomAbsentFromNativeCatalog) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(SceneEntryAssetCatalogDocument());
    auto route = SceneEntryRouteRecord();
    route["native"]["room_bindings"][0]["native_room_index"] = 1;
    EXPECT_THROW(ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(
                     RouteCatalogDocument(nlohmann::json::array({ route })), assets),
                 std::runtime_error);
}

} // namespace
