#include "three_ds_recomp/oot3d/Oot3dNativeResourceContract.h"
#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"
#include "three_ds_recomp/oot3d/Oot3dNativeFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace ThreeDsRecomp::Oot3d {
namespace {

std::string JsonString(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return "";
    }
    return object.at(key).get<std::string>();
}

bool JsonBool(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_boolean()) {
        return false;
    }
    return object.at(key).get<bool>();
}

int64_t JsonInt(const nlohmann::json& object, const char* key, int64_t defaultValue) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number_integer()) {
        return defaultValue;
    }
    return object.at(key).get<int64_t>();
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

void AddIssue(NativeResourceContractValidation& validation, std::string code, std::string resourceId,
              std::string message) {
    validation.Issues.push_back({ std::move(code), std::move(resourceId), std::move(message) });
}

void AddIssue(NativeDemoResourceSetValidation& validation, std::string code, std::string resourceId,
              std::string message) {
    validation.Issues.push_back({ std::move(code), std::move(resourceId), std::move(message) });
}

void AddIssue(NativeResourceProbeResult& result, std::string code, std::string resourceId, std::string message) {
    result.Issues.push_back({ std::move(code), std::move(resourceId), std::move(message) });
}

bool Contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

uint32_t ReadLe32(const std::array<unsigned char, 16>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::string ReadTextPrefix(const std::filesystem::path& path, size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    std::string text(limit, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(file.gcount()));
    return text;
}

bool ProbeGlbV2(const std::filesystem::path& path) {
    std::array<unsigned char, 16> bytes{};
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() < 12) {
        return false;
    }
    return bytes[0] == 'g' && bytes[1] == 'l' && bytes[2] == 'T' && bytes[3] == 'F' && ReadLe32(bytes, 4) == 2;
}

bool ProbeXmlText(const std::filesystem::path& path) {
    auto text = ReadTextPrefix(path, 1024);
    return text.find('<') != std::string::npos && text.find('>') != std::string::npos;
}

bool ProbeJsonText(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    try {
        auto parsed = nlohmann::json::parse(file);
        (void)parsed;
    } catch (...) {
        return false;
    }
    return true;
}

uintmax_t FileSizeOrZero(const std::filesystem::path& path) {
    std::error_code error;
    auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

void ProbeArtifactShape(const NativeResourceContractEntry& resource, NativeResourceProbeEntry& entry,
                        NativeResourceProbeResult& result) {
    const std::filesystem::path artifactPath(resource.RuntimeArtifact);
    const std::string_view runtimeFormat(resource.RuntimeFormat);
    const std::string_view role(resource.Role);

    if (Contains(runtimeFormat, "glb") || role == kRoleRoomVisualMesh || role == kRolePlayerModelAndAnimation) {
        entry.ArtifactKind = "glb_v2";
        if (!ProbeGlbV2(artifactPath)) {
            AddIssue(result, "resource_glb_header_invalid", resource.Id, "runtime artifact is not a GLB v2 file");
        }
        return;
    }

    if (Contains(runtimeFormat, "xml") || role == kRoleSceneCollision) {
        entry.ArtifactKind = "xml_text";
        if (!ProbeXmlText(artifactPath)) {
            AddIssue(result, "resource_xml_header_invalid", resource.Id, "runtime artifact is not readable XML text");
        }
        return;
    }

    if (Contains(runtimeFormat, "manifest") || Contains(runtimeFormat, "json") || role == kRolePlayerAssetValidation) {
        entry.ArtifactKind = "json";
        if (!ProbeJsonText(artifactPath)) {
            AddIssue(result, "resource_json_invalid", resource.Id, "runtime artifact is not readable JSON");
        }
        return;
    }

    entry.ArtifactKind = "opaque_file";
}

bool PathHasJsonExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".json";
}

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open JSON file: " + path.string());
    }
    return nlohmann::json::parse(file);
}

std::string JsonNestedString(const nlohmann::json& data, std::initializer_list<const char*> keys) {
    const nlohmann::json* cursor = &data;
    for (const auto* key : keys) {
        if (!cursor->is_object() || !cursor->contains(key)) {
            return "";
        }
        cursor = &cursor->at(key);
    }
    return cursor->is_string() ? cursor->get<std::string>() : "";
}

std::optional<std::filesystem::path> ArchivePathFromBangSource(std::string_view source) {
    const auto bang = source.find('!');
    if (bang == std::string_view::npos || bang == 0) {
        return std::nullopt;
    }
    return std::filesystem::path(std::string(source.substr(0, bang)));
}

std::vector<std::filesystem::path> CharacterArchiveCandidates(const std::filesystem::path& manifestPath,
                                                              const nlohmann::json& manifest) {
    std::vector<std::filesystem::path> archives;
    const auto nativeBindPoseSource = JsonNestedString(manifest, { "target", "native_bind_pose", "source" });
    if (auto archivePath = ArchivePathFromBangSource(nativeBindPoseSource)) {
        archives.push_back(*archivePath);
    }

    std::filesystem::path archiveRoot;
    if (!archives.empty()) {
        archiveRoot = archives.front().parent_path();
    }

    const auto appendArchive = [&](const nlohmann::json& value) {
        if (!value.is_string()) {
            return;
        }
        std::filesystem::path archivePath(value.get<std::string>());
        if (archivePath.is_relative() && !archiveRoot.empty()) {
            archivePath = archiveRoot / archivePath;
        } else if (archivePath.is_relative()) {
            archivePath = manifestPath.parent_path() / archivePath;
        }
        if (std::find(archives.begin(), archives.end(), archivePath) == archives.end()) {
            archives.push_back(archivePath);
        }
    };

    if (manifest.contains("source_archives") && manifest.at("source_archives").is_object()) {
        const auto& sourceArchives = manifest.at("source_archives");
        if (sourceArchives.contains("model_archive")) {
            appendArchive(sourceArchives.at("model_archive"));
        }
        if (sourceArchives.contains("auxiliary_archives") && sourceArchives.at("auxiliary_archives").is_array()) {
            for (const auto& archive : sourceArchives.at("auxiliary_archives")) {
                appendArchive(archive);
            }
        }
    }
    return archives;
}

std::vector<uint8_t> ExtractFirstAvailableZarFile(const std::vector<std::filesystem::path>& archives,
                                                  std::string_view fileName) {
    std::string lastError;
    for (const auto& archive : archives) {
        if (!std::filesystem::is_regular_file(archive)) {
            continue;
        }
        try {
            return ExtractZarFileBytes(archive, fileName);
        } catch (const std::exception& ex) {
            lastError = ex.what();
        }
    }
    throw std::runtime_error(lastError.empty() ? "no candidate ZAR archive contained the requested file" : lastError);
}

void ProbeSourceShape(const NativeResourceContractEntry& resource, NativeResourceProbeEntry& entry,
                      NativeResourceProbeResult& result) {
    const std::filesystem::path sourcePath(resource.SourcePath);
    const auto expectedKind = NativeFormatKindFromSourceFormat(resource.SourceFormat);
    entry.ExpectedSourceKind = std::string(NativeFormatKindName(expectedKind));

    if (!entry.SourceExists) {
        entry.DetectedSourceKind = "missing";
        entry.SourceProbeIssue = "source_file_missing";
        return;
    }

    if (expectedKind == NativeFormatKind::DerivedManifest) {
        entry.DetectedSourceKind = "derived_manifest";
        entry.SourceNativeProbeRequired = false;
        entry.SourceNativeFormatMatches = ProbeJsonText(sourcePath);
        if (!entry.SourceNativeFormatMatches) {
            entry.SourceProbeIssue = "derived_manifest_json_invalid";
            AddIssue(result, "resource_source_manifest_invalid", resource.Id,
                     "source manifest is not readable JSON");
        }
        return;
    }

    if (expectedKind == NativeFormatKind::CmbPlusCsab && PathHasJsonExtension(sourcePath)) {
        entry.DetectedSourceKind = "derived_manifest";
        entry.SourceNativeProbeRequired = false;
        entry.SourceNativeFormatMatches = false;
        entry.SourceProbeIssue = "native_cmb_csab_container_reference_pending";
        return;
    }

    if (!IsNativeBinaryFormatKind(expectedKind)) {
        entry.DetectedSourceKind = "unknown";
        entry.SourceNativeProbeRequired = false;
        entry.SourceProbeIssue = "unknown_expected_source_format";
        return;
    }

    entry.SourceNativeProbeRequired = true;
    const auto sourceProbe = ProbeOot3dNativeFormatFile(sourcePath);
    entry.DetectedSourceKind = std::string(NativeFormatKindName(sourceProbe.Kind));
    entry.SourceMagic = sourceProbe.Magic;
    entry.SourceProbeIssue = sourceProbe.Issue;
    entry.SourceNativeFormatMatches =
        sourceProbe.HeaderMatches && IsExpectedNativeFormatMatch(expectedKind, sourceProbe.Kind);
    if (!entry.SourceNativeFormatMatches) {
        AddIssue(result, "resource_source_native_format_mismatch", resource.Id,
                 "source file does not match the declared OOT3D native source format");
    }
}

void AddCmbModelCounts(NativeResourceProbeEntry& entry, const CmbModel& model) {
    entry.SourceCmbModelCount += 1;
    entry.SourceTextureCount += model.Textures.size();
    entry.SourceMaterialCount += model.Materials.size();
    entry.SourceMeshCount += model.Meshes.size();
    entry.SourceShapeCount += model.Shapes.size();
    entry.SourcePrimitiveCount += model.PrimitiveCount();
    entry.SourceTriangleCount += model.TriangleCount();
    entry.SourceVertexCount += model.VertexCount();
    entry.SourceBoneCount += model.BoneCount();
}

void ProbeSourceNativeAssetContent(const NativeResourceContractEntry& resource, NativeResourceProbeEntry& entry,
                                   NativeResourceProbeResult& result) {
    const std::filesystem::path sourcePath(resource.SourcePath);
    const bool manifestBackedCmbCsab =
        entry.ExpectedSourceKind == "cmb_plus_csab" && PathHasJsonExtension(sourcePath);
    if (!entry.SourceNativeFormatMatches && !manifestBackedCmbCsab) {
        return;
    }

    const auto detectedKind = NativeFormatKindFromSourceFormat(entry.DetectedSourceKind);
    try {
        if (manifestBackedCmbCsab) {
            entry.SourceNativeParseAttempted = true;
            const auto manifest = ReadJsonFile(sourcePath);
            const auto archives = CharacterArchiveCandidates(sourcePath, manifest);
            if (resource.SourceCmb.empty()) {
                throw std::runtime_error("resource source_cmb is required to resolve native CMB from character manifest");
            }
            const auto cmbBytes = ExtractFirstAvailableZarFile(archives, resource.SourceCmb);
            AddCmbModelCounts(entry, ParseCmbModelBytes(cmbBytes, resource.SourceCmb));
            if (!resource.SourceCsab.empty()) {
                const auto csabBytes = ExtractFirstAvailableZarFile(archives, resource.SourceCsab);
                const auto csab = ParseCsabMetadataBytes(csabBytes);
                entry.SourceCsabFrameCount = csab.FrameCount;
                entry.SourceCsabAnimatedBoneCount = csab.AnimatedBoneCount;
                entry.SourceCsabSkeletonBoneCount = csab.SkeletonBoneCount;
            }
            entry.DetectedSourceKind = "zar_cmb_csab";
            entry.SourceNativeFormatMatches = true;
            entry.SourceProbeIssue.clear();
            entry.SourceNativeParseSucceeded = true;
            return;
        }

        if (detectedKind == NativeFormatKind::Zsi) {
            entry.SourceNativeParseAttempted = true;
            const auto cmbs = ParseZsiEmbeddedCmbsFile(sourcePath);
            entry.SourceEmbeddedCmbCount = cmbs.size();
            for (const auto& cmb : cmbs) {
                AddCmbModelCounts(entry, cmb.Model);
            }
            entry.SourceNativeParseSucceeded = true;
            return;
        }

        if (detectedKind == NativeFormatKind::Cmb) {
            entry.SourceNativeParseAttempted = true;
            AddCmbModelCounts(entry, ParseCmbModelFile(sourcePath));
            entry.SourceNativeParseSucceeded = true;
            return;
        }

        if (detectedKind == NativeFormatKind::Csab) {
            entry.SourceNativeParseAttempted = true;
            const auto csab = ParseCsabMetadataFile(sourcePath);
            entry.SourceCsabFrameCount = csab.FrameCount;
            entry.SourceCsabAnimatedBoneCount = csab.AnimatedBoneCount;
            entry.SourceCsabSkeletonBoneCount = csab.SkeletonBoneCount;
            entry.SourceNativeParseSucceeded = true;
            return;
        }
    } catch (const std::exception& ex) {
        entry.SourceNativeParseSucceeded = false;
        entry.SourceProbeIssue = ex.what();
        AddIssue(result, "resource_source_native_parse_failed", resource.Id,
                 "source file matched a native OOT3D format but the engine-side parser failed");
    }
}

} // namespace

bool IsOot3dLoaderContractName(std::string_view value) {
    return StartsWith(value, kOot3dLoaderContractPrefix);
}

const NativeResourceContractEntry* FindNativeResourceById(const NativeResourceContract& contract,
                                                          std::string_view id) {
    for (const auto& resource : contract.Resources) {
        if (resource.Id == id) {
            return &resource;
        }
    }
    return nullptr;
}

std::vector<const NativeResourceContractEntry*> FindNativeResourcesByRole(const NativeResourceContract& contract,
                                                                          std::string_view role) {
    std::vector<const NativeResourceContractEntry*> matches;
    for (const auto& resource : contract.Resources) {
        if (resource.Role == role) {
            matches.push_back(&resource);
        }
    }
    return matches;
}

NativeResourceContract ParseNativeResourceContract(const nlohmann::json& data) {
    NativeResourceContract contract;
    contract.Format = JsonString(data, "format");
    contract.ManifestPath = JsonString(data, "manifest");
    contract.TargetEngine = JsonString(data, "target_engine");
    contract.RuntimeAssetPolicy = JsonString(data, "runtime_asset_policy");
    contract.N64UsagePolicy = JsonString(data, "n64_usage_policy");
    contract.RuntimeN64AssetSubstitutionAllowed = JsonBool(data, "runtime_n64_asset_substitution_allowed");
    contract.ShipwrightRuntimeReplacementAllowed = JsonBool(data, "shipwright_runtime_replacement_allowed");
    contract.DeclaredResourceCount = JsonInt(data, "resource_count", -1);

    if (!data.is_object() || !data.contains("resources") || !data.at("resources").is_array()) {
        return contract;
    }

    for (const auto& resourceJson : data.at("resources")) {
        NativeResourceContractEntry resource;
        resource.Id = JsonString(resourceJson, "id");
        resource.Role = JsonString(resourceJson, "role");
        resource.SourceFormat = JsonString(resourceJson, "source_format");
        resource.SourcePath = JsonString(resourceJson, "source_path");
        resource.SourceCmb = JsonString(resourceJson, "source_cmb");
        resource.SourceCsab = JsonString(resourceJson, "source_csab");
        resource.RuntimeArtifact = JsonString(resourceJson, "runtime_artifact");
        resource.RuntimeFormat = JsonString(resourceJson, "runtime_format");
        resource.FutureLoaderContract = JsonString(resourceJson, "future_loader_contract");
        resource.NativeOrDirectlyDerived = JsonBool(resourceJson, "native_or_directly_derived");
        resource.RuntimeN64AssetPath = JsonString(resourceJson, "runtime_n64_asset_path");
        contract.Resources.push_back(std::move(resource));
    }

    return contract;
}

NativeResourceContractValidation ValidateNativeResourceContract(const NativeResourceContract& contract) {
    NativeResourceContractValidation validation;

    if (contract.Format != kNativeResourceContractFormat) {
        AddIssue(validation, "invalid_contract_format", "", "OOT3D native resource contract format is required");
    }

    if (contract.TargetEngine != kDedicatedEngineTarget) {
        AddIssue(validation, "invalid_target_engine", "", "contract must target three_ds_recomp_runtime");
    }

    if (contract.RuntimeAssetPolicy != kNativeRuntimeAssetPolicy) {
        AddIssue(validation, "invalid_runtime_asset_policy", "",
                 "runtime assets must be OOT3D-native or directly derived offline");
    }

    if (contract.N64UsagePolicy != kN64ReferenceOnlyPolicy) {
        AddIssue(validation, "invalid_n64_usage_policy", "", "N64 assets may only be used as behavior references");
    }

    if (contract.RuntimeN64AssetSubstitutionAllowed) {
        AddIssue(validation, "runtime_n64_asset_substitution_enabled", "",
                 "runtime substitution from N64 asset paths is forbidden");
    }

    if (contract.ShipwrightRuntimeReplacementAllowed) {
        AddIssue(validation, "shipwright_runtime_replacement_enabled", "",
                 "Shipwright replacement paths are forbidden for OOT3D native assets");
    }

    if (contract.DeclaredResourceCount >= 0 &&
        contract.DeclaredResourceCount != static_cast<int64_t>(contract.Resources.size())) {
        AddIssue(validation, "resource_count_mismatch", "", "declared resource_count does not match resources");
    }

    std::unordered_set<std::string> seenResourceIds;
    for (const auto& resource : contract.Resources) {
        if (resource.Id.empty()) {
            AddIssue(validation, "resource_missing_id", "", "resource id is required");
        } else if (!seenResourceIds.insert(resource.Id).second) {
            AddIssue(validation, "resource_duplicate_id", resource.Id, "resource id must be unique");
        }

        if (!resource.NativeOrDirectlyDerived) {
            AddIssue(validation, "resource_not_native_or_directly_derived", resource.Id,
                     "resource must be marked OOT3D-native or directly offline derived");
        }

        if (!resource.RuntimeN64AssetPath.empty()) {
            AddIssue(validation, "resource_has_runtime_n64_asset_path", resource.Id,
                     "resource must not provide any runtime N64 asset path");
        }

        if (resource.SourceFormat.empty() || !StartsWith(resource.SourceFormat, "oot3d")) {
            AddIssue(validation, "resource_source_format_not_oot3d", resource.Id,
                     "resource source format must be OOT3D-native");
        }

        if (resource.RuntimeArtifact.empty()) {
            AddIssue(validation, "resource_missing_runtime_artifact", resource.Id,
                     "resource must name a runtime artifact produced by the offline OOT3D conversion");
        }

        if (!IsOot3dLoaderContractName(resource.FutureLoaderContract)) {
            AddIssue(validation, "resource_invalid_loader_contract", resource.Id,
                     "future loader contract must use the oot3d.* namespace");
        }
    }

    validation.IsValid = validation.Issues.empty();
    return validation;
}

NativeDemoResourceSetValidation BuildNativeDemoResourceSet(const NativeResourceContract& contract) {
    NativeDemoResourceSetValidation result;
    auto contractValidation = ValidateNativeResourceContract(contract);
    for (const auto& issue : contractValidation.Issues) {
        result.Issues.push_back(issue);
    }

    const std::array<std::pair<std::string_view, const NativeResourceContractEntry**>, 4> requiredRoles = {
        std::pair<std::string_view, const NativeResourceContractEntry**>{ kRoleRoomVisualMesh,
                                                                         &result.Resources.RoomVisualMesh },
        std::pair<std::string_view, const NativeResourceContractEntry**>{ kRoleSceneCollision,
                                                                         &result.Resources.SceneCollision },
        std::pair<std::string_view, const NativeResourceContractEntry**>{ kRolePlayerModelAndAnimation,
                                                                         &result.Resources.PlayerModelAndAnimation },
        std::pair<std::string_view, const NativeResourceContractEntry**>{ kRolePlayerAssetValidation,
                                                                         &result.Resources.PlayerAssetValidation },
    };

    for (const auto& [role, slot] : requiredRoles) {
        auto matches = FindNativeResourcesByRole(contract, role);
        if (matches.empty()) {
            AddIssue(result, "native_demo_resource_role_missing", std::string(role),
                     "required native demo resource role is missing");
            continue;
        }

        if (matches.size() > 1) {
            AddIssue(result, "native_demo_resource_role_ambiguous", std::string(role),
                     "required native demo resource role has multiple matches");
            continue;
        }

        *slot = matches.front();
    }

    result.IsValid = result.Issues.empty();
    return result;
}

NativeResourceProbeResult ProbeNativeDemoResourceFiles(const NativeResourceContract& contract) {
    NativeResourceProbeResult result;
    auto resourceSet = BuildNativeDemoResourceSet(contract);
    for (const auto& issue : resourceSet.Issues) {
        result.Issues.push_back(issue);
    }

    for (const auto& resource : contract.Resources) {
        NativeResourceProbeEntry entry;
        entry.Id = resource.Id;
        entry.Role = resource.Role;
        entry.SourcePath = resource.SourcePath;
        entry.RuntimeArtifact = resource.RuntimeArtifact;
        entry.RuntimeFormat = resource.RuntimeFormat;
        entry.FutureLoaderContract = resource.FutureLoaderContract;

        const std::filesystem::path sourcePath(resource.SourcePath);
        std::error_code sourceError;
        entry.SourceExists = std::filesystem::is_regular_file(sourcePath, sourceError);
        entry.SourceSize = entry.SourceExists ? FileSizeOrZero(sourcePath) : 0;
        if (!entry.SourceExists) {
            AddIssue(result, "resource_source_file_missing", resource.Id, "source file is missing");
        } else {
            ProbeSourceShape(resource, entry, result);
            ProbeSourceNativeAssetContent(resource, entry, result);
        }

        const std::filesystem::path artifactPath(resource.RuntimeArtifact);
        std::error_code artifactError;
        entry.RuntimeArtifactExists = std::filesystem::is_regular_file(artifactPath, artifactError);
        entry.RuntimeArtifactSize = entry.RuntimeArtifactExists ? FileSizeOrZero(artifactPath) : 0;
        entry.RuntimeArtifactReadable = entry.RuntimeArtifactExists && entry.RuntimeArtifactSize > 0;
        if (!entry.RuntimeArtifactExists) {
            AddIssue(result, "resource_runtime_artifact_missing", resource.Id, "runtime artifact is missing");
        } else if (entry.RuntimeArtifactSize == 0) {
            AddIssue(result, "resource_runtime_artifact_empty", resource.Id, "runtime artifact is empty");
        } else {
            ProbeArtifactShape(resource, entry, result);
        }

        result.Entries.push_back(std::move(entry));
    }

    result.IsValid = result.Issues.empty();
    return result;
}

nlohmann::json NativeResourceProbeResultToJson(const NativeResourceProbeResult& result) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : result.Entries) {
        entries.push_back({
            { "id", entry.Id },
            { "role", entry.Role },
            { "source_path", entry.SourcePath },
            { "runtime_artifact", entry.RuntimeArtifact },
            { "runtime_format", entry.RuntimeFormat },
            { "future_loader_contract", entry.FutureLoaderContract },
            { "expected_source_kind", entry.ExpectedSourceKind },
            { "detected_source_kind", entry.DetectedSourceKind },
            { "source_magic", entry.SourceMagic },
            { "source_native_probe_required", entry.SourceNativeProbeRequired },
            { "source_native_format_matches", entry.SourceNativeFormatMatches },
            { "source_native_parse_attempted", entry.SourceNativeParseAttempted },
            { "source_native_parse_succeeded", entry.SourceNativeParseSucceeded },
            { "source_embedded_cmb_count", entry.SourceEmbeddedCmbCount },
            { "source_cmb_model_count", entry.SourceCmbModelCount },
            { "source_texture_count", entry.SourceTextureCount },
            { "source_material_count", entry.SourceMaterialCount },
            { "source_mesh_count", entry.SourceMeshCount },
            { "source_shape_count", entry.SourceShapeCount },
            { "source_primitive_count", entry.SourcePrimitiveCount },
            { "source_triangle_count", entry.SourceTriangleCount },
            { "source_vertex_count", entry.SourceVertexCount },
            { "source_bone_count", entry.SourceBoneCount },
            { "source_csab_frame_count", entry.SourceCsabFrameCount },
            { "source_csab_animated_bone_count", entry.SourceCsabAnimatedBoneCount },
            { "source_csab_skeleton_bone_count", entry.SourceCsabSkeletonBoneCount },
            { "source_probe_issue", entry.SourceProbeIssue },
            { "source_exists", entry.SourceExists },
            { "source_size", entry.SourceSize },
            { "runtime_artifact_exists", entry.RuntimeArtifactExists },
            { "runtime_artifact_readable", entry.RuntimeArtifactReadable },
            { "runtime_artifact_size", entry.RuntimeArtifactSize },
            { "artifact_kind", entry.ArtifactKind },
        });
    }

    nlohmann::json issues = nlohmann::json::array();
    for (const auto& issue : result.Issues) {
        issues.push_back({
            { "code", issue.Code },
            { "resource_id", issue.ResourceId },
            { "message", issue.Message },
        });
    }

    return {
        { "status", result.IsValid ? "valid" : "invalid" },
        { "entry_count", result.Entries.size() },
        { "entries", entries },
        { "issue_count", result.Issues.size() },
        { "issues", issues },
    };
}

} // namespace ThreeDsRecomp::Oot3d
