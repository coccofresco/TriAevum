#include "oot3d_native_pica_visual_frame.h"
#include "fast/renderer3ds/pica_vertex_temporal_policy.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename Value> void HashValue(uint64_t& hash, const Value& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) * kFnvPrime;
    }
}

float InterpolateFinite(float previous, float current, float alpha) {
    if (!std::isfinite(previous) || !std::isfinite(current)) {
        return current;
    }
    return previous + (current - previous) * alpha;
}

bool IsContinuousVertexUniform(size_t index) {
    // Native material slots 0x59 and 0x5C+ contain selectors/configuration.
    // Matrices, bone palettes, light values, colors and the 0x5A/0x5B
    // material vectors are continuous render inputs.
    return index < 0x59U || index == 0x5AU || index == 0x5BU;
}

size_t VertexFormatByteSize(Oot3dPicaVertexFormat format) {
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

const Oot3dPicaVulkanVertexBinding* FindVertexBinding(
    const Oot3dPicaVulkanDrawPlan& plan, uint8_t binding) {
    const auto found = std::find_if(
        plan.VertexBindings.begin(), plan.VertexBindings.end(),
        [binding](const auto& candidate) {
            return candidate.Binding == binding;
        });
    return found != plan.VertexBindings.end() ? &*found : nullptr;
}

Oot3dPicaVulkanVertexBinding* FindVertexBinding(std::vector<Oot3dPicaVulkanVertexBinding>& bindings, uint8_t binding) {
    const auto found = std::find_if(bindings.begin(), bindings.end(),
        [binding](const auto& candidate) {
            return candidate.Binding == binding;
        });
    return found != bindings.end() ? &*found : nullptr;
}

bool EqualBytes(std::span<const uint8_t> left, std::span<const uint8_t> right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin());
}

bool EqualBindingBytes(const Oot3dPicaVulkanVertexBinding &left,
                       const Oot3dPicaVulkanVertexBinding &right) {
  if (left.SharedBytes != nullptr && left.SharedBytes == right.SharedBytes) {
    return true;
  }
  if (left.ContentVersionAvailable && right.ContentVersionAvailable &&
      left.SourcePhysicalAddress == right.SourcePhysicalAddress &&
      left.ContentVersion == right.ContentVersion) {
    return true;
  }
  return EqualBytes(left.ResolvedBytes(), right.ResolvedBytes());
}

bool EqualIndexBytes(const Oot3dPicaVulkanDrawPlan &left,
                     const Oot3dPicaVulkanDrawPlan &right) {
  if (left.SharedIndexBytes != nullptr &&
      left.SharedIndexBytes == right.SharedIndexBytes) {
    return true;
  }
  if (left.IndexContentVersionAvailable && right.IndexContentVersionAvailable &&
      left.IndexPhysicalAddress == right.IndexPhysicalAddress &&
      left.IndexContentVersion == right.IndexContentVersion) {
    return true;
  }
  return EqualBytes(left.ResolvedIndexBytes(), right.ResolvedIndexBytes());
}

bool InterpolatableVertexValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current,
    Oot3dPicaVertexInputRate inputRate) {
    bool candidateBytesDiffer = false;
    for (const auto& right : current.VertexBindings) {
        if (right.InputRate != inputRate) {
            continue;
        }
        const auto* left = FindVertexBinding(previous, right.Binding);
        if (left != nullptr && left->InputRate == inputRate &&
            left->ByteStride == right.ByteStride &&
            !EqualBindingBytes(*left, right)) {
          candidateBytesDiffer = true;
          break;
        }
    }
    if (!candidateBytesDiffer) {
        return false;
    }
    for (const auto& attribute : current.VertexAttributes) {
        if (attribute.Format == Oot3dPicaVertexFormat::UnsignedByte ||
            (current.VertexShader.TemporalProgram &&
             !Fast::Renderer3ds::IsPicaVertexInputContinuousForPresentation(
                 current.VertexShader.TemporalProgram->Hooks, attribute.Location))) {
            continue;
        }
        const auto* left = FindVertexBinding(previous, attribute.Binding);
        const auto* right = FindVertexBinding(current, attribute.Binding);
        const size_t componentBytes = VertexFormatByteSize(attribute.Format);
        const size_t attributeBytes =
            componentBytes * attribute.ComponentCount;
        const auto leftBytes = left != nullptr ? left->ResolvedBytes()
                                               : std::span<const uint8_t>{};
        const auto rightBytes = right != nullptr ? right->ResolvedBytes()
                                                 : std::span<const uint8_t>{};
        if (left == nullptr || right == nullptr ||
            left->InputRate != inputRate || right->InputRate != inputRate ||
            left->ByteStride == 0U || left->ByteStride != right->ByteStride ||
            leftBytes.size() != rightBytes.size() ||
            attribute.ByteOffset > left->ByteStride ||
            attributeBytes > left->ByteStride - attribute.ByteOffset) {
          continue;
        }
        for (size_t offset = attribute.ByteOffset;
             offset + attributeBytes <= leftBytes.size();
             offset += left->ByteStride) {
          if (!std::equal(leftBytes.begin() + offset,
                          leftBytes.begin() + offset + attributeBytes,
                          rightBytes.begin() + offset)) {
            return true;
          }
        }
    }
    return false;
}

bool InterpolateVertexValues(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexInputRate inputRate,
                             std::vector<Oot3dPicaVulkanVertexBinding>& outputBindings) {
    bool interpolated = false;
    for (const auto& attribute : current.VertexAttributes) {
        if (attribute.Format == Oot3dPicaVertexFormat::UnsignedByte ||
            (current.VertexShader.TemporalProgram &&
             !Fast::Renderer3ds::IsPicaVertexInputContinuousForPresentation(
                 current.VertexShader.TemporalProgram->Hooks, attribute.Location))) {
            continue;
        }
        const auto* left = FindVertexBinding(previous, attribute.Binding);
        const auto* right = FindVertexBinding(current, attribute.Binding);
        auto* destination = FindVertexBinding(outputBindings, attribute.Binding);
        const size_t componentBytes = VertexFormatByteSize(attribute.Format);
        const size_t attributeBytes =
            componentBytes * attribute.ComponentCount;
        const auto leftBytes = left != nullptr ? left->ResolvedBytes()
                                               : std::span<const uint8_t>{};
        const auto rightBytes = right != nullptr ? right->ResolvedBytes()
                                                 : std::span<const uint8_t>{};
        if (left == nullptr || right == nullptr || destination == nullptr ||
            left->InputRate != inputRate || right->InputRate != inputRate ||
            destination->InputRate != inputRate || left->ByteStride == 0U ||
            left->ByteStride != right->ByteStride ||
            left->ByteStride != destination->ByteStride ||
            leftBytes.size() != rightBytes.size() ||
            leftBytes.size() != destination->ResolvedBytes().size() ||
            attribute.ByteOffset > left->ByteStride ||
            attributeBytes > left->ByteStride - attribute.ByteOffset) {
          continue;
        }
        auto &destinationBytes = destination->MutableBytes();
        for (size_t recordOffset = attribute.ByteOffset;
             recordOffset + attributeBytes <= leftBytes.size();
             recordOffset += left->ByteStride) {
          for (size_t component = 0; component < attribute.ComponentCount;
               ++component) {
            const size_t offset = recordOffset + component * componentBytes;
            switch (attribute.Format) {
            case Oot3dPicaVertexFormat::SignedByte: {
              const int8_t leftValue = static_cast<int8_t>(leftBytes[offset]);
              const int8_t rightValue = static_cast<int8_t>(rightBytes[offset]);
              const auto value =
                  std::clamp<long>(std::lround(InterpolateFinite(
                                       static_cast<float>(leftValue),
                                       static_cast<float>(rightValue), alpha)),
                                   std::numeric_limits<int8_t>::min(),
                                   std::numeric_limits<int8_t>::max());
              destinationBytes[offset] =
                  static_cast<uint8_t>(static_cast<int8_t>(value));
              interpolated |= leftValue != rightValue;
              break;
            }
            case Oot3dPicaVertexFormat::SignedShort: {
              int16_t leftValue = 0;
              int16_t rightValue = 0;
              std::memcpy(&leftValue, leftBytes.data() + offset,
                          sizeof(leftValue));
              std::memcpy(&rightValue, rightBytes.data() + offset,
                          sizeof(rightValue));
              const auto value =
                  std::clamp<long>(std::lround(InterpolateFinite(
                                       static_cast<float>(leftValue),
                                       static_cast<float>(rightValue), alpha)),
                                   std::numeric_limits<int16_t>::min(),
                                   std::numeric_limits<int16_t>::max());
              const int16_t encoded = static_cast<int16_t>(value);
              std::memcpy(destinationBytes.data() + offset, &encoded,
                          sizeof(encoded));
              interpolated |= leftValue != rightValue;
              break;
            }
            case Oot3dPicaVertexFormat::Float: {
              float leftValue = 0.0F;
              float rightValue = 0.0F;
              std::memcpy(&leftValue, leftBytes.data() + offset,
                          sizeof(leftValue));
              std::memcpy(&rightValue, rightBytes.data() + offset,
                          sizeof(rightValue));
              const float encoded =
                  InterpolateFinite(leftValue, rightValue, alpha);
              std::memcpy(destinationBytes.data() + offset, &encoded,
                          sizeof(encoded));
              interpolated |= leftValue != rightValue;
              break;
            }
            case Oot3dPicaVertexFormat::UnsignedByte:
              break;
            }
          }
        }
    }
    return interpolated;
}

bool ContinuousVertexUniformValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    for (size_t index = 0; index < previous.VertexShader.Uniforms.Floats.size();
         ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        if (previous.VertexShader.Uniforms.Floats[index] !=
            current.VertexShader.Uniforms.Floats[index]) {
            return true;
        }
    }
    return false;
}

bool ContinuousFragmentUniformValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    return previous.FragmentShader.Uniforms.TevConstants !=
               current.FragmentShader.Uniforms.TevConstants ||
           previous.FragmentShader.Uniforms.CombinerBufferColor !=
               current.FragmentShader.Uniforms.CombinerBufferColor ||
           previous.FragmentShader.Uniforms.FogColor !=
               current.FragmentShader.Uniforms.FogColor ||
           previous.FragmentShader.Uniforms.FogLut !=
               current.FragmentShader.Uniforms.FogLut ||
           previous.FragmentShader.Uniforms.TextureLodBias !=
               current.FragmentShader.Uniforms.TextureLodBias ||
           previous.FragmentShader.Uniforms.Lighting !=
               current.FragmentShader.Uniforms.Lighting ||
           previous.FragmentShader.Uniforms.ShadowBiasConstant !=
               current.FragmentShader.Uniforms.ShadowBiasConstant ||
           previous.FragmentShader.Uniforms.ShadowBiasLinear !=
               current.FragmentShader.Uniforms.ShadowBiasLinear;
}

bool ContinuousViewportValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    return previous.State.Viewport.HalfWidth !=
               current.State.Viewport.HalfWidth ||
           previous.State.Viewport.HalfHeight !=
               current.State.Viewport.HalfHeight ||
           previous.State.Viewport.DepthRange !=
               current.State.Viewport.DepthRange ||
           previous.State.Viewport.NearPlane !=
               current.State.Viewport.NearPlane;
}

double NormalizedVertexUniformDelta(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    double maximum = 0.0;
    for (size_t index = 0; index < previous.VertexShader.Uniforms.Floats.size();
         ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        for (size_t component = 0; component < 4U; ++component) {
            const double left =
                previous.VertexShader.Uniforms.Floats[index][component];
            const double right =
                current.VertexShader.Uniforms.Floats[index][component];
            if (!std::isfinite(left) || !std::isfinite(right)) {
                continue;
            }
            const double scale =
                std::max({1.0, std::abs(left), std::abs(right)});
            maximum =
                std::max(maximum, std::abs(right - left) / scale);
        }
    }
    return maximum;
}

uint64_t ComputeStructuralDrawIdentity(
    const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.FragmentShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.BaseVertex);
    HashValue(hash, plan.VertexCount);
    HashValue(hash, plan.State.VertexInput.AttributeCount);
    HashValue(hash, plan.State.VertexInput.VertexOffset);
    for (const auto& loader : plan.State.VertexInput.Loaders) {
        HashValue(hash, loader.DataOffset);
        HashValue(hash, loader.ByteStride);
        HashValue(hash, loader.ComponentCount);
        for (const uint8_t component : loader.Components) {
            HashValue(hash, component);
        }
    }
    for (const auto& binding : plan.VertexBindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, binding.InputRate);
        HashValue(hash, binding.ResolvedBytes().size());
    }
    for (const auto& attribute : plan.VertexAttributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.Format);
        HashValue(hash, attribute.ComponentCount);
        HashValue(hash, attribute.ByteOffset);
    }
    for (const uint8_t byte : plan.ResolvedIndexBytes()) {
      HashValue(hash, byte);
    }
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.Slot);
        HashValue(hash, texture.State.Enabled);
        HashValue(hash, texture.State.Width);
        HashValue(hash, texture.State.Height);
        HashValue(hash, texture.State.Format);
        HashValue(hash, texture.State.Type);
        HashValue(hash, texture.State.WrapS);
        HashValue(hash, texture.State.WrapT);
        HashValue(hash, texture.State.MinLinear);
        HashValue(hash, texture.State.MagLinear);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    HashValue(hash, plan.State.Framebuffer.ColorFormat);
    HashValue(hash, plan.State.Framebuffer.DepthFormat);
    HashValue(hash, plan.State.Framebuffer.Flipped);
    HashValue(hash, plan.State.Topology);
    return hash;
}

uint64_t ComputePipelineDrawIdentity(const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.FragmentShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.VertexCount);
    for (const auto& binding : plan.VertexBindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, binding.InputRate);
    }
    for (const auto& attribute : plan.VertexAttributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.Format);
        HashValue(hash, attribute.ComponentCount);
        HashValue(hash, attribute.ByteOffset);
    }
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.Slot);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    HashValue(hash, plan.State.Framebuffer.ColorFormat);
    HashValue(hash, plan.State.Framebuffer.DepthFormat);
    HashValue(hash, plan.State.Framebuffer.Flipped);
    HashValue(hash, plan.State.Topology);
    return hash;
}

bool HaveMatchingStructuralLayout(const Oot3dPicaVulkanDrawPlan& previous,
                                  const Oot3dPicaVulkanDrawPlan& current) {
  if (previous.CompositionDomain != current.CompositionDomain ||
      previous.Composition != current.Composition ||
      previous.VertexShader.StateKey != current.VertexShader.StateKey ||
      previous.FragmentShader.StateKey != current.FragmentShader.StateKey ||
      previous.Indexed != current.Indexed ||
      previous.IndicesAre16Bit != current.IndicesAre16Bit ||
      previous.BaseVertex != current.BaseVertex ||
      previous.VertexCount != current.VertexCount ||
      !EqualIndexBytes(previous, current) ||
      previous.VertexBindings.size() != current.VertexBindings.size() ||
      previous.VertexAttributes.size() != current.VertexAttributes.size() ||
      previous.Textures.size() != current.Textures.size() ||
      previous.State.Framebuffer.Width != current.State.Framebuffer.Width ||
      previous.State.Framebuffer.Height != current.State.Framebuffer.Height ||
      previous.State.Framebuffer.ColorFormat !=
          current.State.Framebuffer.ColorFormat ||
      previous.State.Framebuffer.DepthFormat !=
          current.State.Framebuffer.DepthFormat ||
      previous.State.Framebuffer.Flipped != current.State.Framebuffer.Flipped ||
      previous.State.Topology != current.State.Topology) {
    return false;
  }
    for (size_t index = 0; index < previous.State.VertexInput.Loaders.size(); ++index) {
        const auto& left = previous.State.VertexInput.Loaders[index];
        const auto& right = current.State.VertexInput.Loaders[index];
        if (left.DataOffset != right.DataOffset ||
            left.ByteStride != right.ByteStride ||
            left.ComponentCount != right.ComponentCount ||
            left.Components != right.Components) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.VertexBindings.size(); ++index) {
        const auto& left = previous.VertexBindings[index];
        const auto& right = current.VertexBindings[index];
        if (left.Binding != right.Binding ||
            left.ByteStride != right.ByteStride ||
            left.InputRate != right.InputRate ||
            left.ResolvedBytes().size() != right.ResolvedBytes().size()) {
          return false;
        }
    }
    for (size_t index = 0; index < previous.VertexAttributes.size(); ++index) {
        const auto& left = previous.VertexAttributes[index];
        const auto& right = current.VertexAttributes[index];
        if (left.Location != right.Location ||
            left.Binding != right.Binding ||
            left.Format != right.Format ||
            left.ComponentCount != right.ComponentCount ||
            left.ByteOffset != right.ByteOffset) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.Textures.size(); ++index) {
        const auto& left = previous.Textures[index];
        const auto& right = current.Textures[index];
        if (left.Slot != right.Slot || left.State.Enabled != right.State.Enabled ||
            left.State.Width != right.State.Width || left.State.Height != right.State.Height ||
            left.State.Format != right.State.Format || left.State.Type != right.State.Type ||
            left.State.WrapS != right.State.WrapS || left.State.WrapT != right.State.WrapT ||
            left.State.MinLinear != right.State.MinLinear || left.State.MagLinear != right.State.MagLinear) {
            return false;
        }
    }
    return true;
}

bool HaveMatchingPipelineLayout(const Oot3dPicaVulkanDrawPlan& previous,
                                const Oot3dPicaVulkanDrawPlan& current) {
    if (previous.CompositionDomain != current.CompositionDomain ||
        previous.Composition != current.Composition ||
        previous.VertexShader.StateKey != current.VertexShader.StateKey ||
        previous.FragmentShader.StateKey != current.FragmentShader.StateKey ||
        previous.Indexed != current.Indexed ||
        previous.IndicesAre16Bit != current.IndicesAre16Bit ||
        previous.VertexCount != current.VertexCount ||
        previous.VertexBindings.size() != current.VertexBindings.size() ||
        previous.VertexAttributes.size() != current.VertexAttributes.size() ||
        previous.Textures.size() != current.Textures.size() ||
        previous.State.Framebuffer.Width != current.State.Framebuffer.Width ||
        previous.State.Framebuffer.Height != current.State.Framebuffer.Height ||
        previous.State.Framebuffer.ColorFormat !=
            current.State.Framebuffer.ColorFormat ||
        previous.State.Framebuffer.DepthFormat !=
            current.State.Framebuffer.DepthFormat ||
        previous.State.Framebuffer.Flipped !=
            current.State.Framebuffer.Flipped ||
        previous.State.Topology != current.State.Topology) {
        return false;
    }
    for (size_t index = 0; index < previous.VertexBindings.size(); ++index) {
        const auto& left = previous.VertexBindings[index];
        const auto& right = current.VertexBindings[index];
        if (left.Binding != right.Binding ||
            left.ByteStride != right.ByteStride ||
            left.InputRate != right.InputRate) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.VertexAttributes.size(); ++index) {
        const auto& left = previous.VertexAttributes[index];
        const auto& right = current.VertexAttributes[index];
        if (left.Location != right.Location || left.Binding != right.Binding ||
            left.Format != right.Format ||
            left.ComponentCount != right.ComponentCount ||
            left.ByteOffset != right.ByteOffset) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.Textures.size(); ++index) {
        if (previous.Textures[index].Slot != current.Textures[index].Slot) {
            return false;
        }
    }
    return true;
}

enum class VisualDrawMatchKind : uint8_t {
    StrictUnique,
    StrictOrdinal,
    StructuralOrdinal,
    PipelineOrdinal,
};

struct VisualDrawMatch {
    size_t PreviousIndex = 0;
    VisualDrawMatchKind Kind = VisualDrawMatchKind::StrictUnique;
};

struct VisualDrawMatching {
    std::vector<std::optional<VisualDrawMatch>> PreviousByCurrent;
    size_t StrictUniqueMatches = 0;
    size_t StrictOrdinalMatches = 0;
    size_t StructuralOrdinalMatches = 0;
    size_t PipelineOrdinalMatches = 0;
    size_t AmbiguousPrevious = 0;
    size_t AmbiguousCurrent = 0;
    size_t UnmatchedPrevious = 0;
    size_t UnmatchedCurrent = 0;
};

struct VisualDrawDeltaFlags {
    bool Continuous = false;
    bool VertexUniforms = false;
    bool FragmentUniforms = false;
    bool Viewport = false;
    bool BlendConstantColor = false;
    bool PerInstanceVertex = false;
    bool PerVertex = false;
};

template <typename Identity> struct VisualIdentityValues {
    const std::vector<uint64_t>* Existing = nullptr;
    const std::vector<Oot3dPicaVulkanDrawPlan>* Draws = nullptr;
    Identity Compute{};
    mutable std::vector<uint64_t> Owned;
    mutable std::vector<bool> Ready;

    uint64_t operator[](size_t index) const {
        if (Existing != nullptr) {
            return (*Existing)[index];
        }
        if (!Ready[index]) {
            Owned[index] = Compute((*Draws)[index]);
            Ready[index] = true;
        }
        return Owned[index];
    }
};

template <typename Identity>
VisualIdentityValues<Identity> ResolveVisualIdentities(const std::vector<uint64_t>& existing,
                                                       const std::vector<Oot3dPicaVulkanDrawPlan>& draws,
                                                       Identity identity) {
    VisualIdentityValues<Identity> values;
    if (existing.size() == draws.size()) {
        values.Existing = &existing;
        return values;
    }
    values.Draws = &draws;
    values.Compute = identity;
    values.Owned.resize(draws.size());
    values.Ready.resize(draws.size(), false);
    return values;
}

template <typename Identity>
VisualIdentityValues<Identity> ResolveVisualIdentities(const std::vector<Oot3dPicaVulkanDrawPlan>& draws,
                                                       Identity identity) {
    static const std::vector<uint64_t> empty;
    return ResolveVisualIdentities(empty, draws, identity);
}

template <typename PreviousIdentities, typename CurrentIdentities>
void MatchEqualCardinalityGroups(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    const std::vector<bool>& previousAlreadyMatched,
    std::vector<bool>& previousMatched,
    std::vector<bool>& currentMatched,
    std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
                                 const PreviousIdentities& previousIdentities,
                                 const CurrentIdentities& currentIdentities, VisualDrawMatchKind uniqueKind,
    VisualDrawMatchKind repeatedKind, size_t& uniqueMatches,
    size_t& repeatedMatches, size_t* ambiguousPrevious,
    size_t* ambiguousCurrent) {
    std::unordered_map<uint64_t, std::vector<size_t>> previousGroups;
    std::unordered_map<uint64_t, std::vector<size_t>> currentGroups;
    for (size_t index = 0; index < previous.Draws.size(); ++index) {
        if (!previousAlreadyMatched[index] && !previousMatched[index]) {
            previousGroups[previousIdentities[index]].push_back(index);
        }
    }
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        if (!currentMatched[index]) {
            currentGroups[currentIdentities[index]].push_back(index);
        }
    }
    for (const auto& [key, currentIndices] : currentGroups) {
        const auto found = previousGroups.find(key);
        if (found == previousGroups.end()) {
            continue;
        }
        const auto& previousIndices = found->second;
        if (previousIndices.size() != currentIndices.size()) {
            if (ambiguousPrevious != nullptr && previousIndices.size() > 1U) {
                *ambiguousPrevious += previousIndices.size();
            }
            if (ambiguousCurrent != nullptr && currentIndices.size() > 1U) {
                *ambiguousCurrent += currentIndices.size();
            }
            continue;
        }
        const bool unique = currentIndices.size() == 1U;
        for (size_t ordinal = 0; ordinal < currentIndices.size(); ++ordinal) {
            const size_t previousIndex = previousIndices[ordinal];
            const size_t currentIndex = currentIndices[ordinal];
            if (!HaveMatchingStructuralLayout(previous.Draws[previousIndex],
                                              current.Draws[currentIndex])) {
                continue;
            }
            const auto kind = unique ? uniqueKind : repeatedKind;
            previousByCurrent[currentIndex] = {previousIndex, kind};
            previousMatched[previousIndex] = true;
            currentMatched[currentIndex] = true;
            if (unique) {
                ++uniqueMatches;
            } else {
                ++repeatedMatches;
            }
        }
    }
}

struct VisualAlignmentCell {
    size_t Matches = 0;
    double Cost = 0.0;
    uint8_t Action = 0;
};

bool IsBetterAlignment(const VisualAlignmentCell& candidate,
                       const VisualAlignmentCell& current) {
    if (candidate.Matches != current.Matches) {
        return candidate.Matches > current.Matches;
    }
    if (candidate.Cost != current.Cost) {
        return candidate.Cost < current.Cost;
    }
    return candidate.Action > current.Action;
}

template <typename PreviousIdentities, typename CurrentIdentities, typename Compatible>
void MatchMonotonicGroups(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    std::vector<bool>& previousMatched,
    std::vector<bool>& currentMatched,
    std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
                          const PreviousIdentities& previousIdentities, const CurrentIdentities& currentIdentities, Compatible compatible, VisualDrawMatchKind kind,
    size_t& matchCount) {
    std::unordered_map<uint64_t, std::vector<size_t>> previousGroups;
    std::unordered_map<uint64_t, std::vector<size_t>> currentGroups;
    for (size_t index = 0; index < previous.Draws.size(); ++index) {
        if (!previousMatched[index]) {
            previousGroups[previousIdentities[index]].push_back(index);
        }
    }
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        if (!currentMatched[index]) {
            currentGroups[currentIdentities[index]].push_back(index);
        }
    }
    for (const auto& [key, currentIndices] : currentGroups) {
        const auto found = previousGroups.find(key);
        if (found == previousGroups.end()) {
            continue;
        }
        const auto& previousIndices = found->second;
        const size_t rowWidth = currentIndices.size() + 1U;
        std::vector<VisualAlignmentCell> cells(
            (previousIndices.size() + 1U) * rowWidth);
        const auto cell = [&](size_t previousCount,
                              size_t currentCount) -> VisualAlignmentCell& {
            return cells[previousCount * rowWidth + currentCount];
        };
        for (size_t previousCount = 1; previousCount <= previousIndices.size();
             ++previousCount) {
            cell(previousCount, 0).Action = 1U;
        }
        for (size_t currentCount = 1; currentCount <= currentIndices.size();
             ++currentCount) {
            cell(0, currentCount).Action = 2U;
        }
        for (size_t previousCount = 1; previousCount <= previousIndices.size();
             ++previousCount) {
            for (size_t currentCount = 1;
                 currentCount <= currentIndices.size(); ++currentCount) {
                VisualAlignmentCell best =
                    cell(previousCount - 1U, currentCount);
                best.Action = 1U;
                auto skipCurrent =
                    cell(previousCount, currentCount - 1U);
                skipCurrent.Action = 2U;
                if (IsBetterAlignment(skipCurrent, best)) {
                    best = skipCurrent;
                }
                const size_t previousIndex =
                    previousIndices[previousCount - 1U];
                const size_t currentIndex =
                    currentIndices[currentCount - 1U];
                if (compatible(previous.Draws[previousIndex],
                               current.Draws[currentIndex])) {
                    auto match =
                        cell(previousCount - 1U, currentCount - 1U);
                    ++match.Matches;
                    const double previousOrder =
                        (static_cast<double>(previousIndex) + 0.5) /
                        std::max<size_t>(previous.Draws.size(), 1U);
                    const double currentOrder =
                        (static_cast<double>(currentIndex) + 0.5) /
                        std::max<size_t>(current.Draws.size(), 1U);
                    match.Cost += NormalizedVertexUniformDelta(
                                      previous.Draws[previousIndex],
                                      current.Draws[currentIndex]) +
                                  std::abs(previousOrder - currentOrder);
                    match.Action = 3U;
                    if (IsBetterAlignment(match, best)) {
                        best = match;
                    }
                }
                cell(previousCount, currentCount) = best;
            }
        }

        size_t previousCount = previousIndices.size();
        size_t currentCount = currentIndices.size();
        while (previousCount != 0U || currentCount != 0U) {
            const uint8_t action = cell(previousCount, currentCount).Action;
            if (action == 3U) {
                const size_t previousIndex =
                    previousIndices[previousCount - 1U];
                const size_t currentIndex =
                    currentIndices[currentCount - 1U];
                previousByCurrent[currentIndex] = {previousIndex, kind};
                previousMatched[previousIndex] = true;
                currentMatched[currentIndex] = true;
                ++matchCount;
                --previousCount;
                --currentCount;
            } else if (action == 2U) {
                --currentCount;
            } else if (action == 1U) {
                --previousCount;
            } else {
                break;
            }
        }
    }
}

VisualDrawMatching BuildVisualDrawMatching(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current) {
    VisualDrawMatching matching;
    matching.PreviousByCurrent.resize(current.Draws.size());
    std::vector<bool> noPreviousMatches(previous.Draws.size(), false);
    std::vector<bool> previousMatched(previous.Draws.size(), false);
    std::vector<bool> currentMatched(current.Draws.size(), false);
    const auto previousStrict =
        ResolveVisualIdentities(previous.StrictDrawIdentities, previous.Draws, ComputeOot3dPicaVisualDrawIdentity);
    const auto currentStrict =
        ResolveVisualIdentities(current.StrictDrawIdentities, current.Draws, ComputeOot3dPicaVisualDrawIdentity);
    const auto previousStructural = ResolveVisualIdentities(previous.Draws, ComputeStructuralDrawIdentity);
    const auto currentStructural = ResolveVisualIdentities(current.Draws, ComputeStructuralDrawIdentity);
    const auto previousPipeline = ResolveVisualIdentities(previous.Draws, ComputePipelineDrawIdentity);
    const auto currentPipeline = ResolveVisualIdentities(current.Draws, ComputePipelineDrawIdentity);

    MatchEqualCardinalityGroups(
        previous, current, noPreviousMatches, previousMatched, currentMatched,
        matching.PreviousByCurrent, previousStrict, currentStrict,
        VisualDrawMatchKind::StrictUnique,
        VisualDrawMatchKind::StrictOrdinal, matching.StrictUniqueMatches,
        matching.StrictOrdinalMatches, &matching.AmbiguousPrevious,
        &matching.AmbiguousCurrent);

    size_t unusedStructuralUniqueMatches = 0;
    MatchEqualCardinalityGroups(
        previous, current, previousMatched, previousMatched, currentMatched,
        matching.PreviousByCurrent, previousStructural, currentStructural,
        VisualDrawMatchKind::StructuralOrdinal,
        VisualDrawMatchKind::StructuralOrdinal,
        unusedStructuralUniqueMatches, matching.StructuralOrdinalMatches,
        nullptr, nullptr);
    matching.StructuralOrdinalMatches += unusedStructuralUniqueMatches;

    MatchMonotonicGroups(
        previous, current, previousMatched, currentMatched,
        matching.PreviousByCurrent,
                         previousStructural, currentStructural,
        HaveMatchingStructuralLayout,
        VisualDrawMatchKind::StructuralOrdinal,
        matching.StructuralOrdinalMatches);
    MatchMonotonicGroups(
        previous, current, previousMatched, currentMatched,
        matching.PreviousByCurrent,
                         previousPipeline, currentPipeline,
        HaveMatchingPipelineLayout, VisualDrawMatchKind::PipelineOrdinal,
        matching.PipelineOrdinalMatches);

    matching.UnmatchedPrevious = static_cast<size_t>(std::count(
        previousMatched.begin(), previousMatched.end(), false));
    matching.UnmatchedCurrent = static_cast<size_t>(std::count(
        currentMatched.begin(), currentMatched.end(), false));
    return matching;
}

Oot3dPicaVisualTransitionStats
AnalyzeVisualTransitionWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                    const VisualDrawMatching& matching,
                                    std::vector<VisualDrawDeltaFlags>* drawDeltas = nullptr) {
    Oot3dPicaVisualTransitionStats stats;
    stats.PreviousDraws = previous.Draws.size();
    stats.CurrentDraws = current.Draws.size();
    stats.StrictUniqueMatchedDraws = matching.StrictUniqueMatches;
    stats.StrictOrdinalMatchedDraws = matching.StrictOrdinalMatches;
    stats.StructuralOrdinalMatchedDraws = matching.StructuralOrdinalMatches;
    stats.PipelineOrdinalMatchedDraws = matching.PipelineOrdinalMatches;
    stats.MatchedDraws = stats.StrictUniqueMatchedDraws + stats.StrictOrdinalMatchedDraws +
                         stats.StructuralOrdinalMatchedDraws + stats.PipelineOrdinalMatchedDraws;
    stats.AmbiguousPreviousDraws = matching.AmbiguousPrevious;
    stats.AmbiguousCurrentDraws = matching.AmbiguousCurrent;
    stats.UnmatchedPreviousDraws = matching.UnmatchedPrevious;
    stats.UnmatchedCurrentDraws = matching.UnmatchedCurrent;
    const size_t maximumDrawCount = std::max(stats.PreviousDraws, stats.CurrentDraws);
    if (maximumDrawCount != 0U) {
        stats.MatchedDrawCoverage = static_cast<double>(stats.MatchedDraws) / static_cast<double>(maximumDrawCount);
    }
    std::vector<double> topTargetDeltas;
    if (drawDeltas != nullptr) {
        drawDeltas->assign(current.Draws.size(), {});
    }
    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        if (!matching.PreviousByCurrent[currentIndex].has_value()) {
            continue;
        }
        const auto& previousDraw = previous.Draws[matching.PreviousByCurrent[currentIndex]->PreviousIndex];
        const auto& currentDraw = current.Draws[currentIndex];
        VisualDrawDeltaFlags delta;
        delta.PerInstanceVertex =
            InterpolatableVertexValuesDiffer(previousDraw, currentDraw, Oot3dPicaVertexInputRate::PerInstance);
        delta.PerVertex =
            previousDraw.BaseVertex == currentDraw.BaseVertex &&
            EqualIndexBytes(previousDraw, currentDraw) &&
            InterpolatableVertexValuesDiffer(
                previousDraw, currentDraw, Oot3dPicaVertexInputRate::PerVertex);
        delta.VertexUniforms =
            ContinuousVertexUniformValuesDiffer(previousDraw, currentDraw);
        delta.FragmentUniforms =
            ContinuousFragmentUniformValuesDiffer(previousDraw, currentDraw);
        delta.Viewport =
            ContinuousViewportValuesDiffer(previousDraw, currentDraw);
        delta.BlendConstantColor =
            previousDraw.State.OutputMerger.Blend.ConstantColor !=
            currentDraw.State.OutputMerger.Blend.ConstantColor;
        delta.Continuous = delta.PerInstanceVertex || delta.PerVertex ||
                           delta.VertexUniforms || delta.FragmentUniforms ||
                           delta.Viewport || delta.BlendConstantColor;
        if (delta.Continuous) {
            ++stats.ChangedContinuousDraws;
        }
        if (delta.PerInstanceVertex) {
            ++stats.ChangedPerInstanceVertexDraws;
        }
        if (delta.PerVertex) {
            ++stats.ChangedPerVertexDraws;
        }
        if (drawDeltas != nullptr) {
            (*drawDeltas)[currentIndex] = delta;
        }
        if (currentDraw.State.Framebuffer.ColorPhysicalAddress == current.TopTransfer.InputPhysicalAddress) {
            ++stats.TopTargetMatchedDraws;
            topTargetDeltas.push_back(NormalizedVertexUniformDelta(previousDraw, currentDraw));
            const auto& currentProgram = currentDraw.VertexShader.TemporalProgram;
            const auto& previousProgram = previousDraw.VertexShader.TemporalProgram;
            const auto skinned = [](const auto& draw) {
                const auto& skeleton = draw.VertexShader.TemporalProgram->Hooks.Skeleton;
                return skeleton.Available() &&
                    (draw.VertexShader.Uniforms.BooleanMask & (1U << skeleton.EnableBooleanUniform)) != 0;
            };
            if (currentProgram && previousProgram && !skinned(currentDraw) && !skinned(previousDraw) &&
                currentDraw.Composition.Layer == Oot3dPicaCompositionLayer::OpaqueWorld &&
                previousDraw.Composition.Layer == Oot3dPicaCompositionLayer::OpaqueWorld &&
                currentProgram->Hooks.Transform == previousProgram->Hooks.Transform) {
                const auto camera = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(
                    currentProgram->Hooks.Transform, previousDraw.VertexShader.Uniforms.Floats,
                    currentDraw.VertexShader.Uniforms.Floats);
                if (camera) {
                    ++stats.CameraMatchedDraws;
                    if (camera->Discontinuous()) ++stats.CameraDiscontinuousDraws;
                }
            }
        }
    }
    if (!topTargetDeltas.empty()) {
        std::sort(topTargetDeltas.begin(), topTargetDeltas.end());
        stats.MedianNormalizedContinuousDelta = topTargetDeltas[topTargetDeltas.size() / 2U];
        const size_t p90Index = ((topTargetDeltas.size() - 1U) * 9U) / 10U;
        stats.P90NormalizedContinuousDelta = topTargetDeltas[p90Index];
    }
    static const bool trace = std::getenv("OOT3D_VISUAL_TRANSITION_DIAGNOSTICS") != nullptr;
    if (trace && stats.CameraDiscontinuousDraws != 0) {
        std::fprintf(stderr, "[visual-camera-cut] previous=%llu current=%llu camera_draws=%zu cut_draws=%zu coverage=%.4f rejected=%u\n",
            static_cast<unsigned long long>(previous.Sequence), static_cast<unsigned long long>(current.Sequence),
            stats.CameraMatchedDraws, stats.CameraDiscontinuousDraws, stats.MatchedDrawCoverage,
            stats.CameraDiscontinuousDraws * 2U >= stats.CameraMatchedDraws ? 1U : 0U);
    }
    return stats;
}

void InterpolateVisualVertexUniforms(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    for (size_t index = 0; index < vertexUniforms.Floats.size(); ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        for (size_t component = 0; component < 4U; ++component) {
            vertexUniforms.Floats[index][component] =
                InterpolateFinite(previous.VertexShader.Uniforms.Floats[index][component],
                                  current.VertexShader.Uniforms.Floats[index][component], alpha);
        }
    }
}

void InterpolateVisualFragmentUniforms(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaFragmentUniformState& fragmentUniforms) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    for (size_t slot = 0; slot < fragmentUniforms.TevConstants.size(); ++slot) {
        for (size_t component = 0; component < 4U; ++component) {
            fragmentUniforms.TevConstants[slot][component] =
                InterpolateFinite(previous.FragmentShader.Uniforms.TevConstants[slot][component],
                                  current.FragmentShader.Uniforms.TevConstants[slot][component], alpha);
        }
    }
    for (size_t component = 0; component < 4U; ++component) {
        fragmentUniforms.CombinerBufferColor[component] =
            InterpolateFinite(previous.FragmentShader.Uniforms.CombinerBufferColor[component],
                              current.FragmentShader.Uniforms.CombinerBufferColor[component], alpha);
        fragmentUniforms.FogColor[component] =
            InterpolateFinite(previous.FragmentShader.Uniforms.FogColor[component],
                              current.FragmentShader.Uniforms.FogColor[component], alpha);
    }
    for (size_t entry = 0; entry < fragmentUniforms.FogLut.size(); ++entry) {
        for (size_t component = 0; component < 2U; ++component) {
            fragmentUniforms.FogLut[entry][component] =
                InterpolateFinite(previous.FragmentShader.Uniforms.FogLut[entry][component],
                                  current.FragmentShader.Uniforms.FogLut[entry][component], alpha);
        }
    }
    for (size_t component = 0;
         component < fragmentUniforms.TextureLodBias.size(); ++component) {
        fragmentUniforms.TextureLodBias[component] = InterpolateFinite(
            previous.FragmentShader.Uniforms.TextureLodBias[component],
            current.FragmentShader.Uniforms.TextureLodBias[component], alpha);
    }
    const auto interpolateVectors = [&](auto& output,
                                        const auto& previousValues,
                                        const auto& currentValues) {
        for (size_t vector = 0; vector < output.size(); ++vector) {
            for (size_t component = 0; component < output[vector].size();
                 ++component) {
                output[vector][component] = InterpolateFinite(
                    previousValues[vector][component],
                    currentValues[vector][component], alpha);
            }
        }
    };
    interpolateVectors(fragmentUniforms.Lighting.Specular0,
                       previous.FragmentShader.Uniforms.Lighting.Specular0,
                       current.FragmentShader.Uniforms.Lighting.Specular0);
    interpolateVectors(fragmentUniforms.Lighting.Specular1,
                       previous.FragmentShader.Uniforms.Lighting.Specular1,
                       current.FragmentShader.Uniforms.Lighting.Specular1);
    interpolateVectors(fragmentUniforms.Lighting.Diffuse,
                       previous.FragmentShader.Uniforms.Lighting.Diffuse,
                       current.FragmentShader.Uniforms.Lighting.Diffuse);
    interpolateVectors(fragmentUniforms.Lighting.Ambient,
                       previous.FragmentShader.Uniforms.Lighting.Ambient,
                       current.FragmentShader.Uniforms.Lighting.Ambient);
    interpolateVectors(fragmentUniforms.Lighting.Position,
                       previous.FragmentShader.Uniforms.Lighting.Position,
                       current.FragmentShader.Uniforms.Lighting.Position);
    interpolateVectors(
        fragmentUniforms.Lighting.SpotDirection,
        previous.FragmentShader.Uniforms.Lighting.SpotDirection,
        current.FragmentShader.Uniforms.Lighting.SpotDirection);
    interpolateVectors(fragmentUniforms.Lighting.Attenuation,
                       previous.FragmentShader.Uniforms.Lighting.Attenuation,
                       current.FragmentShader.Uniforms.Lighting.Attenuation);
    for (size_t component = 0;
         component < fragmentUniforms.Lighting.GlobalAmbient.size();
         ++component) {
        fragmentUniforms.Lighting.GlobalAmbient[component] = InterpolateFinite(
            previous.FragmentShader.Uniforms.Lighting.GlobalAmbient[component],
            current.FragmentShader.Uniforms.Lighting.GlobalAmbient[component],
            alpha);
    }
    fragmentUniforms.ShadowBiasConstant = InterpolateFinite(
        previous.FragmentShader.Uniforms.ShadowBiasConstant,
        current.FragmentShader.Uniforms.ShadowBiasConstant, alpha);
    fragmentUniforms.ShadowBiasLinear = InterpolateFinite(
        previous.FragmentShader.Uniforms.ShadowBiasLinear,
        current.FragmentShader.Uniforms.ShadowBiasLinear, alpha);
}

void InterpolateVisualViewport(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaViewportState& viewport) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    viewport.HalfWidth = InterpolateFinite(previous.State.Viewport.HalfWidth, current.State.Viewport.HalfWidth, alpha);
    viewport.HalfHeight =
        InterpolateFinite(previous.State.Viewport.HalfHeight, current.State.Viewport.HalfHeight, alpha);
    viewport.DepthRange =
        InterpolateFinite(previous.State.Viewport.DepthRange, current.State.Viewport.DepthRange, alpha);
    viewport.NearPlane = InterpolateFinite(previous.State.Viewport.NearPlane, current.State.Viewport.NearPlane, alpha);
}

void InterpolateVisualBlendConstantColor(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    std::array<float, 4>& blendConstantColor) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    for (size_t component = 0; component < 4U; ++component) {
        blendConstantColor[component] = InterpolateFinite(
            previous.State.OutputMerger.Blend.ConstantColor[component],
            current.State.OutputMerger.Blend.ConstantColor[component], alpha);
    }
}

void InterpolateCompatibleVisualRenderState(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms,
    Oot3dPicaFragmentUniformState& fragmentUniforms,
    Oot3dPicaViewportState& viewport,
    std::array<float, 4>& blendConstantColor) {
    InterpolateVisualVertexUniforms(previous, current, alpha,
                                    vertexUniforms);
    InterpolateVisualFragmentUniforms(previous, current, alpha,
                                      fragmentUniforms);
    InterpolateVisualViewport(previous, current, alpha, viewport);
    InterpolateVisualBlendConstantColor(previous, current, alpha,
                                        blendConstantColor);
}

void InterpolateCompatibleVisualDraw(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current,
                                     float alpha, Oot3dPicaVulkanDrawPlan& output) {
    output = current;
    InterpolateCompatibleVisualRenderState(previous, current, alpha, output.VertexShader.Uniforms,
                                           output.FragmentShader.Uniforms, output.State.Viewport,
                                           output.State.OutputMerger.Blend.ConstantColor);
    const bool perInstanceInterpolated = InterpolateVertexValues(
        previous, current, alpha, Oot3dPicaVertexInputRate::PerInstance,
        output.VertexBindings);
    bool perVertexInterpolated = false;
    if (previous.BaseVertex == current.BaseVertex &&
        EqualIndexBytes(previous, current)) {
      perVertexInterpolated = InterpolateVertexValues(
          previous, current, alpha, Oot3dPicaVertexInputRate::PerVertex,
          output.VertexBindings);
    }
    if (perInstanceInterpolated || perVertexInterpolated) {
      output.GeometryIdentityAvailable = false;
    }
}

bool SampleVisualFrameWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                   float alpha, const VisualDrawMatching& matching, Oot3dPicaVisualFrameSample& output,
                                   const std::vector<VisualDrawDeltaFlags>* drawDeltas = nullptr) {
    output.TopTransfer = current.TopTransfer;
    output.MatchedDraws = matching.StrictUniqueMatches + matching.StrictOrdinalMatches +
                          matching.StructuralOrdinalMatches + matching.PipelineOrdinalMatches;
    output.StrictUniqueMatchedDraws = matching.StrictUniqueMatches;
    output.StrictOrdinalMatchedDraws = matching.StrictOrdinalMatches;
    output.StructuralOrdinalMatchedDraws = matching.StructuralOrdinalMatches;
    output.PipelineOrdinalMatchedDraws = matching.PipelineOrdinalMatches;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.Draws.resize(current.Draws.size());

    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& currentDraw = current.Draws[currentIndex];
        const auto match = matching.PreviousByCurrent[currentIndex];
        auto& sampled = output.Draws[currentIndex];
        if (match.has_value()) {
            const auto& previousDraw = previous.Draws[match->PreviousIndex];
            InterpolateCompatibleVisualDraw(previousDraw, currentDraw, alpha, sampled);
            VisualDrawDeltaFlags delta;
            if (drawDeltas != nullptr) {
                delta = (*drawDeltas)[currentIndex];
            } else {
                delta.PerInstanceVertex =
                    InterpolatableVertexValuesDiffer(previousDraw, currentDraw, Oot3dPicaVertexInputRate::PerInstance);
                delta.PerVertex =
                    previousDraw.BaseVertex == currentDraw.BaseVertex &&
                    EqualIndexBytes(previousDraw, currentDraw) &&
                    InterpolatableVertexValuesDiffer(
                        previousDraw, currentDraw,
                        Oot3dPicaVertexInputRate::PerVertex);
                delta.VertexUniforms =
                    ContinuousVertexUniformValuesDiffer(previousDraw,
                                                        currentDraw);
                delta.FragmentUniforms =
                    ContinuousFragmentUniformValuesDiffer(previousDraw,
                                                          currentDraw);
                delta.Viewport = ContinuousViewportValuesDiffer(previousDraw,
                                                                currentDraw);
                delta.BlendConstantColor =
                    previousDraw.State.OutputMerger.Blend.ConstantColor !=
                    currentDraw.State.OutputMerger.Blend.ConstantColor;
                delta.Continuous =
                    delta.PerInstanceVertex || delta.PerVertex ||
                    delta.VertexUniforms || delta.FragmentUniforms ||
                    delta.Viewport || delta.BlendConstantColor;
            }
            if (delta.Continuous) {
                ++output.InterpolatedDraws;
            }
            if (delta.PerInstanceVertex) {
                ++output.InterpolatedPerInstanceVertexDraws;
            }
            if (delta.PerVertex) {
                ++output.InterpolatedPerVertexDraws;
            }
        } else {
            sampled = currentDraw;
        }
    }
    if (output.Draws.empty()) {
        return false;
    }

    output.MemoryFills = current.MemoryFills;
    output.DisplayTransfers = current.DisplayTransfers;
    return true;
}

bool SampleVisualFrameViewWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                       float alpha, const VisualDrawMatching& matching,
                                       Oot3dPicaVisualFrameViewSample& output,
                                       const std::vector<VisualDrawDeltaFlags>& drawDeltas) {
    output.TopTransfer = current.TopTransfer;
    output.MemoryFills = &current.MemoryFills;
    output.DisplayTransfers = &current.DisplayTransfers;
    output.MatchedDraws = matching.StrictUniqueMatches + matching.StrictOrdinalMatches +
                          matching.StructuralOrdinalMatches + matching.PipelineOrdinalMatches;
    output.StrictUniqueMatchedDraws = matching.StrictUniqueMatches;
    output.StrictOrdinalMatchedDraws = matching.StrictOrdinalMatches;
    output.StructuralOrdinalMatchedDraws = matching.StructuralOrdinalMatches;
    output.PipelineOrdinalMatchedDraws = matching.PipelineOrdinalMatches;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.Draws.resize(current.Draws.size());

    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& currentDraw = current.Draws[currentIndex];
        auto& sampled = output.Draws[currentIndex];
        sampled.BasePlan = &currentDraw;
        sampled.VertexUniforms.reset();
        sampled.FragmentUniforms.reset();
        sampled.Viewport.reset();
        sampled.BlendConstantColor.reset();
        sampled.HasVertexBindingOverrides = false;

        const auto match = matching.PreviousByCurrent[currentIndex];
        if (!match.has_value()) {
            continue;
        }
        const auto& previousDraw = previous.Draws[match->PreviousIndex];
        const auto& delta = drawDeltas[currentIndex];
        if (delta.VertexUniforms) {
            sampled.VertexUniforms.emplace(currentDraw.VertexShader.Uniforms);
            InterpolateVisualVertexUniforms(
                previousDraw, currentDraw, alpha, *sampled.VertexUniforms);
        }
        if (delta.FragmentUniforms) {
            sampled.FragmentUniforms.emplace(currentDraw.FragmentShader.Uniforms);
            InterpolateVisualFragmentUniforms(
                previousDraw, currentDraw, alpha, *sampled.FragmentUniforms);
        }
        if (delta.Viewport) {
            sampled.Viewport.emplace(currentDraw.State.Viewport);
            InterpolateVisualViewport(previousDraw, currentDraw, alpha,
                                      *sampled.Viewport);
        }
        if (delta.BlendConstantColor) {
            sampled.BlendConstantColor.emplace(currentDraw.State.OutputMerger.Blend.ConstantColor);
            InterpolateVisualBlendConstantColor(
                previousDraw, currentDraw, alpha,
                *sampled.BlendConstantColor);
        }
        if (delta.PerInstanceVertex || delta.PerVertex) {
            sampled.VertexBindings = currentDraw.VertexBindings;
            sampled.HasVertexBindingOverrides = true;
            if (delta.PerInstanceVertex) {
                InterpolateVertexValues(previousDraw, currentDraw, alpha, Oot3dPicaVertexInputRate::PerInstance,
                                        sampled.VertexBindings);
            }
            if (delta.PerVertex) {
                InterpolateVertexValues(previousDraw, currentDraw, alpha, Oot3dPicaVertexInputRate::PerVertex,
                                        sampled.VertexBindings);
            }
        }
        if (delta.Continuous) {
            ++output.InterpolatedDraws;
        }
        if (delta.PerInstanceVertex) {
            ++output.InterpolatedPerInstanceVertexDraws;
        }
        if (delta.PerVertex) {
            ++output.InterpolatedPerVertexDraws;
        }
    }
    return !output.Draws.empty();
}

} // namespace

struct Oot3dPicaPreparedVisualTransition::Impl {
    VisualDrawMatching Matching;
    std::vector<VisualDrawDeltaFlags> DrawDeltas;
    Oot3dPicaVisualTransitionStats Transition;
    uint64_t PreviousSequence = 0U;
    uint64_t CurrentSequence = 0U;
    size_t PreviousDrawCount = 0U;
    size_t CurrentDrawCount = 0U;
    bool Prepared = false;
};

Oot3dPicaPreparedVisualTransition::Oot3dPicaPreparedVisualTransition()
    : mImpl(std::make_unique<Impl>()) {
}

Oot3dPicaPreparedVisualTransition::~Oot3dPicaPreparedVisualTransition() =
    default;

Oot3dPicaPreparedVisualTransition::Oot3dPicaPreparedVisualTransition(
    Oot3dPicaPreparedVisualTransition&&) noexcept = default;

Oot3dPicaPreparedVisualTransition&
Oot3dPicaPreparedVisualTransition::operator=(
    Oot3dPicaPreparedVisualTransition&&) noexcept = default;

bool Oot3dPicaPreparedVisualTransition::Prepare(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    Oot3dPicaVisualInterpolationTiming* timing) {
    if (mImpl == nullptr) {
        mImpl = std::make_unique<Impl>();
    }
    Reset();

    const auto matchingStart = std::chrono::steady_clock::now();
    mImpl->Matching = BuildVisualDrawMatching(previous, current);
    const auto analysisStart = std::chrono::steady_clock::now();
    mImpl->Transition = AnalyzeVisualTransitionWithMatching(
        previous, current, mImpl->Matching, &mImpl->DrawDeltas);
    const auto finished = std::chrono::steady_clock::now();
    if (timing != nullptr) {
        timing->MatchingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                analysisStart - matchingStart)
                .count());
        timing->AnalysisNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - analysisStart)
                .count());
    }

    mImpl->PreviousSequence = previous.Sequence;
    mImpl->CurrentSequence = current.Sequence;
    mImpl->PreviousDrawCount = previous.Draws.size();
    mImpl->CurrentDrawCount = current.Draws.size();
    mImpl->Prepared = !current.Draws.empty();
    return mImpl->Prepared;
}

bool Oot3dPicaPreparedVisualTransition::Sample(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current, float alpha,
    Oot3dPicaVisualFrameViewSample& output,
    Oot3dPicaVisualInterpolationTiming* timing) const {
    if (!Ready() || previous.Sequence != mImpl->PreviousSequence ||
        current.Sequence != mImpl->CurrentSequence ||
        previous.Draws.size() != mImpl->PreviousDrawCount ||
        current.Draws.size() != mImpl->CurrentDrawCount) {
        return false;
    }

    const auto samplingStart = std::chrono::steady_clock::now();
    const bool sampled = SampleVisualFrameViewWithMatching(
        previous, current, alpha, mImpl->Matching, output,
        mImpl->DrawDeltas);
    if (timing != nullptr) {
        const auto finished = std::chrono::steady_clock::now();
        timing->SamplingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - samplingStart)
                .count());
    }
    return sampled;
}

void Oot3dPicaPreparedVisualTransition::Reset() noexcept {
    if (mImpl == nullptr) {
        return;
    }
    mImpl->Matching = {};
    mImpl->DrawDeltas.clear();
    mImpl->Transition = {};
    mImpl->PreviousSequence = 0U;
    mImpl->CurrentSequence = 0U;
    mImpl->PreviousDrawCount = 0U;
    mImpl->CurrentDrawCount = 0U;
    mImpl->Prepared = false;
}

bool Oot3dPicaPreparedVisualTransition::Ready() const noexcept {
    return mImpl != nullptr && mImpl->Prepared;
}

const Oot3dPicaVisualTransitionStats&
Oot3dPicaPreparedVisualTransition::Stats() const noexcept {
    static const Oot3dPicaVisualTransitionStats unavailable;
    return mImpl != nullptr ? mImpl->Transition : unavailable;
}

Oot3dPicaVulkanDrawOverrides Oot3dPicaVisualDrawView::Overrides() const {
    Oot3dPicaVulkanDrawOverrides overrides;
    overrides.VertexUniforms = VertexUniforms.has_value() ? &*VertexUniforms : nullptr;
    overrides.FragmentUniforms = FragmentUniforms.has_value() ? &*FragmentUniforms : nullptr;
    overrides.Viewport = Viewport.has_value() ? &*Viewport : nullptr;
    overrides.BlendConstantColor = BlendConstantColor.has_value() ? &*BlendConstantColor : nullptr;
    overrides.VertexBindings = HasVertexBindingOverrides ? &VertexBindings : nullptr;
    return overrides;
}

uint64_t
ComputeOot3dPicaVisualDrawIdentity(const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, plan.CompositionDomain);
    HashValue(hash, plan.Composition.Layer);
    HashValue(hash, plan.Composition.Provenance);
    HashValue(hash, plan.Composition.SourcePc);
    HashValue(hash, plan.Composition.NativeValue);
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.FragmentShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.VertexCount);
    HashValue(hash, plan.State.VertexInput.PhysicalBaseAddress);
    HashValue(hash, plan.State.VertexInput.IndexPhysicalAddress);
    HashValue(hash, plan.State.VertexInput.VertexOffset);
    for (const auto& loader : plan.State.VertexInput.Loaders) {
        HashValue(hash, loader.PhysicalAddress);
        HashValue(hash, loader.DataOffset);
        HashValue(hash, loader.ByteStride);
        HashValue(hash, loader.ComponentCount);
        for (const uint8_t component : loader.Components) {
            HashValue(hash, component);
        }
    }
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.Slot);
        HashValue(hash, texture.State.PhysicalAddress);
        HashValue(hash, texture.State.Width);
        HashValue(hash, texture.State.Height);
        HashValue(hash, texture.State.Format);
        HashValue(hash, texture.State.Type);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    HashValue(hash, plan.State.Framebuffer.ColorFormat);
    HashValue(hash, plan.State.Framebuffer.DepthFormat);
    HashValue(hash, plan.State.Topology);
    return hash;
}

bool AreOot3dPicaVisualDrawsCompatible(const Oot3dPicaVulkanDrawPlan& previous,
                                       const Oot3dPicaVulkanDrawPlan& current) {
    return HaveMatchingPipelineLayout(previous, current);
}

bool InterpolateOot3dPicaVisualDraw(const Oot3dPicaVulkanDrawPlan& previous,
                                    const Oot3dPicaVulkanDrawPlan& current,
                                    float alpha,
                                    Oot3dPicaVulkanDrawPlan& output) {
    if (!AreOot3dPicaVisualDrawsCompatible(previous, current)) {
        return false;
    }
    InterpolateCompatibleVisualDraw(previous, current, alpha, output);
    return true;
}

Oot3dPicaVisualTransitionStats
AnalyzeOot3dPicaVisualTransition(const Oot3dPicaVisualFrame& previous,
                                 const Oot3dPicaVisualFrame& current) {
    const auto matching = BuildVisualDrawMatching(previous, current);
    return AnalyzeVisualTransitionWithMatching(previous, current, matching);
}

bool SampleOot3dPicaVisualFrame(const Oot3dPicaVisualFrame& previous,
                                const Oot3dPicaVisualFrame& current,
                                float alpha,
                                Oot3dPicaVisualFrameSample& output) {
    const auto matching = BuildVisualDrawMatching(previous, current);
    return SampleVisualFrameWithMatching(previous, current, alpha, matching, output);
}

bool AnalyzeAndSampleOot3dPicaVisualFrame(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                          float alpha, Oot3dPicaVisualTransitionStats& stats,
                                          Oot3dPicaVisualFrameSample& output) {
    const auto matching = BuildVisualDrawMatching(previous, current);
    std::vector<VisualDrawDeltaFlags> drawDeltas;
    stats = AnalyzeVisualTransitionWithMatching(previous, current, matching, &drawDeltas);
    return SampleVisualFrameWithMatching(previous, current, alpha, matching, output, &drawDeltas);
}

bool AnalyzeAndSampleOot3dPicaVisualFrameView(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                              float alpha, Oot3dPicaVisualTransitionStats& stats,
                                              Oot3dPicaVisualFrameViewSample& output,
                                              Oot3dPicaVisualInterpolationTiming* timing) {
    const auto matchingStart = std::chrono::steady_clock::now();
    const auto matching = BuildVisualDrawMatching(previous, current);
    const auto analysisStart = std::chrono::steady_clock::now();
    std::vector<VisualDrawDeltaFlags> drawDeltas;
    stats = AnalyzeVisualTransitionWithMatching(previous, current, matching, &drawDeltas);
    const auto samplingStart = std::chrono::steady_clock::now();
    const bool sampled = SampleVisualFrameViewWithMatching(
        previous, current, alpha, matching, output, drawDeltas);
    if (timing != nullptr) {
        const auto finished = std::chrono::steady_clock::now();
        timing->MatchingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                analysisStart - matchingStart)
                .count());
        timing->AnalysisNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                samplingStart - analysisStart)
                .count());
        timing->SamplingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - samplingStart)
                .count());
    }
    return sampled;
}

bool ViewOot3dPicaVisualFrame(
    const Oot3dPicaVisualFrame& frame,
    Oot3dPicaVisualFrameViewSample& output) {
    output.TopTransfer = frame.TopTransfer;
    output.MemoryFills = &frame.MemoryFills;
    output.DisplayTransfers = &frame.DisplayTransfers;
    output.MatchedDraws = 0;
    output.StrictUniqueMatchedDraws = 0;
    output.StrictOrdinalMatchedDraws = 0;
    output.StructuralOrdinalMatchedDraws = 0;
    output.PipelineOrdinalMatchedDraws = 0;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.Draws.resize(frame.Draws.size());
    for (size_t index = 0; index < frame.Draws.size(); ++index) {
        auto& view = output.Draws[index];
        view.BasePlan = &frame.Draws[index];
        view.VertexUniforms.reset();
        view.FragmentUniforms.reset();
        view.Viewport.reset();
        view.BlendConstantColor.reset();
        view.VertexBindings.clear();
        view.HasVertexBindingOverrides = false;
    }
    return !output.Draws.empty();
}

void Oot3dPicaVisualFrameAccumulator::Append(Oot3dPicaVulkanDrawPlan plan) {
    mPendingDraws.push_back(std::move(plan));
}

void Oot3dPicaVisualFrameAccumulator::Append(
    Oot3dPicaMemoryFillSubmission fill) {
    mPendingMemoryFills.push_back(std::move(fill));
}

void Oot3dPicaVisualFrameAccumulator::Append(
    Oot3dPicaDisplayTransferSubmission transfer) {
    mPendingDisplayTransfers.push_back(std::move(transfer));
}

std::optional<Oot3dPicaVisualFrame> Oot3dPicaVisualFrameAccumulator::Finish(
    const Oot3dPicaDisplayTransferSubmission& topTransfer) {
    if (mPendingDraws.empty()) {
        return std::nullopt;
    }
    Oot3dPicaVisualFrame frame;
    frame.Sequence = mNextSequence++;
    frame.TopTransfer = topTransfer;
    frame.Draws = std::move(mPendingDraws);
    frame.StrictDrawIdentities.reserve(frame.Draws.size());
    for (const auto& draw : frame.Draws) {
        frame.StrictDrawIdentities.push_back(ComputeOot3dPicaVisualDrawIdentity(draw));
    }
    frame.MemoryFills = std::move(mPendingMemoryFills);
    frame.DisplayTransfers = std::move(mPendingDisplayTransfers);
    mPendingDraws.clear();
    mPendingMemoryFills.clear();
    mPendingDisplayTransfers.clear();
    return frame;
}

size_t Oot3dPicaVisualFrameAccumulator::PendingDrawCount() const {
    return mPendingDraws.size();
}

Oot3dPicaVisualFrameAccumulatorState
Oot3dPicaVisualFrameAccumulator::CaptureState() const {
    return {mNextSequence, mPendingDraws, mPendingMemoryFills,
            mPendingDisplayTransfers};
}

bool Oot3dPicaVisualFrameAccumulator::RestoreState(
    Oot3dPicaVisualFrameAccumulatorState state) {
    if (state.NextSequence == 0U) {
        return false;
    }
    mNextSequence = state.NextSequence;
    mPendingDraws = std::move(state.PendingDraws);
    mPendingMemoryFills = std::move(state.PendingMemoryFills);
    mPendingDisplayTransfers = std::move(state.PendingDisplayTransfers);
    return true;
}

double Oot3dPicaVisualContinuityTracker::CurrentThreshold() const {
    constexpr double kMinimumNormalizedDeltaThreshold = 0.5;
    constexpr double kBaselineMultiplier = 4.0;
    constexpr double kBaselineAllowance = 0.1;
    if (mAcceptedDeltas.empty()) {
        return kMinimumNormalizedDeltaThreshold;
    }
    auto sorted = mAcceptedDeltas;
    std::sort(sorted.begin(), sorted.end());
    const double baseline = sorted[sorted.size() / 2U];
    return std::max(kMinimumNormalizedDeltaThreshold,
                    baseline * kBaselineMultiplier + kBaselineAllowance);
}

bool Oot3dPicaVisualContinuityTracker::Accept(
    const Oot3dPicaVisualTransitionStats& transition) {
    constexpr size_t kMaximumAcceptedDeltaHistory = 31U;
    constexpr double kMinimumMatchedDrawCoverage = 0.85;
    const bool structuralDiscontinuity =
        transition.MatchedDrawCoverage < kMinimumMatchedDrawCoverage;
    const bool motionOutlier =
        transition.MedianNormalizedContinuousDelta > CurrentThreshold();
    if (transition.TopTargetMatchedDraws == 0U ||
        (transition.CameraDiscontinuousDraws != 0U &&
         transition.CameraDiscontinuousDraws * 2U >= transition.CameraMatchedDraws) ||
        (structuralDiscontinuity && motionOutlier)) {
        mAcceptedDeltas.clear();
        return false;
    }
    if (mAcceptedDeltas.size() == kMaximumAcceptedDeltaHistory) {
        mAcceptedDeltas.erase(mAcceptedDeltas.begin());
    }
    mAcceptedDeltas.push_back(
        transition.MedianNormalizedContinuousDelta);
    return true;
}

Oot3dPicaVisualContinuityTrackerState
Oot3dPicaVisualContinuityTracker::CaptureState() const {
    return {mAcceptedDeltas};
}

bool Oot3dPicaVisualContinuityTracker::RestoreState(
    Oot3dPicaVisualContinuityTrackerState state) {
    constexpr size_t kMaximumAcceptedDeltaHistory = 31U;
    if (state.AcceptedDeltas.size() > kMaximumAcceptedDeltaHistory ||
        std::any_of(state.AcceptedDeltas.begin(), state.AcceptedDeltas.end(),
                    [](double value) {
                        return !std::isfinite(value) || value < 0.0;
                    })) {
        return false;
    }
    mAcceptedDeltas = std::move(state.AcceptedDeltas);
    return true;
}

} // namespace Oot3dNativeGame
