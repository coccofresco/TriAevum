#include "oot3d_native_actor_core_contract.h"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

constexpr std::array<std::string_view, 9> kRequiredFunctions = {
    "Actor_Noop",       "Actor_Destroy",    "Actor_Delete",
    "Actor_Spawn",      "Actor_Kill",       "Actor_ChangeType",
    "Actor_InitContext", "Actor_UpdateAll", "Actor_Init",
};

uint32_t ReadU32(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) ||
        (!object.at(key).is_number_unsigned() && !object.at(key).is_number_integer())) {
        throw std::runtime_error(std::string("Actor Core contract has invalid ") + key);
    }
    const auto value = object.at(key).get<int64_t>();
    if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("Actor Core contract overflows ") + key);
    }
    return static_cast<uint32_t>(value);
}

std::string ReadNonEmptyString(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_string() ||
        object.at(key).get_ref<const std::string&>().empty()) {
        throw std::runtime_error(std::string("Actor Core contract has invalid ") + key);
    }
    return object.at(key).get<std::string>();
}

} // namespace

ActorCoreContract ActorCoreContract::LoadFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open Actor Core contract: " + path.string());
    }
    nlohmann::json document;
    stream >> document;
    if (document.value("format", "") != "oot3d_actor_core_native_contract_v1" ||
        document.value("status", "") != "ready") {
        throw std::runtime_error("unsupported or incomplete Actor Core contract");
    }

    ActorCoreContract result;
    const auto& source = document.at("source");
    result.mSnapshotId = ReadNonEmptyString(source, "snapshot_id");
    result.mSourceRevision = ReadNonEmptyString(source, "source_base_revision");
    result.mCodeBinSha256 = ReadNonEmptyString(source, "code_bin_sha256");
    if (result.mCodeBinSha256.size() != 64) {
        throw std::runtime_error("Actor Core contract has an invalid code.bin SHA-256");
    }

    const auto& structures = document.at("structures");
    result.mActorEntrySize = ReadU32(structures.at("actor_entry"), "size");
    const auto& context = structures.at("actor_context");
    result.mActorContextSize = ReadU32(context, "size");
    result.mCategoryListCount = ReadU32(context, "category_list_count");
    if (result.mActorEntrySize == 0 || result.mActorContextSize == 0 ||
        result.mCategoryListCount == 0) {
        throw std::runtime_error("Actor Core contract has empty native structure dimensions");
    }

    const auto& invariants = document.at("native_invariants");
    if (invariants.value("spawn_total_guard_operator", "") != "less_than" ||
        invariants.value("category_insertion", "") != "prepend_head" ||
        invariants.value("category_iteration", "") != "ascending_index_then_head_to_tail") {
        throw std::runtime_error("Actor Core contract has unsupported lifecycle invariants");
    }
    result.mSpawnTotalGuardValue = ReadU32(invariants, "spawn_total_guard_value");
    if (result.mSpawnTotalGuardValue == 0) {
        throw std::runtime_error("Actor Core contract has an empty spawn guard");
    }

    const auto& functions = document.at("functions");
    for (const auto name : kRequiredFunctions) {
        if (!functions.contains(std::string(name))) {
            throw std::runtime_error("Actor Core contract is missing " + std::string(name));
        }
        const auto& function = functions.at(std::string(name));
        ActorCoreFunctionContract decoded;
        decoded.Address = ReadU32(function, "address");
        decoded.Size = ReadU32(function, "size");
        decoded.Family = ReadNonEmptyString(function, "family");
        if (decoded.Address == 0 || decoded.Size == 0 ||
            function.value("confidence", "") != "high") {
            throw std::runtime_error("Actor Core function is incomplete: " + std::string(name));
        }
        result.mFunctions.emplace(name, std::move(decoded));
    }
    return result;
}

uint32_t ActorCoreContract::ActorEntrySize() const { return mActorEntrySize; }
uint32_t ActorCoreContract::ActorContextSize() const { return mActorContextSize; }
uint32_t ActorCoreContract::CategoryListCount() const { return mCategoryListCount; }
uint32_t ActorCoreContract::SpawnTotalGuardValue() const { return mSpawnTotalGuardValue; }

const ActorCoreFunctionContract& ActorCoreContract::Function(std::string_view name) const {
    const auto found = mFunctions.find(std::string(name));
    if (found == mFunctions.end()) {
        throw std::out_of_range("Actor Core function is not in the native contract");
    }
    return found->second;
}

const std::string& ActorCoreContract::SnapshotId() const { return mSnapshotId; }
const std::string& ActorCoreContract::SourceRevision() const { return mSourceRevision; }
const std::string& ActorCoreContract::CodeBinSha256() const { return mCodeBinSha256; }

} // namespace Oot3dNativeGame
