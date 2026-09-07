#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dEngineCapabilities.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <nlohmann/json.hpp>

namespace ThreeDsRecomp::Oot3d {
namespace {

const std::vector<const AssetCatalogRecord*> kEmptyRecordList;

std::vector<std::string> ReadStringArray(const nlohmann::json& object, const char* key) {
    std::vector<std::string> values;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return values;
    }
    values.reserve(it->size());
    for (const auto& value : *it) {
        if (!value.is_string()) {
            throw std::runtime_error(std::string("OOT3D catalog field contains a non-string: ") + key);
        }
        values.push_back(value.get<std::string>());
    }
    return values;
}

std::vector<int32_t> ReadIntArray(const nlohmann::json& object, const char* key) {
    std::vector<int32_t> values;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return values;
    }
    for (const auto& value : *it) {
        if (!value.is_number_integer()) {
            throw std::runtime_error(std::string("OOT3D catalog field contains a non-integer: ") + key);
        }
        values.push_back(value.get<int32_t>());
    }
    return values;
}

int64_t RoomKey(int32_t sceneId, int32_t roomIndex) {
    return (static_cast<int64_t>(sceneId) << 32) | static_cast<uint32_t>(roomIndex);
}

std::string RequiredString(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        throw std::runtime_error(std::string("OOT3D catalog record lacks string field: ") + key);
    }
    return it->get<std::string>();
}

std::string OptionalString(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

std::pair<std::string, std::string> SplitSourceIdentity(std::string_view identity) {
    const size_t colon = identity.find(':');
    const size_t bang = identity.find('!', colon == std::string_view::npos ? 0 : colon + 1);
    if (colon == std::string_view::npos || bang == std::string_view::npos ||
        bang <= colon + 1 || bang + 1 >= identity.size()) {
        return {};
    }
    return { std::string(identity.substr(colon + 1, bang - colon - 1)),
             std::string(identity.substr(bang + 1)) };
}

} // namespace

AssetCatalog AssetCatalog::Parse(const nlohmann::json& document) {
    if (!document.is_object() || document.value("format", "") != "oot3d_asset_catalog_v1" ||
        document.value("status", "") != "complete") {
        throw std::runtime_error("Unsupported or incomplete OOT3D asset catalog");
    }
    const auto recordsIt = document.find("records");
    if (recordsIt == document.end() || !recordsIt->is_array()) {
        throw std::runtime_error("OOT3D asset catalog has no records array");
    }

    AssetCatalog catalog;
    catalog.mRecords.reserve(recordsIt->size());
    for (const auto& source : *recordsIt) {
        if (!source.is_object()) {
            throw std::runtime_error("OOT3D asset catalog contains a non-object record");
        }
        AssetCatalogRecord record;
        record.AssetId = RequiredString(source, "asset_id");
        record.Family = RequiredString(source, "family");
        record.SourceIdentity = RequiredString(source, "source_identity");
        std::tie(record.SourceContainer, record.SourceMember) =
            SplitSourceIdentity(record.SourceIdentity);
        record.CanonicalResources = ReadStringArray(source, "canonical_resources");
        record.Dependencies = ReadStringArray(source, "dependencies");
        record.RequiredEngineCapabilities = ReadStringArray(source, "required_engine_capabilities");
        record.SupportTier = source.value("support_tier", 0U);
        record.RuntimeState = RequiredString(source, "runtime_state");
        const auto metadata = source.find("metadata");
        if (metadata != source.end() && metadata->is_object()) {
            const auto modelKind = metadata->find("model_kind");
            if (modelKind != metadata->end() && modelKind->is_string()) {
                record.ModelKind = modelKind->get<std::string>();
            }
        }
        const auto ownership = source.find("ownership");
        if (ownership != source.end() && ownership->is_object()) {
            record.SceneId = ownership->value("scene_id", -1);
            record.RoomIndex = ownership->value("room_index", -1);
            record.SceneStem = OptionalString(*ownership, "scene_stem");
            record.SceneShardManifestResource = OptionalString(*ownership, "scene_shard_manifest_resource");
            record.SetupIndices = ReadIntArray(*ownership, "setup_indices");
        }
        catalog.mRecords.push_back(std::move(record));
    }
    catalog.BuildIndices();
    return catalog;
}

AssetCatalog AssetCatalog::LoadFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Unable to open OOT3D asset catalog: " + path.string());
    }
    return Parse(nlohmann::json::parse(stream));
}

void AssetCatalog::BuildIndices() {
    mById.reserve(mRecords.size());
    for (const auto& record : mRecords) {
        if (!mById.emplace(record.AssetId, &record).second) {
            throw std::runtime_error("Duplicate OOT3D asset id: " + record.AssetId);
        }
        mByFamily[record.Family].push_back(&record);
        if (!record.SourceContainer.empty()) {
            mBySourceContainer[record.SourceContainer].push_back(&record);
        }
        if (record.SceneId >= 0) {
            mBySceneAndFamily[std::to_string(record.SceneId) + ":" + record.Family].push_back(&record);
        }
        if (record.Family == "scene_profile" && record.SceneId >= 0) {
            if (!mScenesById.emplace(record.SceneId, &record).second) {
                throw std::runtime_error("Duplicate OOT3D scene id: " + std::to_string(record.SceneId));
            }
        } else if (record.Family == "scene_room_source" && record.SceneId >= 0 && record.RoomIndex >= 0) {
            mRoomsBySceneAndIndex[RoomKey(record.SceneId, record.RoomIndex)].push_back(&record);
        }
    }
    for (const auto& record : mRecords) {
        for (const auto& dependency : record.Dependencies) {
            if (!dependency.starts_with("source:") && !mById.contains(dependency)) {
                throw std::runtime_error("Unresolved OOT3D asset dependency: " + dependency);
            }
            mDependents[dependency].push_back(&record);
        }
    }
}

const AssetCatalogRecord* AssetCatalog::Find(std::string_view assetId) const {
    const auto it = mById.find(std::string(assetId));
    return it == mById.end() ? nullptr : it->second;
}

const std::vector<const AssetCatalogRecord*>& AssetCatalog::FindFamily(std::string_view family) const {
    const auto it = mByFamily.find(std::string(family));
    return it == mByFamily.end() ? kEmptyRecordList : it->second;
}

const std::vector<const AssetCatalogRecord*>& AssetCatalog::FindDependents(std::string_view assetId) const {
    const auto it = mDependents.find(std::string(assetId));
    return it == mDependents.end() ? kEmptyRecordList : it->second;
}

const std::vector<const AssetCatalogRecord*>& AssetCatalog::FindSourceContainer(
    std::string_view container) const {
    const auto it = mBySourceContainer.find(std::string(container));
    return it == mBySourceContainer.end() ? kEmptyRecordList : it->second;
}

const AssetCatalogRecord* AssetCatalog::FindScene(int32_t sceneId) const {
    const auto it = mScenesById.find(sceneId);
    return it == mScenesById.end() ? nullptr : it->second;
}

const std::vector<const AssetCatalogRecord*>& AssetCatalog::FindRooms(int32_t sceneId, int32_t roomIndex) const {
    const auto it = mRoomsBySceneAndIndex.find(RoomKey(sceneId, roomIndex));
    return it == mRoomsBySceneAndIndex.end() ? kEmptyRecordList : it->second;
}

const std::vector<const AssetCatalogRecord*>& AssetCatalog::FindSceneFamily(int32_t sceneId,
                                                                           std::string_view family) const {
    const auto it = mBySceneAndFamily.find(std::to_string(sceneId) + ":" + std::string(family));
    return it == mBySceneAndFamily.end() ? kEmptyRecordList : it->second;
}

std::vector<std::string> AssetCatalog::MissingCapabilities(const AssetCatalogRecord& record,
                                                           const std::vector<std::string>& available) const {
    std::vector<std::string> missing;
    for (const auto& required : record.RequiredEngineCapabilities) {
        if (std::find(available.begin(), available.end(), required) == available.end()) {
            missing.push_back(required);
        }
    }
    return missing;
}

std::vector<std::string> AssetCatalog::MissingEngineCapabilities(const AssetCatalogRecord& record) const {
    return MissingCapabilities(record, EngineCapabilities());
}

const std::vector<AssetCatalogRecord>& AssetCatalog::Records() const {
    return mRecords;
}

bool AssetCatalog::Empty() const {
    return mRecords.empty();
}

} // namespace ThreeDsRecomp::Oot3d
