#include "oot3d_room_compilation_unit.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3d {
namespace {

constexpr std::string_view kCatalogFormat = "oot3d_room_compilation_unit_catalog_v1";
constexpr std::string_view kUnitFormat = "oot3d_room_compilation_unit_v1";

struct CatalogRecord {
    std::string routeId;
    std::string unitId;
    std::string scenePath;
    std::string payloadSha256;
    std::string resourcePath;
    std::string status;
    int32_t sceneId = -1;
    int32_t setupIndex = -1;
    int32_t initialRoomIndex = -1;
    uint32_t roomCount = 0;
    uint32_t actorInstanceCount = 0;
    uint32_t actorProfileCount = 0;
    uint32_t objectDependencyCount = 0;
    uint32_t unresolvedCount = 0;
};

const nlohmann::json& RequireObject(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_object()) {
        throw std::runtime_error(std::string("missing object: ") + key);
    }
    return parent[key];
}

const nlohmann::json& RequireArray(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_array()) {
        throw std::runtime_error(std::string("missing array: ") + key);
    }
    return parent[key];
}

std::string RequireString(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_string()) {
        throw std::runtime_error(std::string("missing string: ") + key);
    }
    const std::string value = parent[key].get<std::string>();
    if (value.empty()) {
        throw std::runtime_error(std::string("empty string: ") + key);
    }
    return value;
}

std::string OptionalString(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object() || !parent.contains(key) || parent[key].is_null()) {
        return {};
    }
    if (!parent[key].is_string()) {
        throw std::runtime_error(std::string("invalid string: ") + key);
    }
    return parent[key].get<std::string>();
}

int64_t RequireInteger(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object() || !parent.contains(key)) {
        throw std::runtime_error(std::string("missing integer: ") + key);
    }
    const auto& value = parent[key];
    if (value.is_number_unsigned()) {
        const uint64_t unsignedValue = value.get<uint64_t>();
        if (unsignedValue > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error(std::string("integer outside int64: ") + key);
        }
        return static_cast<int64_t>(unsignedValue);
    }
    if (!value.is_number_integer()) {
        throw std::runtime_error(std::string("missing integer: ") + key);
    }
    return value.get<int64_t>();
}

int32_t RequireInt32(const nlohmann::json& parent, const char* key) {
    const int64_t value = RequireInteger(parent, key);
    if (value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        throw std::runtime_error(std::string("integer outside int32: ") + key);
    }
    return static_cast<int32_t>(value);
}

uint32_t RequireUint32(const nlohmann::json& parent, const char* key) {
    const int64_t value = RequireInteger(parent, key);
    if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("integer outside uint32: ") + key);
    }
    return static_cast<uint32_t>(value);
}

uint32_t ParseAddress(const std::string& value, const char* key) {
    size_t parsedLength = 0;
    uint64_t address = 0;
    try {
        address = std::stoull(value, &parsedLength, 0);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid native address: ") + key);
    }
    if (parsedLength != value.size() || address > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("native address outside uint32: ") + key);
    }
    return static_cast<uint32_t>(address);
}

bool IsSha256(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

int16_t RequireRawInt16(const nlohmann::json& parent, const char* key) {
    const int64_t value = RequireInteger(parent, key);
    if (value < std::numeric_limits<int16_t>::min() ||
        value > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error(std::string("integer outside native int16 bits: ") + key);
    }
    return static_cast<int16_t>(static_cast<uint16_t>(value));
}

RoomCompilationVec3s RequireVec3s(const nlohmann::json& parent, const char* key) {
    const auto& values = RequireArray(parent, key);
    if (values.size() != 3) {
        throw std::runtime_error(std::string("native vec3 must contain three values: ") + key);
    }
    RoomCompilationVec3s result;
    int16_t* targets[] = {&result.x, &result.y, &result.z};
    for (size_t index = 0; index < values.size(); ++index) {
        if (!values[index].is_number_integer() && !values[index].is_number_unsigned()) {
            throw std::runtime_error(std::string("native vec3 contains a non-integer: ") + key);
        }
        const int64_t value = values[index].get<int64_t>();
        if (value < std::numeric_limits<int16_t>::min() ||
            value > std::numeric_limits<int16_t>::max()) {
            throw std::runtime_error(std::string("native vec3 value outside int16: ") + key);
        }
        *targets[index] = static_cast<int16_t>(value);
    }
    return result;
}

bool OptionalBool(const nlohmann::json& parent, const char* key, bool fallback = false) {
    if (!parent.is_object() || !parent.contains(key)) {
        return fallback;
    }
    if (!parent[key].is_boolean()) {
        throw std::runtime_error(std::string("invalid boolean: ") + key);
    }
    return parent[key].get<bool>();
}

bool RequireBool(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_boolean()) {
        throw std::runtime_error(std::string("missing boolean: ") + key);
    }
    return parent[key].get<bool>();
}

bool VecMatches(const RoomCompilationVec3s& lhs, const RoomCompilationVec3s& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

CatalogRecord ParseCatalogRecord(const nlohmann::json& value) {
    CatalogRecord result;
    result.routeId = RequireString(value, "route_id");
    result.unitId = RequireString(value, "unit_id");
    result.sceneId = RequireInt32(value, "scene_id");
    result.scenePath = RequireString(value, "scene_path");
    result.setupIndex = RequireInt32(value, "setup_index");
    result.initialRoomIndex = RequireInt32(value, "initial_room_index");
    result.status = RequireString(value, "status");
    result.payloadSha256 = RequireString(value, "payload_sha256");
    result.resourcePath = RequireString(value, "resource_path");
    if (!IsSha256(result.payloadSha256) ||
        result.resourcePath.rfind(kRoomCompilationUnitResourceRoot, 0) != 0 ||
        result.resourcePath.find("..") != std::string::npos) {
        throw std::runtime_error("invalid room compilation unit digest or resource path");
    }
    const auto& closure = RequireObject(value, "closure");
    result.roomCount = RequireUint32(closure, "room_count");
    result.actorInstanceCount = RequireUint32(closure, "actor_instance_count");
    result.actorProfileCount = RequireUint32(closure, "unique_actor_profile_count");
    result.objectDependencyCount = RequireUint32(closure, "object_dependency_count");
    result.unresolvedCount = RequireUint32(closure, "unresolved_count");
    return result;
}

RoomCompilationActorEntry ParseActorEntry(const nlohmann::json& value) {
    RoomCompilationActorEntry result;
    result.instanceKey = RequireString(value, "instance_key");
    result.profileKey = RequireString(value, "profile_key");
    result.sourceKind = RequireString(value, "source_kind");
    const auto& entry = RequireObject(value, "entry");
    result.actorId = RequireInt32(entry, "actor_id");
    result.actorName = RequireString(entry, "actor_name");
    result.sourceIndex = RequireInt32(entry, "index");
    result.position = RequireVec3s(entry, "pos");
    result.params = RequireRawInt16(entry, "params");
    result.transition = result.sourceKind == "scene_transition_actor_list";
    if (result.transition) {
        if (!value.contains("room_index") || !value["room_index"].is_null()) {
            throw std::runtime_error("transition actor must not claim one owning room");
        }
        result.frontRoomIndex = RequireInt32(entry, "front_room");
        result.backRoomIndex = RequireInt32(entry, "back_room");
        result.frontEffect = RequireInt32(entry, "front_effect");
        result.backEffect = RequireInt32(entry, "back_effect");
        result.rotation.y = RequireRawInt16(entry, "rot_y");
    } else {
        if (result.sourceKind != "scene_spawn" && result.sourceKind != "room_actor_list") {
            throw std::runtime_error("unsupported room compilation actor source kind");
        }
        result.roomIndex = RequireInt32(value, "room_index");
        result.sourceOffset = RequireInteger(entry, "offset");
        result.rotation = RequireVec3s(entry, "rot");
    }
    return result;
}

RoomCompilationActorBehaviorGraph ParseActorBehaviorGraph(
    const nlohmann::json& value, uint32_t actorInstanceSize) {
    RoomCompilationActorBehaviorGraph graph;
    graph.status = RequireString(value, "status");
    graph.owner = OptionalString(value, "owner");
    graph.structure = OptionalString(value, "structure");
    graph.runtimeBindingStatus = RequireString(value, "runtime_binding_status");
    graph.consumerStatus = value.contains("consumer_status")
                               ? RequireString(value, "consumer_status")
                               : "consumer_evidence_unavailable";
    graph.consumerRuntimeBindingStatus =
        value.contains("consumer_runtime_binding_status")
            ? RequireString(value, "consumer_runtime_binding_status")
            : "consumer_evidence_not_available";
    const uint32_t declaredFunctionCount = RequireUint32(value, "function_count");
    const uint32_t declaredTransitionCount =
        RequireUint32(value, "action_transition_count");
    const uint32_t declaredFieldCount = RequireUint32(value, "structure_field_count");
    const uint32_t declaredIndirectRootCount =
        value.contains("indirect_root_count")
            ? RequireUint32(value, "indirect_root_count")
            : 0;
    const uint32_t declaredNativeAbiFunctionCount =
        value.contains("native_abi_function_count")
            ? RequireUint32(value, "native_abi_function_count")
            : 0;
    const uint32_t declaredConsumerRootCount =
        value.contains("consumer_root_count")
            ? RequireUint32(value, "consumer_root_count")
            : 0;
    const uint32_t declaredConsumerFunctionCount =
        value.contains("consumer_function_count")
            ? RequireUint32(value, "consumer_function_count")
            : 0;
    const uint32_t declaredConsumerCallEdgeCount =
        value.contains("consumer_call_edge_count")
            ? RequireUint32(value, "consumer_call_edge_count")
            : 0;
    const uint32_t declaredActorLocalConsumerFunctionCount =
        value.contains("actor_local_consumer_function_count")
            ? RequireUint32(value, "actor_local_consumer_function_count")
            : 0;
    const uint32_t declaredNativeServiceDependencyCount =
        value.contains("native_service_dependency_count")
            ? RequireUint32(value, "native_service_dependency_count")
            : 0;
    const auto& functions = RequireArray(value, "functions");
    const auto& transitions = RequireArray(value, "action_transitions");
    const auto& fields = RequireArray(value, "structure_fields");
    const auto& initialActions = RequireArray(value, "initial_action_candidates");
    const nlohmann::json emptyArray = nlohmann::json::array();
    const auto& consumerRoots = value.contains("consumer_roots")
                                    ? RequireArray(value, "consumer_roots")
                                    : emptyArray;
    const auto& consumerCallEdges = value.contains("consumer_call_edges")
                                        ? RequireArray(value, "consumer_call_edges")
                                        : emptyArray;
    const auto& unresolvedConsumerDependencies =
        value.contains("unresolved_consumer_dependencies")
            ? RequireArray(value, "unresolved_consumer_dependencies")
            : emptyArray;

    if (graph.status == "workflow_graph_unavailable") {
        if (graph.runtimeBindingStatus != "behavior_evidence_not_available" ||
            !functions.empty() || !transitions.empty() || !fields.empty() ||
            !initialActions.empty() || !consumerRoots.empty() ||
            !consumerCallEdges.empty() || !unresolvedConsumerDependencies.empty() ||
            graph.consumerStatus != "consumer_evidence_unavailable" ||
            graph.consumerRuntimeBindingStatus != "consumer_evidence_not_available" ||
            !value.contains("structure_size") ||
            !value["structure_size"].is_null() || declaredFunctionCount != 0 ||
            declaredTransitionCount != 0 || declaredFieldCount != 0 ||
            declaredIndirectRootCount != 0 || declaredNativeAbiFunctionCount != 0 ||
            declaredConsumerRootCount != 0 || declaredConsumerFunctionCount != 0 ||
            declaredConsumerCallEdgeCount != 0 ||
            declaredActorLocalConsumerFunctionCount != 0 ||
            declaredNativeServiceDependencyCount != 0) {
            throw std::runtime_error(
                "unavailable actor behavior graph contains native evidence");
        }
        return graph;
    }
    if ((graph.status != "workflow_graph_recovered" &&
         graph.status != "native_function_set_recovered" &&
         graph.status != "native_consumer_graph_recovered") || graph.owner.empty() ||
        graph.structure.empty() ||
        graph.runtimeBindingStatus != "consumer_must_bind_native_behavior_graph") {
        throw std::runtime_error("unsupported actor behavior graph");
    }
    graph.available = true;
    graph.structureSize = RequireUint32(value, "structure_size");
    if (graph.structureSize == 0 || graph.structureSize != actorInstanceSize) {
        throw std::runtime_error(
            "actor behavior graph structure size differs from ActorInit");
    }

    std::unordered_map<std::string, uint32_t> functionAddresses;
    std::unordered_map<uint32_t, std::string> functionNamesByAddress;
    std::unordered_set<uint32_t> addresses;
    uint32_t observedIndirectRootCount = 0;
    uint32_t observedNativeAbiFunctionCount = 0;
    uint32_t observedConsumerFunctionCount = 0;
    uint32_t observedActorLocalConsumerFunctionCount = 0;
    uint32_t observedNativeServiceDependencyCount = 0;
    for (const auto& functionJson : functions) {
        RoomCompilationActorBehaviorFunction function;
        function.name = RequireString(functionJson, "name");
        function.family = RequireString(functionJson, "family");
        function.confidence = RequireString(functionJson, "confidence");
        function.returnType = OptionalString(functionJson, "return_type");
        function.parameterTypes = OptionalString(functionJson, "param_types");
        function.parameterNames = OptionalString(functionJson, "param_names");
        function.source = OptionalString(functionJson, "source");
        function.evidenceFile = OptionalString(functionJson, "evidence_file");
        function.signatureEvidenceFile =
            OptionalString(functionJson, "signature_evidence_file");
        function.discoveryKind = OptionalString(functionJson, "discovery_kind");
        function.closureKind = OptionalString(functionJson, "closure_kind");
        function.ownerResolution = OptionalString(functionJson, "owner_resolution");
        function.consumerRole = OptionalString(functionJson, "consumer_role");
        function.bodyStatus = OptionalString(functionJson, "body_status");
        function.sourceTranche = functionJson.contains("source_tranche")
                                     ? RequireUint32(functionJson, "source_tranche")
                                     : 0;
        function.consumerDepth = functionJson.contains("consumer_depth")
                                     ? RequireUint32(functionJson, "consumer_depth")
                                     : 0;
        function.runtimeAddress = ParseAddress(
            RequireString(functionJson, "address"), "actor behavior function");
        function.byteLength = RequireUint32(functionJson, "byte_length");
        if (function.runtimeAddress == 0 || function.byteLength == 0 ||
            !functionAddresses.emplace(function.name, function.runtimeAddress).second ||
            !addresses.insert(function.runtimeAddress).second) {
            throw std::runtime_error("duplicate or empty actor behavior function");
        }
        functionNamesByAddress.emplace(function.runtimeAddress, function.name);
        if (!function.consumerRole.empty()) {
            if ((function.consumerRole != "native_lifecycle_consumer" &&
                 function.consumerRole != "actor_local_consumer_helper" &&
                 function.consumerRole != "native_service_dependency") ||
                (function.bodyStatus != "reviewed_original_callback_body" &&
                 function.bodyStatus != "reviewed_original_function_body" &&
                 function.bodyStatus != "original_function_inventory_only") ||
                (function.consumerRole == "native_lifecycle_consumer" &&
                 function.consumerDepth != 0)) {
                throw std::runtime_error(
                    "actor original-consumer function metadata is invalid");
            }
            ++observedConsumerFunctionCount;
            observedActorLocalConsumerFunctionCount +=
                function.consumerRole == "actor_local_consumer_helper" ? 1u : 0u;
            observedNativeServiceDependencyCount +=
                function.consumerRole == "native_service_dependency" ? 1u : 0u;
        } else if (!function.bodyStatus.empty() || function.consumerDepth != 0) {
            throw std::runtime_error(
                "ordinary actor behavior function contains consumer metadata");
        }
        if (!function.closureKind.empty()) {
            const std::string expectedFamilyPrefix =
                graph.structure == "Oot3dPlayer" ? "player_" : "actor_";
            const std::string paddedTypes = ";" + function.parameterTypes + ";";
            const std::string ownerPointer = ";" + graph.structure + "*;";
            if ((function.closureKind != "indirect_root" &&
                 function.closureKind != "parent_owned_root" &&
                 function.closureKind != "maintained_abi" &&
                 function.closureKind != "callable_identity" &&
                 function.closureKind != "actor_private_native" &&
                 function.closureKind != "actor_priority_native") ||
                function.sourceTranche == 0) {
                throw std::runtime_error("actor native-ABI closure metadata is invalid");
            }
            if (function.ownerResolution ==
                "exact_concrete_instance_pointer_in_native_abi") {
                if (function.family.rfind(expectedFamilyPrefix, 0) != 0 ||
                    paddedTypes.find(ownerPointer) == std::string::npos) {
                    throw std::runtime_error(
                        "actor native-ABI function lacks concrete ownership evidence");
                }
            } else if (function.ownerResolution ==
                       "exact_maintained_actor_symbol_prefix_and_native_context_abi") {
                if (function.closureKind != "maintained_abi" ||
                    function.family.rfind("actor_", 0) != 0 ||
                    function.name.rfind(graph.owner + "_", 0) != 0 ||
                    paddedTypes.find(";Oot3dPlayState*;") == std::string::npos ||
                    paddedTypes.find(";void*;") == std::string::npos) {
                    throw std::runtime_error(
                        "maintained actor ABI lacks exact symbol/context ownership");
                }
            } else {
                throw std::runtime_error("actor native-ABI ownership is unresolved");
            }
            if (function.closureKind == "indirect_root") {
                if (function.discoveryKind != "zero_direct_caller_native_root") {
                    throw std::runtime_error(
                        "indirect actor root lacks reviewed discovery evidence");
                }
                ++observedIndirectRootCount;
            } else if (!function.discoveryKind.empty()) {
                throw std::runtime_error(
                    "non-indirect actor ABI contains indirect discovery metadata");
            }
            ++observedNativeAbiFunctionCount;
        } else if (!function.ownerResolution.empty() || function.sourceTranche != 0 ||
                   (!function.signatureEvidenceFile.empty() &&
                    function.consumerRole.empty()) ||
                   !function.discoveryKind.empty()) {
            throw std::runtime_error(
                "ordinary actor behavior function contains native-ABI metadata");
        }
        graph.functions.push_back(std::move(function));
    }

    for (const auto& transitionJson : transitions) {
        RoomCompilationActorBehaviorTransition transition;
        transition.sourceFunction = RequireString(transitionJson, "source_function");
        transition.targetName = RequireString(transitionJson, "target_name");
        transition.evidence = OptionalString(transitionJson, "evidence");
        transition.status = RequireString(transitionJson, "status");
        transition.evidenceFile = OptionalString(transitionJson, "evidence_file");
        transition.literalAddress = ParseAddress(
            RequireString(transitionJson, "literal_address"),
            "actor behavior action literal");
        transition.targetAddress = ParseAddress(
            RequireString(transitionJson, "target_address"),
            "actor behavior action target");
        if (transition.literalAddress == 0 ||
            !functionAddresses.contains(transition.sourceFunction)) {
            throw std::runtime_error("actor behavior transition source is outside its graph");
        }
        const auto target = functionAddresses.find(transition.targetName);
        if (transition.status == "exact_function" &&
            (target == functionAddresses.end() ||
             target->second != transition.targetAddress)) {
            throw std::runtime_error("actor behavior transition target is outside its graph");
        }
        graph.actionTransitions.push_back(std::move(transition));
    }

    std::unordered_set<uint32_t> fieldOffsets;
    for (const auto& fieldJson : fields) {
        RoomCompilationActorStructureField field;
        field.offset = RequireUint32(fieldJson, "offset");
        field.type = RequireString(fieldJson, "type");
        field.name = RequireString(fieldJson, "name");
        field.confidence = RequireString(fieldJson, "confidence");
        field.source = OptionalString(fieldJson, "source");
        field.evidenceFile = OptionalString(fieldJson, "evidence_file");
        if (field.offset >= graph.structureSize ||
            !fieldOffsets.insert(field.offset).second) {
            throw std::runtime_error("actor behavior structure field is outside its layout");
        }
        graph.structureFields.push_back(std::move(field));
    }

    std::unordered_set<std::string> initialActionNames;
    for (const auto& actionJson : initialActions) {
        if (!actionJson.is_string()) {
            throw std::runtime_error("actor initial action candidate is not a string");
        }
        const std::string action = actionJson.get<std::string>();
        if (!functionAddresses.contains(action) ||
            !initialActionNames.insert(action).second) {
            throw std::runtime_error("actor initial action candidate is outside its graph");
        }
        graph.initialActionCandidates.push_back(action);
    }

    std::unordered_set<std::string> consumerRootNames;
    std::unordered_set<std::string> consumerRootSlots;
    for (const auto& rootJson : consumerRoots) {
        RoomCompilationActorConsumerRoot root;
        root.slot = RequireString(rootJson, "slot");
        root.name = RequireString(rootJson, "name");
        root.bodyStatus = RequireString(rootJson, "body_status");
        root.runtimeAddress = ParseAddress(
            RequireString(rootJson, "address"), "actor original-consumer root");
        const auto functionName = functionNamesByAddress.find(root.runtimeAddress);
        const auto functionAddress = functionAddresses.find(root.name);
        const auto function = std::find_if(
            graph.functions.begin(), graph.functions.end(),
            [&root](const RoomCompilationActorBehaviorFunction& candidate) {
                return candidate.runtimeAddress == root.runtimeAddress;
            });
        if ((root.slot != "init" && root.slot != "destroy" &&
             root.slot != "update" && root.slot != "draw") ||
            functionName == functionNamesByAddress.end() ||
            functionName->second != root.name ||
            functionAddress == functionAddresses.end() ||
            functionAddress->second != root.runtimeAddress ||
            function == graph.functions.end() ||
            function->consumerRole != "native_lifecycle_consumer" ||
            function->bodyStatus != root.bodyStatus ||
            !consumerRootNames.insert(root.name).second ||
            !consumerRootSlots.insert(root.slot).second) {
            throw std::runtime_error(
                "actor original-consumer root identity is invalid");
        }
        graph.consumerRoots.push_back(std::move(root));
    }

    std::set<std::pair<uint32_t, uint32_t>> consumerEdgeKeys;
    for (const auto& edgeJson : consumerCallEdges) {
        RoomCompilationActorConsumerCallEdge edge;
        edge.callerName = RequireString(edgeJson, "caller_name");
        edge.calleeName = RequireString(edgeJson, "callee_name");
        edge.relation = RequireString(edgeJson, "relation");
        edge.callerAddress = ParseAddress(
            RequireString(edgeJson, "caller_address"),
            "actor original-consumer caller");
        edge.calleeAddress = ParseAddress(
            RequireString(edgeJson, "callee_address"),
            "actor original-consumer callee");
        const auto caller = functionNamesByAddress.find(edge.callerAddress);
        const auto callee = functionNamesByAddress.find(edge.calleeAddress);
        if (edge.relation != "direct_arm_call" ||
            caller == functionNamesByAddress.end() ||
            caller->second != edge.callerName ||
            callee == functionNamesByAddress.end() ||
            callee->second != edge.calleeName ||
            !consumerEdgeKeys.emplace(edge.callerAddress, edge.calleeAddress).second) {
            throw std::runtime_error(
                "actor original-consumer direct-call edge is invalid");
        }
        graph.consumerCallEdges.push_back(std::move(edge));
    }

    for (const auto& dependencyJson : unresolvedConsumerDependencies) {
        if (!dependencyJson.is_string()) {
            throw std::runtime_error(
                "actor unresolved consumer dependency is not an address");
        }
        const std::string dependency = dependencyJson.get<std::string>();
        if (ParseAddress(dependency, "actor unresolved consumer dependency") == 0) {
            throw std::runtime_error("actor unresolved consumer dependency is null");
        }
        graph.unresolvedConsumerDependencies.push_back(dependency);
    }

    if (!graph.consumerRoots.empty()) {
        if ((graph.consumerStatus != "original_lifecycle_consumers_recovered" &&
             graph.consumerStatus != "lifecycle_consumer_inventory_partial") ||
            graph.consumerRuntimeBindingStatus !=
                "consumer_must_lower_original_bodies_and_bind_native_services") {
            throw std::runtime_error(
                "actor original-consumer binding status is invalid");
        }
    } else if (graph.consumerStatus != "consumer_evidence_unavailable" ||
               graph.consumerRuntimeBindingStatus !=
                   "consumer_evidence_not_available") {
        throw std::runtime_error(
            "actor behavior contains consumer status without roots");
    }

    if (declaredFunctionCount != graph.functions.size() ||
        declaredTransitionCount != graph.actionTransitions.size() ||
        declaredFieldCount != graph.structureFields.size() || graph.functions.empty() ||
        declaredIndirectRootCount != observedIndirectRootCount ||
        declaredNativeAbiFunctionCount != observedNativeAbiFunctionCount ||
        declaredConsumerRootCount != graph.consumerRoots.size() ||
        declaredConsumerFunctionCount != observedConsumerFunctionCount ||
        declaredConsumerCallEdgeCount != graph.consumerCallEdges.size() ||
        declaredActorLocalConsumerFunctionCount !=
            observedActorLocalConsumerFunctionCount ||
        declaredNativeServiceDependencyCount !=
            observedNativeServiceDependencyCount) {
        throw std::runtime_error("actor behavior graph counts are inconsistent");
    }
    graph.indirectRootCount = observedIndirectRootCount;
    graph.nativeAbiFunctionCount = observedNativeAbiFunctionCount;
    graph.consumerRootCount = declaredConsumerRootCount;
    graph.consumerFunctionCount = observedConsumerFunctionCount;
    graph.consumerCallEdgeCount = declaredConsumerCallEdgeCount;
    graph.actorLocalConsumerFunctionCount =
        observedActorLocalConsumerFunctionCount;
    graph.nativeServiceDependencyCount = observedNativeServiceDependencyCount;
    return graph;
}

RoomCompilationIndirectRootCatalog ParseIndirectRootCatalog(
    const nlohmann::json& value) {
    RoomCompilationIndirectRootCatalog catalog;
    catalog.available = true;
    catalog.status = RequireString(value, "status");
    catalog.sourceSnapshotId = RequireString(value, "source_snapshot_id");
    catalog.ownershipPolicy = RequireString(value, "ownership_policy");
    catalog.runtimeBindingStatus = RequireString(value, "runtime_binding_status");
    catalog.rootCount = RequireUint32(value, "root_count");
    catalog.actorCandidateCount = RequireUint32(value, "actor_candidate_count");
    catalog.profileBoundRootCount = RequireUint32(value, "profile_bound_root_count");
    catalog.unboundActorCandidateCount =
        RequireUint32(value, "unbound_actor_candidate_count");
    if (RequireString(value, "format") != "oot3d_indirect_native_root_catalog_v1" ||
        catalog.status != "reviewed_native_roots_indexed" ||
        catalog.runtimeBindingStatus !=
            "profile_bound_roots_require_consumer_binding" ||
        catalog.rootCount < catalog.actorCandidateCount ||
        catalog.actorCandidateCount < catalog.profileBoundRootCount ||
        catalog.unboundActorCandidateCount !=
            catalog.actorCandidateCount - catalog.profileBoundRootCount) {
        throw std::runtime_error("unsupported indirect native-root catalog");
    }
    const auto& familyCounts = RequireObject(value, "family_counts");
    uint64_t observedRootCount = 0;
    for (auto iterator = familyCounts.begin(); iterator != familyCounts.end(); ++iterator) {
        if (iterator.key().empty() ||
            (!iterator.value().is_number_unsigned() &&
             !iterator.value().is_number_integer())) {
            throw std::runtime_error("invalid indirect native-root family count");
        }
        const int64_t count = iterator.value().get<int64_t>();
        if (count < 0 || count > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("indirect native-root family count is outside uint32");
        }
        catalog.familyCounts.emplace(iterator.key(), static_cast<uint32_t>(count));
        observedRootCount += static_cast<uint32_t>(count);
    }
    if (observedRootCount != catalog.rootCount) {
        throw std::runtime_error("indirect native-root family counts do not close");
    }
    for (const auto& source : RequireArray(value, "source_catalogs")) {
        if (!source.is_string() || source.get<std::string>().empty()) {
            throw std::runtime_error("indirect native-root source catalog is invalid");
        }
        catalog.sourceCatalogs.push_back(source.get<std::string>());
    }
    if (catalog.sourceCatalogs.empty()) {
        throw std::runtime_error("indirect native-root catalog lacks provenance");
    }
    return catalog;
}

RoomCompilationNativeAbiCatalogSummary ParseNativeAbiCatalogSummary(
    const nlohmann::json& value) {
    RoomCompilationNativeAbiCatalogSummary catalog;
    catalog.available = true;
    catalog.status = RequireString(value, "status");
    catalog.sourceSnapshotId = RequireString(value, "source_snapshot_id");
    catalog.sourceBaseRevision = RequireString(value, "source_base_revision");
    catalog.codeBinSha256 = RequireString(value, "code_bin_sha256");
    catalog.payloadSha256 = RequireString(value, "payload_sha256");
    catalog.ownershipPolicy = RequireString(value, "ownership_policy");
    catalog.runtimeBindingStatus = RequireString(value, "runtime_binding_status");
    catalog.functionCount = RequireUint32(value, "function_count");
    catalog.nativePointerFunctionCount =
        RequireUint32(value, "native_pointer_function_count");
    catalog.profileBoundFunctionCount =
        RequireUint32(value, "profile_bound_function_count");
    if (RequireString(value, "format") != "oot3d_native_abi_catalog_v1" ||
        catalog.status != "reviewed_native_abi_catalog_complete" ||
        catalog.runtimeBindingStatus !=
            "catalog_mounted_once_profile_bindings_in_rcu" ||
        catalog.functionCount == 0 ||
        catalog.functionCount < catalog.nativePointerFunctionCount ||
        catalog.functionCount < catalog.profileBoundFunctionCount ||
        !IsSha256(catalog.codeBinSha256) || !IsSha256(catalog.payloadSha256)) {
        throw std::runtime_error("unsupported native ABI catalog summary");
    }
    uint64_t observedFunctionCount = 0;
    const auto& closureKindCounts = RequireObject(value, "closure_kind_counts");
    for (auto iterator = closureKindCounts.begin();
         iterator != closureKindCounts.end(); ++iterator) {
        if (iterator.key().empty() ||
            (!iterator.value().is_number_unsigned() &&
             !iterator.value().is_number_integer())) {
            throw std::runtime_error("invalid native ABI closure count");
        }
        const int64_t count = iterator.value().get<int64_t>();
        if (count < 0 || count > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("native ABI closure count is outside uint32");
        }
        catalog.closureKindCounts.emplace(iterator.key(), static_cast<uint32_t>(count));
        observedFunctionCount += static_cast<uint32_t>(count);
    }
    if (observedFunctionCount != catalog.functionCount) {
        throw std::runtime_error("native ABI closure counts do not close");
    }
    return catalog;
}

RoomCompilationActorProfile ParseActorProfile(const nlohmann::json& value) {
    RoomCompilationActorProfile profile;
    profile.profileKey = RequireString(value, "profile_key");
    profile.actorId = RequireInt32(value, "actor_id");
    profile.actorName = RequireString(value, "actor_name");
    profile.objectId = RequireInt32(value, "object_id");
    profile.objectRomPath = OptionalString(value, "object_rom_path");
    profile.profileAddress = RequireString(value, "profile_address");
    profile.overlayEntryAddress = RequireString(value, "overlay_entry_address");
    profile.overlayTableAddress = RequireString(value, "overlay_table_address");
    profile.profileRuntimeAddress = ParseAddress(profile.profileAddress, "actor profile");
    profile.overlayEntryRuntimeAddress =
        ParseAddress(profile.overlayEntryAddress, "actor overlay entry");
    profile.overlayTableRuntimeAddress =
        ParseAddress(profile.overlayTableAddress, "actor overlay table");
    profile.actorInitEvidenceStatus = OptionalString(value, "actor_init_evidence_status");
    profile.runtimeBindingStatus = RequireString(value, "runtime_binding_status");
    profile.category = RequireInt32(value, "category");
    profile.flags = RequireUint32(value, "flags");
    profile.instanceSize = RequireUint32(value, "instance_size");
    if (profile.actorId < 0 || profile.actorId > std::numeric_limits<uint16_t>::max() ||
        profile.objectId < 0 || profile.objectId > std::numeric_limits<uint16_t>::max() ||
        profile.category < 0 || profile.category > std::numeric_limits<uint8_t>::max() ||
        profile.profileRuntimeAddress == 0) {
        throw std::runtime_error("actor profile identity is outside native bounds");
    }
    profile.behaviorGraph = ParseActorBehaviorGraph(
        RequireObject(value, "behavior_graph"), profile.instanceSize);
    if (value.contains("callback_evidence_counts")) {
        const auto& counts = RequireObject(value, "callback_evidence_counts");
        for (auto iterator = counts.begin(); iterator != counts.end(); ++iterator) {
            if (!iterator.value().is_number_unsigned() && !iterator.value().is_number_integer()) {
                throw std::runtime_error("actor callback evidence count is not an integer");
            }
            const int64_t count = iterator.value().get<int64_t>();
            if (count < 0 || count > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("actor callback evidence count is outside uint32");
            }
            profile.callbackEvidenceCounts.emplace(iterator.key(), static_cast<uint32_t>(count));
        }
    }
    const auto& callbacks = RequireObject(value, "callbacks");
    std::unordered_map<std::string, uint32_t> observedCallbackEvidenceCounts;
    for (const char* slot : {"init", "destroy", "update", "draw"}) {
        const auto& callbackJson = RequireObject(callbacks, slot);
        RoomCompilationActorCallback callback;
        callback.slot = RequireString(callbackJson, "slot");
        callback.name = OptionalString(callbackJson, "name");
        callback.address = RequireString(callbackJson, "address");
        callback.runtimeAddress = ParseAddress(callback.address, "actor callback");
        callback.evidenceStatus = RequireString(callbackJson, "evidence_status");
        callback.runtimeBindingStatus = RequireString(callbackJson, "runtime_binding_status");
        callback.present = callback.runtimeAddress != 0;
        if (callback.slot != slot || (callback.present && callback.name.empty())) {
            throw std::runtime_error("actor callback slot does not match its profile key");
        }
        if (callback.present) {
            ++observedCallbackEvidenceCounts[callback.evidenceStatus];
        } else if (callback.runtimeBindingStatus != "not_required") {
            throw std::runtime_error(
                "absent actor callback unexpectedly requires a runtime binding");
        }
        profile.callbacks.push_back(std::move(callback));
    }
    if (observedCallbackEvidenceCounts != profile.callbackEvidenceCounts) {
        throw std::runtime_error("actor callback evidence counts do not match its profile");
    }
    return profile;
}

RoomCompilationLifecycleContract ParseRoomLifecycleContract(const nlohmann::json& value) {
    RoomCompilationLifecycleContract contract;
    contract.available = true;
    contract.format = RequireString(value, "format");
    contract.status = RequireString(value, "status");
    contract.evidenceSource = RequireString(value, "evidence_source");
    contract.sourceSnapshotId = RequireString(value, "source_snapshot_id");
    if (contract.format != "oot3d_room_lifecycle_contract_v1" ||
        contract.status != "workflow_semantics_recovered") {
        throw std::runtime_error("unsupported room lifecycle contract");
    }

    const auto& functions = RequireObject(value, "functions");
    for (const char* role : {"initialize", "request", "process_request", "destroy",
                             "cleanup_room_actors", "spawn_transition_actors",
                             "queue_resource_cleanup", "process_resource_cleanup"}) {
        const auto& functionJson = RequireObject(functions, role);
        RoomCompilationLifecycleFunction function;
        function.role = role;
        function.name = RequireString(functionJson, "name");
        function.returnType = OptionalString(functionJson, "return_type");
        function.parameterTypes = OptionalString(functionJson, "param_types");
        function.confidence = OptionalString(functionJson, "confidence");
        function.source = OptionalString(functionJson, "source");
        function.notes = OptionalString(functionJson, "notes");
        function.runtimeAddress =
            ParseAddress(RequireString(functionJson, "address"), "room lifecycle function");
        if (function.runtimeAddress == 0) {
            throw std::runtime_error("room lifecycle function has a null address");
        }
        contract.functions.push_back(std::move(function));
    }
    if (functions.size() != contract.functions.size()) {
        throw std::runtime_error("room lifecycle contract contains unknown function roles");
    }

    const auto& state = RequireObject(value, "state_model");
    const uint32_t idleState = RequireUint32(state, "idle");
    const uint32_t loadingState = RequireUint32(state, "loading");
    const uint32_t terminalState = RequireUint32(state, "terminal");
    if (idleState > UINT8_MAX || loadingState > UINT8_MAX || terminalState > UINT8_MAX ||
        idleState == loadingState || idleState == terminalState || loadingState == terminalState) {
        throw std::runtime_error("room lifecycle load states are invalid");
    }
    contract.idleState = static_cast<uint8_t>(idleState);
    contract.loadingState = static_cast<uint8_t>(loadingState);
    contract.terminalState = static_cast<uint8_t>(terminalState);
    contract.maxResidentRoomCount = RequireUint32(state, "max_resident_room_count");
    contract.initialBufferCount = RequireUint32(state, "initial_buffer_count");
    contract.bufferCountWithoutTransitionActors =
        RequireUint32(state, "buffer_count_without_transition_actors");
    contract.bufferCountWithTransitionActors =
        RequireUint32(state, "buffer_count_with_transition_actors");

    const auto& actors = RequireObject(value, "actor_residency");
    contract.roomActorRetentionPolicy = RequireString(actors, "room_actor_retention_policy");
    contract.transitionActorResidencyPolicy =
        RequireString(actors, "transition_actor_residency_policy");
    contract.transitionActorIdSpawnMask =
        RequireUint32(actors, "transition_actor_id_spawn_mask");
    contract.transitionActorParamsIndexStride =
        RequireUint32(actors, "transition_actor_params_index_stride");
    contract.transitionActorSpawnMarker =
        RequireString(actors, "transition_actor_spawn_marker");

    const auto& resources = RequireObject(value, "resource_residency");
    contract.previousRoomCleanupBeforeNextRequest =
        RequireBool(resources, "previous_room_cleanup_before_next_request");
    contract.cleanupDelayTicks = RequireUint32(resources, "cleanup_delay_ticks");
    contract.roomCommandsInstallOnRequestCompletion =
        RequireBool(resources, "room_commands_install_on_request_completion");

    if (contract.maxResidentRoomCount != 2 ||
        contract.bufferCountWithoutTransitionActors != 1 ||
        contract.bufferCountWithTransitionActors != 3 ||
        contract.roomActorRetentionPolicy != "negative_room_or_current_or_previous" ||
        contract.transitionActorResidencyPolicy !=
            "front_or_back_matches_current_or_previous" ||
        contract.transitionActorIdSpawnMask != 0x1FFF ||
        contract.transitionActorParamsIndexStride != 0x400 ||
        contract.transitionActorSpawnMarker != "negate_source_actor_id" ||
        !contract.previousRoomCleanupBeforeNextRequest || contract.cleanupDelayTicks != 1 ||
        !contract.roomCommandsInstallOnRequestCompletion) {
        throw std::runtime_error("room lifecycle consumer policies are unsupported");
    }
    return contract;
}

RoomCompilationFactPredicate ParseFactPredicate(const nlohmann::json& value,
                                                 const char* label) {
    RoomCompilationFactPredicate predicate;
    predicate.key = RequireString(value, "key");
    predicate.equals = RequireString(value, "equals");
    if (predicate.key.empty() || predicate.equals.empty()) {
        throw std::runtime_error(std::string(label) + " predicate is empty");
    }
    return predicate;
}

RoomCompilationSceneCallbackContract ParseRoomCallbackContract(
    const nlohmann::json& value) {
    RoomCompilationSceneCallbackContract contract;
    contract.available = true;
    contract.configIndex = RequireInt32(value, "config_index");
    contract.configName = RequireString(value, "config_name");
    const auto& callbackJson = RequireObject(value, "callbacks");
    for (const char* role : {"init", "cleanup", "prepare_draw"}) {
        const auto& entry = RequireObject(callbackJson, role);
        RoomCompilationSceneCallback callback;
        callback.role = role;
        callback.name = RequireString(entry, "name");
        callback.evidenceStatus = RequireString(entry, "evidence_status");
        callback.runtimeAddress = ParseAddress(
            RequireString(entry, "address"), "room scene callback");
        if (callback.runtimeAddress == 0) {
            throw std::runtime_error("room scene callback has a null address");
        }
        contract.callbacks.push_back(std::move(callback));
    }

    const auto& runtime = RequireObject(value, "runtime_contract");
    if (RequireString(runtime, "format") !=
        "oot3d_room_scene_callback_native_runtime_v1") {
        throw std::runtime_error("unsupported room scene callback runtime format");
    }
    contract.status = RequireString(runtime, "status");
    contract.sourceRegistry = RequireString(runtime, "source_registry");
    const uint32_t runtimeAddress = ParseAddress(
        RequireString(runtime, "callback_address"), "room callback runtime contract");
    if (runtimeAddress != contract.callbacks.back().runtimeAddress) {
        throw std::runtime_error(
            "room callback runtime address differs from prepare_draw callback");
    }
    if (contract.status == "native_typed_operations_unavailable") {
        if (!RequireArray(runtime, "operations").empty()) {
            throw std::runtime_error(
                "unavailable room callback unexpectedly contains operations");
        }
        return contract;
    }
    if (contract.status != "native_typed_operations_ready") {
        throw std::runtime_error("unsupported room callback runtime status");
    }
    contract.functionSha256 = RequireString(runtime, "function_sha256");
    contract.functionSize = RequireUint32(runtime, "function_size");
    contract.literalVerificationCount =
        RequireUint32(runtime, "literal_verification_count");
    if (!IsSha256(contract.functionSha256) || contract.functionSize == 0 ||
        contract.literalVerificationCount == 0) {
        throw std::runtime_error("room callback code evidence is invalid");
    }

    for (const auto& operationJson : RequireArray(runtime, "operations")) {
        if (RequireString(operationJson, "kind") !=
                "material_tev_constant_alpha" ||
            RequireUint32(operationJson, "native_operation") != 2) {
            throw std::runtime_error("unsupported room callback operation");
        }
        RoomCompilationMaterialTevAlphaOperation operation;
        operation.nativeOperation = 2;
        const auto& target = RequireObject(operationJson, "target");
        operation.roomIndex = RequireInt32(target, "room_index");
        operation.resourceIndex = RequireUint32(target, "resource_index");
        operation.constantIndex = RequireUint32(target, "constant_index");
        if (operation.roomIndex < 0 || operation.constantIndex >= 6) {
            throw std::runtime_error("room callback material target is outside native bounds");
        }
        std::set<int32_t> materialIndices;
        for (const auto& materialIndex : RequireArray(target, "material_indices")) {
            if (!materialIndex.is_number_integer() &&
                !materialIndex.is_number_unsigned()) {
                throw std::runtime_error("room callback material index is not an integer");
            }
            const int64_t value = materialIndex.get<int64_t>();
            if (value < 0 || value > std::numeric_limits<int32_t>::max() ||
                !materialIndices.insert(static_cast<int32_t>(value)).second) {
                throw std::runtime_error("room callback material index is invalid");
            }
            operation.materialIndices.push_back(static_cast<int32_t>(value));
        }
        if (operation.materialIndices.empty()) {
            throw std::runtime_error("room callback material target is empty");
        }
        const auto& rgb = RequireArray(operationJson, "color_rgb_f32_bits");
        if (rgb.size() != operation.colorRgbF32Bits.size()) {
            throw std::runtime_error("room callback RGB vector has the wrong size");
        }
        for (size_t index = 0; index < rgb.size(); ++index) {
            if (!rgb[index].is_string()) {
                throw std::runtime_error("room callback RGB bits are not encoded as hex");
            }
            operation.colorRgbF32Bits[index] = ParseAddress(
                rgb[index].get<std::string>(), "room callback RGB bits");
        }

        const auto& alpha = RequireObject(operationJson, "alpha");
        operation.alphaBaseF32Bits = ParseAddress(
            RequireString(alpha, "base_f32_bits"), "room callback alpha base");
        operation.alphaSelectorScaleF32Bits = ParseAddress(
            RequireString(alpha, "selector_scale_f32_bits"),
            "room callback selector scale");
        const auto& selector = RequireObject(alpha, "selector");
        operation.selector.defaultValue = RequireUint32(selector, "default_value");
        operation.selector.excludedSetupIndex =
            RequireInt32(selector, "excluded_setup_index");
        operation.selector.drawParamSetupIndex =
            RequireInt32(selector, "draw_param_setup_index");
        operation.selector.drawParamIndex = RequireUint32(selector, "draw_param_index");
        operation.selector.progressionValue =
            RequireUint32(selector, "progression_value");
        operation.selector.setupLessThan = RequireInt32(selector, "setup_less_than");
        operation.selector.alternateFact = ParseFactPredicate(
            RequireObject(selector, "alternate_fact"), "alternate fact");
        operation.selector.requiredFact = ParseFactPredicate(
            RequireObject(selector, "required_fact"), "required fact");

        const auto& random = RequireObject(alpha, "random");
        if (RequireString(random, "function") != "Rand_S16Offset") {
            throw std::runtime_error("unsupported room callback random function");
        }
        operation.random.functionAddress = ParseAddress(
            RequireString(random, "function_address"),
            "room callback random function address");
        operation.random.functionSize = RequireUint32(random, "function_size");
        operation.random.functionSha256 = RequireString(random, "function_sha256");
        operation.random.stateAddress = ParseAddress(
            RequireString(random, "state_address"),
            "room callback random state address");
        operation.random.offset = RequireInt32(random, "offset");
        operation.random.range = RequireInt32(random, "range");
        operation.random.initialState = RequireUint32(random, "lcg_initial_state");
        operation.random.multiplier = ParseAddress(
            RequireString(random, "lcg_multiplier"), "room callback LCG multiplier");
        operation.random.increment = ParseAddress(
            RequireString(random, "lcg_increment"), "room callback LCG increment");
        operation.random.scaleF32Bits = ParseAddress(
            RequireString(random, "scale_f32_bits"), "room callback random scale");
        operation.random.middleTestAddU32 = ParseAddress(
            RequireString(random, "middle_test_add_u32"),
            "room callback middle-test addend");
        operation.random.middleTestLessThanU32 = ParseAddress(
            RequireString(random, "middle_test_less_than_u32"),
            "room callback middle-test limit");
        operation.random.lowCompareF32Bits = ParseAddress(
            RequireString(random, "low_compare_f32_bits"),
            "room callback low comparison");
        if (operation.random.functionAddress == 0 ||
            operation.random.functionSize == 0 ||
            !IsSha256(operation.random.functionSha256) ||
            operation.random.stateAddress == 0 || operation.random.range <= 0 ||
            operation.random.multiplier == 0) {
            throw std::runtime_error("room callback random contract is invalid");
        }
        contract.materialTevAlphaOperations.push_back(std::move(operation));
    }
    if (contract.materialTevAlphaOperations.empty()) {
        throw std::runtime_error("ready room callback has no executable operations");
    }
    contract.executable = true;
    return contract;
}

RoomCompilationUnit ParseUnitDocument(const nlohmann::json& document) {
    if (RequireString(document, "format") != kUnitFormat ||
        RequireInt32(document, "schema_version") != 1) {
        throw std::runtime_error("unsupported room compilation unit format");
    }
    RoomCompilationUnit result;
    result.status = RequireString(document, "status");
    if (result.status.rfind("composition_complete_", 0) != 0) {
        throw std::runtime_error("runtime cannot consume a composition-incomplete unit");
    }
    const auto& identity = RequireObject(document, "identity");
    result.routeId = RequireString(identity, "route_id");
    result.unitId = RequireString(identity, "unit_id");
    result.sceneId = RequireInt32(identity, "scene_id");
    result.scenePath = RequireString(identity, "scene_path");
    result.setupIndex = RequireInt32(identity, "setup_index");
    result.initialRoomIndex = RequireInt32(identity, "initial_room_index");
    result.payloadSha256 = RequireString(identity, "payload_sha256");
    if (!IsSha256(result.payloadSha256)) {
        throw std::runtime_error("room compilation unit payload digest is invalid");
    }
    for (const auto& roomIndex : RequireArray(identity, "native_room_indices")) {
        if (!roomIndex.is_number_integer() && !roomIndex.is_number_unsigned()) {
            throw std::runtime_error("native room index is not an integer");
        }
        result.nativeRoomIndices.push_back(roomIndex.get<int32_t>());
    }

    const auto& sourceAssets = RequireObject(document, "source_assets");
    const auto& codeBin = RequireObject(sourceAssets, "code_bin");
    result.sourceCodeBinPath = RequireString(codeBin, "logical_path");
    result.sourceCodeBinSha256 = RequireString(codeBin, "sha256");
    if (!IsSha256(result.sourceCodeBinSha256)) {
        throw std::runtime_error("room compilation unit code.bin digest is invalid");
    }
    if (document.contains("room_lifecycle_contract")) {
        result.roomLifecycle =
            ParseRoomLifecycleContract(RequireObject(document, "room_lifecycle_contract"));
    }
    if (document.contains("room_callback_contract") &&
        !RequireObject(document, "room_callback_contract").empty()) {
        result.roomCallbacks =
            ParseRoomCallbackContract(RequireObject(document, "room_callback_contract"));
    }
    if (document.contains("indirect_native_root_catalog")) {
        result.indirectRootCatalog = ParseIndirectRootCatalog(
            RequireObject(document, "indirect_native_root_catalog"));
    }
    if (document.contains("native_abi_catalog") &&
        document["native_abi_catalog"].is_object()) {
        result.nativeAbiCatalog = ParseNativeAbiCatalogSummary(
            RequireObject(document, "native_abi_catalog"));
    }

    const auto& sceneSetup = RequireObject(document, "scene_setup");
    for (const auto& commandJson : RequireArray(sceneSetup, "commands")) {
        RoomCompilationSceneCommand command;
        const uint32_t commandId = RequireUint32(commandJson, "command_id");
        const uint32_t parameter = RequireUint32(commandJson, "parameter");
        command.commandWord = RequireUint32(commandJson, "command_word");
        command.argument = RequireUint32(commandJson, "argument");
        if (commandId > std::numeric_limits<uint8_t>::max() ||
            parameter > std::numeric_limits<uint8_t>::max() ||
            commandId != (command.commandWord & 0xFF) ||
            parameter != ((command.commandWord >> 8) & 0xFF)) {
            throw std::runtime_error("invalid native scene command header");
        }
        command.commandId = static_cast<uint8_t>(commandId);
        command.parameter = static_cast<uint8_t>(parameter);
        if (command.commandId == 0x15) {
            if (result.sceneAudio.available) {
                throw std::runtime_error("duplicate native scene sound settings");
            }
            result.sceneAudio.available = true;
            result.sceneAudio.soundSpecId = command.parameter;
            result.sceneAudio.natureAmbienceId =
                static_cast<uint8_t>((command.commandWord >> 16) & 0xFF);
            result.sceneAudio.bgmSoundId = command.argument;
            result.sceneAudio.commandWord = command.commandWord;
        }
        result.sceneCommands.push_back(command);
    }
    if (result.sceneCommands.empty() || result.sceneCommands.size() > 0x20 ||
        result.sceneCommands.back().commandId != 0x14) {
        throw std::runtime_error("native scene command stream lacks its end marker");
    }

    const auto& entrypoint = RequireObject(document, "entrypoint");
    const auto& playerEntry = RequireObject(entrypoint, "entry");
    result.entrypoint.globalEntranceIndex = RequireInt32(entrypoint, "global_entrance_index");
    result.entrypoint.localEntranceIndex = RequireInt32(entrypoint, "local_entrance_index");
    result.entrypoint.spawnIndex = RequireInt32(entrypoint, "spawn_index");
    result.entrypoint.roomIndex = RequireInt32(entrypoint, "room_index");
    result.entrypoint.actorId = RequireInt32(playerEntry, "actor_id");
    result.entrypoint.position = RequireVec3s(playerEntry, "pos");
    result.entrypoint.rotation = RequireVec3s(playerEntry, "rot");
    result.entrypoint.params = static_cast<uint16_t>(RequireRawInt16(playerEntry, "params"));

    std::set<int32_t> roomIndices;
    for (const auto& roomJson : RequireArray(document, "rooms")) {
        RoomCompilationRoom room;
        room.roomIndex = RequireInt32(roomJson, "room_index");
        room.setupIndex = RequireInt32(roomJson, "setup_index");
        room.initiallyActive = OptionalBool(roomJson, "initially_active");
        if (room.setupIndex != result.setupIndex) {
            throw std::runtime_error("compiled room setup does not match unit setup");
        }
        if (!roomIndices.insert(room.roomIndex).second) {
            throw std::runtime_error("duplicate room in room compilation unit");
        }
        if (roomJson.contains("source") && roomJson["source"].is_object()) {
            room.sourcePath = OptionalString(roomJson["source"], "logical_path");
        }
        for (const auto& commandJson : RequireArray(roomJson, "commands")) {
            RoomCompilationSceneCommand command;
            const uint32_t commandId = RequireUint32(commandJson, "command_id");
            const uint32_t parameter = RequireUint32(commandJson, "parameter");
            command.commandWord = RequireUint32(commandJson, "command_word");
            command.argument = RequireUint32(commandJson, "argument");
            if (commandId > std::numeric_limits<uint8_t>::max() ||
                parameter > std::numeric_limits<uint8_t>::max() ||
                commandId != (command.commandWord & 0xFF) ||
                parameter != ((command.commandWord >> 8) & 0xFF)) {
                throw std::runtime_error("invalid native room command header");
            }
            command.commandId = static_cast<uint8_t>(commandId);
            command.parameter = static_cast<uint8_t>(parameter);
            room.commands.push_back(command);
        }
        if (room.commands.empty() || room.commands.size() > 0x20 ||
            room.commands.back().commandId != 0x14) {
            throw std::runtime_error("native room command stream lacks its end marker");
        }
        for (const auto& meshJson : RequireArray(roomJson, "mesh_resources")) {
            room.meshNames.push_back(RequireString(meshJson, "name"));
        }
        const auto& objectList = RequireObject(roomJson, "object_list");
        for (const char* key : {"entries", "unknown_entries"}) {
            for (const auto& objectJson : RequireArray(objectList, key)) {
                room.objectIds.push_back(RequireInt32(objectJson, "object_id"));
            }
        }
        result.rooms.push_back(std::move(room));
    }

    std::unordered_set<std::string> instanceKeys;
    for (const auto& actorJson : RequireArray(document, "actor_instances")) {
        auto actor = ParseActorEntry(actorJson);
        if (!instanceKeys.insert(actor.instanceKey).second) {
            throw std::runtime_error("duplicate actor instance in room compilation unit");
        }
        const auto assignToRoom = [&](int32_t roomIndex) {
            auto room = std::find_if(
                result.rooms.begin(), result.rooms.end(),
                [roomIndex](const auto& candidate) { return candidate.roomIndex == roomIndex; });
            if (room == result.rooms.end()) {
                throw std::runtime_error("actor instance refers to an unknown room");
            }
            if (std::find(room->actorInstanceKeys.begin(), room->actorInstanceKeys.end(),
                          actor.instanceKey) == room->actorInstanceKeys.end()) {
                room->actorInstanceKeys.push_back(actor.instanceKey);
            }
        };
        if (actor.transition) {
            assignToRoom(actor.frontRoomIndex);
            assignToRoom(actor.backRoomIndex);
        } else {
            assignToRoom(actor.roomIndex);
        }
        result.actorInstances.push_back(std::move(actor));
    }

    std::unordered_map<std::string, std::pair<int32_t, int32_t>> profileIdentities;
    for (const auto& profileJson : RequireArray(document, "actor_profiles")) {
        auto profile = ParseActorProfile(profileJson);
        if (!profileIdentities
                 .emplace(profile.profileKey, std::pair(profile.actorId, profile.objectId))
                 .second) {
            throw std::runtime_error("duplicate actor profile in room compilation unit");
        }
        result.actorProfiles.push_back(std::move(profile));
    }
    for (const auto& actor : result.actorInstances) {
        const auto profile = profileIdentities.find(actor.profileKey);
        if (profile == profileIdentities.end()) {
            throw std::runtime_error("actor instance refers to an unknown profile");
        }
        const int32_t profileActorId =
            actor.transition && result.roomLifecycle.available
                ? actor.actorId &
                      static_cast<int32_t>(result.roomLifecycle.transitionActorIdSpawnMask)
                : actor.actorId;
        if (profile->second.first != profileActorId) {
            throw std::runtime_error("actor instance id does not match its profile");
        }
    }

    std::unordered_set<int32_t> dependencyIds;
    for (const auto& dependencyJson : RequireArray(document, "object_dependencies")) {
        RoomCompilationObjectDependency dependency;
        dependency.objectId = RequireInt32(dependencyJson, "object_id");
        dependency.payloadStatus = RequireString(dependencyJson, "payload_status");
        if (!dependencyIds.insert(dependency.objectId).second) {
            throw std::runtime_error("duplicate object dependency in room compilation unit");
        }
        if (dependencyJson.contains("native_path_record") &&
            dependencyJson["native_path_record"].is_object()) {
            dependency.logicalPath =
                OptionalString(dependencyJson["native_path_record"], "logical_path");
        }
        if (dependency.logicalPath.empty() && dependencyJson.contains("source") &&
            dependencyJson["source"].is_object()) {
            dependency.logicalPath = OptionalString(dependencyJson["source"], "logical_path");
        }
        if (dependencyJson.contains("required_by")) {
            for (const auto& owner : RequireArray(dependencyJson, "required_by")) {
                if (!owner.is_string()) {
                    throw std::runtime_error("object dependency owner is not a string");
                }
                dependency.requiredBy.push_back(owner.get<std::string>());
            }
        }
        result.objectDependencies.push_back(std::move(dependency));
    }
    for (const auto& profile : result.actorProfiles) {
        if (dependencyIds.count(profile.objectId) == 0) {
            throw std::runtime_error("actor profile refers to an undeclared object dependency");
        }
    }
    for (const auto& room : result.rooms) {
        for (const int32_t objectId : room.objectIds) {
            if (dependencyIds.count(objectId) == 0) {
                throw std::runtime_error("room object list refers to an undeclared dependency");
            }
        }
    }
    for (const auto& operation : result.roomCallbacks.materialTevAlphaOperations) {
        if (roomIndices.count(operation.roomIndex) == 0) {
            throw std::runtime_error(
                "room callback operation targets an undeclared room");
        }
    }

    const auto& closure = RequireObject(document, "closure");
    if (RequireString(closure, "composition_status") != "composition_complete") {
        throw std::runtime_error("runtime unit composition is not complete");
    }
    result.behaviorEvidenceStatus = RequireString(closure, "behavior_evidence_status");
    result.unresolvedCount = RequireUint32(closure, "unresolved_count");
    result.behaviorGraphProfileCount =
        RequireUint32(closure, "behavior_graph_profile_count");
    result.behaviorFunctionCount = RequireUint32(closure, "behavior_function_count");
    result.behaviorActionTransitionCount =
        RequireUint32(closure, "behavior_action_transition_count");
    result.behaviorStructureFieldCount =
        RequireUint32(closure, "behavior_structure_field_count");
    result.behaviorIndirectRootCount =
        closure.contains("behavior_indirect_root_count")
            ? RequireUint32(closure, "behavior_indirect_root_count")
            : 0;
    result.behaviorNativeAbiFunctionCount =
        closure.contains("behavior_native_abi_function_count")
            ? RequireUint32(closure, "behavior_native_abi_function_count")
            : 0;
    result.behaviorConsumerRootCount =
        closure.contains("behavior_consumer_root_count")
            ? RequireUint32(closure, "behavior_consumer_root_count")
            : 0;
    result.behaviorConsumerFunctionCount =
        closure.contains("behavior_consumer_function_count")
            ? RequireUint32(closure, "behavior_consumer_function_count")
            : 0;
    result.behaviorConsumerCallEdgeCount =
        closure.contains("behavior_consumer_call_edge_count")
            ? RequireUint32(closure, "behavior_consumer_call_edge_count")
            : 0;
    result.behaviorActorLocalConsumerFunctionCount =
        closure.contains("behavior_actor_local_consumer_function_count")
            ? RequireUint32(
                  closure, "behavior_actor_local_consumer_function_count")
            : 0;
    result.behaviorNativeServiceDependencyCount =
        closure.contains("behavior_native_service_dependency_count")
            ? RequireUint32(closure, "behavior_native_service_dependency_count")
            : 0;
    uint32_t observedBehaviorGraphProfileCount = 0;
    uint32_t observedBehaviorFunctionCount = 0;
    uint32_t observedBehaviorActionTransitionCount = 0;
    uint32_t observedBehaviorStructureFieldCount = 0;
    uint32_t observedBehaviorIndirectRootCount = 0;
    uint32_t observedBehaviorNativeAbiFunctionCount = 0;
    uint32_t observedBehaviorConsumerRootCount = 0;
    uint32_t observedBehaviorConsumerFunctionCount = 0;
    uint32_t observedBehaviorConsumerCallEdgeCount = 0;
    uint32_t observedBehaviorActorLocalConsumerFunctionCount = 0;
    uint32_t observedBehaviorNativeServiceDependencyCount = 0;
    for (const auto& profile : result.actorProfiles) {
        observedBehaviorGraphProfileCount += profile.behaviorGraph.available ? 1u : 0u;
        observedBehaviorFunctionCount +=
            static_cast<uint32_t>(profile.behaviorGraph.functions.size());
        observedBehaviorActionTransitionCount +=
            static_cast<uint32_t>(profile.behaviorGraph.actionTransitions.size());
        observedBehaviorStructureFieldCount +=
            static_cast<uint32_t>(profile.behaviorGraph.structureFields.size());
        observedBehaviorIndirectRootCount += profile.behaviorGraph.indirectRootCount;
        observedBehaviorNativeAbiFunctionCount +=
            profile.behaviorGraph.nativeAbiFunctionCount;
        observedBehaviorConsumerRootCount +=
            profile.behaviorGraph.consumerRootCount;
        observedBehaviorConsumerFunctionCount +=
            profile.behaviorGraph.consumerFunctionCount;
        observedBehaviorConsumerCallEdgeCount +=
            profile.behaviorGraph.consumerCallEdgeCount;
        observedBehaviorActorLocalConsumerFunctionCount +=
            profile.behaviorGraph.actorLocalConsumerFunctionCount;
        observedBehaviorNativeServiceDependencyCount +=
            profile.behaviorGraph.nativeServiceDependencyCount;
    }
    if (RequireUint32(closure, "room_count") != result.rooms.size() ||
        RequireUint32(closure, "actor_instance_count") != result.actorInstances.size() ||
        RequireUint32(closure, "unique_actor_profile_count") != result.actorProfiles.size() ||
        RequireUint32(closure, "object_dependency_count") != result.objectDependencies.size() ||
        result.behaviorGraphProfileCount != observedBehaviorGraphProfileCount ||
        result.behaviorFunctionCount != observedBehaviorFunctionCount ||
        result.behaviorActionTransitionCount !=
            observedBehaviorActionTransitionCount ||
        result.behaviorStructureFieldCount != observedBehaviorStructureFieldCount ||
        result.behaviorIndirectRootCount != observedBehaviorIndirectRootCount ||
        result.behaviorNativeAbiFunctionCount !=
            observedBehaviorNativeAbiFunctionCount ||
        result.behaviorConsumerRootCount !=
            observedBehaviorConsumerRootCount ||
        result.behaviorConsumerFunctionCount !=
            observedBehaviorConsumerFunctionCount ||
        result.behaviorConsumerCallEdgeCount !=
            observedBehaviorConsumerCallEdgeCount ||
        result.behaviorActorLocalConsumerFunctionCount !=
            observedBehaviorActorLocalConsumerFunctionCount ||
        result.behaviorNativeServiceDependencyCount !=
            observedBehaviorNativeServiceDependencyCount ||
        RequireArray(document, "unresolved").size() != result.unresolvedCount) {
        throw std::runtime_error("room compilation unit closure counts do not match");
    }
    if (result.indirectRootCatalog.available &&
        result.indirectRootCatalog.profileBoundRootCount !=
            result.behaviorIndirectRootCount) {
        throw std::runtime_error(
            "room compilation indirect-root catalog differs from profile bindings");
    }
    if (result.nativeAbiCatalog.available &&
        (result.nativeAbiCatalog.profileBoundFunctionCount !=
             result.behaviorNativeAbiFunctionCount ||
         result.nativeAbiCatalog.sourceSnapshotId !=
             result.indirectRootCatalog.sourceSnapshotId ||
         result.nativeAbiCatalog.codeBinSha256 != result.sourceCodeBinSha256)) {
        throw std::runtime_error(
            "room compilation native ABI catalog differs from profile bindings");
    }
    const auto initiallyActiveRooms =
        std::count_if(result.rooms.begin(), result.rooms.end(),
                      [](const auto& room) { return room.initiallyActive; });
    if (roomIndices.count(result.initialRoomIndex) == 0 ||
        initiallyActiveRooms != 1 || !result.FindRoom(result.initialRoomIndex)->initiallyActive ||
        result.entrypoint.roomIndex != result.initialRoomIndex) {
        throw std::runtime_error("room compilation unit initial room is inconsistent");
    }
    const std::set<int32_t> declaredRooms(result.nativeRoomIndices.begin(),
                                          result.nativeRoomIndices.end());
    if (declaredRooms != roomIndices) {
        throw std::runtime_error(
            "room compilation unit native room identity does not match its rooms");
    }

    const auto playerInstances =
        std::count_if(result.actorInstances.begin(), result.actorInstances.end(),
                      [](const auto& actor) { return actor.sourceKind == "scene_spawn"; });
    if (playerInstances != 1) {
        throw std::runtime_error("room compilation unit must contain one scene spawn instance");
    }
    if (result.roomLifecycle.available) {
        const auto transitionActorCount =
            std::count_if(result.actorInstances.begin(), result.actorInstances.end(),
                          [](const auto& actor) { return actor.transition; });
        const uint32_t expectedBufferCount = transitionActorCount == 0
                                                 ? result.roomLifecycle.bufferCountWithoutTransitionActors
                                                 : result.roomLifecycle.bufferCountWithTransitionActors;
        if (result.roomLifecycle.initialBufferCount != expectedBufferCount) {
            throw std::runtime_error(
                "room lifecycle buffer count does not match transition actor presence");
        }
    }
    const auto player =
        std::find_if(result.actorInstances.begin(), result.actorInstances.end(),
                     [](const auto& actor) { return actor.sourceKind == "scene_spawn"; });
    if (player->actorId != result.entrypoint.actorId ||
        player->sourceIndex != result.entrypoint.spawnIndex ||
        player->roomIndex != result.entrypoint.roomIndex ||
        !VecMatches(player->position, result.entrypoint.position) ||
        !VecMatches(player->rotation, result.entrypoint.rotation) ||
        static_cast<uint16_t>(player->params) != result.entrypoint.params) {
        throw std::runtime_error("room compilation unit scene spawn does not match its entrypoint");
    }
    return result;
}

void ValidateCatalogRecord(const RoomCompilationUnit& unit, const CatalogRecord& record) {
    if (unit.routeId != record.routeId || unit.unitId != record.unitId ||
        unit.sceneId != record.sceneId || unit.scenePath != record.scenePath ||
        unit.setupIndex != record.setupIndex || unit.initialRoomIndex != record.initialRoomIndex ||
        unit.status != record.status || unit.payloadSha256 != record.payloadSha256 ||
        unit.rooms.size() != record.roomCount ||
        unit.actorInstances.size() != record.actorInstanceCount ||
        unit.actorProfiles.size() != record.actorProfileCount ||
        unit.objectDependencies.size() != record.objectDependencyCount ||
        unit.unresolvedCount != record.unresolvedCount) {
        throw std::runtime_error("room compilation unit does not match its catalog record");
    }
}

} // namespace

const RoomCompilationActorCallback* RoomCompilationActorProfile::FindCallback(
    std::string_view slot) const {
    const auto result =
        std::find_if(callbacks.begin(), callbacks.end(),
                     [slot](const auto& callback) { return callback.slot == slot; });
    return result == callbacks.end() ? nullptr : &*result;
}

const RoomCompilationActorBehaviorFunction*
RoomCompilationActorBehaviorGraph::FindFunction(std::string_view name) const {
    const auto result =
        std::find_if(functions.begin(), functions.end(), [name](const auto& function) {
            return function.name == name;
        });
    return result == functions.end() ? nullptr : &*result;
}

const RoomCompilationLifecycleFunction* RoomCompilationLifecycleContract::FindFunction(
    std::string_view role) const {
    const auto result =
        std::find_if(functions.begin(), functions.end(),
                     [role](const auto& function) { return function.role == role; });
    return result == functions.end() ? nullptr : &*result;
}

const RoomCompilationRoom* RoomCompilationUnit::FindRoom(int32_t roomIndex) const {
    const auto result = std::find_if(rooms.begin(), rooms.end(), [roomIndex](const auto& room) {
        return room.roomIndex == roomIndex;
    });
    return result == rooms.end() ? nullptr : &*result;
}

const RoomCompilationActorProfile* RoomCompilationUnit::FindActorProfile(
    std::string_view profileKey) const {
    const auto result =
        std::find_if(actorProfiles.begin(), actorProfiles.end(), [profileKey](const auto& profile) {
            return profile.profileKey == profileKey;
        });
    return result == actorProfiles.end() ? nullptr : &*result;
}

const RoomCompilationObjectDependency* RoomCompilationUnit::FindObjectDependency(
    int32_t objectId) const {
    const auto result = std::find_if(
        objectDependencies.begin(), objectDependencies.end(),
        [objectId](const auto& dependency) { return dependency.objectId == objectId; });
    return result == objectDependencies.end() ? nullptr : &*result;
}

const RoomCompilationUnit* RoomCompilationUnitCatalog::Find(std::string_view routeId,
                                                            int32_t setupIndex) const {
    const auto result = std::find_if(units.begin(), units.end(), [&](const auto& unit) {
        return unit.routeId == routeId && unit.setupIndex == setupIndex;
    });
    return result == units.end() ? nullptr : &*result;
}

RoomCompilationUnit ParseRoomCompilationUnit(std::string_view payload) {
    if (payload.empty()) {
        throw std::runtime_error("room compilation unit payload is empty");
    }
    return ParseUnitDocument(nlohmann::json::parse(payload));
}

RoomCompilationUnit LoadRoomCompilationUnitFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open room compilation unit: " + path.string());
    }
    return ParseUnitDocument(nlohmann::json::parse(stream));
}

RoomCompilationUnitCatalog ParseRoomCompilationUnitCatalog(
    std::string_view catalogPayload, const RoomCompilationPayloadLoader& payloadLoader) {
    if (catalogPayload.empty() || !payloadLoader) {
        throw std::runtime_error("room compilation unit catalog input is incomplete");
    }
    const auto catalog = nlohmann::json::parse(catalogPayload);
    if (RequireString(catalog, "format") != kCatalogFormat ||
        RequireInt32(catalog, "schema_version") != 1 ||
        RequireString(catalog, "status") != "complete") {
        throw std::runtime_error("unsupported room compilation unit catalog");
    }
    const auto& recordsJson = RequireArray(catalog, "records");
    const auto& excludedJson = RequireArray(catalog, "excluded");
    if (RequireUint32(catalog, "unit_count") != recordsJson.size() ||
        RequireUint32(catalog, "excluded_unit_count") != excludedJson.size()) {
        throw std::runtime_error("room compilation unit catalog counts do not match");
    }

    RoomCompilationUnitCatalog result;
    result.excludedUnitCount = static_cast<uint32_t>(excludedJson.size());
    result.units.reserve(recordsJson.size());
    std::set<std::pair<std::string, int32_t>> routeSetups;
    for (const auto& recordJson : recordsJson) {
        const auto record = ParseCatalogRecord(recordJson);
        if (!routeSetups.emplace(record.routeId, record.setupIndex).second) {
            throw std::runtime_error("duplicate route/setup in room compilation unit catalog");
        }
        auto unit = ParseRoomCompilationUnit(payloadLoader(record.resourcePath));
        ValidateCatalogRecord(unit, record);
        unit.resourcePath = record.resourcePath;
        result.units.push_back(std::move(unit));
    }
    return result;
}

} // namespace Oot3d
