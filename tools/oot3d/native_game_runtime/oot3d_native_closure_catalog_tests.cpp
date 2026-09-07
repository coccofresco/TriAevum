#include "oot3d_native_closure_catalog.h"

#include "oot3d_a32_provenance.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path WriteFixture(
    const char* functions,
    std::string_view revision = oot3d::recomp::kA32SourceRevision) {
    const auto path = std::filesystem::temp_directory_path() /
                      "oot3d_native_closure_catalog_test.json";
    std::ofstream stream(path);
    stream << R"({"format":"oot3d_native_corpus_promotion_v1","payload_sha256":"corpus","source":{"revision":")"
           << revision
           << R"("},"room_compilation_unit":{"unit_id":"unit","payload_sha256":"room"},"counts":{"extracted_bodies":1,"functions_without_listed_hazards":1},"functions":)"
           << functions << '}';
    return path;
}

} // namespace

int main() {
    try {
        const auto path = WriteFixture(
            R"([{"address":"0x1234","name":"Actor_Init","module":"game/actors","body_status":"extracted","compile_readiness":"requires_generated_layout_and_service_declarations","hazards":[]}])");
        const auto catalog = Oot3dNativeGame::NativeClosureCatalog::LoadFile(path);
        const auto* function = catalog.Find(0x1234);
        Require(function != nullptr && function->Name == "Actor_Init",
                "catalog did not resolve address");
        Require(catalog.FunctionCount() == 1 && catalog.ExtractedBodyCount() == 1,
                "catalog counts are incorrect");
        Require(catalog.RoomCompilationUnitId() == "unit" &&
                    catalog.RoomCompilationPayloadSha256() == "room",
                "catalog provenance is incorrect");
        std::filesystem::remove(path);

        const auto stalePath = WriteFixture("[]", "stale-revision");
        bool staleRejected = false;
        try {
            (void)Oot3dNativeGame::NativeClosureCatalog::LoadFile(stalePath);
        } catch (const std::runtime_error&) {
            staleRejected = true;
        }
        std::filesystem::remove(stalePath);
        Require(staleRejected, "catalog accepted a stale A32 source revision");

        const auto duplicatePath = WriteFixture(
            R"([{"address":"0x1234","name":"A"},{"address":"0x1234","name":"B"}])");
        bool rejected = false;
        try {
            (void)Oot3dNativeGame::NativeClosureCatalog::LoadFile(duplicatePath);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        std::filesystem::remove(duplicatePath);
        Require(rejected, "catalog accepted duplicate addresses");
        std::cout << "oot3d native closure catalog tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
