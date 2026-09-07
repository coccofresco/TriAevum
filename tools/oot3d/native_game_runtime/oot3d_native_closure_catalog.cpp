#include "oot3d_native_closure_catalog.h"

#include "oot3d_a32_provenance.h"
#include "recomp/a32_runtime.h"

#ifdef OOT3D_NATIVE_A32_AOT_AVAILABLE
#include "oot3d_a32_generated.h"
#endif

#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

uint32_t ParseAddress(const nlohmann::json& value) {
    if (!value.is_string()) {
        throw std::runtime_error("native closure address is not a string");
    }
    const auto text = value.get<std::string>();
    size_t parsed = 0;
    const auto address = std::stoull(text, &parsed, 0);
    if (parsed != text.size() || address == 0 ||
        address > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("native closure address is invalid: " + text);
    }
    return static_cast<uint32_t>(address);
}

} // namespace

NativeClosureCatalog NativeClosureCatalog::LoadFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open native closure manifest: " + path.string());
    }
    nlohmann::json document;
    stream >> document;
    if (document.value("format", "") != "oot3d_native_corpus_promotion_v1") {
        throw std::runtime_error("unsupported native closure manifest format");
    }
    if (!document.contains("functions") || !document.at("functions").is_array() ||
        !document.contains("source") || !document.at("source").is_object() ||
        !document.contains("room_compilation_unit") ||
        !document.at("room_compilation_unit").is_object()) {
        throw std::runtime_error("native closure manifest is incomplete");
    }

    NativeClosureCatalog result;
    result.mPayloadSha256 = document.value("payload_sha256", "");
    result.mSourceRevision = document.at("source").value("revision", "");
    result.mRoomCompilationUnitId =
        document.at("room_compilation_unit").value("unit_id", "");
    result.mRoomCompilationPayloadSha256 =
        document.at("room_compilation_unit").value("payload_sha256", "");
    const auto counts = document.value("counts", nlohmann::json::object());
    result.mExtractedBodyCount = counts.value("extracted_bodies", 0u);
    result.mBodyWithoutListedHazardsCount =
        counts.value("functions_without_listed_hazards", 0u);
    if (result.mPayloadSha256.empty() || result.mSourceRevision.empty() ||
        result.mRoomCompilationUnitId.empty() ||
        result.mRoomCompilationPayloadSha256.empty()) {
        throw std::runtime_error("native closure manifest has incomplete provenance");
    }
    if (result.mSourceRevision != oot3d::recomp::kA32SourceRevision) {
        throw std::runtime_error(
            "native closure revision differs from the pinned A32 runtime: " +
            result.mSourceRevision);
    }

    std::set<std::string> names;
    for (const auto& row : document.at("functions")) {
        NativeClosureFunction function;
        function.Address = ParseAddress(row.at("address"));
        function.Name = row.value("name", "");
        function.Module = row.value("module", "");
        function.BodyStatus = row.value("body_status", "");
        function.CompileReadiness = row.value("compile_readiness", "");
        function.Hazards = row.value("hazards", std::vector<std::string>{});
#ifdef OOT3D_NATIVE_A32_AOT_AVAILABLE
        const auto* aotFunction = oot3d::recomp::a32::FindFunction(
            oot3d::recomp::GetA32GeneratedRegistry(), function.Address);
        function.AotBound =
            aotFunction != nullptr && aotFunction->entry == function.Address;
        result.mAotBoundFunctionCount += function.AotBound ? 1U : 0U;
#endif
        if (function.Name.empty() || !names.insert(function.Name).second ||
            result.mFunctions.contains(function.Address)) {
            throw std::runtime_error("native closure manifest has duplicate function identity");
        }
        result.mFunctions.emplace(function.Address, std::move(function));
    }
    if (result.mFunctions.empty()) {
        throw std::runtime_error("native closure manifest has no functions");
    }
    return result;
}

const NativeClosureFunction* NativeClosureCatalog::Find(uint32_t address) const {
    const auto found = mFunctions.find(address);
    return found == mFunctions.end() ? nullptr : &found->second;
}

const std::string& NativeClosureCatalog::SourceRevision() const { return mSourceRevision; }
const std::string& NativeClosureCatalog::PayloadSha256() const { return mPayloadSha256; }
const std::string& NativeClosureCatalog::RoomCompilationUnitId() const {
    return mRoomCompilationUnitId;
}
const std::string& NativeClosureCatalog::RoomCompilationPayloadSha256() const {
    return mRoomCompilationPayloadSha256;
}
size_t NativeClosureCatalog::FunctionCount() const { return mFunctions.size(); }
size_t NativeClosureCatalog::ExtractedBodyCount() const { return mExtractedBodyCount; }
size_t NativeClosureCatalog::BodyWithoutListedHazardsCount() const {
    return mBodyWithoutListedHazardsCount;
}
size_t NativeClosureCatalog::AotBoundFunctionCount() const {
    return mAotBoundFunctionCount;
}
bool NativeClosureCatalog::AotAvailable() const {
#ifdef OOT3D_NATIVE_A32_AOT_AVAILABLE
    return true;
#else
    return false;
#endif
}

nlohmann::json NativeClosureCatalog::Diagnostics() const {
    return {
        {"available", true},
        {"source_revision", mSourceRevision},
        {"payload_sha256", mPayloadSha256},
        {"room_compilation_unit_id", mRoomCompilationUnitId},
        {"room_compilation_payload_sha256", mRoomCompilationPayloadSha256},
        {"function_count", mFunctions.size()},
        {"extracted_body_count", mExtractedBodyCount},
        {"body_without_listed_hazards_count", mBodyWithoutListedHazardsCount},
        {"a32_source_revision", oot3d::recomp::kA32SourceRevision},
        {"a32_source_snapshot_id", oot3d::recomp::kA32SourceSnapshotId},
        {"aot_available", AotAvailable()},
        {"aot_bound_function_count", mAotBoundFunctionCount},
    };
}

} // namespace Oot3dNativeGame
