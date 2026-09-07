#pragma once

#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "oot3d_native_pica_shader_gen.h"
#include "oot3d_native_pica_submission.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

enum class Oot3dPicaVertexInputRate : uint8_t {
    PerVertex,
    PerInstance,
};

struct Oot3dPicaVulkanVertexBinding {
    uint8_t Binding = 0;
    uint16_t ByteStride = 0;
    Oot3dPicaVertexInputRate InputRate =
        Oot3dPicaVertexInputRate::PerVertex;
    std::vector<uint8_t> Bytes;
    std::shared_ptr<const std::vector<uint8_t>> SharedBytes;
    uint32_t SourcePhysicalAddress = 0;
    uint64_t ContentVersion = 0;
    bool ContentVersionAvailable = false;

    [[nodiscard]] std::span<const uint8_t> ResolvedBytes() const {
      return SharedBytes != nullptr ? std::span<const uint8_t>(*SharedBytes)
                                    : std::span<const uint8_t>(Bytes);
    }

    std::vector<uint8_t> &MutableBytes() {
      if (SharedBytes != nullptr) {
        Bytes.assign(SharedBytes->begin(), SharedBytes->end());
        SharedBytes.reset();
      }
      ContentVersion = 0;
      ContentVersionAvailable = false;
      return Bytes;
    }
};

struct Oot3dPicaVulkanVertexAttribute {
    uint8_t Location = 0;
    uint8_t Binding = 0;
    Oot3dPicaVertexFormat Format = Oot3dPicaVertexFormat::Float;
    uint8_t ComponentCount = 4;
    uint16_t ByteOffset = 0;
};

struct Oot3dPicaVulkanTextureBinding {
    uint8_t Slot = 0;
    Oot3dPicaTextureState State;
    std::vector<uint8_t> NativeBytes;
    std::shared_ptr<const std::vector<uint8_t>> SharedNativeBytes;
    uint64_t NativeContentHash = 0;
    bool NativeContentHashAvailable = false;
    uint64_t NativeBaseLevelContentHash = 0;
    bool NativeBaseLevelContentHashAvailable = false;

    [[nodiscard]] std::span<const uint8_t> ResolvedNativeBytes() const {
        return SharedNativeBytes != nullptr
                   ? std::span<const uint8_t>(*SharedNativeBytes)
                   : std::span<const uint8_t>(NativeBytes);
    }
};

bool ResolveOot3dPicaTextureContentIdentity(
    Oot3dPicaVulkanTextureBinding& texture, std::string* error = nullptr);

struct Oot3dPicaVulkanDrawPlan {
    uint64_t SubmissionId = 0;
    uint32_t CommandListAddress = 0;
    uint32_t CommandListOffsetWords = 0;
    Oot3dPicaCompositionDomain CompositionDomain =
        Oot3dPicaCompositionDomain::Unknown;
    Oot3dPicaCompositionAttribution Composition;
    Oot3dPicaCanonicalDrawIdentity CanonicalIdentity;
    Oot3dPicaFragmentFeatureSet FragmentFeatures;
    Oot3dPicaGeneratedVertexShader VertexShader;
    Oot3dPicaGeneratedFragmentShader FragmentShader;
    std::shared_ptr<const std::string> SharedVertexShaderSource;
    std::shared_ptr<const std::string> SharedFragmentShaderSource;
    Oot3dPicaDecodedDrawState State;
    std::vector<Oot3dPicaVulkanVertexBinding> VertexBindings;
    std::vector<Oot3dPicaVulkanVertexAttribute> VertexAttributes;
    std::vector<uint8_t> IndexBytes;
    std::shared_ptr<const std::vector<uint8_t>> SharedIndexBytes;
    uint32_t IndexPhysicalAddress = 0;
    uint64_t IndexContentVersion = 0;
    bool IndexContentVersionAvailable = false;
    bool Indexed = false;
    bool IndicesAre16Bit = false;
    int32_t BaseVertex = 0;
    uint32_t VertexCount = 0;
    std::vector<Oot3dPicaVulkanTextureBinding> Textures;
    std::shared_ptr<const Oot3dPicaLightingLutState> LightingLuts;
    uint64_t GeometryIdentity = 0;
    uint64_t GeometryContentVersion = 0;
    bool GeometryIdentityAvailable = false;

    [[nodiscard]]

    std::span<const uint8_t> ResolvedIndexBytes() const {
      return SharedIndexBytes != nullptr
                 ? std::span<const uint8_t>(*SharedIndexBytes)
                 : std::span<const uint8_t>(IndexBytes);
    }

    std::string_view ResolvedVertexShaderSource() const {
      return SharedVertexShaderSource != nullptr
                 ? std::string_view(*SharedVertexShaderSource)
                 : std::string_view(VertexShader.Source);
    }

    std::string_view ResolvedFragmentShaderSource() const {
        return SharedFragmentShaderSource != nullptr
                   ? std::string_view(*SharedFragmentShaderSource)
                   : std::string_view(FragmentShader.Source);
    }
};

struct Oot3dPicaVulkanDrawOverrides {
    const Oot3dPicaVertexUniformState* VertexUniforms = nullptr;
    const Oot3dPicaFragmentUniformState* FragmentUniforms = nullptr;
    const Oot3dPicaViewportState* Viewport = nullptr;
    const std::array<float, 4>* BlendConstantColor = nullptr;
    const std::vector<Oot3dPicaVulkanVertexBinding>* VertexBindings = nullptr;
};

struct Oot3dPicaVulkanCachedShaderSource {
    std::shared_ptr<const std::string> Source;
    Oot3d::Renderer::PicaShaderSourceIdentity Identity;
};

struct Oot3dPicaVulkanVertexStructuralStateKey {
    uint64_t VertexProgramMutationIdentity = 0;
    uint64_t VertexSwizzleMutationIdentity = 0;
    uint64_t GeometryProgramMutationIdentity = 0;
    uint64_t GeometrySwizzleMutationIdentity = 0;
    size_t VertexProgramWordCount = 0;
    size_t VertexSwizzleWordCount = 0;
    size_t GeometryProgramWordCount = 0;
    size_t GeometrySwizzleWordCount = 0;
    // 0x04f..0x056 plus shader-mode/input-map registers 0x229,
    // 0x2b9, 0x2ba and 0x2bd. These are the complete non-bytecode inputs
    // used by the canonical vertex-program identity.
    std::array<uint32_t, 12> InterfaceWords{};
    bool operator==(
        const Oot3dPicaVulkanVertexStructuralStateKey&) const = default;
};

struct Oot3dPicaVulkanVertexStructuralStateKeyHash {
    size_t operator()(
        const Oot3dPicaVulkanVertexStructuralStateKey& key) const noexcept {
        size_t hash = 1469598103934665603ULL;
        const auto append = [&](uint64_t value) {
            hash ^= static_cast<size_t>(value);
            hash *= static_cast<size_t>(1099511628211ULL);
        };
        append(key.VertexProgramMutationIdentity);
        append(key.VertexSwizzleMutationIdentity);
        append(key.GeometryProgramMutationIdentity);
        append(key.GeometrySwizzleMutationIdentity);
        append(key.VertexProgramWordCount);
        append(key.VertexSwizzleWordCount);
        append(key.GeometryProgramWordCount);
        append(key.GeometrySwizzleWordCount);
        for (const uint32_t word : key.InterfaceWords) {
            append(word);
        }
        return hash;
    }
};

struct Oot3dPicaVulkanCachedVertexStructuralState {
    uint64_t ShaderStateKey = 0;
    uint64_t CanonicalProgramId = 0;
};

struct Oot3dPicaVulkanShaderSourceCache {
    std::unordered_map<uint64_t, Oot3dPicaVulkanCachedShaderSource>
        VertexSources;
    std::unordered_map<uint64_t, Oot3dPicaVulkanCachedShaderSource>
        FragmentSources;
    std::unordered_map<
        uint64_t,
        std::shared_ptr<const Oot3dPicaTemporalVertexProgram>>
        VertexTemporalPrograms;
    std::unordered_map<uint64_t, Oot3d::Renderer::PicaShaderHookLayout>
        FragmentHooks;
    // Exact key equality makes the shortcut collision-safe; the public
    // deterministic FNV identities remain unchanged even if the host hash
    // function collides.
    std::unordered_map<
        Oot3dPicaVulkanVertexStructuralStateKey,
        Oot3dPicaVulkanCachedVertexStructuralState,
        Oot3dPicaVulkanVertexStructuralStateKeyHash>
        VertexStructuralStates;
    uint64_t VertexHits = 0;
    uint64_t VertexMisses = 0;
    uint64_t FragmentHits = 0;
    uint64_t FragmentMisses = 0;
};

struct Oot3dPicaVertexSemanticLocations {
    uint8_t Position = 0xff;
    uint8_t TexCoord0 = 0xff;
};

// CMB SEPD/VATR stores position first and UV0 after the optional normal and
// color streams. Resolve those asset semantics through the live PICA
// attribute-to-input map instead of assuming fixed shader input locations.
Oot3dPicaVertexSemanticLocations ResolveOot3dCmbVertexSemanticLocations(
    const Oot3dPicaDecodedDrawState& state);

bool BuildOot3dPicaVulkanDrawPlan(
    const Oot3dPicaDrawSubmission& submission,
    Oot3dPicaVulkanDrawPlan& plan,
    std::string* error = nullptr,
    Oot3dPicaVulkanShaderSourceCache* shaderCache = nullptr);

bool BuildOot3dPicaVulkanDrawPlanAndConsumeResources(
    Oot3dPicaDrawSubmission& submission,
    Oot3dPicaVulkanDrawPlan& plan,
    std::string* error = nullptr,
    Oot3dPicaVulkanShaderSourceCache* shaderCache = nullptr);

} // namespace Oot3dNativeGame
