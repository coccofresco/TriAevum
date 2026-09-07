#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace ThreeDsRecomp::Oot3d {

constexpr std::string_view kNativeResourceContractFormat = "oot3d_native_resource_contract_v1";
constexpr std::string_view kDedicatedEngineTarget = "dedicated_shipwright_libultraship_fork";
constexpr std::string_view kNativeRuntimeAssetPolicy = "oot3d_native_or_directly_offline_derived_only";
constexpr std::string_view kN64ReferenceOnlyPolicy = "behavior_baseline_and_delta_reference_only";
constexpr std::string_view kOot3dLoaderContractPrefix = "oot3d.";
constexpr std::string_view kRoleRoomVisualMesh = "room_visual_mesh";
constexpr std::string_view kRoleSceneCollision = "scene_collision";
constexpr std::string_view kRolePlayerModelAndAnimation = "player_model_and_animation";
constexpr std::string_view kRolePlayerAssetValidation = "player_asset_validation";

struct NativeResourceContractEntry {
    std::string Id;
    std::string Role;
    std::string SourceFormat;
    std::string SourcePath;
    std::string SourceCmb;
    std::string SourceCsab;
    std::string RuntimeArtifact;
    std::string RuntimeFormat;
    std::string FutureLoaderContract;
    bool NativeOrDirectlyDerived = false;
    std::string RuntimeN64AssetPath;
};

struct NativeResourceContract {
    std::string Format;
    std::string ManifestPath;
    std::string TargetEngine;
    std::string RuntimeAssetPolicy;
    std::string N64UsagePolicy;
    bool RuntimeN64AssetSubstitutionAllowed = false;
    bool ShipwrightRuntimeReplacementAllowed = false;
    int64_t DeclaredResourceCount = -1;
    std::vector<NativeResourceContractEntry> Resources;
};

struct NativeResourceContractIssue {
    std::string Code;
    std::string ResourceId;
    std::string Message;
};

struct NativeResourceContractValidation {
    bool IsValid = false;
    std::vector<NativeResourceContractIssue> Issues;
};

struct NativeDemoResourceSet {
    const NativeResourceContractEntry* RoomVisualMesh = nullptr;
    const NativeResourceContractEntry* SceneCollision = nullptr;
    const NativeResourceContractEntry* PlayerModelAndAnimation = nullptr;
    const NativeResourceContractEntry* PlayerAssetValidation = nullptr;
};

struct NativeDemoResourceSetValidation {
    bool IsValid = false;
    NativeDemoResourceSet Resources;
    std::vector<NativeResourceContractIssue> Issues;
};

struct NativeResourceProbeEntry {
    std::string Id;
    std::string Role;
    std::string SourcePath;
    std::string RuntimeArtifact;
    std::string RuntimeFormat;
    std::string FutureLoaderContract;
    std::string ExpectedSourceKind;
    std::string DetectedSourceKind;
    std::string SourceMagic;
    std::string SourceProbeIssue;
    bool SourceExists = false;
    bool SourceNativeProbeRequired = false;
    bool SourceNativeFormatMatches = false;
    bool SourceNativeParseAttempted = false;
    bool SourceNativeParseSucceeded = false;
    uintmax_t SourceEmbeddedCmbCount = 0;
    uintmax_t SourceCmbModelCount = 0;
    uintmax_t SourceTextureCount = 0;
    uintmax_t SourceMaterialCount = 0;
    uintmax_t SourceMeshCount = 0;
    uintmax_t SourceShapeCount = 0;
    uintmax_t SourcePrimitiveCount = 0;
    uintmax_t SourceTriangleCount = 0;
    uintmax_t SourceVertexCount = 0;
    uintmax_t SourceBoneCount = 0;
    uint32_t SourceCsabFrameCount = 0;
    uint32_t SourceCsabAnimatedBoneCount = 0;
    uint32_t SourceCsabSkeletonBoneCount = 0;
    bool RuntimeArtifactExists = false;
    bool RuntimeArtifactReadable = false;
    uintmax_t SourceSize = 0;
    uintmax_t RuntimeArtifactSize = 0;
    std::string ArtifactKind;
};

struct NativeResourceProbeResult {
    bool IsValid = false;
    std::vector<NativeResourceProbeEntry> Entries;
    std::vector<NativeResourceContractIssue> Issues;
};

NativeResourceContract ParseNativeResourceContract(const nlohmann::json& data);
NativeResourceContractValidation ValidateNativeResourceContract(const NativeResourceContract& contract);
const NativeResourceContractEntry* FindNativeResourceById(const NativeResourceContract& contract, std::string_view id);
std::vector<const NativeResourceContractEntry*> FindNativeResourcesByRole(const NativeResourceContract& contract,
                                                                          std::string_view role);
NativeDemoResourceSetValidation BuildNativeDemoResourceSet(const NativeResourceContract& contract);
NativeResourceProbeResult ProbeNativeDemoResourceFiles(const NativeResourceContract& contract);
nlohmann::json NativeResourceProbeResultToJson(const NativeResourceProbeResult& result);
bool IsOot3dLoaderContractName(std::string_view value);

} // namespace ThreeDsRecomp::Oot3d
