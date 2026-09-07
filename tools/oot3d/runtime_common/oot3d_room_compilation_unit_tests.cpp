#include "oot3d_native_abi_catalog.h"
#include "oot3d_room_compilation_unit.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

nlohmann::json Callback(std::string_view slot) {
    return {
        {"slot", slot},
        {"name", nullptr},
        {"address", "0x00000000"},
        {"evidence_status", "not_present"},
        {"runtime_binding_status", "not_required"},
    };
}

nlohmann::json LifecycleContract() {
    nlohmann::json functions = nlohmann::json::object();
    uint32_t address = 0x2000;
    for (const char* role : {"initialize", "request", "process_request", "destroy",
                             "cleanup_room_actors", "spawn_transition_actors",
                             "queue_resource_cleanup", "process_resource_cleanup"}) {
        functions[role] = {
            {"address", std::to_string(address)},
            {"name", role},
            {"return_type", "void"},
            {"param_types", ""},
            {"confidence", "high"},
            {"source", "fixture"},
            {"notes", "fixture"},
        };
        ++address;
    }
    return {
        {"format", "oot3d_room_lifecycle_contract_v1"},
        {"status", "workflow_semantics_recovered"},
        {"evidence_source", "immutable_zelda3drecomp_snapshot"},
        {"source_snapshot_id", "fixture"},
        {"functions", std::move(functions)},
        {"state_model",
         {
             {"idle", 0},
             {"loading", 1},
             {"terminal", 2},
             {"max_resident_room_count", 2},
             {"initial_buffer_count", 1},
             {"buffer_count_without_transition_actors", 1},
             {"buffer_count_with_transition_actors", 3},
         }},
        {"actor_residency",
         {
             {"room_actor_retention_policy", "negative_room_or_current_or_previous"},
             {"transition_actor_residency_policy",
              "front_or_back_matches_current_or_previous"},
             {"transition_actor_id_spawn_mask", 0x1FFF},
             {"transition_actor_params_index_stride", 0x400},
             {"transition_actor_spawn_marker", "negate_source_actor_id"},
         }},
        {"resource_residency",
         {
             {"previous_room_cleanup_before_next_request", true},
             {"cleanup_delay_ticks", 1},
             {"room_commands_install_on_request_completion", true},
         }},
        {"structure_evidence", nlohmann::json::array()},
    };
}

nlohmann::json RoomCallbackContract() {
    return {
        {"config_index", 4},
        {"config_name", "Fixture"},
        {"callbacks",
         {
             {"init",
              {{"address", "0x00003000"},
               {"name", "Fixture_Init"},
               {"evidence_status", "callback_identity_recovered"}}},
             {"cleanup",
              {{"address", "0x00003004"},
               {"name", "Fixture_Cleanup"},
               {"evidence_status", "callback_identity_recovered"}}},
             {"prepare_draw",
              {{"address", "0x00003008"},
               {"name", "Fixture_PrepareDraw"},
               {"evidence_status", "callback_identity_recovered"}}},
         }},
        {"runtime_contract",
         {
             {"format", "oot3d_room_scene_callback_native_runtime_v1"},
             {"status", "native_typed_operations_ready"},
             {"callback_address", "0x00003008"},
             {"callback_name", "Fixture_PrepareDraw"},
             {"role", "prepare_draw"},
             {"function_size", 32},
             {"function_sha256", std::string(64, 'c')},
             {"literal_verification_count", 4},
             {"runtime_binding_status", "native_typed_operations_ready"},
             {"source_registry", "fixture.json"},
             {"operations",
              {{
                  {"kind", "material_tev_constant_alpha"},
                  {"native_operation", 2},
                  {"target",
                   {{"room_index", 0},
                    {"resource_index", 0},
                    {"material_indices", {2, 3}},
                    {"constant_index", 0}}},
                  {"color_rgb_f32_bits",
                   {"0x3F800000", "0x3F800000", "0x3F800000"}},
                  {"alpha",
                   {{"base_f32_bits", "0x3F800000"},
                    {"selector_scale_f32_bits", "0x3A1DEB07"},
                    {"selector",
                     {{"default_value", 0},
                      {"excluded_setup_index", 4},
                      {"draw_param_setup_index", 6},
                      {"draw_param_index", 0},
                      {"progression_value", 1660},
                      {"setup_less_than", 4},
                      {"alternate_fact", {{"key", "player.age"}, {"equals", "adult"}}},
                      {"required_fact",
                       {{"key", "quest.kokiri_emerald"}, {"equals", "true"}}}}},
                    {"random",
                     {{"function", "Rand_S16Offset"},
                      {"function_address", "0x00368B68"},
                      {"function_size", 48},
                      {"function_sha256", std::string(64, 'c')},
                      {"state_address", "0x0050C0C4"},
                      {"offset", 0},
                      {"range", 100},
                      {"lcg_initial_state", 1},
                      {"lcg_multiplier", "0x0019660D"},
                      {"lcg_increment", "0x3C6EF35F"},
                      {"scale_f32_bits", "0x3B03126F"},
                      {"middle_test_add_u32", "0xC2333332"},
                      {"middle_test_less_than_u32", "0x01A66665"},
                      {"low_compare_f32_bits", "0x3DCCCCCD"}}}}},
              }}},
         }},
    };
}

nlohmann::json Unit() {
    const std::string digest(64, 'a');
    return {
        {"format", "oot3d_room_compilation_unit_v1"},
        {"schema_version", 1},
        {"status", "composition_complete_behavior_incomplete"},
        {"identity",
         {
             {"route_id", "scene_entry:test"},
             {"unit_id", "scene_entry:test:setup-0"},
             {"scene_id", 85},
             {"scene_path", "scene/test_info.zsi"},
             {"setup_index", 0},
             {"native_room_indices", {0}},
             {"initial_room_index", 0},
             {"payload_sha256", digest},
         }},
        {"source_assets",
         {
             {"code_bin",
              {
                  {"logical_path", "exefs/code.bin"},
                  {"sha256", std::string(64, 'b')},
              }},
         }},
        {"room_lifecycle_contract", LifecycleContract()},
        {"room_callback_contract", RoomCallbackContract()},
        {"scene_setup",
         {
             {"commands",
              {{{"command_id", 0x15},
                {"parameter", 1},
                {"command_word", 0x00040115},
                {"argument", 0x010005A9}},
               {{"command_id", 0x14},
                {"parameter", 0},
                {"command_word", 0x14},
                {"argument", 0}}}},
         }},
        {"entrypoint",
         {
             {"global_entrance_index", 529},
             {"local_entrance_index", 3},
             {"spawn_index", 3},
             {"room_index", 0},
             {"entry",
              {
                  {"actor_id", 0},
                  {"actor_name", "ACTOR_PLAYER"},
                  {"index", 3},
                  {"offset", 128},
                  {"params", 0x0DFF},
                  {"pos", {-31, 100, 1073}},
                  {"rot", {0, -32767, 0}},
              }},
         }},
        {"rooms",
         {{
             {"room_index", 0},
             {"setup_index", 0},
             {"initially_active", true},
             {"source", {{"logical_path", "scene/test_0_info.zsi"}}},
             {"commands",
              {{{"command_id", 0x14},
                {"parameter", 0},
                {"command_word", 0x14},
                {"argument", 0}}}},
             {"mesh_resources", {{{"name", "test_00"}}}},
             {"object_list",
              {
                  {"entries", {{{"object_id", 1}}}},
                  {"unknown_entries", nlohmann::json::array()},
              }},
         }}},
        {"actor_instances",
         {{
             {"instance_key", "player-entry"},
             {"profile_key", "actor:0x0000"},
             {"source_kind", "scene_spawn"},
             {"room_index", 0},
             {"entry",
              {
                  {"actor_id", 0},
                  {"actor_name", "ACTOR_PLAYER"},
                  {"index", 3},
                  {"offset", 128},
                  {"params", 0x0DFF},
                  {"pos", {-31, 100, 1073}},
                  {"rot", {0, -32767, 0}},
              }},
         }}},
        {"actor_profiles",
         {{
             {"profile_key", "actor:0x0000"},
             {"actor_id", 0},
             {"actor_name", "ACTOR_PLAYER"},
             {"object_id", 1},
             {"object_rom_path", "actor/zelda_keep.zar"},
             {"profile_address", "0x00001000"},
             {"overlay_entry_address", "0x00000000"},
             {"overlay_table_address", "0x00000000"},
             {"actor_init_evidence_status", "exact_snapshot_record"},
             {"runtime_binding_status", "consumer_must_bind_native_profile"},
             {"category", 2},
             {"flags", 0},
             {"instance_size", 256},
             {"behavior_graph",
              {
                  {"status", "workflow_graph_recovered"},
                  {"owner", "Player"},
                  {"structure", "Oot3dPlayer"},
                  {"structure_size", 256},
                  {"function_count", 2},
                  {"action_transition_count", 0},
                   {"structure_field_count", 1},
                   {"indirect_root_count", 1},
                   {"native_abi_function_count", 1},
                   {"consumer_status", "original_lifecycle_consumers_recovered"},
                   {"consumer_root_count", 1},
                   {"consumer_function_count", 2},
                   {"consumer_call_edge_count", 1},
                   {"actor_local_consumer_function_count", 0},
                   {"native_service_dependency_count", 1},
                  {"initial_action_candidates", {"Player_Standing"}},
                  {"functions",
                   {{{"address", "0x00004000"},
                     {"name", "Player_Standing"},
                     {"family", "player_action"},
                     {"byte_length", 32},
                     {"confidence", "high"},
                     {"return_type", "void"},
                     {"param_types", "Oot3dPlayer*;Oot3dPlayState*"},
                     {"param_names", "this, play"},
                     {"source", "fixture"},
                     {"evidence_file", "fixture.csv"},
                     {"signature_evidence_file", "fixture_signatures.csv"},
                      {"discovery_kind", "zero_direct_caller_native_root"},
                      {"closure_kind", "indirect_root"},
                     {"owner_resolution",
                      "exact_concrete_instance_pointer_in_native_abi"},
                     {"source_tranche", 152},
                     {"consumer_role", "native_lifecycle_consumer"},
                     {"consumer_depth", 0},
                     {"body_status", "reviewed_original_callback_body"}},
                    {{"address", "0x00004100"},
                     {"name", "Actor_MoveForward"},
                     {"family", "engine_actors"},
                     {"byte_length", 64},
                     {"confidence", "high"},
                     {"return_type", "void"},
                     {"param_types", "Oot3dActor*"},
                     {"param_names", "actor"},
                     {"source", "fixture"},
                     {"evidence_file", "fixture.csv"},
                     {"consumer_role", "native_service_dependency"},
                     {"consumer_depth", 1},
                     {"body_status", "reviewed_original_function_body"}}}},
                  {"action_transitions", nlohmann::json::array()},
                  {"structure_fields",
                   {{{"offset", 0},
                     {"type", "Actor"},
                     {"name", "actor"},
                     {"confidence", "high"},
                     {"source", "fixture"},
                     {"evidence_file", "fixture.csv"}}}},
                  {"consumer_roots",
                   {{{"slot", "update"},
                     {"address", "0x00004000"},
                     {"name", "Player_Standing"},
                     {"body_status", "reviewed_original_callback_body"}}}},
                  {"consumer_call_edges",
                   {{{"caller_address", "0x00004000"},
                     {"caller_name", "Player_Standing"},
                     {"callee_address", "0x00004100"},
                     {"callee_name", "Actor_MoveForward"},
                     {"relation", "direct_arm_call"}}}},
                  {"unresolved_consumer_dependencies", nlohmann::json::array()},
                  {"consumer_runtime_binding_status",
                   "consumer_must_lower_original_bodies_and_bind_native_services"},
                  {"runtime_binding_status",
                   "consumer_must_bind_native_behavior_graph"},
              }},
             {"callback_evidence_counts", nlohmann::json::object()},
             {"callbacks",
              {
                  {"init", Callback("init")},
                  {"destroy", Callback("destroy")},
                  {"update", Callback("update")},
                  {"draw", Callback("draw")},
              }},
         }}},
        {"indirect_native_root_catalog",
         {
             {"format", "oot3d_indirect_native_root_catalog_v1"},
             {"status", "reviewed_native_roots_indexed"},
             {"source_snapshot_id", "fixture"},
             {"root_count", 10},
             {"actor_candidate_count", 8},
             {"profile_bound_root_count", 1},
             {"unbound_actor_candidate_count", 7},
             {"family_counts", {{"actor_state", 7}, {"player_action", 1},
                                {"runtime_state", 2}}},
             {"source_catalogs", {"fixture.csv", "fixture_signatures.csv"}},
             {"ownership_policy", "fixture exact native ABI policy"},
             {"runtime_binding_status",
              "profile_bound_roots_require_consumer_binding"},
         }},
        {"native_abi_catalog",
         {
             {"format", "oot3d_native_abi_catalog_v1"},
             {"status", "reviewed_native_abi_catalog_complete"},
             {"source_snapshot_id", "fixture"},
             {"source_base_revision", std::string(40, '1')},
             {"code_bin_sha256", std::string(64, 'b')},
             {"payload_sha256", std::string(64, 'd')},
             {"function_count", 10},
             {"native_pointer_function_count", 8},
             {"profile_bound_function_count", 1},
             {"closure_kind_counts", {{"indirect_root", 10}}},
             {"ownership_policy", "fixture exact native ABI policy"},
             {"runtime_binding_status",
              "catalog_mounted_once_profile_bindings_in_rcu"},
         }},
        {"object_dependencies",
         {{
             {"object_id", 1},
             {"payload_status", "native_zar_materialized"},
             {"native_path_record", {{"logical_path", "actor/zelda_keep.zar"}}},
             {"required_by", {"actor:0x0000"}},
         }}},
        {"closure",
         {
             {"composition_status", "composition_complete"},
             {"behavior_evidence_status", "behavior_evidence_incomplete"},
             {"runtime_binding_status", "consumer_binding_required"},
             {"room_count", 1},
             {"actor_instance_count", 1},
             {"unique_actor_profile_count", 1},
             {"object_dependency_count", 1},
             {"behavior_graph_profile_count", 1},
             {"behavior_function_count", 2},
             {"behavior_action_transition_count", 0},
             {"behavior_structure_field_count", 1},
             {"behavior_indirect_root_count", 1},
             {"behavior_native_abi_function_count", 1},
             {"behavior_consumer_root_count", 1},
             {"behavior_consumer_function_count", 2},
             {"behavior_consumer_call_edge_count", 1},
             {"behavior_actor_local_consumer_function_count", 0},
             {"behavior_native_service_dependency_count", 1},
             {"unresolved_count", 0},
         }},
        {"unresolved", nlohmann::json::array()},
    };
}

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const nlohmann::json nativeAbiDocument = {
            {"format", "oot3d_native_abi_catalog_v1"},
            {"schema_version", 1},
            {"status", "reviewed_native_abi_catalog_complete"},
            {"source_snapshot_id", "fixture"},
            {"source_base_revision", std::string(40, '1')},
            {"code_bin_sha256", std::string(64, 'b')},
            {"payload_sha256", std::string(64, 'c')},
            {"ownership_policy", "fixture exact ownership"},
            {"function_count", 1},
            {"native_pointer_function_count", 1},
            {"subsystem_contract_count", 1},
            {"complete_subsystem_contract_count", 1},
            {"closure_kind_counts", {{"maintained_abi", 1}}},
            {"family_counts", {{"actor_state", 1}}},
            {"subsystem_contracts",
             {{{"id", "kokiri_limb_render"},
               {"status", "reviewed_native_contract_complete"},
               {"required_functions", {"EnKo_OverrideLimbDraw"}},
               {"available_functions", {"EnKo_OverrideLimbDraw"}},
               {"missing_functions", nlohmann::json::array()},
               {"source_tranches", {156}},
               {"evidence_files", {"fixture.csv"}}}}},
            {"functions",
             {{{"address", "0x002335B4"},
               {"byte_length", 96},
               {"source_tranche", 156},
               {"name", "EnKo_OverrideLimbDraw"},
               {"family", "actor_state"},
               {"confidence", "high"},
               {"return_type", "s32"},
               {"closure_kind", "maintained_abi"},
               {"source", "fixture.md"},
               {"evidence_file", "fixture.csv"},
               {"signature_evidence_file", "fixture_signatures.csv"},
               {"parameter_types",
                {"Oot3dPlayState*", "s32", "Oot3dMtx3x4*", "void*"}},
               {"parameter_names", {"play", "limb", "mtx", "state"}},
               {"native_pointer_types", {"Oot3dMtx3x4*", "Oot3dPlayState*"}}}}},
        };
        const auto nativeAbi = Oot3d::ParseNativeAbiCatalog(nativeAbiDocument.dump());
        Expect(nativeAbi.FindByName("EnKo_OverrideLimbDraw") != nullptr,
               "native ABI lookup by name");
        Expect(nativeAbi.FindByAddress(0x002335B4) != nullptr,
               "native ABI lookup by address");
        Expect(nativeAbi.FindSubsystemContract("kokiri_limb_render") != nullptr,
               "native ABI subsystem contract lookup");

        const auto document = Unit();
        const auto unit = Oot3d::ParseRoomCompilationUnit(document.dump());
        Expect(unit.rooms.size() == 1 && unit.rooms.front().meshNames.size() == 1,
               "parse room mesh list");
        Expect(unit.actorProfiles.front().profileRuntimeAddress == 0x1000,
               "parse native profile address");
        Expect(unit.actorProfiles.front().behaviorGraph.available &&
                   unit.actorProfiles.front().behaviorGraph.structureSize == 256 &&
                   unit.actorProfiles.front().behaviorGraph.FindFunction(
                       "Player_Standing") != nullptr &&
                   unit.actorProfiles.front().behaviorGraph.indirectRootCount == 1 &&
                   unit.actorProfiles.front().behaviorGraph.consumerRoots.size() == 1 &&
                   unit.actorProfiles.front().behaviorGraph.consumerCallEdges.size() == 1 &&
                   unit.actorProfiles.front().behaviorGraph.consumerFunctionCount == 2 &&
                   unit.behaviorGraphProfileCount == 1 &&
                   unit.behaviorFunctionCount == 2 &&
                    unit.behaviorStructureFieldCount == 1 &&
                    unit.behaviorIndirectRootCount == 1 &&
                    unit.behaviorNativeAbiFunctionCount == 1 &&
                    unit.behaviorConsumerRootCount == 1 &&
                    unit.behaviorConsumerFunctionCount == 2 &&
                    unit.behaviorConsumerCallEdgeCount == 1 &&
                    unit.behaviorNativeServiceDependencyCount == 1 &&
                    unit.indirectRootCatalog.available &&
                    unit.indirectRootCatalog.rootCount == 10 &&
                    unit.indirectRootCatalog.profileBoundRootCount == 1 &&
                    unit.nativeAbiCatalog.available &&
                    unit.nativeAbiCatalog.profileBoundFunctionCount == 1,
               "parse native actor behavior graph");
        Expect(unit.roomLifecycle.available && unit.roomLifecycle.maxResidentRoomCount == 2 &&
                   unit.roomLifecycle.FindFunction("request") != nullptr,
               "parse native room lifecycle contract");
        Expect(unit.roomCallbacks.executable &&
                   unit.roomCallbacks.materialTevAlphaOperations.size() == 1 &&
                   unit.roomCallbacks.materialTevAlphaOperations.front().materialIndices ==
                       std::vector<int32_t>({2, 3}),
               "parse native room prepare-draw operation");
        Expect(unit.rooms.front().setupIndex == 0 &&
                   unit.rooms.front().commands.size() == 1 &&
                   unit.rooms.front().commands.front().commandId == 0x14,
               "parse native room setup command stream");
        Expect(unit.sceneCommands.size() == 2 && unit.sceneAudio.available &&
                   unit.sceneAudio.soundSpecId == 1 &&
                   unit.sceneAudio.natureAmbienceId == 4 &&
                   unit.sceneAudio.bgmSoundId == 0x010005A9,
               "parse native scene audio command");

        for (const char* closureKind : { "callable_identity", "actor_private_native",
                                         "actor_priority_native" }) {
            auto typedClosureDocument = Unit();
            auto& graph = typedClosureDocument["actor_profiles"][0]["behavior_graph"];
            auto& function = graph["functions"][0];
            function["closure_kind"] = closureKind;
            function.erase("discovery_kind");
            graph["indirect_root_count"] = 0;

            auto& indirectCatalog = typedClosureDocument["indirect_native_root_catalog"];
            indirectCatalog["root_count"] = 9;
            indirectCatalog["actor_candidate_count"] = 7;
            indirectCatalog["profile_bound_root_count"] = 0;
            indirectCatalog["unbound_actor_candidate_count"] = 7;
            indirectCatalog["family_counts"] = {
                { "actor_state", 7 }, { "runtime_state", 2 }
            };

            auto& nativeCatalog = typedClosureDocument["native_abi_catalog"];
            nativeCatalog["closure_kind_counts"] = {
                { "indirect_root", 9 }, { closureKind, 1 }
            };
            typedClosureDocument["closure"]["behavior_indirect_root_count"] = 0;

            const auto typedClosureUnit =
                Oot3d::ParseRoomCompilationUnit(typedClosureDocument.dump());
            const auto* typedFunction = typedClosureUnit.actorProfiles.front()
                                                .behaviorGraph.FindFunction("Player_Standing");
            Expect(typedFunction != nullptr && typedFunction->closureKind == closureKind,
                   "parse refined native actor closure kind");
        }

        const nlohmann::json catalog = {
            {"format", "oot3d_room_compilation_unit_catalog_v1"},
            {"schema_version", 1},
            {"status", "complete"},
            {"unit_count", 1},
            {"excluded_unit_count", 0},
            {"records",
             {{
                 {"route_id", unit.routeId},
                 {"unit_id", unit.unitId},
                 {"scene_id", unit.sceneId},
                 {"scene_path", unit.scenePath},
                 {"setup_index", unit.setupIndex},
                 {"native_room_indices", unit.nativeRoomIndices},
                 {"initial_room_index", unit.initialRoomIndex},
                 {"status", unit.status},
                 {"composition_status", "composition_complete"},
                 {"behavior_evidence_status", unit.behaviorEvidenceStatus},
                 {"payload_sha256", unit.payloadSha256},
                 {"resource_path", "oot3d/room_units/test.json"},
                 {"closure",
                  {
                      {"room_count", 1},
                      {"actor_instance_count", 1},
                      {"unique_actor_profile_count", 1},
                      {"object_dependency_count", 1},
                      {"unresolved_count", 0},
                  }},
             }}},
            {"excluded", nlohmann::json::array()},
        };
        const auto parsedCatalog =
            Oot3d::ParseRoomCompilationUnitCatalog(catalog.dump(), [&](std::string_view path) {
                Expect(path == "oot3d/room_units/test.json", "catalog payload path");
                return document.dump();
            });
        Expect(parsedCatalog.Find("scene_entry:test", 0) != nullptr, "catalog route/setup lookup");

        auto invalid = document;
        invalid["actor_instances"][0]["entry"]["actor_id"] = 1;
        bool rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject actor/profile identity mismatch");

        invalid = document;
        invalid["rooms"][0]["initially_active"] = false;
        rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject inconsistent active room identity");

        invalid = document;
        invalid["rooms"][0]["setup_index"] = 1;
        rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject mismatched room setup identity");

        invalid = document;
        invalid["actor_profiles"][0]["behavior_graph"]["structure_size"] = 255;
        rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject actor behavior layout mismatch");

        invalid = document;
        invalid["closure"]["behavior_function_count"] = 3;
        rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject actor behavior closure mismatch");

        invalid = document;
        invalid["scene_setup"]["commands"][0]["parameter"] = 2;
        rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject invalid native scene command stream");

        invalid = document;
        invalid["rooms"][0]["commands"][0]["command_id"] = 0x08;
        rejected = false;
        try {
            Oot3d::ParseRoomCompilationUnit(invalid.dump());
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "reject invalid native room command stream");

        if (argc > 1) {
            const auto fixture = Oot3d::LoadRoomCompilationUnitFile(argv[1]);
            Expect(!fixture.rooms.empty() && !fixture.actorInstances.empty() &&
                       !fixture.actorProfiles.empty() && !fixture.objectDependencies.empty(),
                   "load external room compilation unit");
            std::cout << fixture.unitId << ' ' << fixture.rooms.size() << ' '
                      << fixture.actorInstances.size() << ' ' << fixture.actorProfiles.size() << ' '
                      << fixture.objectDependencies.size() << '\n';
        }
        if (argc > 2) {
            const auto catalogFixture = Oot3d::LoadNativeAbiCatalogFile(argv[2]);
            Expect(catalogFixture.functions.size() >= 2000 &&
                       catalogFixture.FindByName("EnKo_OverrideLimbDraw") != nullptr &&
                       catalogFixture.FindByName("LightContext_InsertLight") != nullptr,
                   "load external native ABI catalog");
            std::cout << catalogFixture.sourceSnapshotId << ' '
                      << catalogFixture.functions.size() << '\n';
        }
        std::cout << "oot3d_room_compilation_runtime_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "oot3d_room_compilation_runtime_tests: " << error.what() << '\n';
        return 1;
    }
}
