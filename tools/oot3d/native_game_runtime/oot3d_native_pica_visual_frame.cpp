#include "oot3d_native_pica_visual_frame.h"
#include "fast/renderer3ds/pica_vertex_temporal_policy.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"
#include "fast/renderer3ds/pica_factored_interpolation.h"
#include "oot3d_native_pica_affine_layout.h"

#include <algorithm>
#include <tuple>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <map>
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
    bool LightBinding = false;
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
    // Native instance matches are independent of draw order and cardinality.
    // Reserve every evidenced draw from the legacy interpolation matcher,
    // including unmatched/ambiguous ones, so it cannot pair their uniforms by
    // occurrence. Unknown producers retain their existing visual sampling,
    // but cannot acquire exact renderer motion history.
    using NativeKey = std::pair<Fast::Renderer3ds::PicaTemporalIdentity, std::array<uint64_t, 8>>;
    const auto nativeKey = [](const Oot3dPicaVulkanDrawPlan& draw) -> NativeKey {
        const auto& fb = draw.State.Framebuffer;
        return {draw.Composition.TemporalIdentity, {fb.ColorPhysicalAddress, fb.DepthPhysicalAddress,
            fb.Width, fb.Height, fb.ColorFormat, fb.DepthFormat,
            uint64_t(uint16_t(draw.State.Viewport.CornerX)), uint64_t(uint16_t(draw.State.Viewport.CornerY))}};
    };
    std::map<NativeKey, std::vector<size_t>> oldInstances, newInstances;
    for (size_t i = 0; i < previous.Draws.size(); ++i)
        if (previous.Draws[i].Composition.TemporalIdentity.Available()) {
            oldInstances[nativeKey(previous.Draws[i])].push_back(i);
            previousMatched[i] = true;
        }
    for (size_t i = 0; i < current.Draws.size(); ++i)
        if (current.Draws[i].Composition.TemporalIdentity.Available()) {
            newInstances[nativeKey(current.Draws[i])].push_back(i);
            currentMatched[i] = true;
        }
    for (const auto& [key, indices] : oldInstances)
        if (indices.size() != 1) matching.AmbiguousPrevious += indices.size();
    for (const auto& [key, indices] : newInstances) {
        if (indices.size() != 1) matching.AmbiguousCurrent += indices.size();
        const auto old = oldInstances.find(key);
        if (indices.size() == 1 && old != oldInstances.end() && old->second.size() == 1 &&
            HaveMatchingPipelineLayout(previous.Draws[old->second[0]], current.Draws[indices[0]])) {
            matching.PreviousByCurrent[indices[0]] = {old->second[0], VisualDrawMatchKind::StrictUnique};
            ++matching.StrictUniqueMatches;
        }
    }
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

    const auto actualMatches = static_cast<size_t>(std::count_if(
        matching.PreviousByCurrent.begin(), matching.PreviousByCurrent.end(),
        [](const auto& match) { return match.has_value(); }));
    matching.UnmatchedPrevious = previous.Draws.size() - actualMatches;
    matching.UnmatchedCurrent = current.Draws.size() - actualMatches;
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
        delta.LightBinding = previousDraw.LightBinding != currentDraw.LightBinding;
        delta.Continuous = delta.PerInstanceVertex || delta.PerVertex ||
                           delta.VertexUniforms || delta.FragmentUniforms ||
                           delta.Viewport || delta.BlendConstantColor || delta.LightBinding;
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

struct VisualSceneLightInterpolation {
    using Snapshot = Fast::Renderer3ds::PicaScenePointLights;
    std::shared_ptr<const Snapshot> Previous, Current, Sampled;
    using DirectionalSnapshot = Fast::Renderer3ds::PicaSceneDirectionalLights;
    std::shared_ptr<const DirectionalSnapshot> PreviousDirectionals, CurrentDirectionals, SampledDirectionals;

    std::shared_ptr<const DirectionalSnapshot> ResolveDirectionals(
        const std::shared_ptr<const DirectionalSnapshot>& previous,
        const std::shared_ptr<const DirectionalSnapshot>& current, float alpha) {
        if (!previous || !current || previous == current || alpha >= 1.0F) return current;
        // Never across a discontinuity of the scene's light.
        if (previous->ContinuityEpoch != current->ContinuityEpoch) return current;
        if (PreviousDirectionals == previous && CurrentDirectionals == current) return SampledDirectionals;
        auto sample = std::make_shared<DirectionalSnapshot>(*current);
        if (previous->ShadowCasterIdentity == current->ShadowCasterIdentity)
            sample->ShadowStrength = InterpolateFinite(previous->ShadowStrength, current->ShadowStrength, alpha);
        for (uint32_t i = 0; i < sample->Count; ++i) {
            auto& light = sample->Lights[i];
            const auto end = previous->Lights.begin() + previous->Count;
            const auto found = std::find_if(previous->Lights.begin(), end, [&](const auto& before) {
                return before.SourceIdentity == light.SourceIdentity;
            });
            if (found == end) continue;
            for (size_t channel = 0; channel < 3; ++channel) {
                light.DirectionTowardSource[channel] = InterpolateFinite(
                    found->DirectionTowardSource[channel], light.DirectionTowardSource[channel], alpha);
                light.Color[channel] = InterpolateFinite(found->Color[channel], light.Color[channel], alpha);
            }
        }
        PreviousDirectionals = previous;
        CurrentDirectionals = current;
        SampledDirectionals = std::move(sample);
        return SampledDirectionals;
    }

    std::shared_ptr<const Snapshot> Resolve(const std::shared_ptr<const Snapshot>& previous,
                                          const std::shared_ptr<const Snapshot>& current, float alpha) {
        if (!previous || !current || previous == current || alpha >= 1.0F) return current;
        if (Previous == previous && Current == current) return Sampled;
        auto sample = std::make_shared<Snapshot>(*current);
        for (uint32_t i = 0; i < sample->Count; ++i) {
            auto& light = sample->Lights[i];
            const auto end = previous->Lights.begin() + previous->Count;
            const auto found = std::find_if(previous->Lights.begin(), end, [&](const auto& before) {
                return before.SourceIdentity == light.SourceIdentity && before.Group == light.Group;
            });
            if (found == end) continue;
            for (size_t channel = 0; channel < 3; ++channel) {
                light.PositionWorld[channel] = InterpolateFinite(found->PositionWorld[channel], light.PositionWorld[channel], alpha);
                light.Color[channel] = InterpolateFinite(found->Color[channel], light.Color[channel], alpha);
            }
            light.Radius = InterpolateFinite(found->Radius, light.Radius, alpha);
        }
        Previous = previous;
        Current = current;
        Sampled = std::move(sample);
        return Sampled;
    }
};

void InterpolateVisualLightBinding(
    const Fast::Renderer3ds::PicaNativeLightBinding& previous,
    const Fast::Renderer3ds::PicaNativeLightBinding& current, float alpha,
    Fast::Renderer3ds::PicaNativeLightBinding& output, VisualSceneLightInterpolation& scenePoints) {
    output = current;
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    output.ScenePoints = scenePoints.Resolve(previous.ScenePoints, current.ScenePoints, alpha);
    output.SceneDirectionals = scenePoints.ResolveDirectionals(previous.SceneDirectionals, current.SceneDirectionals, alpha);
    if (previous.WorldToView && current.WorldToView) {
        for (size_t component = 0; component < output.WorldToView->size(); ++component)
            (*output.WorldToView)[component] = InterpolateFinite(
                (*previous.WorldToView)[component], (*current.WorldToView)[component], alpha);
    } else {
        // Do not associate an interpolated pose with an unpaired live camera.
        output.WorldToView.reset();
    }
    for (size_t slot = 0; slot < output.Sources.size(); ++slot) {
        const auto& before = previous.Sources[slot];
        const auto& after = current.Sources[slot];
        if (before.SourceIdentity == 0U || before.SourceIdentity != after.SourceIdentity ||
            before.Kind != after.Kind) continue;
        auto& light = output.Sources[slot];
        for (size_t component = 0; component < 3U; ++component) {
            light.PositionWorld[component] = InterpolateFinite(before.PositionWorld[component], after.PositionWorld[component], alpha);
            light.UnattenuatedColor[component] = InterpolateFinite(before.UnattenuatedColor[component], after.UnattenuatedColor[component], alpha);
            light.ReferenceColor[component] = InterpolateFinite(before.ReferenceColor[component], after.ReferenceColor[component], alpha);
        }
        light.Radius = InterpolateFinite(before.Radius, after.Radius, alpha);
    }
}

// Return a conservative exact mask of the slots referenced by this draw.
// A missing snapshot or an unfamiliar addressing mode disables factoring;
// selecting rigid-looking rows from an unknown active palette could separate
// a joint shared with an excluded, non-rigid matrix.
std::optional<uint32_t> CmbPaletteSlots(
    const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current) {
    const auto& a = previous.VertexShader.Uniforms;
    const auto& b = current.VertexShader.Uniforms;
    if (a.BooleanMask != b.BooleanMask || a.Integers != b.Integers) return {};
    for (size_t row : {89U, 92U, 93U, 94U, 95U}) if (a.Floats[row] != b.Floats[row]) return {};
    if (b.Floats[93] != std::array<float,4>{0,1,2,3} ||
        b.Floats[95][0] != 3.0F || b.Floats[95][1] != 4.0F ||
        (b.Floats[89][3] != 0.0F && b.Floats[89][3] != 1.0F)) return {};
    if (!(b.BooleanMask & 4U)) return 1U; // unskinned CMB: c20..22 only
    size_t influences = 1;
    if (b.BooleanMask & 8U) {
        const float count = b.Floats[92][3];
        if (count != 2.0F && count != 3.0F && count != 4.0F) return {};
        influences = static_cast<size_t>(count);
    }
    const auto findAttribute = [](const auto& plan, uint8_t location) -> const Oot3dPicaVulkanVertexAttribute* {
        const auto it = std::find_if(plan.VertexAttributes.begin(), plan.VertexAttributes.end(),
            [=](const auto& attribute) { return attribute.Location == location; });
        return it == plan.VertexAttributes.end() ? nullptr : &*it;
    };
    const auto* left = findAttribute(previous,6), *right = findAttribute(current,6);
    if (!left || !right || left->ComponentCount < influences || right->ComponentCount < influences ||
        !current.VertexCount || previous.VertexCount != current.VertexCount ||
        previous.Indexed != current.Indexed || previous.BaseVertex != current.BaseVertex ||
        previous.IndicesAre16Bit != current.IndicesAre16Bit || !EqualIndexBytes(previous,current)) return {};
    const auto read = [](const auto& plan, const auto& attribute, size_t vertex, size_t component)
        -> std::optional<float> {
        const auto* binding = FindVertexBinding(plan,attribute.Binding);
        if (!binding || !binding->ByteStride) return {};
        const size_t size = VertexFormatByteSize(attribute.Format);
        if (!size || attribute.ByteOffset > binding->ByteStride ||
            size*attribute.ComponentCount > binding->ByteStride-attribute.ByteOffset) return {};
        const auto bytes = binding->ResolvedBytes();
        const size_t record = binding->InputRate == Oot3dPicaVertexInputRate::PerInstance ? 0 : vertex;
        if (record >= bytes.size()/binding->ByteStride) return {};
        const size_t offset = record*binding->ByteStride+attribute.ByteOffset+component*size;
        switch (attribute.Format) {
        case Oot3dPicaVertexFormat::UnsignedByte: return float(bytes[offset]);
        case Oot3dPicaVertexFormat::SignedByte: return float(static_cast<int8_t>(bytes[offset]));
        case Oot3dPicaVertexFormat::SignedShort: {
            int16_t value; std::memcpy(&value,bytes.data()+offset,2); return float(value);
        }
        case Oot3dPicaVertexFormat::Float: {
            float value; std::memcpy(&value,bytes.data()+offset,4); return value;
        }
        }
        return {};
    };
    uint32_t selected = 0;
    const auto indices = current.ResolvedIndexBytes();
    const size_t indexSize = current.IndicesAre16Bit ? 2 : 1;
    if (current.Indexed && current.VertexCount > indices.size()/indexSize) return {};
    for (size_t i = 0; i < current.VertexCount; ++i) {
        int64_t vertex = static_cast<int64_t>(i);
        if (current.Indexed) {
            vertex = indices[i*indexSize];
            if (indexSize == 2) vertex |= int64_t(indices[i*indexSize+1]) << 8;
            vertex += current.BaseVertex;
        }
        if (vertex < 0) return {};
        for (size_t component = 0; component < influences; ++component) {
            const auto from = read(previous,*left,size_t(vertex),component);
            const auto to = read(current,*right,size_t(vertex),component);
            // Reject fractional, wrapped and out-of-palette addresses instead
            // of approximating PICA's MOVA/wrap semantics with a guessed span.
            if (!from || !to || !std::isfinite(*to) || *from != *to ||
                *to < 0 || *to >= 10 || std::floor(*to) != *to) return {};
            selected |= 1U << static_cast<uint32_t>(*to);
        }
    }
    return selected;
}

bool CmbAffineLayouts(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current) {
    const auto& from = previous.VertexShader.TemporalProgram;
    const auto& to = current.VertexShader.TemporalProgram;
    return from && to && from->AffineLayout == kOot3dCmbAffineLayout && to->AffineLayout == from->AffineLayout;
}

using VisualUniformFloats = decltype(Oot3dPicaVertexUniformState{}.Floats);
using VisualMatrixBits = std::array<uint32_t, 12>;

VisualMatrixBits AffineBits(const VisualUniformFloats& floats, size_t first) {
    VisualMatrixBits bits{};
    for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c)
        bits[row * 4 + c] = std::bit_cast<uint32_t>(floats[first + row][c]);
    return bits;
}

// A draw's referenced palette slots and the left motion it shares with the
// other draws of its actor, prepared once per interpolated sample. A draw of
// the actor that has no previous match carries a reconstructed previous
// palette instead of being shown at the new pose.
struct VisualFactoredPalette {
    bool Prepared = false;
    std::optional<uint32_t> Selected;
    std::optional<Fast::Renderer3ds::PicaRigidMotionRows> SharedMotion;
    std::optional<VisualUniformFloats> CarriedPrevious;
};

// An actor is drawn in pieces (body, hands, attachments) whose palettes can
// share joints across draws: the hand continues the arm. The factored proof
// keeps such a joint only if both draws use the SAME left motion, so the
// draws of one actor whose palettes share an output space (equal model-view
// rows, the same at both endpoints for every member) take one motion: the
// least-residual motion over all the actor's bones, the body's rather than a
// swinging limb's (PicaLeastResidualMotion). Native actor evidence (owner and
// lifetime) defines the actor; without it each draw keeps its own reference.
//
// The game also swaps pieces between frames (Link's open and closed hands
// when he turns). Such a draw has no previous match: shown at its new pose
// while the rest of the actor is still in between, it would come off the
// body for that sample. It is interpolated from a previous palette rebuilt
// from the actor: a bone the actor's matched draws also use this frame (the
// same matrix) takes that bone's previous pose, so both copies move together;
// any other bone is carried by the actor's common motion (inverse(D) * N1).
std::vector<VisualFactoredPalette> ShareVisualPaletteMotions(
    const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
    const std::vector<std::optional<VisualDrawMatch>>& previousByCurrent) {
    std::vector<VisualFactoredPalette> result(current.Draws.size());
    using Key = std::tuple<uint64_t, uint64_t, VisualMatrixBits>;
    struct Group {
        VisualMatrixBits PriorSpace{};
        std::optional<Fast::Renderer3ds::PicaRigidMotionRows> Motion;
        // Current bone matrix -> its previous pose; empty when ambiguous.
        std::map<VisualMatrixBits, std::optional<VisualMatrixBits>> Bones;
        // Each distinct bone once, whichever draws reference it.
        std::vector<Fast::Renderer3ds::PicaAffineEndpoints> Matrices;
    };
    const auto endpoints = [](const VisualUniformFloats& before, const VisualUniformFloats& after, size_t first) {
        Fast::Renderer3ds::PicaAffineEndpoints matrix{};
        for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c) {
            matrix[0][row][c] = before[first + row][c];
            matrix[1][row][c] = after[first + row][c];
        }
        return matrix;
    };
    std::map<Key, Group> groups;
    std::vector<std::optional<Key>> keys(current.Draws.size());
    const auto actorKey = [](const Oot3dPicaVulkanDrawPlan& plan) -> std::optional<Key> {
        const auto& id = plan.Composition.TemporalIdentity;
        if (!id.Available() || id.Owner == 0) return {};
        const auto first = plan.VertexShader.TemporalProgram->AffineLayout.ModelView.FirstUniform;
        return Key{id.Owner, id.Lifetime, AffineBits(plan.VertexShader.Uniforms.Floats, first)};
    };
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        const auto& match = previousByCurrent[index];
        if (!match) continue;
        const auto& before = previous.Draws[match->PreviousIndex];
        const auto& after = current.Draws[index];
        if (!CmbAffineLayouts(before, after)) continue;
        auto& entry = result[index];
        entry.Prepared = true;
        entry.Selected = CmbPaletteSlots(before, after);
        const auto key = actorKey(after);
        const auto& priorId = before.Composition.TemporalIdentity;
        if (!entry.Selected || !key || priorId.Owner != std::get<0>(*key) || priorId.Lifetime != std::get<1>(*key))
            continue;
        const auto& layout = after.VertexShader.TemporalProgram->AffineLayout;
        const auto priorSpace = AffineBits(before.VertexShader.Uniforms.Floats, layout.ModelView.FirstUniform);
        auto [found, inserted] = groups.try_emplace(*key);
        auto& group = found->second;
        if (inserted) group.PriorSpace = priorSpace;
        else if (group.PriorSpace != priorSpace) continue;
        keys[index] = key;
        for (size_t slot = 0; slot < layout.Palette.MatrixCount; ++slot) {
            if (!(*entry.Selected & (1U << slot))) continue;
            const size_t first = layout.Palette.FirstUniform + 3 * slot;
            const auto prior = AffineBits(before.VertexShader.Uniforms.Floats, first);
            const auto [bone, added] = group.Bones.try_emplace(AffineBits(after.VertexShader.Uniforms.Floats, first), prior);
            if (added)
                group.Matrices.push_back(endpoints(before.VertexShader.Uniforms.Floats,
                                                   after.VertexShader.Uniforms.Floats, first));
            else if (bone->second != prior)
                bone->second.reset();
        }
    }
    for (auto& [key, group] : groups) group.Motion = Fast::Renderer3ds::PicaLeastResidualMotion(group.Matrices);
    for (size_t index = 0; index < current.Draws.size(); ++index)
        if (keys[index]) result[index].SharedMotion = groups[*keys[index]].Motion;
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        const auto& after = current.Draws[index];
        if (previousByCurrent[index] || !CmbAffineLayouts(after, after)) continue;
        const auto key = actorKey(after);
        const auto found = key ? groups.find(*key) : groups.end();
        if (found == groups.end() || !found->second.Motion) continue;
        const auto selected = CmbPaletteSlots(after, after);
        if (!selected) continue;
        const auto& group = found->second;
        const auto& motion = *group.Motion;
        const auto& layout = after.VertexShader.TemporalProgram->AffineLayout;
        VisualUniformFloats prior = after.VertexShader.Uniforms.Floats;
        // The group's previous model-view: the piece shares the actor's space.
        for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c)
            prior[layout.ModelView.FirstUniform + row][c] = std::bit_cast<float>(group.PriorSpace[row * 4 + c]);
        for (size_t slot = 0; slot < layout.Palette.MatrixCount; ++slot) {
            if (!(*selected & (1U << slot))) continue;
            const size_t first = layout.Palette.FirstUniform + 3 * slot;
            const auto bone = group.Bones.find(AffineBits(after.VertexShader.Uniforms.Floats, first));
            if (bone != group.Bones.end() && bone->second) {
                for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c)
                    prior[first + row][c] = std::bit_cast<float>((*bone->second)[row * 4 + c]);
                continue;
            }
            // inverse(D) * N1 with D = [R | t]: R^T * (N1 - [0 | t]).
            for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c) {
                double value = 0.0;
                for (size_t k = 0; k < 3; ++k)
                    value += motion[k][row] * (double(after.VertexShader.Uniforms.Floats[first + k][c]) -
                                               (c == 3 ? motion[k][3] : 0.0));
                prior[first + row][c] = static_cast<float>(value);
            }
        }
        auto& entry = result[index];
        entry.Prepared = true;
        entry.Selected = selected;
        entry.SharedMotion = motion;
        entry.CarriedPrevious = prior;
    }
    return result;
}

// An unmatched draw of an actor, interpolated from its carried palette.
bool InterpolateCarriedVisualDraw(const Oot3dPicaVulkanDrawPlan& current, const VisualFactoredPalette& palette,
                                  float alpha, Oot3dPicaVertexUniformState& vertexUniforms) {
    if (!palette.CarriedPrevious || !palette.Selected || !palette.SharedMotion) return false;
    const auto& layout = current.VertexShader.TemporalProgram->AffineLayout;
    auto factored = current.VertexShader.Uniforms.Floats;
    if (!Fast::Renderer3ds::InterpolatePicaFactoredUniformGroup(
            *palette.CarriedPrevious, current.VertexShader.Uniforms.Floats, layout.Palette, *palette.Selected,
            alpha, factored, &*palette.SharedMotion) ||
        !Fast::Renderer3ds::InterpolatePicaFactoredUniformGroup(
            *palette.CarriedPrevious, current.VertexShader.Uniforms.Floats, layout.ModelView, 1U, alpha, factored))
        return false;
    vertexUniforms.Floats = factored;
    return true;
}

void InterpolateVisualVertexUniforms(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms,
    const VisualFactoredPalette* palette = nullptr) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    const auto& before = previous.VertexShader.Uniforms.Floats;
    const auto& after = current.VertexShader.Uniforms.Floats;
    for (size_t index = 0; index < vertexUniforms.Floats.size(); ++index) {
        if (!IsContinuousVertexUniform(index)) continue;
        for (size_t component = 0; component < 4; ++component)
            vertexUniforms.Floats[index][component] = alpha == 0.0F ? before[index][component]
                : alpha == 1.0F ? after[index][component]
                : InterpolateFinite(before[index][component],after[index][component],alpha);
    }
    if (!(alpha > 0.0F && alpha < 1.0F)) return;
    const auto& previousProgram = previous.VertexShader.TemporalProgram;
    const auto& currentProgram = current.VertexShader.TemporalProgram;
    if (previousProgram && currentProgram) {
        const auto previousTranslations = DescribeOot3dTextureTranslations(
            previousProgram->CmbTextureCoordinates, before, previous.VertexShader.Uniforms.BooleanMask,
            previous.State, previous.FragmentShader.Uniforms.FragmentControl[2]);
        const auto currentTranslations = DescribeOot3dTextureTranslations(
            currentProgram->CmbTextureCoordinates, after, current.VertexShader.Uniforms.BooleanMask,
            current.State, current.FragmentShader.Uniforms.FragmentControl[2]);
        Fast::Renderer3ds::InterpolatePicaTextureTranslations(
            before, after, previousTranslations, currentTranslations, alpha, vertexUniforms.Floats);
    }
    if (!CmbAffineLayouts(previous, current)) return;
    const auto selected = palette && palette->Prepared ? palette->Selected : CmbPaletteSlots(previous,current);
    if (!selected) return;
    const auto& layout = current.VertexShader.TemporalProgram->AffineLayout;
    const auto* shared = palette && palette->SharedMotion ? &*palette->SharedMotion : nullptr;
    auto factored = vertexUniforms.Floats;
    // Palette outputs feed model-view inputs: two coordinate spaces, hence
    // separate motions. Commit both groups together or keep scalar fallback.
    if (Fast::Renderer3ds::InterpolatePicaFactoredUniformGroup(
            before,after,layout.Palette,*selected,alpha,factored,shared) &&
        Fast::Renderer3ds::InterpolatePicaFactoredUniformGroup(
            before,after,layout.ModelView,1U,alpha,factored))
        vertexUniforms.Floats = factored;
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
    std::array<float, 4>& blendConstantColor,
    const VisualFactoredPalette* palette = nullptr) {
    InterpolateVisualVertexUniforms(previous, current, alpha,
                                    vertexUniforms, palette);
    InterpolateVisualFragmentUniforms(previous, current, alpha,
                                      fragmentUniforms);
    InterpolateVisualViewport(previous, current, alpha, viewport);
    InterpolateVisualBlendConstantColor(previous, current, alpha,
                                        blendConstantColor);
}

void InterpolateCompatibleVisualDraw(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current,
                                     float alpha, Oot3dPicaVulkanDrawPlan& output,
                                     VisualSceneLightInterpolation& scenePoints,
                                     const VisualFactoredPalette* palette = nullptr) {
    output = current;
    InterpolateCompatibleVisualRenderState(previous, current, alpha, output.VertexShader.Uniforms,
                                           output.FragmentShader.Uniforms, output.State.Viewport,
                                           output.State.OutputMerger.Blend.ConstantColor, palette);
    InterpolateVisualLightBinding(previous.LightBinding, current.LightBinding, alpha, output.LightBinding, scenePoints);
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
    VisualSceneLightInterpolation scenePoints;
    const auto palettes = alpha > 0.0F && alpha < 1.0F
        ? ShareVisualPaletteMotions(previous, current, matching.PreviousByCurrent)
        : std::vector<VisualFactoredPalette>{};

    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& currentDraw = current.Draws[currentIndex];
        const auto match = matching.PreviousByCurrent[currentIndex];
        auto& sampled = output.Draws[currentIndex];
        if (match.has_value()) {
            const auto& previousDraw = previous.Draws[match->PreviousIndex];
            InterpolateCompatibleVisualDraw(previousDraw, currentDraw, alpha, sampled, scenePoints,
                                            palettes.empty() ? nullptr : &palettes[currentIndex]);
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
                delta.LightBinding = previousDraw.LightBinding != currentDraw.LightBinding;
                delta.Continuous =
                    delta.PerInstanceVertex || delta.PerVertex ||
                    delta.VertexUniforms || delta.FragmentUniforms ||
                    delta.Viewport || delta.BlendConstantColor || delta.LightBinding;
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
            if (!palettes.empty() && palettes[currentIndex].CarriedPrevious &&
                InterpolateCarriedVisualDraw(currentDraw, palettes[currentIndex], alpha,
                                             sampled.VertexShader.Uniforms))
                ++output.InterpolatedDraws;
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
    output.MemoryFills = current.MemoryFills;
    output.DisplayTransfers = current.DisplayTransfers;
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
    VisualSceneLightInterpolation scenePoints;
    const auto palettes = alpha > 0.0F && alpha < 1.0F
        ? ShareVisualPaletteMotions(previous, current, matching.PreviousByCurrent)
        : std::vector<VisualFactoredPalette>{};

    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& currentDraw = current.Draws[currentIndex];
        auto& sampled = output.Draws[currentIndex];
        sampled.BasePlan = &currentDraw;
        sampled.VertexUniforms.reset();
        sampled.FragmentUniforms.reset();
        sampled.Viewport.reset();
        sampled.BlendConstantColor.reset();
        sampled.LightBinding.reset();
        sampled.HasVertexBindingOverrides = false;

        const auto match = matching.PreviousByCurrent[currentIndex];
        if (!match.has_value()) {
            if (!palettes.empty() && palettes[currentIndex].CarriedPrevious) {
                sampled.VertexUniforms.emplace(currentDraw.VertexShader.Uniforms);
                if (InterpolateCarriedVisualDraw(currentDraw, palettes[currentIndex], alpha, *sampled.VertexUniforms))
                    ++output.InterpolatedDraws;
                else
                    sampled.VertexUniforms.reset();
            }
            continue;
        }
        const auto& previousDraw = previous.Draws[match->PreviousIndex];
        const auto& delta = drawDeltas[currentIndex];
        if (delta.LightBinding) {
            sampled.LightBinding.emplace();
            InterpolateVisualLightBinding(previousDraw.LightBinding, currentDraw.LightBinding,
                                           alpha, *sampled.LightBinding, scenePoints);
        }
        if (delta.VertexUniforms) {
            sampled.VertexUniforms.emplace(currentDraw.VertexShader.Uniforms);
            InterpolateVisualVertexUniforms(
                previousDraw, currentDraw, alpha, *sampled.VertexUniforms,
                palettes.empty() ? nullptr : &palettes[currentIndex]);
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
    overrides.LightBinding = LightBinding.has_value() ? &*LightBinding : nullptr;
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
    HashValue(hash, plan.Composition.NativeProjectedShadow);
    HashValue(hash, plan.Composition.NativeActorGeometry);
    HashValue(hash, plan.Composition.NativeActorIdentity);
    const auto& temporal = plan.Composition.TemporalIdentity;
    HashValue(hash, temporal.Owner); HashValue(hash, temporal.Instance);
    HashValue(hash, temporal.Part); HashValue(hash, temporal.Slot); HashValue(hash, temporal.Lifetime);
    HashValue(hash, plan.Composition.IsolatedLighting);
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
    VisualSceneLightInterpolation scenePoints;
    InterpolateCompatibleVisualDraw(previous, current, alpha, output, scenePoints);
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
    Oot3dPicaVisualFrameViewSample& output, bool skipCompletedDependencies) {
    output.TopTransfer = frame.TopTransfer;
    output.MemoryFills = frame.MemoryFills;
    output.DisplayTransfers = frame.DisplayTransfers;
    size_t firstDraw = 0U;
    if (skipCompletedDependencies && frame.DependencyExecutedThroughDraw) {
        const auto lastDraw = *frame.DependencyExecutedThroughDraw;
        while (firstDraw < frame.Draws.size() &&
               frame.Draws[firstDraw].SubmissionId <= lastDraw) ++firstDraw;
        const auto firstFill = std::find_if(
            frame.MemoryFills.begin(), frame.MemoryFills.end(),
            [lastDraw](const auto& fill) {
                return fill.BeforeDrawSubmissionId > lastDraw;
            });
        output.MemoryFills = {firstFill, frame.MemoryFills.end()};
        output.DisplayTransfers = output.DisplayTransfers.subspan(
            std::min(frame.DependencyExecutedTransfers,
                     output.DisplayTransfers.size()));
    }
    output.MatchedDraws = 0;
    output.StrictUniqueMatchedDraws = 0;
    output.StrictOrdinalMatchedDraws = 0;
    output.StructuralOrdinalMatchedDraws = 0;
    output.PipelineOrdinalMatchedDraws = 0;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.Draws.resize(frame.Draws.size() - firstDraw);
    for (size_t index = 0; index < output.Draws.size(); ++index) {
        auto& view = output.Draws[index];
        view.BasePlan = &frame.Draws[firstDraw + index];
        view.VertexUniforms.reset();
        view.FragmentUniforms.reset();
        view.Viewport.reset();
        view.BlendConstantColor.reset();
        view.LightBinding.reset();
        view.VertexBindings.clear();
        view.HasVertexBindingOverrides = false;
    }
    return !frame.Draws.empty();
}

void CollectOot3dPicaPlanCopyReferences(
    const Oot3dPicaVulkanDrawPlan& plan,
    std::vector<Fast::Renderer3ds::PicaTextureCopyReference>& out) {
    for (const auto& texture : plan.Textures) {
        out.insert(out.end(), texture.CopyReferences.begin(),
                   texture.CopyReferences.end());
    }
}

void CollectOot3dPicaFrameCopyReferences(
    const Oot3dPicaVisualFrame& frame,
    std::vector<Fast::Renderer3ds::PicaTextureCopyReference>& out) {
    for (const auto& plan : frame.Draws) {
        CollectOot3dPicaPlanCopyReferences(plan, out);
    }
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
    auto frames = FinishFrames(topTransfer, {});
    if (frames.empty()) {
        return std::nullopt;
    }
    return std::move(frames.back());
}

std::vector<Oot3dPicaVisualFrame> Oot3dPicaVisualFrameAccumulator::FinishFrames(
    const Oot3dPicaDisplayTransferSubmission& topTransfer,
    std::span<const uint32_t> displayOutputs,
    std::span<const Oot3dPicaViewportState> continuationViewports) {
    std::vector<Oot3dPicaVisualFrame> frames;
    if (mPendingDraws.empty()) {
        return frames;
    }
    // A raw-copy dependency flush has executed every pending draw, fill and
    // transfer up to the last completed copy (ViewDependencyWork). The guest
    // may have issued that copy in its next frame, after the selected
    // snapshot: the boundary then also covers the frames closed here, which no
    // longer hold the copy.
    std::optional<uint64_t> executedThroughDraw;
    size_t executedTransfers = 0U;
    for (size_t index = 0U; index < mPendingDisplayTransfers.size(); ++index) {
        const auto& transfer = mPendingDisplayTransfers[index];
        if (transfer.Transfer.TextureCopyBytes != 0U && !transfer.SignalInterrupt) {
            executedThroughDraw = transfer.AfterDrawSubmissionId;
            executedTransfers = index + 1U;
        }
    }
    // The guest may already have drawn its next frame into the other display
    // buffer: from the first later draw into the selected transfer's source,
    // the work belongs to the next visual frame and stays pending (the
    // selected frame's later draws into other targets, such as the bottom
    // screen, remain in it). Draws into the source on a continuation canvas
    // (a lower-screen menu relocated onto the upper target after the upper
    // snapshot) still complete the frame that snapshot belongs to.
    const auto continuesFrame = [&](const Oot3dPicaVulkanDrawPlan& draw) {
        const auto& viewport = draw.State.Viewport;
        return std::any_of(continuationViewports.begin(), continuationViewports.end(),
                           [&](const Oot3dPicaViewportState& canvas) {
                               return viewport.CornerX == canvas.CornerX &&
                                      viewport.CornerY == canvas.CornerY &&
                                      viewport.HalfWidth == canvas.HalfWidth &&
                                      viewport.HalfHeight == canvas.HalfHeight;
                           });
    };
    const auto startsNextFrame = [&](const Oot3dPicaVulkanDrawPlan& draw, uint64_t snapshotDraw) {
        return draw.SubmissionId > snapshotDraw &&
               draw.State.Framebuffer.ColorPhysicalAddress == topTransfer.InputPhysicalAddress &&
               !continuesFrame(draw);
    };
    std::vector<Oot3dPicaVulkanDrawPlan> nextDraws;
    std::vector<Oot3dPicaMemoryFillSubmission> nextFills;
    std::vector<Oot3dPicaDisplayTransferSubmission> nextTransfers;
    const auto next = std::find_if(mPendingDraws.begin(), mPendingDraws.end(), [&](const auto& draw) {
        return startsNextFrame(draw, topTransfer.AfterDrawSubmissionId);
    });
    if (next != mPendingDraws.begin() && next != mPendingDraws.end()) {
        const uint64_t nextStart = next->SubmissionId;
        nextDraws.assign(std::make_move_iterator(next), std::make_move_iterator(mPendingDraws.end()));
        mPendingDraws.erase(next, mPendingDraws.end());
        const auto fill = std::find_if(mPendingMemoryFills.begin(), mPendingMemoryFills.end(),
                                       [&](const auto& item) { return item.BeforeDrawSubmissionId >= nextStart; });
        nextFills.assign(fill, mPendingMemoryFills.end());
        mPendingMemoryFills.erase(fill, mPendingMemoryFills.end());
        const auto transfer = std::find_if(mPendingDisplayTransfers.begin(), mPendingDisplayTransfers.end(),
                                           [&](const auto& item) { return item.AfterDrawSubmissionId >= nextStart; });
        nextTransfers.assign(transfer, mPendingDisplayTransfers.end());
        mPendingDisplayTransfers.erase(transfer, mPendingDisplayTransfers.end());
    }
    // Ends of earlier guest frames: their snapshot of the same source into a
    // display framebuffer, each followed by at least one draw.
    std::vector<const Oot3dPicaDisplayTransferSubmission*> boundaries;
    for (const auto& transfer : mPendingDisplayTransfers) {
        if (transfer.Transfer.TextureCopyBytes != 0U ||
            transfer.CompletionId == topTransfer.CompletionId ||
            transfer.InputPhysicalAddress != topTransfer.InputPhysicalAddress ||
            transfer.AfterDrawSubmissionId >= topTransfer.AfterDrawSubmissionId ||
            std::find(displayOutputs.begin(), displayOutputs.end(),
                      transfer.OutputPhysicalAddress) == displayOutputs.end()) {
            continue;
        }
        const uint64_t previousEnd = boundaries.empty() ? 0U : boundaries.back()->AfterDrawSubmissionId;
        const bool drawsBefore = std::any_of(mPendingDraws.begin(), mPendingDraws.end(), [&](const auto& draw) {
            return draw.SubmissionId > previousEnd && draw.SubmissionId <= transfer.AfterDrawSubmissionId;
        });
        if (drawsBefore) boundaries.push_back(&transfer);
    }
    std::vector<uint64_t> ends;
    for (const auto* boundary : boundaries) ends.push_back(boundary->AfterDrawSubmissionId);
    const bool drawsAfter = !ends.empty() &&
        std::any_of(mPendingDraws.begin(), mPendingDraws.end(),
                    [&](const auto& draw) { return draw.SubmissionId > ends.back(); });
    if (!drawsAfter) {
        boundaries.clear();
        ends.clear();
    }
    // An earlier guest frame likewise keeps its continuation-canvas work, up
    // to the following frame's first draw into the source.
    if (!continuationViewports.empty()) {
        for (auto& end : ends) {
            const auto following = std::find_if(mPendingDraws.begin(), mPendingDraws.end(),
                                                [&](const auto& draw) { return startsNextFrame(draw, end); });
            if (following != mPendingDraws.begin() && following != mPendingDraws.end()) {
                end = std::max(end, std::prev(following)->SubmissionId);
            }
        }
    }
    std::vector<Oot3dPicaDisplayTransferSubmission> tops;
    for (const auto* boundary : boundaries) tops.push_back(*boundary);
    tops.push_back(topTransfer);
    ends.push_back(UINT64_MAX);

    size_t draw = 0U, fill = 0U, transfer = 0U;
    for (size_t segment = 0U; segment < ends.size(); ++segment) {
        const uint64_t end = ends[segment];
        Oot3dPicaVisualFrame frame;
        frame.Sequence = mNextSequence++;
        frame.TopTransfer = tops[segment];
        const size_t firstDraw = draw;
        while (draw < mPendingDraws.size() && mPendingDraws[draw].SubmissionId <= end) ++draw;
        frame.Draws.assign(std::make_move_iterator(mPendingDraws.begin() + firstDraw),
                           std::make_move_iterator(mPendingDraws.begin() + draw));
        frame.StrictDrawIdentities.reserve(frame.Draws.size());
        for (const auto& plan : frame.Draws) {
            frame.StrictDrawIdentities.push_back(ComputeOot3dPicaVisualDrawIdentity(plan));
        }
        // A fill precedes its draw; a transfer follows its draw.
        const size_t firstFill = fill;
        while (fill < mPendingMemoryFills.size() && mPendingMemoryFills[fill].BeforeDrawSubmissionId <= end) ++fill;
        frame.MemoryFills.assign(mPendingMemoryFills.begin() + firstFill, mPendingMemoryFills.begin() + fill);
        const size_t firstTransfer = transfer;
        while (transfer < mPendingDisplayTransfers.size() &&
               mPendingDisplayTransfers[transfer].AfterDrawSubmissionId <= end) ++transfer;
        frame.DisplayTransfers.assign(mPendingDisplayTransfers.begin() + firstTransfer,
                                      mPendingDisplayTransfers.begin() + transfer);
        frame.DependencyExecutedThroughDraw = executedThroughDraw;
        frame.DependencyExecutedTransfers =
            std::min(executedTransfers, transfer) - std::min(executedTransfers, firstTransfer);
        frames.push_back(std::move(frame));
    }
    const size_t largest = std::max_element(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
        return a.Draws.size() < b.Draws.size();
    })->Draws.size();
    mPendingDraws = std::move(nextDraws);
    mPendingMemoryFills = std::move(nextFills);
    mPendingDisplayTransfers = std::move(nextTransfers);
    // Draw plans are several kilobytes each. Starting the next frame at this
    // frame's size avoids regrowing the vector and moving every plan again.
    mPendingDraws.reserve(largest);
    return frames;
}

size_t Oot3dPicaVisualFrameAccumulator::PendingDrawCount() const {
    return mPendingDraws.size();
}

bool Oot3dPicaVisualFrameAccumulator::HasPendingRawCopyCompletion() const {
    return std::any_of(
        mPendingDisplayTransfers.begin(), mPendingDisplayTransfers.end(),
        [](const Oot3dPicaDisplayTransferSubmission& transfer) {
            return transfer.SignalInterrupt &&
                   transfer.Transfer.TextureCopyBytes != 0U;
        });
}

bool Oot3dPicaVisualFrameAccumulator::ViewDependencyWork(
    Oot3dPicaVisualFrameViewSample& output) {
    size_t firstTransfer = 0U;
    size_t endTransfer = 0U;
    std::optional<uint64_t> completedDraw;
    uint64_t lastDraw = 0U;
    for (size_t index = 0U; index < mPendingDisplayTransfers.size(); ++index) {
        const auto& transfer = mPendingDisplayTransfers[index];
        if (transfer.Transfer.TextureCopyBytes == 0U) continue;
        if (!transfer.SignalInterrupt) {
            firstTransfer = index + 1U;
            completedDraw = transfer.AfterDrawSubmissionId;
        } else {
            endTransfer = index + 1U;
            lastDraw = transfer.AfterDrawSubmissionId;
        }
    }
    if (endTransfer <= firstTransfer) return false;

    output.TopTransfer = {};
    output.Draws.clear();
    for (const auto& draw : mPendingDraws) {
        if ((!completedDraw || draw.SubmissionId > *completedDraw) &&
            draw.SubmissionId <= lastDraw) {
            output.Draws.push_back({.BasePlan = &draw});
        }
    }
    const auto firstFill = std::find_if(
        mPendingMemoryFills.begin(), mPendingMemoryFills.end(),
        [completedDraw](const auto& fill) {
            return !completedDraw || fill.BeforeDrawSubmissionId > *completedDraw;
        });
    const auto endFill = std::find_if(
        firstFill, mPendingMemoryFills.end(),
        [lastDraw](const auto& fill) {
            return fill.BeforeDrawSubmissionId > lastDraw;
        });
    output.MemoryFills = {firstFill, endFill};
    output.DisplayTransfers =
        std::span<const Oot3dPicaDisplayTransferSubmission>(
            mPendingDisplayTransfers).subspan(
                firstTransfer, endTransfer - firstTransfer);
    return true;
}

void Oot3dPicaVisualFrameAccumulator::CompleteDependencyWork() {
    for (auto& transfer : mPendingDisplayTransfers) {
        if (transfer.Transfer.TextureCopyBytes != 0U) {
            transfer.SignalInterrupt = false;
        }
    }
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
