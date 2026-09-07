#include "oot3d_native_actor_core_contract.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

nlohmann::json ContractDocument(uint32_t spawnGuard = 201) {
    nlohmann::json functions = nlohmann::json::object();
    uint32_t address = 0x1000;
    for (const char* name : {
             "Actor_Noop", "Actor_Destroy", "Actor_Delete", "Actor_Spawn",
             "Actor_Kill", "Actor_ChangeType", "Actor_InitContext",
             "Actor_UpdateAll", "Actor_Init",
         }) {
        functions[name] = {
            { "address", address }, { "size", 4 }, { "family", "test" },
            { "confidence", "high" },
        };
        address += 4;
    }
    return {
        { "format", "oot3d_actor_core_native_contract_v1" },
        { "status", "ready" },
        { "source", {
            { "snapshot_id", "test-snapshot" },
            { "source_base_revision", "test-revision" },
            { "code_bin_sha256", std::string(64, 'a') },
        } },
        { "structures", {
            { "actor_entry", { { "size", 0x10 } } },
            { "actor_context", { { "size", 0x20C }, { "category_list_count", 12 } } },
        } },
        { "native_invariants", {
            { "spawn_total_guard_operator", "less_than" },
            { "spawn_total_guard_value", spawnGuard },
            { "category_insertion", "prepend_head" },
            { "category_iteration", "ascending_index_then_head_to_tail" },
        } },
        { "functions", std::move(functions) },
    };
}

std::filesystem::path WriteContract(const nlohmann::json& document, const char* name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << document.dump(2);
    return path;
}

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        const auto path = WriteContract(ContractDocument(), "oot3d_actor_core_contract_test.json");
        const auto contract = Oot3dNativeGame::ActorCoreContract::LoadFile(path);
        Expect(contract.ActorEntrySize() == 0x10, "actor entry size");
        Expect(contract.ActorContextSize() == 0x20C, "actor context size");
        Expect(contract.CategoryListCount() == 12, "category count");
        Expect(contract.SpawnTotalGuardValue() == 201, "spawn guard");
        Expect(contract.Function("Actor_UpdateAll").Address != 0, "update address");

        auto invalid = ContractDocument();
        invalid["native_invariants"]["category_insertion"] = "append_tail";
        const auto invalidPath = WriteContract(invalid, "oot3d_actor_core_contract_invalid_test.json");
        bool rejected = false;
        try {
            (void)Oot3dNativeGame::ActorCoreContract::LoadFile(invalidPath);
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "unsupported insertion must be rejected");
        std::cout << "oot3d_native_actor_core_contract_tests: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_actor_core_contract_tests: " << ex.what() << '\n';
        return 1;
    }
}
