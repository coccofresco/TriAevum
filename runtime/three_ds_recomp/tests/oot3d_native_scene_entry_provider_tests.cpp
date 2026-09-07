#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSceneEntryProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

void PutLe16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void PutLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> NativeSceneBytes(uint16_t params = 0x0DFF, uint8_t entranceRoom = 0) {
    std::vector<uint8_t> bytes(0xB8, 0);
    bytes[0] = 'Z';
    bytes[1] = 'S';
    bytes[2] = 'I';
    bytes[3] = 0x01;

    PutLe32(bytes, 0x18, 0x15);
    PutLe32(bytes, 0x20, 0x0400);
    PutLe32(bytes, 0x24, 0x60);
    PutLe32(bytes, 0x28, 0x0406);
    PutLe32(bytes, 0x2C, 0xA0);
    PutLe32(bytes, 0x30, 0x14);

    const size_t spawnOffset = 0x70 + 3 * 0x10;
    PutLe16(bytes, spawnOffset + 0x00, 0);
    PutLe16(bytes, spawnOffset + 0x02, static_cast<uint16_t>(-31));
    PutLe16(bytes, spawnOffset + 0x04, 100);
    PutLe16(bytes, spawnOffset + 0x06, 1073);
    PutLe16(bytes, spawnOffset + 0x08, 0);
    PutLe16(bytes, spawnOffset + 0x0A, static_cast<uint16_t>(-32767));
    PutLe16(bytes, spawnOffset + 0x0C, 0);
    PutLe16(bytes, spawnOffset + 0x0E, params);

    for (uint8_t index = 0; index < 4; ++index) {
        bytes[0xB0 + index * 2] = index;
        bytes[0xB0 + index * 2 + 1] = entranceRoom;
    }
    return bytes;
}

nlohmann::json AssetCatalogDocument() {
    return {
        { "format", "oot3d_asset_catalog_v1" },
        { "status", "complete" },
        { "records", nlohmann::json::array({ {
            { "asset_id", "scene:spot04_info.zsi" },
            { "family", "scene_profile" },
            { "source_identity", "scene:spot04_info.zsi" },
            { "canonical_resources", { "oot3d/native/scene/spot04_info.zsi" } },
            { "dependencies", nlohmann::json::array() },
            { "required_engine_capabilities", { "native_zsi_scene_commands" } },
            { "support_tier", 2 },
            { "runtime_state", "packaged_not_bound" },
            { "ownership", {
                { "scene_id", 0x55 },
                { "scene_stem", "spot04" },
                { "setup_indices", { 0 } },
            } },
        }, {
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
                { "setup_indices", { 0 } },
            } },
        } }) },
    };
}

nlohmann::json RouteCatalogDocument() {
    return {
        { "format", "oot3d_semantic_route_catalog_v1" },
        { "status", "complete" },
        { "records", nlohmann::json::array({ {
            { "route_id", "scene_entry:SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE" },
            { "kind", "scene_entry" },
            { "scaffold", {
                { "system", "oot_n64_gameplay" },
                { "request_kind", "scene_entry" },
                { "semantic_key", "SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE" },
                { "scene_id", 0x55 },
                { "entrance_index", 0x211 },
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
                { "global_entrance_index", 900 },
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
        } }) },
    };
}

TEST(Oot3dNativeSceneEntryProvider, ParsesExactNativeZsiEntranceAndSpawn) {
    const auto entry = ThreeDsRecomp::Oot3d::ParseNativeZsiSceneEntry(NativeSceneBytes(), 0, 3, "spot04_info.zsi");

    EXPECT_EQ(entry.SpawnIndex, 3);
    EXPECT_EQ(entry.Room, 0);
    EXPECT_EQ(entry.ActorId, 0);
    EXPECT_EQ(entry.PositionX, -31);
    EXPECT_EQ(entry.PositionY, 100);
    EXPECT_EQ(entry.PositionZ, 1073);
    EXPECT_EQ(entry.RotationY, -32767);
    EXPECT_EQ(entry.Params, 0x0DFF);
    EXPECT_EQ(entry.PlayerStartMode, 0x0D);
    EXPECT_EQ(entry.CameraDataIndex, 0xFF);
    EXPECT_FALSE(entry.UsesExplicitCameraData);
}

TEST(Oot3dNativeSceneEntryProvider, ResolvesShipIntentToNativeSpatialState) {
    const auto bytes = NativeSceneBytes(0x0D05);
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument());
    const auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(RouteCatalogDocument(), assets);
    ThreeDsRecomp::Oot3d::NativeSourceProvider sources([bytes](const std::string& path) {
        if (path != "oot3d/native/scene/spot04_info.zsi") {
            return std::shared_ptr<const std::vector<uint8_t>>{};
        }
        return std::make_shared<const std::vector<uint8_t>>(bytes);
    });
    ThreeDsRecomp::Oot3d::NativeSceneEntryProvider provider(assets, sources);

    std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts{
        { "player.age", "child" },
        { "world.day_phase", "day" },
        { "gameplay.layer", "normal" },
    };
    const auto selection = provider.ResolveSemanticEntry(routes, 0x211, facts);

    ASSERT_TRUE(selection.Ready()) << selection.Status;
    EXPECT_EQ(selection.Entry.GlobalEntranceIndex, 900);
    EXPECT_EQ(selection.Entry.LocalEntranceIndex, 3);
    EXPECT_EQ(selection.Entry.PositionX, -31);
    EXPECT_EQ(selection.Entry.CameraDataIndex, 5);
    EXPECT_TRUE(selection.Entry.UsesExplicitCameraData);
    EXPECT_EQ(provider.ResolveSemanticEntry(routes, 0x212, facts).Status,
              "semantic_scene_entry_route_missing");
    facts[1].Value = "night";
    EXPECT_EQ(provider.ResolveSemanticEntry(routes, 0x211, facts).Status,
              "semantic_scene_entry_variant_missing");
}

TEST(Oot3dNativeSceneEntryProvider, RejectsSpawnRoomOutsideSemanticRoomMap) {
    const auto bytes = NativeSceneBytes(0x0DFF, 1);
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument());
    const auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(RouteCatalogDocument(), assets);
    ThreeDsRecomp::Oot3d::NativeSourceProvider sources([bytes](const std::string&) {
        return std::make_shared<const std::vector<uint8_t>>(bytes);
    });
    ThreeDsRecomp::Oot3d::NativeSceneEntryProvider provider(assets, sources);
    const std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts{
        { "player.age", "child" },
        { "world.day_phase", "day" },
        { "gameplay.layer", "normal" },
    };

    EXPECT_EQ(provider.ResolveSemanticEntry(routes, 0x211, facts).Status,
              "native_scene_entry_room_not_routed");
}

} // namespace
