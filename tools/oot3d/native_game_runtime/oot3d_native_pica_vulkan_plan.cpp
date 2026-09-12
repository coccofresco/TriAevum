#include "oot3d_native_pica_vulkan_plan.h"

#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace Oot3dNativeGame {
namespace {

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

size_t FormatByteSize(Oot3dPicaVertexFormat format) {
    switch (format) {
    case Oot3dPicaVertexFormat::SignedByte:
    case Oot3dPicaVertexFormat::UnsignedByte:
        return 1U;
    case Oot3dPicaVertexFormat::SignedShort:
        return 2U;
    case Oot3dPicaVertexFormat::Float:
        return 4U;
    }
    return 0U;
}

size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

template <typename Submission>
auto FindResource(Submission& submission, Oot3dPicaResourceKind kind,
                  uint8_t slot = 0) -> decltype(submission.Resources.data()) {
    for (auto& resource : submission.Resources) {
        if (resource.Kind == kind && resource.Slot == slot) {
            return &resource;
        }
    }
    return nullptr;
}

void AppendFloat4(std::vector<uint8_t>& bytes,
                  const std::array<float, 4>& value) {
    const size_t offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, value.data(), sizeof(value));
}

constexpr uint64_t kGeometryFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kGeometryFnvPrime = 1099511628211ULL;

template <typename Value>
void HashGeometryValue(uint64_t &hash, const Value &value) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
  for (size_t index = 0; index < sizeof(Value); ++index) {
    hash = (hash ^ bytes[index]) * kGeometryFnvPrime;
  }
}

uint64_t HashGeometryBytes(std::span<const uint8_t> bytes) {
  uint64_t hash = kGeometryFnvOffset;
  for (const uint8_t byte : bytes) {
    hash = (hash ^ byte) * kGeometryFnvPrime;
  }
  return hash == 0U ? 1U : hash;
}

bool ResolveTextureContentIdentity(
    Oot3dPicaVulkanTextureBinding& texture, std::string* error) {
    const auto bytes = texture.ResolvedNativeBytes();
    if (bytes.empty()) {
        SetError(error, "native PICA texture identity has no payload");
        return false;
    }
    if (!texture.NativeContentHashAvailable) {
        texture.NativeContentHash = HashGeometryBytes(bytes);
        texture.NativeContentHashAvailable = true;
    }
    const auto baseLevelBytes =
        Oot3dPicaTextureMipLevelByteSize(texture.State, 0U);
    if (!baseLevelBytes.has_value() || *baseLevelBytes > bytes.size()) {
        SetError(error, "native PICA texture base mip is truncated");
        return false;
    }
    if (texture.NativeBaseLevelContentHashAvailable) {
        return true;
    }
    // The resource snapshot already owns an authoritative hash for the full
    // native payload.  With a single mip the base level is that same payload,
    // so avoid hashing every texture byte again for every draw plan.
    texture.NativeBaseLevelContentHash =
        *baseLevelBytes == bytes.size()
            ? texture.NativeContentHash
            : HashGeometryBytes(bytes.first(*baseLevelBytes));
    texture.NativeBaseLevelContentHashAvailable = true;
    return true;
}

void FinalizeGeometryContract(Oot3dPicaVulkanDrawPlan &plan) {
  uint64_t identity = kGeometryFnvOffset;
  uint64_t version = kGeometryFnvOffset;
  bool versionAvailable = true;
  HashGeometryValue(identity, plan.Indexed);
  HashGeometryValue(identity, plan.IndicesAre16Bit);
  HashGeometryValue(identity, plan.BaseVertex);
  HashGeometryValue(identity, plan.VertexCount);
  HashGeometryValue(identity, plan.State.Topology);
  HashGeometryValue(version, plan.Indexed);
  for (const auto &binding : plan.VertexBindings) {
    const auto bytes = binding.ResolvedBytes();
    HashGeometryValue(identity, binding.Binding);
    HashGeometryValue(identity, binding.ByteStride);
    HashGeometryValue(identity, binding.InputRate);
    HashGeometryValue(identity, binding.SourcePhysicalAddress);
    HashGeometryValue(identity, bytes.size());
    if (!binding.ContentVersionAvailable) {
      versionAvailable = false;
    } else {
      HashGeometryValue(version, binding.ContentVersion);
    }
  }
  for (const auto &attribute : plan.VertexAttributes) {
    HashGeometryValue(identity, attribute.Location);
    HashGeometryValue(identity, attribute.Binding);
    HashGeometryValue(identity, attribute.Format);
    HashGeometryValue(identity, attribute.ComponentCount);
    HashGeometryValue(identity, attribute.ByteOffset);
  }
  if (plan.Indexed) {
    HashGeometryValue(identity, plan.IndexPhysicalAddress);
    HashGeometryValue(identity, plan.ResolvedIndexBytes().size());
    if (!plan.IndexContentVersionAvailable) {
      versionAvailable = false;
    } else {
      HashGeometryValue(version, plan.IndexContentVersion);
    }
  }
  plan.GeometryIdentity = identity == 0U ? 1U : identity;
  plan.GeometryContentVersion = version == 0U ? 1U : version;
  plan.GeometryIdentityAvailable = versionAvailable;
}

template <bool ConsumeResources, bool PreparationOnly = false, typename Submission>
bool BuildOot3dPicaVulkanDrawPlanImpl(
    Submission& submission,
    Oot3dPicaVulkanDrawPlan& plan, std::string* error,
    Oot3dPicaVulkanShaderSourceCache* shaderCache) {
    plan = {};
    plan.VertexBindings.reserve(16U);
    plan.VertexAttributes.reserve(16U);
    plan.Textures.reserve(submission.State.Textures.size());
    uint64_t vertexShaderKey = 0;
    uint64_t canonicalVertexProgramId = 0;
    if (shaderCache != nullptr) {
        Oot3dPicaVulkanVertexStructuralStateKey structuralKey;
        structuralKey.VertexProgramMutationIdentity =
            submission.Packet.VertexShader.Program.MutationIdentity();
        structuralKey.VertexSwizzleMutationIdentity =
            submission.Packet.VertexShader.Swizzles.MutationIdentity();
        structuralKey.GeometryProgramMutationIdentity =
            submission.Packet.GeometryShader.Program.MutationIdentity();
        structuralKey.GeometrySwizzleMutationIdentity =
            submission.Packet.GeometryShader.Swizzles.MutationIdentity();
        structuralKey.VertexProgramWordCount =
            submission.Packet.VertexShader.ProgramWordCount;
        structuralKey.VertexSwizzleWordCount =
            submission.Packet.VertexShader.SwizzleWordCount;
        structuralKey.GeometryProgramWordCount =
            submission.Packet.GeometryShader.ProgramWordCount;
        structuralKey.GeometrySwizzleWordCount =
            submission.Packet.GeometryShader.SwizzleWordCount;
        size_t interfaceIndex = 0;
        for (uint16_t reg = 0x04fU; reg <= 0x056U; ++reg) {
            structuralKey.InterfaceWords[interfaceIndex++] =
                submission.Packet.Registers[reg];
        }
        for (const uint16_t reg :
             std::array<uint16_t, 4>{0x229U, 0x2b9U, 0x2baU, 0x2bdU}) {
            structuralKey.InterfaceWords[interfaceIndex++] =
                submission.Packet.Registers[reg];
        }
        const auto cached =
            shaderCache->VertexStructuralStates.find(structuralKey);
        if (cached != shaderCache->VertexStructuralStates.end()) {
            vertexShaderKey = cached->second.ShaderStateKey;
            canonicalVertexProgramId = cached->second.CanonicalProgramId;
        } else {
            Oot3dPicaVulkanCachedVertexStructuralState cachedState;
            cachedState.ShaderStateKey =
                ComputeOot3dPicaVertexShaderStateKey(submission.Packet);
            cachedState.CanonicalProgramId =
                BuildOot3dPicaCanonicalVertexProgramId(
                    submission.Packet, submission.State);
            vertexShaderKey = cachedState.ShaderStateKey;
            canonicalVertexProgramId = cachedState.CanonicalProgramId;
            shaderCache->VertexStructuralStates.emplace(
                std::move(structuralKey), cachedState);
        }
        plan.CanonicalIdentity = BuildOot3dPicaCanonicalDrawIdentity(
            submission.Packet, submission.State,
            canonicalVertexProgramId);
    } else {
        plan.CanonicalIdentity = BuildOot3dPicaCanonicalDrawIdentity(
            submission.Packet, submission.State);
        vertexShaderKey =
            ComputeOot3dPicaVertexShaderStateKey(submission.Packet);
    }
    plan.FragmentFeatures = AnalyzeOot3dPicaFragmentFeatures(
        submission.Packet, submission.State);
    if (submission.State.ShaderInterface.GeometryShaderEnabled) {
        SetError(error, "native PICA Vulkan draw uses a geometry shader");
        return false;
    }
    const auto cachedVertex =
        shaderCache != nullptr
            ? shaderCache->VertexSources.find(vertexShaderKey)
            : decltype(shaderCache->VertexSources.find(vertexShaderKey)){};
    if (shaderCache != nullptr &&
        cachedVertex != shaderCache->VertexSources.end()) {
        plan.VertexShader.StateKey = vertexShaderKey;
        plan.SharedVertexShaderSource = cachedVertex->second.Source;
        plan.VertexShader.SourceIdentity = cachedVertex->second.Identity;
        const auto cachedTemporalProgram =
            shaderCache->VertexTemporalPrograms.find(vertexShaderKey);
        if (cachedTemporalProgram !=
            shaderCache->VertexTemporalPrograms.end()) {
            plan.VertexShader.TemporalProgram =
                cachedTemporalProgram->second;
        }
        plan.VertexShader.Uniforms =
            BuildOot3dPicaVertexUniformState(submission.Packet);
        ++shaderCache->VertexHits;
    } else {
        if (!GenerateOot3dPicaVertexShader(
                submission.Packet, submission.State, plan.VertexShader,
                error)) {
            return false;
        }
        if (shaderCache != nullptr) {
            plan.SharedVertexShaderSource =
                std::make_shared<const std::string>(
                    std::move(plan.VertexShader.Source));
            shaderCache->VertexSources.emplace(
                plan.VertexShader.StateKey,
                Oot3dPicaVulkanCachedShaderSource{
                    plan.SharedVertexShaderSource,
                    plan.VertexShader.SourceIdentity});
            shaderCache->VertexTemporalPrograms.emplace(
                plan.VertexShader.StateKey,
                plan.VertexShader.TemporalProgram);
            ++shaderCache->VertexMisses;
        }
    }

    const uint64_t fragmentShaderKey =
        ComputeOot3dPicaFragmentShaderStateKey(submission.Packet,
                                               submission.State);
    const auto cachedFragment =
        shaderCache != nullptr
            ? shaderCache->FragmentSources.find(fragmentShaderKey)
            : decltype(shaderCache->FragmentSources.find(fragmentShaderKey)){};
    if (shaderCache != nullptr &&
        cachedFragment != shaderCache->FragmentSources.end()) {
        plan.FragmentShader.StateKey = fragmentShaderKey;
        plan.SharedFragmentShaderSource = cachedFragment->second.Source;
        plan.FragmentShader.SourceIdentity = cachedFragment->second.Identity;
        const auto cachedHooks =
            shaderCache->FragmentHooks.find(fragmentShaderKey);
        if (cachedHooks != shaderCache->FragmentHooks.end()) {
            plan.FragmentShader.Hooks = cachedHooks->second;
        }
        plan.FragmentShader.Uniforms =
            BuildOot3dPicaFragmentUniformState(submission.Packet);
        ++shaderCache->FragmentHits;
    } else {
        if (!GenerateOot3dPicaFragmentShader(
                submission.Packet, submission.State, plan.FragmentShader,
                error)) {
            return false;
        }
        if (shaderCache != nullptr) {
            plan.SharedFragmentShaderSource =
                std::make_shared<const std::string>(
                    std::move(plan.FragmentShader.Source));
            shaderCache->FragmentSources.emplace(
                plan.FragmentShader.StateKey,
                Oot3dPicaVulkanCachedShaderSource{
                    plan.SharedFragmentShaderSource,
                    plan.FragmentShader.SourceIdentity});
            shaderCache->FragmentHooks.emplace(
                plan.FragmentShader.StateKey,
                plan.FragmentShader.Hooks);
            ++shaderCache->FragmentMisses;
        }
    }

    plan.SubmissionId = submission.Id;
    plan.CommandListAddress = submission.Packet.CommandListAddress;
    plan.CommandListOffsetWords = submission.Packet.CommandListOffsetWords;
    plan.CompositionDomain = submission.Packet.CompositionDomain;
    plan.Composition = submission.Packet.Composition;
    plan.LightingLuts = submission.Packet.LightingLuts;
    plan.State = submission.State;
    plan.Indexed = submission.State.VertexInput.Indexed;
    plan.IndicesAre16Bit =
        submission.State.VertexInput.IndicesAre16Bit;
    plan.VertexCount = submission.State.VertexInput.VertexCount;
    if (submission.MinimumVertexIndex >
        static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        SetError(error, "native PICA Vulkan base vertex exceeds signed range");
        return false;
    }
    plan.BaseVertex = plan.Indexed
                          ? -static_cast<int32_t>(
                                submission.MinimumVertexIndex)
                          : 0;
    if (plan.Indexed && !PreparationOnly) {
        auto* indices = FindResource(
            submission, Oot3dPicaResourceKind::IndexBuffer);
        if (indices == nullptr) {
            SetError(error, "native PICA Vulkan draw has no index snapshot");
            return false;
        }
        plan.IndexPhysicalAddress = indices->PhysicalAddress;
        plan.IndexContentVersion = indices->ContentVersion;
        plan.IndexContentVersionAvailable = indices->ContentVersionAvailable;
        if (indices->SharedBytes != nullptr) {
          if constexpr (ConsumeResources) {
            plan.SharedIndexBytes = std::move(indices->SharedBytes);
          } else {
            plan.SharedIndexBytes = indices->SharedBytes;
          }
        } else if constexpr (ConsumeResources) {
          plan.IndexBytes = std::move(indices->Bytes);
        } else {
          plan.IndexBytes = indices->Bytes;
        }
    }

    std::array<std::optional<Oot3dPicaVulkanVertexAttribute>, 16>
        attributesByLocation;
    const auto& vertex = submission.State.VertexInput;
    for (size_t loaderIndex = 0; loaderIndex < vertex.Loaders.size();
         ++loaderIndex) {
        const auto& loader = vertex.Loaders[loaderIndex];
        if (loader.ComponentCount == 0U || loader.ByteStride == 0U) {
            continue;
        }
        auto* resource = FindResource(
            submission, Oot3dPicaResourceKind::VertexLoader,
            static_cast<uint8_t>(loaderIndex));
        if (resource == nullptr && !PreparationOnly) {
            SetError(error, "native PICA Vulkan draw has no vertex snapshot");
            return false;
        }
        if (plan.VertexBindings.size() >= 15U) {
            SetError(error, "native PICA Vulkan draw exceeds binding limit");
            return false;
        }
        const uint8_t binding =
            static_cast<uint8_t>(plan.VertexBindings.size());
        size_t byteOffset = 0;
        bool hasAttribute = false;
        for (size_t component = 0; component < loader.ComponentCount;
             ++component) {
            const uint8_t attributeIndex = loader.Components[component];
            if (attributeIndex < 12U) {
                const auto& attribute = vertex.Attributes[attributeIndex];
                const size_t elementSize = FormatByteSize(attribute.Format);
                if (elementSize == 0U) {
                    SetError(error, "native PICA vertex format is invalid");
                    return false;
                }
                byteOffset = AlignUp(byteOffset, elementSize);
                const uint8_t location = submission.State.ShaderInterface
                                             .InputRegisterByAttribute
                                                 [attributeIndex];
                if (location >= attributesByLocation.size() ||
                    byteOffset > std::numeric_limits<uint16_t>::max()) {
                    SetError(error, "native PICA vertex attribute is invalid");
                    return false;
                }
                attributesByLocation[location] =
                    Oot3dPicaVulkanVertexAttribute{
                        location, binding, attribute.Format,
                        attribute.ComponentCount,
                        static_cast<uint16_t>(byteOffset)};
                byteOffset += elementSize * attribute.ComponentCount;
                hasAttribute = true;
            } else {
                byteOffset = AlignUp(byteOffset, 4U);
                byteOffset += static_cast<size_t>(attributeIndex - 11U) * 4U;
            }
        }
        if (byteOffset > loader.ByteStride) {
            SetError(error, "native PICA vertex loader exceeds its stride");
            return false;
        }
        if (hasAttribute) {
          Oot3dPicaVulkanVertexBinding vertexBinding;
          vertexBinding.Binding = binding;
          vertexBinding.ByteStride = loader.ByteStride;
          vertexBinding.InputRate = Oot3dPicaVertexInputRate::PerVertex;
          if constexpr (!PreparationOnly) {
            vertexBinding.SourcePhysicalAddress = resource->PhysicalAddress;
            vertexBinding.ContentVersion = resource->ContentVersion;
            vertexBinding.ContentVersionAvailable = resource->ContentVersionAvailable;
            if (resource->SharedBytes != nullptr) {
              if constexpr (ConsumeResources) {
                vertexBinding.SharedBytes = std::move(resource->SharedBytes);
              } else {
                vertexBinding.SharedBytes = resource->SharedBytes;
              }
            } else if constexpr (ConsumeResources) {
              vertexBinding.Bytes = std::move(resource->Bytes);
            } else {
              vertexBinding.Bytes = resource->Bytes;
            }
          }
            plan.VertexBindings.push_back(std::move(vertexBinding));
        }
    }

    if (plan.VertexBindings.size() >= 16U) {
        SetError(error, "native PICA Vulkan draw has no fixed binding slot");
        return false;
    }
    Oot3dPicaVulkanVertexBinding fixedBinding;
    fixedBinding.Binding = static_cast<uint8_t>(plan.VertexBindings.size());
    fixedBinding.InputRate = Oot3dPicaVertexInputRate::PerInstance;
    fixedBinding.Bytes.reserve(
        (static_cast<size_t>(vertex.AttributeCount) + 1U) *
        sizeof(std::array<float, 4>));
    AppendFloat4(fixedBinding.Bytes, {0.0F, 0.0F, 0.0F, 1.0F});
    for (size_t attributeIndex = 0;
         attributeIndex < vertex.AttributeCount; ++attributeIndex) {
        if (!vertex.Attributes[attributeIndex].Default) {
            continue;
        }
        const uint8_t location = submission.State.ShaderInterface
                                     .InputRegisterByAttribute[attributeIndex];
        if (location >= attributesByLocation.size()) {
            SetError(error, "native PICA default attribute location is invalid");
            return false;
        }
        if (attributesByLocation[location].has_value()) {
            continue;
        }
        const size_t byteOffset = fixedBinding.Bytes.size();
        AppendFloat4(fixedBinding.Bytes,
                     submission.Packet.DefaultAttributes[attributeIndex]);
        attributesByLocation[location] =
            Oot3dPicaVulkanVertexAttribute{
                location, fixedBinding.Binding,
                Oot3dPicaVertexFormat::Float, 4U,
                static_cast<uint16_t>(byteOffset)};
    }
    fixedBinding.ByteStride =
        static_cast<uint16_t>(fixedBinding.Bytes.size());
    fixedBinding.ContentVersion = HashGeometryBytes(fixedBinding.Bytes);
    fixedBinding.ContentVersionAvailable = true;
    for (size_t location = 0; location < attributesByLocation.size();
         ++location) {
        if (!attributesByLocation[location].has_value()) {
            attributesByLocation[location] =
                Oot3dPicaVulkanVertexAttribute{
                    static_cast<uint8_t>(location), fixedBinding.Binding,
                    Oot3dPicaVertexFormat::Float, 4U, 0U};
        }
        plan.VertexAttributes.push_back(
            *attributesByLocation[location]);
    }
    plan.VertexBindings.push_back(std::move(fixedBinding));
    if constexpr (PreparationOnly) return true;
    FinalizeGeometryContract(plan);

    for (size_t texture = 0; texture < submission.State.Textures.size();
         ++texture) {
        if (!submission.State.Textures[texture].Enabled) {
            continue;
        }
        auto* resource = FindResource(
            submission, Oot3dPicaResourceKind::Texture,
            static_cast<uint8_t>(texture));
        if (resource == nullptr) {
            SetError(error, "native PICA Vulkan draw has no texture snapshot");
            return false;
        }
        Oot3dPicaVulkanTextureBinding textureBinding{
            static_cast<uint8_t>(texture),
            submission.State.Textures[texture], {}};
        if (resource->SharedBytes != nullptr) {
            if constexpr (ConsumeResources) {
                textureBinding.SharedNativeBytes =
                    std::move(resource->SharedBytes);
            } else {
                textureBinding.SharedNativeBytes = resource->SharedBytes;
            }
        } else if constexpr (ConsumeResources) {
            textureBinding.NativeBytes = std::move(resource->Bytes);
        } else {
            textureBinding.NativeBytes = resource->Bytes;
        }
        textureBinding.NativeContentHash = resource->ContentHash;
        textureBinding.NativeContentHashAvailable =
            resource->ContentHashAvailable;
        textureBinding.NativeBaseLevelContentHash = resource->BaseLevelContentHash;
        textureBinding.NativeBaseLevelContentHashAvailable = resource->BaseLevelContentHashAvailable;
        if (!ResolveTextureContentIdentity(textureBinding, error)) {
            return false;
        }
        plan.Textures.push_back(std::move(textureBinding));
    }
    return true;
}

} // namespace

bool ResolveOot3dPicaTextureContentIdentity(
    Oot3dPicaVulkanTextureBinding& texture, std::string* error) {
    return ResolveTextureContentIdentity(texture, error);
}

Oot3dPicaVertexSemanticLocations ResolveOot3dCmbVertexSemanticLocations(
    const Oot3dPicaDecodedDrawState& state) {
    Oot3dPicaVertexSemanticLocations result;
    const auto& vertex = state.VertexInput;
    if (vertex.AttributeCount == 0U ||
        vertex.Attributes[0].ComponentCount < 3U) {
        return result;
    }

    const uint8_t position =
        state.ShaderInterface.InputRegisterByAttribute[0];
    if (position >= 16U) {
        return result;
    }
    result.Position = position;

    // UV0 is the first two-component CMB stream following position. Normal
    // and color have three and four components respectively; later
    // two-component streams are secondary UVs.
    for (uint8_t attribute = 1U; attribute < vertex.AttributeCount;
         ++attribute) {
        if (vertex.Attributes[attribute].ComponentCount != 2U) {
            continue;
        }
        const uint8_t location =
            state.ShaderInterface.InputRegisterByAttribute[attribute];
        if (location < 16U) {
            result.TexCoord0 = location;
        }
        break;
    }
    return result;
}

bool BuildOot3dPicaVulkanDrawPlan(
    const Oot3dPicaDrawSubmission& submission,
    Oot3dPicaVulkanDrawPlan& plan, std::string* error,
    Oot3dPicaVulkanShaderSourceCache* shaderCache) {
    return BuildOot3dPicaVulkanDrawPlanImpl<false>(
        submission, plan, error, shaderCache);
}

bool BuildOot3dPicaVulkanDrawPlanAndConsumeResources(
    Oot3dPicaDrawSubmission& submission,
    Oot3dPicaVulkanDrawPlan& plan, std::string* error,
    Oot3dPicaVulkanShaderSourceCache* shaderCache) {
    return BuildOot3dPicaVulkanDrawPlanImpl<true>(
        submission, plan, error, shaderCache);
}

bool BuildOot3dPicaVulkanPipelinePlan(
    const Oot3dPicaDrawSubmission& submission,
    Oot3dPicaVulkanDrawPlan& plan, std::string* error,
    Oot3dPicaVulkanShaderSourceCache* shaderCache) {
    return BuildOot3dPicaVulkanDrawPlanImpl<false, true>(
        submission, plan, error, shaderCache);
}

} // namespace Oot3dNativeGame
