#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Oot3d {

inline constexpr std::string_view kNativeAbiCatalogResource =
    "oot3d/catalog/oot3d_native_abi_catalog.json";

struct NativeAbiFunction {
    uint32_t runtimeAddress = 0;
    uint32_t byteLength = 0;
    uint32_t sourceTranche = 0;
    std::string address;
    std::string name;
    std::string family;
    std::string confidence;
    std::string returnType;
    std::string closureKind;
    std::string source;
    std::string evidenceFile;
    std::string signatureEvidenceFile;
    std::vector<std::string> parameterTypes;
    std::vector<std::string> parameterNames;
    std::vector<std::string> nativePointerTypes;
};

struct NativeAbiSubsystemContract {
    std::string id;
    std::string status;
    std::vector<std::string> requiredFunctions;
    std::vector<std::string> availableFunctions;
    std::vector<std::string> missingFunctions;
    std::vector<uint32_t> sourceTranches;
    std::vector<std::string> evidenceFiles;
};

struct NativeAbiCatalog {
    std::string sourceSnapshotId;
    std::string sourceBaseRevision;
    std::string codeBinSha256;
    std::string payloadSha256;
    std::string ownershipPolicy;
    uint32_t nativePointerFunctionCount = 0;
    uint32_t completeSubsystemContractCount = 0;
    std::unordered_map<std::string, uint32_t> closureKindCounts;
    std::unordered_map<std::string, uint32_t> familyCounts;
    std::vector<NativeAbiFunction> functions;
    std::vector<NativeAbiSubsystemContract> subsystemContracts;

    const NativeAbiFunction* FindByName(std::string_view name) const;
    const NativeAbiFunction* FindByAddress(uint32_t runtimeAddress) const;
    const NativeAbiSubsystemContract* FindSubsystemContract(std::string_view id) const;
};

NativeAbiCatalog ParseNativeAbiCatalog(std::string_view payload);
NativeAbiCatalog LoadNativeAbiCatalogFile(const std::filesystem::path& path);

} // namespace Oot3d
