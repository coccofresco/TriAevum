#include "oot3d_native_abi_catalog.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace Oot3d {
namespace {

constexpr std::string_view kFormat = "oot3d_native_abi_catalog_v1";

const nlohmann::json& RequireObject(const nlohmann::json& value, const char* key) {
    if (!value.contains(key) || !value.at(key).is_object()) {
        throw std::runtime_error(std::string("native ABI catalog lacks object: ") + key);
    }
    return value.at(key);
}

const nlohmann::json& RequireArray(const nlohmann::json& value, const char* key) {
    if (!value.contains(key) || !value.at(key).is_array()) {
        throw std::runtime_error(std::string("native ABI catalog lacks array: ") + key);
    }
    return value.at(key);
}

std::string RequireString(const nlohmann::json& value, const char* key) {
    if (!value.contains(key) || !value.at(key).is_string()) {
        throw std::runtime_error(std::string("native ABI catalog lacks string: ") + key);
    }
    return value.at(key).get<std::string>();
}

uint32_t RequireUint32(const nlohmann::json& value, const char* key) {
    if (!value.contains(key) ||
        (!value.at(key).is_number_unsigned() && !value.at(key).is_number_integer())) {
        throw std::runtime_error(std::string("native ABI catalog lacks uint32: ") + key);
    }
    const int64_t result = value.at(key).get<int64_t>();
    if (result < 0 || result > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("native ABI catalog uint32 is invalid: ") + key);
    }
    return static_cast<uint32_t>(result);
}

bool IsSha256(std::string_view value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

uint32_t ParseAddress(std::string_view value) {
    if (value.size() != 10 || value.substr(0, 2) != "0x") {
        throw std::runtime_error("native ABI address is not canonical hex");
    }
    uint32_t result = 0;
    const auto parsed = std::from_chars(value.data() + 2, value.data() + value.size(),
                                        result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        result == 0) {
        throw std::runtime_error("native ABI address is invalid");
    }
    return result;
}

std::vector<std::string> StringArray(const nlohmann::json& value, const char* key) {
    std::vector<std::string> result;
    for (const auto& entry : RequireArray(value, key)) {
        if (!entry.is_string() || entry.get<std::string>().empty()) {
            throw std::runtime_error(std::string("native ABI string array is invalid: ") + key);
        }
        result.push_back(entry.get<std::string>());
    }
    return result;
}

std::vector<uint32_t> Uint32Array(const nlohmann::json& value, const char* key) {
    std::vector<uint32_t> result;
    for (const auto& entry : RequireArray(value, key)) {
        if ((!entry.is_number_unsigned() && !entry.is_number_integer()) ||
            entry.get<int64_t>() <= 0 ||
            entry.get<int64_t>() > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(std::string("native ABI uint array is invalid: ") + key);
        }
        result.push_back(static_cast<uint32_t>(entry.get<int64_t>()));
    }
    if (!std::is_sorted(result.begin(), result.end()) ||
        std::adjacent_find(result.begin(), result.end()) != result.end()) {
        throw std::runtime_error(std::string("native ABI uint array is not canonical: ") + key);
    }
    return result;
}

std::unordered_map<std::string, uint32_t> CountMap(const nlohmann::json& value,
                                                   const char* key,
                                                   uint32_t expectedTotal) {
    std::unordered_map<std::string, uint32_t> result;
    uint64_t total = 0;
    const auto& counts = RequireObject(value, key);
    for (auto iterator = counts.begin(); iterator != counts.end(); ++iterator) {
        if (iterator.key().empty() ||
            (!iterator.value().is_number_unsigned() &&
             !iterator.value().is_number_integer())) {
            throw std::runtime_error(std::string("native ABI count map is invalid: ") + key);
        }
        const int64_t count = iterator.value().get<int64_t>();
        if (count < 0 || count > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(std::string("native ABI count is invalid: ") + key);
        }
        result.emplace(iterator.key(), static_cast<uint32_t>(count));
        total += static_cast<uint32_t>(count);
    }
    if (total != expectedTotal) {
        throw std::runtime_error(std::string("native ABI count map does not close: ") + key);
    }
    return result;
}

} // namespace

const NativeAbiFunction* NativeAbiCatalog::FindByName(std::string_view name) const {
    const auto found = std::find_if(functions.begin(), functions.end(),
                                    [name](const auto& function) {
                                        return function.name == name;
                                    });
    return found == functions.end() ? nullptr : &*found;
}

const NativeAbiFunction* NativeAbiCatalog::FindByAddress(uint32_t runtimeAddress) const {
    const auto found = std::lower_bound(
        functions.begin(), functions.end(), runtimeAddress,
        [](const auto& function, uint32_t address) {
            return function.runtimeAddress < address;
        });
    return found == functions.end() || found->runtimeAddress != runtimeAddress ? nullptr
                                                                                : &*found;
}

const NativeAbiSubsystemContract* NativeAbiCatalog::FindSubsystemContract(
    std::string_view id) const {
    const auto found = std::find_if(
        subsystemContracts.begin(), subsystemContracts.end(),
        [id](const auto& contract) { return contract.id == id; });
    return found == subsystemContracts.end() ? nullptr : &*found;
}

NativeAbiCatalog ParseNativeAbiCatalog(std::string_view payload) {
    const auto document = nlohmann::json::parse(payload);
    if (!document.is_object() || RequireString(document, "format") != kFormat ||
        RequireUint32(document, "schema_version") != 1 ||
        RequireString(document, "status") != "reviewed_native_abi_catalog_complete") {
        throw std::runtime_error("unsupported native ABI catalog");
    }

    NativeAbiCatalog catalog;
    catalog.sourceSnapshotId = RequireString(document, "source_snapshot_id");
    catalog.sourceBaseRevision = RequireString(document, "source_base_revision");
    catalog.codeBinSha256 = RequireString(document, "code_bin_sha256");
    catalog.payloadSha256 = RequireString(document, "payload_sha256");
    catalog.ownershipPolicy = RequireString(document, "ownership_policy");
    const uint32_t functionCount = RequireUint32(document, "function_count");
    catalog.nativePointerFunctionCount =
        RequireUint32(document, "native_pointer_function_count");
    const uint32_t subsystemContractCount =
        RequireUint32(document, "subsystem_contract_count");
    catalog.completeSubsystemContractCount =
        RequireUint32(document, "complete_subsystem_contract_count");
    if (catalog.sourceSnapshotId.empty() || catalog.sourceBaseRevision.size() != 40 ||
        !IsSha256(catalog.codeBinSha256) || !IsSha256(catalog.payloadSha256) ||
        catalog.ownershipPolicy.empty() || functionCount == 0 ||
        catalog.nativePointerFunctionCount > functionCount ||
        catalog.completeSubsystemContractCount > subsystemContractCount) {
        throw std::runtime_error("native ABI catalog provenance is invalid");
    }
    catalog.closureKindCounts = CountMap(document, "closure_kind_counts", functionCount);
    catalog.familyCounts = CountMap(document, "family_counts", functionCount);

    const auto& functions = RequireArray(document, "functions");
    if (functions.size() != functionCount) {
        throw std::runtime_error("native ABI catalog function count differs from payload");
    }
    std::unordered_set<uint32_t> addresses;
    std::unordered_set<std::string> names;
    uint32_t observedNativePointers = 0;
    uint32_t previousAddress = 0;
    for (const auto& functionJson : functions) {
        if (!functionJson.is_object()) {
            throw std::runtime_error("native ABI function is not an object");
        }
        NativeAbiFunction function;
        function.address = RequireString(functionJson, "address");
        function.runtimeAddress = ParseAddress(function.address);
        function.byteLength = RequireUint32(functionJson, "byte_length");
        function.sourceTranche = RequireUint32(functionJson, "source_tranche");
        function.name = RequireString(functionJson, "name");
        function.family = RequireString(functionJson, "family");
        function.confidence = RequireString(functionJson, "confidence");
        function.returnType = RequireString(functionJson, "return_type");
        function.closureKind = RequireString(functionJson, "closure_kind");
        function.source = RequireString(functionJson, "source");
        function.evidenceFile = RequireString(functionJson, "evidence_file");
        function.signatureEvidenceFile =
            RequireString(functionJson, "signature_evidence_file");
        function.parameterTypes = StringArray(functionJson, "parameter_types");
        function.parameterNames = StringArray(functionJson, "parameter_names");
        function.nativePointerTypes = StringArray(functionJson, "native_pointer_types");
        if (function.parameterNames.size() != function.parameterTypes.size() ||
            function.byteLength == 0 ||
            (function.sourceTranche == 0 &&
             function.closureKind != "reviewed_signature") ||
            !catalog.closureKindCounts.contains(function.closureKind) ||
            !catalog.familyCounts.contains(function.family) ||
            function.runtimeAddress < previousAddress ||
            !addresses.insert(function.runtimeAddress).second ||
            !names.insert(function.name).second) {
            throw std::runtime_error("native ABI function identity is invalid");
        }
        for (const auto& pointerType : function.nativePointerTypes) {
            if (std::find(function.parameterTypes.begin(), function.parameterTypes.end(),
                          pointerType) == function.parameterTypes.end()) {
                throw std::runtime_error(
                    "native ABI pointer evidence is absent from parameter types");
            }
        }
        observedNativePointers += function.nativePointerTypes.empty() ? 0u : 1u;
        previousAddress = function.runtimeAddress;
        catalog.functions.push_back(std::move(function));
    }
    if (observedNativePointers != catalog.nativePointerFunctionCount) {
        throw std::runtime_error("native ABI pointer-function count does not close");
    }

    const auto& contracts = RequireArray(document, "subsystem_contracts");
    if (contracts.size() != subsystemContractCount) {
        throw std::runtime_error("native ABI subsystem contract count differs from payload");
    }
    std::unordered_set<std::string> contractIds;
    uint32_t completeContracts = 0;
    for (const auto& contractJson : contracts) {
        NativeAbiSubsystemContract contract;
        contract.id = RequireString(contractJson, "id");
        contract.status = RequireString(contractJson, "status");
        contract.requiredFunctions = StringArray(contractJson, "required_functions");
        contract.availableFunctions = StringArray(contractJson, "available_functions");
        contract.missingFunctions = StringArray(contractJson, "missing_functions");
        contract.sourceTranches = Uint32Array(contractJson, "source_tranches");
        contract.evidenceFiles = StringArray(contractJson, "evidence_files");
        if (!contractIds.insert(contract.id).second || contract.requiredFunctions.empty()) {
            throw std::runtime_error("native ABI subsystem contract identity is invalid");
        }
        std::unordered_set<std::string> required(contract.requiredFunctions.begin(),
                                                 contract.requiredFunctions.end());
        std::unordered_set<std::string> observed;
        for (const auto& name : contract.availableFunctions) {
            if (!required.contains(name) || catalog.FindByName(name) == nullptr ||
                !observed.insert(name).second) {
                throw std::runtime_error("native ABI available subsystem function is invalid");
            }
        }
        for (const auto& name : contract.missingFunctions) {
            if (!required.contains(name) || catalog.FindByName(name) != nullptr ||
                !observed.insert(name).second) {
                throw std::runtime_error("native ABI missing subsystem function is invalid");
            }
        }
        if (observed.size() != required.size()) {
            throw std::runtime_error("native ABI subsystem function set does not close");
        }
        const bool complete = contract.missingFunctions.empty();
        if (contract.status != (complete ? "reviewed_native_contract_complete"
                                        : "reviewed_native_contract_incomplete")) {
            throw std::runtime_error("native ABI subsystem contract status is invalid");
        }
        completeContracts += complete ? 1u : 0u;
        catalog.subsystemContracts.push_back(std::move(contract));
    }
    if (completeContracts != catalog.completeSubsystemContractCount) {
        throw std::runtime_error("native ABI complete subsystem count does not close");
    }
    return catalog;
}

NativeAbiCatalog LoadNativeAbiCatalogFile(const std::filesystem::path& path) {
    std::ifstream source(path, std::ios::binary);
    if (!source) {
        throw std::runtime_error("failed to open native ABI catalog: " + path.string());
    }
    const std::string payload((std::istreambuf_iterator<char>(source)),
                              std::istreambuf_iterator<char>());
    return ParseNativeAbiCatalog(payload);
}

} // namespace Oot3d
