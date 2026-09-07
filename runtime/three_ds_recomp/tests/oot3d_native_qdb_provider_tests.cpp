#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeQdbProvider.h"

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

std::vector<uint8_t> ValidNativeQdbBytes() {
    std::vector<uint8_t> bytes(0x60, 0);
    PutLe32(bytes, 0x00, 0x51444220);
    PutLe32(bytes, 0x04, 3);
    PutLe32(bytes, 0x08, 2);
    PutLe32(bytes, 0x0C, 120);
    PutLe32(bytes, 0x10, 0x2D);
    PutLe32(bytes, 0x20, 0x0A);
    PutLe32(bytes, 0x24, 1);
    PutLe32(bytes, 0x58, 0xFFFFFFFF);
    return bytes;
}

nlohmann::json AssetCatalogDocument() {
    return {
        { "format", "oot3d_asset_catalog_v1" },
        { "status", "complete" },
        { "records", nlohmann::json::array({ {
            { "asset_id", "qdb:scene/spot00.zar!demo/opening.qdb" },
            { "family", "cutscene_timeline" },
            { "source_identity", "qdb:scene/spot00.zar!demo/opening.qdb" },
            { "canonical_resources", { "oot3d/native/qdb/scene/spot00.zar/0002_opening.qdb" } },
            { "dependencies", nlohmann::json::array() },
            { "required_engine_capabilities", { "native_qdb_source", "native_qdb_command_stream" } },
            { "support_tier", 2 },
            { "runtime_state", "packaged_not_bound" },
        } }) },
    };
}

TEST(Oot3dNativeQdbProvider, ParsesNativeHeaderCommandsAndTerminator) {
    const auto bytes = ValidNativeQdbBytes();
    const auto timeline = ThreeDsRecomp::Oot3d::ParseNativeQdb(bytes, "opening.qdb");

    ASSERT_EQ(timeline.Commands.size(), 2);
    EXPECT_EQ(timeline.VersionOrFlags, 3);
    EXPECT_EQ(timeline.EndFrame, 120);
    EXPECT_EQ(timeline.DecodedSize, 0x58);
    EXPECT_EQ(timeline.TrailerSize, 8);
    EXPECT_EQ(ThreeDsRecomp::Oot3d::NativeQdbCommandCategoryName(timeline.Commands[0].Category), "fixed16");
    EXPECT_EQ(timeline.Commands[1].CommandId, 0x0A);
    EXPECT_EQ(timeline.Commands[1].EntryCount, 1);
    EXPECT_EQ(timeline.Commands[1].TotalSize, 0x38);
}

TEST(Oot3dNativeQdbProvider, LoadsCatalogResourceWithoutN64Substitution) {
    const auto bytes = ValidNativeQdbBytes();
    const auto assets = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(AssetCatalogDocument());
    const auto* record = assets.Find("qdb:scene/spot00.zar!demo/opening.qdb");
    ASSERT_NE(record, nullptr);
    ThreeDsRecomp::Oot3d::NativeQdbProvider provider([bytes](const std::string& path) {
        if (path != "oot3d/native/qdb/scene/spot00.zar/0002_opening.qdb") {
            return std::shared_ptr<const std::vector<uint8_t>>{};
        }
        return std::make_shared<const std::vector<uint8_t>>(bytes);
    });

    const auto qdb = provider.Load(*record);

    ASSERT_NE(qdb, nullptr);
    EXPECT_EQ(qdb->CatalogRecord, record);
    EXPECT_EQ(qdb->Timeline.EndFrame, 120);
    EXPECT_EQ(qdb->Source->ResourcePath,
              "oot3d/native/qdb/scene/spot00.zar/0002_opening.qdb");
    EXPECT_EQ(provider.Load(*record), qdb);
}

TEST(Oot3dNativeQdbProvider, RejectsMissingNativeTerminator) {
    auto bytes = ValidNativeQdbBytes();
    PutLe32(bytes, 0x58, 0);
    EXPECT_THROW(ThreeDsRecomp::Oot3d::ParseNativeQdb(bytes, "bad.qdb"), std::runtime_error);
}

} // namespace
