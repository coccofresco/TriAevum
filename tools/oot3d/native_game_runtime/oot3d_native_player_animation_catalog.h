#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

struct PlayerAnimationBinding {
    uint32_t GroupIndex = 0;
    uint32_t AnimationTypeIndex = 0;
    uint32_t CsabTypeLocalIndex = 0;
    std::string CsabName;
};

struct PlayerDirectAnimationBinding {
    uint32_t CsabTypeLocalIndex = 0;
    std::string CsabName;
};

struct PlayerDirectSemanticAnimationBinding {
    std::string Semantic;
    PlayerDirectAnimationBinding Binding;
};

class PlayerAnimationCatalog {
  public:
    static PlayerAnimationCatalog LoadFile(const std::filesystem::path& path);

    const std::filesystem::path& CodeBinPath() const;
    const std::filesystem::path& ActorArchivePath() const;
    uint32_t GroupCount() const;
    uint32_t AnimationTypeCount() const;
    PlayerAnimationBinding Resolve(uint32_t groupIndex, uint32_t animationTypeIndex) const;
    std::vector<PlayerAnimationBinding> ResolveAll() const;
    PlayerAnimationBinding ResolveSemantic(std::string_view semanticGroup,
                                           uint32_t animationTypeIndex) const;
    PlayerDirectAnimationBinding ResolveDirectSemantic(std::string_view semanticClip) const;
    std::vector<PlayerDirectSemanticAnimationBinding> ResolveAllDirectSemantic() const;

  private:
    struct Group {
        uint32_t Index = 0;
        uint32_t RuntimeAddress = 0;
        std::vector<uint32_t> CsabTypeLocalIndices;
        std::vector<std::string> CsabNames;
    };

    std::filesystem::path mCodeBinPath;
    std::filesystem::path mActorArchivePath;
    uint32_t mAnimationTypeCount = 0;
    std::vector<Group> mGroups;
    std::unordered_map<std::string, uint32_t> mSemanticGroups;
    std::unordered_map<std::string, PlayerDirectAnimationBinding> mDirectSemanticClips;
};

} // namespace Oot3dNativeGame
