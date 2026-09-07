#include "oot3d_native_player_animation_catalog.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

void RunPlayerAnimationCatalogTests() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("oot3d_player_catalog_" + unique);
    std::filesystem::create_directories(root);
    const auto codeBin = root / "code.bin";
    const auto actorZar = root / "player.zar";
    const auto contract = root / "contract.json";
    std::ofstream(codeBin, std::ios::binary).put('\0');
    std::ofstream(actorZar, std::ios::binary).put('\0');

    const nlohmann::json document = {
        { "format", "oot3d_player_animation_group_native_contract_v1" },
        { "status", "ready" },
        { "source", { { "code_bin", codeBin.string() }, { "actor_zar", actorZar.string() } } },
        { "table", { { "runtime_address", 0x1000 }, { "group_count", 2 },
                     { "animation_type_count", 2 } } },
        { "animation_groups", {
            { { "group_index", 1 }, { "runtime_address", 0x1008 },
              { "csab_type_local_indices", { 20, 21 } },
              { "csab_members", { "anim/run_free.csab", "anim/run.csab" } } },
            { { "group_index", 0 }, { "runtime_address", 0x1000 },
              { "csab_type_local_indices", { 10, 11 } },
              { "csab_members", { "anim/wait_free.csab", "anim/wait.csab" } } },
        } },
        { "semantic_groups", {
            { { "semantic_group", "idle" }, { "oot3d_group_index", 0 },
              { "runtime_address", 0x1000 },
              { "csab_type_local_indices", { 10, 11 } },
              { "csab_members", { "anim/wait_free.csab", "anim/wait.csab" } } },
            { { "semantic_group", "run" }, { "oot3d_group_index", 1 },
              { "runtime_address", 0x1008 },
              { "csab_type_local_indices", { 20, 21 } },
              { "csab_members", { "anim/run_free.csab", "anim/run.csab" } } },
        } },
        { "direct_semantic_clips", {
            { { "semantic_clip", "landing_wait" },
              { "csab_type_local_index", 31 },
              { "csab_member", "anim/landing_wait.csab" },
              { "resolution", "unique_native_zar_member_stem" } },
        } },
    };
    std::ofstream(contract) << document.dump(2);

    const auto catalog = Oot3dNativeGame::PlayerAnimationCatalog::LoadFile(contract);
    if (catalog.GroupCount() != 2 || catalog.AnimationTypeCount() != 2 ||
        catalog.ResolveAll().size() != 4 ||
        catalog.Resolve(1, 0).CsabName != "anim/run_free.csab" ||
        catalog.ResolveSemantic("run", 1).CsabName != "anim/run.csab" ||
        catalog.ResolveDirectSemantic("landing_wait").CsabName != "anim/landing_wait.csab" ||
        catalog.ResolveDirectSemantic("landing_wait").CsabTypeLocalIndex != 31 ||
        catalog.Resolve(0, 1).CsabTypeLocalIndex != 11 ||
        catalog.ResolveAllDirectSemantic().size() != 1 ||
        catalog.ResolveAllDirectSemantic().front().Semantic != "landing_wait" ||
        !std::filesystem::equivalent(catalog.CodeBinPath(), codeBin)) {
        throw std::runtime_error("native player animation catalog did not preserve table bindings");
    }
    std::filesystem::remove_all(root);
}
