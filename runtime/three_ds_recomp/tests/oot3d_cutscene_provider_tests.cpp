#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dCutsceneProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

void PutLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> Qdb(int32_t endFrame) {
    std::vector<uint8_t> bytes(0x20, 0);
    PutLe32(bytes, 0, 0x51444220);
    PutLe32(bytes, 4, 3);
    PutLe32(bytes, 8, 1);
    PutLe32(bytes, 0xC, static_cast<uint32_t>(endFrame));
    PutLe32(bytes, 0x10, 0x2D);
    bytes.resize(0x30, 0);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    return bytes;
}

nlohmann::json AssetRecord(const std::string& id, const std::string& family,
                           const std::string& resource, int32_t sceneId) {
    return {
        { "asset_id", id }, { "family", family }, { "source_identity", id },
        { "canonical_resources", resource.empty() ? nlohmann::json::array() : nlohmann::json::array({ resource }) },
        { "dependencies", nlohmann::json::array() },
        { "required_engine_capabilities", nlohmann::json::array() },
        { "support_tier", 2 }, { "runtime_state", "packaged_not_bound" },
        { "ownership", {
            { "scene_id", sceneId }, { "scene_stem", "spot00" }, { "setup_indices", { 0, 6 } },
        } },
    };
}

nlohmann::json Assets() {
    return {
        { "format", "oot3d_asset_catalog_v1" }, { "status", "complete" },
        { "records", nlohmann::json::array({
            AssetRecord("scene:spot00_info.zsi", "scene_profile", "oot3d/native/scene/spot00_info.zsi", 0x51),
            AssetRecord("qdb:scene/spot00.zar!demo/epona_00.qdb", "cutscene_timeline", "qdb/epona_00.qdb", 0x51),
            AssetRecord("qdb:scene/spot00.zar!demo/epona_01.qdb", "cutscene_timeline", "qdb/epona_01.qdb", 0x51),
        }) },
    };
}

nlohmann::json Routes() {
    return {
        { "format", "oot3d_semantic_route_catalog_v1" }, { "status", "complete" },
        { "records", nlohmann::json::array({ {
            { "route_id", "cutscene:CUTSCENE_OPENING_TITLE" }, { "kind", "cutscene" },
            { "scaffold", {
                { "system", "oot_n64_gameplay" }, { "request_kind", "cutscene" },
                { "semantic_key", "CUTSCENE_OPENING_TITLE" }, { "scene_id", 0x51 },
                { "game_state", "Opening" }, { "scene_setup_index", 7 },
                { "cutscene_index", 0xFFF3 },
            } },
            { "native", {
                { "system", "oot3d" }, { "asset_id", "scene:spot00_info.zsi" },
                { "scene_id", 0x51 }, { "scene_path", "spot00_info.zsi" },
                { "scene_stem", "spot00" }, { "setup_indices", { 6 } },
                { "sequence_asset_ids", {
                    "qdb:scene/spot00.zar!demo/epona_00.qdb",
                    "qdb:scene/spot00.zar!demo/epona_01.qdb",
                } },
            } },
            { "authority", {
                { "control_flow", "oot_n64_gameplay_scaffold" }, { "content", "oot3d_native" },
                { "timing", "oot3d_native" }, { "rendering", "oot3d_native" },
            } },
            { "completion_event", "cutscene_finished" }, { "status", "resolved" },
        } }) },
    };
}

TEST(Oot3dCutsceneProvider, LoadsOrderedNativeSequenceAndReturnsCompletionContract) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(Assets());
    const auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(Routes(), assets);
    ThreeDsRecomp::Oot3d::CutsceneProvider provider(assets, [](const std::string& path) {
        const auto bytes = path == "qdb/epona_00.qdb" ? Qdb(1680) :
                           path == "qdb/epona_01.qdb" ? Qdb(2595) : std::vector<uint8_t>{};
        if (bytes.empty()) {
            return std::shared_ptr<const std::vector<uint8_t>>{};
        }
        return std::make_shared<const std::vector<uint8_t>>(bytes);
    });

    const auto selection = provider.ResolveSemanticCutscene(routes, "Opening", 0x51, 7, 0xFFF3);

    ASSERT_TRUE(selection.Ready());
    ASSERT_EQ(selection.NativeTimelines.size(), 2);
    EXPECT_EQ(selection.NativeTimelines[0]->Timeline.EndFrame, 1680);
    EXPECT_EQ(selection.NativeTimelines[1]->Timeline.EndFrame, 2595);
    const auto completion = provider.Complete(selection);
    EXPECT_EQ(completion.RouteId, "cutscene:CUTSCENE_OPENING_TITLE");
    EXPECT_EQ(completion.Event, "cutscene_finished");
}

TEST(Oot3dCutsceneProvider, DoesNotFallbackWhenNativeTimelineIsMissing) {
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(Assets());
    const auto routes = ThreeDsRecomp::Oot3d::SemanticRouteCatalog::Parse(Routes(), assets);
    ThreeDsRecomp::Oot3d::CutsceneProvider provider(assets, [](const std::string&) {
        return std::shared_ptr<const std::vector<uint8_t>>{};
    });

    const auto selection = provider.ResolveSemanticCutscene(routes, "Opening", 0x51, 7, 0xFFF3);

    EXPECT_FALSE(selection.Ready());
    EXPECT_EQ(selection.Status, "native_timeline_source_missing");
    EXPECT_THROW(provider.Complete(selection), std::invalid_argument);
}

} // namespace
