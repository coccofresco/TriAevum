#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace ThreeDsRecomp::Oot3d {

struct AssetCatalogRecord {
    std::string AssetId;
    std::string Family;
    std::string SourceIdentity;
    std::string SourceContainer;
    std::string SourceMember;
    std::string ModelKind;
    std::vector<std::string> CanonicalResources;
    std::vector<std::string> Dependencies;
    std::vector<std::string> RequiredEngineCapabilities;
    uint32_t SupportTier = 0;
    std::string RuntimeState;
    int32_t SceneId = -1;
    int32_t RoomIndex = -1;
    std::string SceneStem;
    std::string SceneShardManifestResource;
    std::vector<int32_t> SetupIndices;
};

class AssetCatalog {
  public:
    static AssetCatalog Parse(const nlohmann::json& document);
    static AssetCatalog LoadFile(const std::filesystem::path& path);

    const AssetCatalogRecord* Find(std::string_view assetId) const;
    const std::vector<const AssetCatalogRecord*>& FindFamily(std::string_view family) const;
    const std::vector<const AssetCatalogRecord*>& FindDependents(std::string_view assetId) const;
    const std::vector<const AssetCatalogRecord*>& FindSourceContainer(std::string_view container) const;
    const AssetCatalogRecord* FindScene(int32_t sceneId) const;
    const std::vector<const AssetCatalogRecord*>& FindRooms(int32_t sceneId, int32_t roomIndex) const;
    const std::vector<const AssetCatalogRecord*>& FindSceneFamily(int32_t sceneId, std::string_view family) const;
    std::vector<std::string> MissingCapabilities(const AssetCatalogRecord& record,
                                                 const std::vector<std::string>& available) const;
    std::vector<std::string> MissingEngineCapabilities(const AssetCatalogRecord& record) const;
    const std::vector<AssetCatalogRecord>& Records() const;
    bool Empty() const;

  private:
    void BuildIndices();

    std::vector<AssetCatalogRecord> mRecords;
    std::unordered_map<std::string, const AssetCatalogRecord*> mById;
    std::unordered_map<std::string, std::vector<const AssetCatalogRecord*>> mByFamily;
    std::unordered_map<std::string, std::vector<const AssetCatalogRecord*>> mDependents;
    std::unordered_map<std::string, std::vector<const AssetCatalogRecord*>> mBySourceContainer;
    std::unordered_map<int32_t, const AssetCatalogRecord*> mScenesById;
    std::unordered_map<int64_t, std::vector<const AssetCatalogRecord*>> mRoomsBySceneAndIndex;
    std::unordered_map<std::string, std::vector<const AssetCatalogRecord*>> mBySceneAndFamily;
};

} // namespace ThreeDsRecomp::Oot3d
