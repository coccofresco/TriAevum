#include "oot3d_native_player_animation_catalog.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

uint32_t ReadU32(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) ||
        (!object.at(key).is_number_unsigned() && !object.at(key).is_number_integer())) {
        throw std::runtime_error(std::string("player animation contract has invalid ") + key);
    }
    const auto value = object.at(key).get<int64_t>();
    if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("player animation contract overflows ") + key);
    }
    return static_cast<uint32_t>(value);
}

std::filesystem::path ReadSourcePath(const nlohmann::json& source, const char* key) {
    if (!source.contains(key) || !source.at(key).is_string()) {
        throw std::runtime_error(std::string("player animation contract has no source ") + key);
    }
    const std::filesystem::path path = source.at(key).get<std::string>();
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("player animation contract source is missing: " + path.string());
    }
    return path;
}

} // namespace

PlayerAnimationCatalog PlayerAnimationCatalog::LoadFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open player animation contract: " + path.string());
    }
    nlohmann::json document;
    stream >> document;
    if (document.value("format", "") != "oot3d_player_animation_group_native_contract_v1" ||
        document.value("status", "") != "ready") {
        throw std::runtime_error("unsupported or incomplete player animation contract");
    }

    PlayerAnimationCatalog result;
    const auto& source = document.at("source");
    result.mCodeBinPath = ReadSourcePath(source, "code_bin");
    result.mActorArchivePath = ReadSourcePath(source, "actor_zar");

    const auto& table = document.at("table");
    const uint32_t declaredGroupCount = ReadU32(table, "group_count");
    result.mAnimationTypeCount = ReadU32(table, "animation_type_count");
    const uint32_t tableAddress = ReadU32(table, "runtime_address");
    if (declaredGroupCount == 0 || result.mAnimationTypeCount == 0 ||
        !document.contains("animation_groups") || !document.at("animation_groups").is_array() ||
        document.at("animation_groups").size() != declaredGroupCount) {
        throw std::runtime_error("player animation contract table dimensions do not match its groups");
    }

    result.mGroups.resize(declaredGroupCount);
    std::vector<bool> populated(declaredGroupCount, false);
    for (const auto& groupJson : document.at("animation_groups")) {
        const uint32_t index = ReadU32(groupJson, "group_index");
        if (index >= declaredGroupCount || populated[index]) {
            throw std::runtime_error("player animation contract has a duplicate or invalid group index");
        }
        auto& group = result.mGroups[index];
        group.Index = index;
        group.RuntimeAddress = ReadU32(groupJson, "runtime_address");
        const uint64_t expectedAddress = static_cast<uint64_t>(tableAddress) +
            static_cast<uint64_t>(index) * result.mAnimationTypeCount * sizeof(uint32_t);
        if (group.RuntimeAddress != expectedAddress) {
            throw std::runtime_error("player animation group address does not match the native table stride");
        }

        const auto& indices = groupJson.at("csab_type_local_indices");
        const auto& names = groupJson.at("csab_members");
        if (!indices.is_array() || !names.is_array() ||
            indices.size() != result.mAnimationTypeCount || names.size() != result.mAnimationTypeCount) {
            throw std::runtime_error("player animation group has the wrong native variant count");
        }
        for (uint32_t type = 0; type < result.mAnimationTypeCount; ++type) {
            if ((!indices[type].is_number_unsigned() && !indices[type].is_number_integer()) ||
                indices[type].get<int64_t>() < 0 || !names[type].is_string() ||
                names[type].get_ref<const std::string&>().empty()) {
                throw std::runtime_error("player animation group has an invalid CSAB binding");
            }
            group.CsabTypeLocalIndices.push_back(static_cast<uint32_t>(indices[type].get<int64_t>()));
            group.CsabNames.push_back(names[type].get<std::string>());
        }
        populated[index] = true;
    }

    if (!document.contains("semantic_groups") || !document.at("semantic_groups").is_array()) {
        throw std::runtime_error("player animation contract has no semantic groups");
    }
    for (const auto& semanticJson : document.at("semantic_groups")) {
        if (!semanticJson.contains("semantic_group") || !semanticJson.at("semantic_group").is_string() ||
            semanticJson.at("semantic_group").get_ref<const std::string&>().empty()) {
            throw std::runtime_error("player animation contract has an invalid semantic group name");
        }
        const auto& semanticName = semanticJson.at("semantic_group").get_ref<const std::string&>();
        const uint32_t groupIndex = ReadU32(semanticJson, "oot3d_group_index");
        if (groupIndex >= result.mGroups.size() ||
            !result.mSemanticGroups.emplace(semanticName, groupIndex).second) {
            throw std::runtime_error("player animation contract has a duplicate or invalid semantic group");
        }
        const auto& group = result.mGroups[groupIndex];
        if (ReadU32(semanticJson, "runtime_address") != group.RuntimeAddress ||
            semanticJson.at("csab_type_local_indices") != group.CsabTypeLocalIndices ||
            semanticJson.at("csab_members") != group.CsabNames) {
            throw std::runtime_error("semantic player animation group disagrees with its native table row");
        }
    }

    if (!document.contains("direct_semantic_clips") ||
        !document.at("direct_semantic_clips").is_array()) {
        throw std::runtime_error("player animation contract has no direct semantic clips");
    }
    for (const auto& clipJson : document.at("direct_semantic_clips")) {
        const std::string resolution = clipJson.value("resolution", "");
        if (!clipJson.contains("semantic_clip") || !clipJson.at("semantic_clip").is_string() ||
            !clipJson.contains("csab_member") || !clipJson.at("csab_member").is_string() ||
            (resolution != "unique_native_zar_member_stem" &&
             resolution != "native_child_age_record_index" &&
             resolution != "native_action_immediate_index")) {
            throw std::runtime_error("player animation contract has an invalid direct semantic clip");
        }
        const auto& semantic = clipJson.at("semantic_clip").get_ref<const std::string&>();
        const auto& member = clipJson.at("csab_member").get_ref<const std::string&>();
        if (semantic.empty() || member.empty() ||
            !result.mDirectSemanticClips.emplace(
                semantic,
                PlayerDirectAnimationBinding{
                    ReadU32(clipJson, "csab_type_local_index"), member }).second) {
            throw std::runtime_error("player animation contract has a duplicate direct semantic clip");
        }
    }
    return result;
}

const std::filesystem::path& PlayerAnimationCatalog::CodeBinPath() const {
    return mCodeBinPath;
}

const std::filesystem::path& PlayerAnimationCatalog::ActorArchivePath() const {
    return mActorArchivePath;
}

uint32_t PlayerAnimationCatalog::GroupCount() const {
    return static_cast<uint32_t>(mGroups.size());
}

uint32_t PlayerAnimationCatalog::AnimationTypeCount() const {
    return mAnimationTypeCount;
}

PlayerAnimationBinding PlayerAnimationCatalog::Resolve(uint32_t groupIndex,
                                                       uint32_t animationTypeIndex) const {
    if (groupIndex >= mGroups.size() || animationTypeIndex >= mAnimationTypeCount) {
        throw std::out_of_range("OOT3D player animation group or type is outside the native table");
    }
    const auto& group = mGroups[groupIndex];
    return {
        groupIndex,
        animationTypeIndex,
        group.CsabTypeLocalIndices[animationTypeIndex],
        group.CsabNames[animationTypeIndex],
    };
}

std::vector<PlayerAnimationBinding> PlayerAnimationCatalog::ResolveAll() const {
    std::vector<PlayerAnimationBinding> bindings;
    bindings.reserve(mGroups.size() * mAnimationTypeCount);
    for (uint32_t groupIndex = 0; groupIndex < mGroups.size(); ++groupIndex) {
        for (uint32_t animationTypeIndex = 0;
             animationTypeIndex < mAnimationTypeCount; ++animationTypeIndex) {
            bindings.push_back(Resolve(groupIndex, animationTypeIndex));
        }
    }
    return bindings;
}

PlayerAnimationBinding PlayerAnimationCatalog::ResolveSemantic(
    std::string_view semanticGroup, uint32_t animationTypeIndex) const {
    const auto found = mSemanticGroups.find(std::string(semanticGroup));
    if (found == mSemanticGroups.end()) {
        throw std::out_of_range("OOT3D player animation semantic group is not in the native contract");
    }
    return Resolve(found->second, animationTypeIndex);
}

PlayerDirectAnimationBinding PlayerAnimationCatalog::ResolveDirectSemantic(
    std::string_view semanticClip) const {
    const auto found = mDirectSemanticClips.find(std::string(semanticClip));
    if (found == mDirectSemanticClips.end()) {
        throw std::out_of_range("OOT3D direct player animation semantic is not in the native contract");
    }
    return found->second;
}

std::vector<PlayerDirectSemanticAnimationBinding>
PlayerAnimationCatalog::ResolveAllDirectSemantic() const {
    std::vector<PlayerDirectSemanticAnimationBinding> bindings;
    bindings.reserve(mDirectSemanticClips.size());
    for (const auto& [semantic, binding] : mDirectSemanticClips) {
        bindings.push_back({ semantic, binding });
    }
    std::sort(bindings.begin(), bindings.end(),
              [](const auto& left, const auto& right) {
                  return left.Semantic < right.Semantic;
              });
    return bindings;
}

} // namespace Oot3dNativeGame
