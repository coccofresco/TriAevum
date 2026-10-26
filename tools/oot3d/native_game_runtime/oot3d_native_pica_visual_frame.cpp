#include "oot3d_native_pica_visual_frame.h"
#include "fast/renderer3ds/pica_vertex_temporal_policy.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"
#include "fast/renderer3ds/pica_factored_interpolation.h"
#include "oot3d_native_pica_affine_layout.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
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

bool SameBitsAsCurrent(const std::array<float, 4>& output, const std::array<float, 4>& current) {
    return std::memcmp(output.data(), current.data(), sizeof(output)) == 0;
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
    const auto& before = previous.VertexShader.Uniforms.Floats;
    const auto& after = current.VertexShader.Uniforms.Floats;
    for (size_t index = 0; index < before.size(); ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        // An unchanged row adds nothing (|x - x| / scale is 0, a NaN or an
        // infinity is skipped): most rows of a draw are constant.
        if (std::memcmp(before[index].data(), after[index].data(), sizeof(before[index])) == 0) {
            continue;
        }
        for (size_t component = 0; component < 4U; ++component) {
            const double left = before[index][component];
            const double right = after[index][component];
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

// Draw identities of one frame, computed on first use (the later matching
// stages only look at the draws the earlier ones left unmatched). The storage
// belongs to the transition scratch and is kept between frames.
template <typename Identity> struct VisualIdentityValues {
    const uint64_t* Existing = nullptr;
    const Oot3dPicaVulkanDrawPlan* Draws = nullptr;
    uint64_t* Owned = nullptr;
    uint8_t* Ready = nullptr;
    Identity Compute{};

    uint64_t operator[](size_t index) const {
        if (Existing != nullptr) {
            return Existing[index];
        }
        if (Ready[index] == 0U) {
            Owned[index] = Compute(Draws[index]);
            Ready[index] = 1U;
        }
        return Owned[index];
    }
};

struct VisualIdentityStorage {
    std::vector<uint64_t> Values;
    std::vector<uint8_t> Ready;
};

template <typename Identity>
VisualIdentityValues<Identity> ResolveVisualIdentities(const std::vector<uint64_t>& existing,
                                                       const std::vector<Oot3dPicaVulkanDrawPlan>& draws,
                                                       Identity identity, VisualIdentityStorage& storage) {
    VisualIdentityValues<Identity> values;
    if (existing.size() == draws.size()) {
        values.Existing = existing.data();
        return values;
    }
    storage.Values.resize(draws.size());
    storage.Ready.assign(draws.size(), 0U);
    values.Draws = draws.data();
    values.Owned = storage.Values.data();
    values.Ready = storage.Ready.data();
    values.Compute = identity;
    return values;
}

// (identity, draw index): sorted, the draws of one identity are a run in
// draw order, exactly the groups the hash maps used to collect.
using VisualKeyedDraw = std::pair<uint64_t, uint32_t>;

// The native instance key, compact and without padding (it is compared as
// one array).
using VisualNativeKey = std::array<uint64_t, 13>;
static_assert(std::has_unique_object_representations_v<VisualNativeKey>);
struct VisualNativeDraw {
    VisualNativeKey Key{};
    uint32_t Index = 0;
    bool operator<(const VisualNativeDraw& other) const {
        return Key != other.Key ? Key < other.Key : Index < other.Index;
    }
};

struct VisualAlignmentCell {
    size_t Matches = 0;
    double Cost = 0.0;
    uint8_t Action = 0;
};

// Everything the matching needs per frame, kept between frames: no
// allocation once the capacities cover the scene.
struct VisualMatchingScratch {
    std::vector<uint8_t> PreviousMatched, CurrentMatched;
    std::vector<VisualKeyedDraw> PreviousKeys, CurrentKeys;
    std::vector<VisualNativeDraw> OldInstances, NewInstances;
    std::vector<VisualAlignmentCell> Cells;
    VisualIdentityStorage PreviousStrict, CurrentStrict, PreviousStructural, CurrentStructural,
        PreviousPipeline, CurrentPipeline;
};

// Ranges of equal identity in two sorted key lists, in key order. The result
// of every matching stage is independent of the order its groups are visited
// (each group touches only its own draws), so this replaces the unordered
// maps exactly.
template <typename Visit>
void ForEachCommonVisualGroup(const std::vector<VisualKeyedDraw>& previousKeys,
                              const std::vector<VisualKeyedDraw>& currentKeys, Visit visit) {
    size_t p = 0;
    size_t c = 0;
    while (c < currentKeys.size()) {
        const uint64_t key = currentKeys[c].first;
        size_t cEnd = c + 1U;
        while (cEnd < currentKeys.size() && currentKeys[cEnd].first == key) ++cEnd;
        while (p < previousKeys.size() && previousKeys[p].first < key) ++p;
        size_t pEnd = p;
        while (pEnd < previousKeys.size() && previousKeys[pEnd].first == key) ++pEnd;
        if (pEnd != p) {
            visit(std::span<const VisualKeyedDraw>(previousKeys.data() + p, pEnd - p),
                  std::span<const VisualKeyedDraw>(currentKeys.data() + c, cEnd - c));
        }
        p = pEnd;
        c = cEnd;
    }
}

template <typename PreviousIdentities, typename CurrentIdentities>
void CollectVisualKeys(size_t previousCount, size_t currentCount, const uint8_t* previousExcluded,
                       const uint8_t* previousMatched, const uint8_t* currentMatched,
                       const PreviousIdentities& previousIdentities, const CurrentIdentities& currentIdentities,
                       VisualMatchingScratch& scratch) {
    auto& previousKeys = scratch.PreviousKeys;
    auto& currentKeys = scratch.CurrentKeys;
    previousKeys.clear();
    currentKeys.clear();
    for (size_t index = 0; index < previousCount; ++index) {
        if ((previousExcluded == nullptr || previousExcluded[index] == 0U) && previousMatched[index] == 0U) {
            previousKeys.emplace_back(previousIdentities[index], static_cast<uint32_t>(index));
        }
    }
    for (size_t index = 0; index < currentCount; ++index) {
        if (currentMatched[index] == 0U) {
            currentKeys.emplace_back(currentIdentities[index], static_cast<uint32_t>(index));
        }
    }
    std::sort(previousKeys.begin(), previousKeys.end());
    std::sort(currentKeys.begin(), currentKeys.end());
}

template <typename PreviousIdentities, typename CurrentIdentities>
void MatchEqualCardinalityGroups(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    const uint8_t* previousExcluded,
    VisualMatchingScratch& scratch,
    std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
    const PreviousIdentities& previousIdentities,
    const CurrentIdentities& currentIdentities, VisualDrawMatchKind uniqueKind,
    VisualDrawMatchKind repeatedKind, size_t& uniqueMatches,
    size_t& repeatedMatches, size_t* ambiguousPrevious,
    size_t* ambiguousCurrent) {
    uint8_t* const previousMatched = scratch.PreviousMatched.data();
    uint8_t* const currentMatched = scratch.CurrentMatched.data();
    CollectVisualKeys(previous.Draws.size(), current.Draws.size(), previousExcluded, previousMatched,
                      currentMatched, previousIdentities, currentIdentities, scratch);
    const auto* const previousDraws = previous.Draws.data();
    const auto* const currentDraws = current.Draws.data();
    ForEachCommonVisualGroup(scratch.PreviousKeys, scratch.CurrentKeys,
                             [&](std::span<const VisualKeyedDraw> previousIndices,
                                 std::span<const VisualKeyedDraw> currentIndices) {
        if (previousIndices.size() != currentIndices.size()) {
            if (ambiguousPrevious != nullptr && previousIndices.size() > 1U) {
                *ambiguousPrevious += previousIndices.size();
            }
            if (ambiguousCurrent != nullptr && currentIndices.size() > 1U) {
                *ambiguousCurrent += currentIndices.size();
            }
            return;
        }
        const bool unique = currentIndices.size() == 1U;
        const auto kind = unique ? uniqueKind : repeatedKind;
        for (size_t ordinal = 0; ordinal < currentIndices.size(); ++ordinal) {
            const size_t previousIndex = previousIndices[ordinal].second;
            const size_t currentIndex = currentIndices[ordinal].second;
            if (!HaveMatchingStructuralLayout(previousDraws[previousIndex], currentDraws[currentIndex])) {
                continue;
            }
            previousByCurrent[currentIndex] = VisualDrawMatch{previousIndex, kind};
            previousMatched[previousIndex] = 1U;
            currentMatched[currentIndex] = 1U;
            if (unique) {
                ++uniqueMatches;
            } else {
                ++repeatedMatches;
            }
        }
    });
}

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
    VisualMatchingScratch& scratch,
    std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
    const PreviousIdentities& previousIdentities, const CurrentIdentities& currentIdentities,
    Compatible compatible, VisualDrawMatchKind kind, size_t& matchCount) {
    uint8_t* const previousMatched = scratch.PreviousMatched.data();
    uint8_t* const currentMatched = scratch.CurrentMatched.data();
    CollectVisualKeys(previous.Draws.size(), current.Draws.size(), nullptr, previousMatched, currentMatched,
                      previousIdentities, currentIdentities, scratch);
    const auto* const previousDraws = previous.Draws.data();
    const auto* const currentDraws = current.Draws.data();
    auto& cells = scratch.Cells;
    ForEachCommonVisualGroup(scratch.PreviousKeys, scratch.CurrentKeys,
                             [&](std::span<const VisualKeyedDraw> previousIndices,
                                 std::span<const VisualKeyedDraw> currentIndices) {
        const size_t previousCountTotal = previousIndices.size();
        const size_t currentCountTotal = currentIndices.size();
        const size_t rowWidth = currentCountTotal + 1U;
        cells.assign((previousCountTotal + 1U) * rowWidth, VisualAlignmentCell{});
        VisualAlignmentCell* const table = cells.data();
        for (size_t previousCount = 1; previousCount <= previousCountTotal; ++previousCount) {
            table[previousCount * rowWidth].Action = 1U;
        }
        for (size_t currentCount = 1; currentCount <= currentCountTotal; ++currentCount) {
            table[currentCount].Action = 2U;
        }
        for (size_t previousCount = 1; previousCount <= previousCountTotal; ++previousCount) {
            const size_t previousIndex = previousIndices[previousCount - 1U].second;
            const auto& previousDraw = previousDraws[previousIndex];
            VisualAlignmentCell* const row = table + previousCount * rowWidth;
            const VisualAlignmentCell* const above = row - rowWidth;
            for (size_t currentCount = 1; currentCount <= currentCountTotal; ++currentCount) {
                VisualAlignmentCell best = above[currentCount];
                best.Action = 1U;
                VisualAlignmentCell skipCurrent = row[currentCount - 1U];
                skipCurrent.Action = 2U;
                if (IsBetterAlignment(skipCurrent, best)) {
                    best = skipCurrent;
                }
                const size_t currentIndex = currentIndices[currentCount - 1U].second;
                const auto& currentDraw = currentDraws[currentIndex];
                if (compatible(previousDraw, currentDraw)) {
                    VisualAlignmentCell match = above[currentCount - 1U];
                    ++match.Matches;
                    // Spelled as before: -ffast-math must contract it alike.
                    const double previousOrder =
                        (static_cast<double>(previousIndex) + 0.5) /
                        std::max<size_t>(previous.Draws.size(), 1U);
                    const double currentOrder =
                        (static_cast<double>(currentIndex) + 0.5) /
                        std::max<size_t>(current.Draws.size(), 1U);
                    match.Cost += NormalizedVertexUniformDelta(previousDraw, currentDraw) +
                                  std::abs(previousOrder - currentOrder);
                    match.Action = 3U;
                    if (IsBetterAlignment(match, best)) {
                        best = match;
                    }
                }
                row[currentCount] = best;
            }
        }

        size_t previousCount = previousCountTotal;
        size_t currentCount = currentCountTotal;
        while (previousCount != 0U || currentCount != 0U) {
            const uint8_t action = table[previousCount * rowWidth + currentCount].Action;
            if (action == 3U) {
                const size_t previousIndex = previousIndices[previousCount - 1U].second;
                const size_t currentIndex = currentIndices[currentCount - 1U].second;
                previousByCurrent[currentIndex] = VisualDrawMatch{previousIndex, kind};
                previousMatched[previousIndex] = 1U;
                currentMatched[currentIndex] = 1U;
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
    });
}

VisualNativeKey MakeVisualNativeKey(const Oot3dPicaVulkanDrawPlan& draw) {
    const auto& id = draw.Composition.TemporalIdentity;
    const auto& fb = draw.State.Framebuffer;
    return {id.Owner, id.Instance, id.Part, id.Slot, id.Lifetime,
            uint64_t(fb.ColorPhysicalAddress), uint64_t(fb.DepthPhysicalAddress),
            uint64_t(fb.Width), uint64_t(fb.Height), uint64_t(fb.ColorFormat), uint64_t(fb.DepthFormat),
            uint64_t(uint16_t(draw.State.Viewport.CornerX)), uint64_t(uint16_t(draw.State.Viewport.CornerY))};
}

// Runs of equal native key in a sorted instance list.
template <typename Visit>
void ForEachVisualNativeRun(const std::vector<VisualNativeDraw>& draws, Visit visit) {
    for (size_t begin = 0; begin < draws.size();) {
        size_t end = begin + 1U;
        while (end < draws.size() && draws[end].Key == draws[begin].Key) ++end;
        visit(begin, end);
        begin = end;
    }
}

void BuildVisualDrawMatching(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    VisualDrawMatching& matching,
    VisualMatchingScratch& scratch) {
    matching.PreviousByCurrent.assign(current.Draws.size(), std::nullopt);
    matching.StrictUniqueMatches = 0;
    matching.StrictOrdinalMatches = 0;
    matching.StructuralOrdinalMatches = 0;
    matching.PipelineOrdinalMatches = 0;
    matching.AmbiguousPrevious = 0;
    matching.AmbiguousCurrent = 0;
    matching.UnmatchedPrevious = 0;
    matching.UnmatchedCurrent = 0;
    scratch.PreviousMatched.assign(previous.Draws.size(), 0U);
    scratch.CurrentMatched.assign(current.Draws.size(), 0U);
    // Native instance matches are independent of draw order and cardinality.
    // Reserve every evidenced draw from the legacy interpolation matcher,
    // including unmatched/ambiguous ones, so it cannot pair their uniforms by
    // occurrence. Unknown producers retain their existing visual sampling,
    // but cannot acquire exact renderer motion history.
    auto& oldInstances = scratch.OldInstances;
    auto& newInstances = scratch.NewInstances;
    oldInstances.clear();
    newInstances.clear();
    for (size_t i = 0; i < previous.Draws.size(); ++i)
        if (previous.Draws[i].Composition.TemporalIdentity.Available()) {
            oldInstances.push_back({MakeVisualNativeKey(previous.Draws[i]), static_cast<uint32_t>(i)});
            scratch.PreviousMatched[i] = 1U;
        }
    for (size_t i = 0; i < current.Draws.size(); ++i)
        if (current.Draws[i].Composition.TemporalIdentity.Available()) {
            newInstances.push_back({MakeVisualNativeKey(current.Draws[i]), static_cast<uint32_t>(i)});
            scratch.CurrentMatched[i] = 1U;
        }
    std::sort(oldInstances.begin(), oldInstances.end());
    std::sort(newInstances.begin(), newInstances.end());
    ForEachVisualNativeRun(oldInstances, [&](size_t begin, size_t end) {
        if (end - begin != 1U) matching.AmbiguousPrevious += end - begin;
    });
    size_t old = 0;
    ForEachVisualNativeRun(newInstances, [&](size_t begin, size_t end) {
        if (end - begin != 1U) matching.AmbiguousCurrent += end - begin;
        const auto& key = newInstances[begin].Key;
        while (old < oldInstances.size() && oldInstances[old].Key < key) ++old;
        if (end - begin != 1U || old == oldInstances.size() || oldInstances[old].Key != key) return;
        const bool oldUnique = old + 1U == oldInstances.size() || oldInstances[old + 1U].Key != key;
        const size_t previousIndex = oldInstances[old].Index;
        const size_t currentIndex = newInstances[begin].Index;
        if (oldUnique && HaveMatchingPipelineLayout(previous.Draws[previousIndex], current.Draws[currentIndex])) {
            matching.PreviousByCurrent[currentIndex] = VisualDrawMatch{previousIndex, VisualDrawMatchKind::StrictUnique};
            ++matching.StrictUniqueMatches;
        }
    });
    static const std::vector<uint64_t> none;
    const auto previousStrict = ResolveVisualIdentities(previous.StrictDrawIdentities, previous.Draws,
                                                        ComputeOot3dPicaVisualDrawIdentity, scratch.PreviousStrict);
    const auto currentStrict = ResolveVisualIdentities(current.StrictDrawIdentities, current.Draws,
                                                       ComputeOot3dPicaVisualDrawIdentity, scratch.CurrentStrict);
    const auto previousStructural =
        ResolveVisualIdentities(none, previous.Draws, ComputeStructuralDrawIdentity, scratch.PreviousStructural);
    const auto currentStructural =
        ResolveVisualIdentities(none, current.Draws, ComputeStructuralDrawIdentity, scratch.CurrentStructural);
    const auto previousPipeline =
        ResolveVisualIdentities(none, previous.Draws, ComputePipelineDrawIdentity, scratch.PreviousPipeline);
    const auto currentPipeline =
        ResolveVisualIdentities(none, current.Draws, ComputePipelineDrawIdentity, scratch.CurrentPipeline);

    MatchEqualCardinalityGroups(
        previous, current, nullptr, scratch,
        matching.PreviousByCurrent, previousStrict, currentStrict,
        VisualDrawMatchKind::StrictUnique,
        VisualDrawMatchKind::StrictOrdinal, matching.StrictUniqueMatches,
        matching.StrictOrdinalMatches, &matching.AmbiguousPrevious,
        &matching.AmbiguousCurrent);

    // A UI draw pairs with its previous self only on native evidence (its
    // temporal identity or its strict draw identity: source, buffers and
    // textures). Ordinal pairing by shape alone lets a HUD element that
    // appears, vanishes or blinks (the Navi prompt) take another element's
    // place, which then slides across the screen between native frames. An
    // unpaired UI draw is presented as the guest drew it.
    for (size_t index = 0; index < previous.Draws.size(); ++index)
        if (previous.Draws[index].CompositionDomain == Oot3dPicaCompositionDomain::Ui)
            scratch.PreviousMatched[index] = 1U;
    for (size_t index = 0; index < current.Draws.size(); ++index)
        if (current.Draws[index].CompositionDomain == Oot3dPicaCompositionDomain::Ui)
            scratch.CurrentMatched[index] = 1U;

    size_t unusedStructuralUniqueMatches = 0;
    MatchEqualCardinalityGroups(
        previous, current, nullptr, scratch,
        matching.PreviousByCurrent, previousStructural, currentStructural,
        VisualDrawMatchKind::StructuralOrdinal,
        VisualDrawMatchKind::StructuralOrdinal,
        unusedStructuralUniqueMatches, matching.StructuralOrdinalMatches,
        nullptr, nullptr);
    matching.StructuralOrdinalMatches += unusedStructuralUniqueMatches;

    MatchMonotonicGroups(
        previous, current, scratch, matching.PreviousByCurrent,
        previousStructural, currentStructural,
        HaveMatchingStructuralLayout,
        VisualDrawMatchKind::StructuralOrdinal,
        matching.StructuralOrdinalMatches);
    MatchMonotonicGroups(
        previous, current, scratch, matching.PreviousByCurrent,
        previousPipeline, currentPipeline,
        HaveMatchingPipelineLayout, VisualDrawMatchKind::PipelineOrdinal,
        matching.PipelineOrdinalMatches);

    const auto actualMatches = static_cast<size_t>(std::count_if(
        matching.PreviousByCurrent.begin(), matching.PreviousByCurrent.end(),
        [](const auto& match) { return match.has_value(); }));
    matching.UnmatchedPrevious = previous.Draws.size() - actualMatches;
    matching.UnmatchedCurrent = current.Draws.size() - actualMatches;
}

VisualDrawMatching BuildVisualDrawMatching(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current) {
    VisualDrawMatching matching;
    VisualMatchingScratch scratch;
    BuildVisualDrawMatching(previous, current, matching, scratch);
    return matching;
}

Oot3dPicaVisualTransitionStats
AnalyzeVisualTransitionWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                    const VisualDrawMatching& matching,
                                    std::vector<VisualDrawDeltaFlags>* drawDeltas = nullptr,
                                    std::vector<double>* topTargetScratch = nullptr) {
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
    std::vector<double> localTopTargetDeltas;
    auto& topTargetDeltas = topTargetScratch != nullptr ? *topTargetScratch : localTopTargetDeltas;
    topTargetDeltas.clear();
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
    Fast::Renderer3ds::PicaNativeLightBinding& output, VisualSceneLightInterpolation& scenePoints,
    bool rigidModelView = false) {
    output = current;
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    output.ScenePoints = scenePoints.Resolve(previous.ScenePoints, current.ScenePoints, alpha);
    output.SceneDirectionals = scenePoints.ResolveDirectionals(previous.SceneDirectionals, current.SceneDirectionals, alpha);
    if (previous.WorldToView && current.WorldToView) {
        // The camera follows the draw's model-view: a factored (rigid) model-
        // view needs the same factored camera, or inverse(camera) * model-view
        // no longer gives the draw's world pose (shadow casters, lighting).
        if (!rigidModelView || !Fast::Renderer3ds::InterpolatePicaRigidCameraMatrix(
                                   *previous.WorldToView, *current.WorldToView, alpha, *output.WorldToView))
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

// One vertex attribute of a draw, located and bounds-checked once: the bone
// index reads of CmbPaletteSlots run for every referenced vertex.
struct VisualAttributeReader {
    const uint8_t* Bytes = nullptr;
    size_t Records = 0;
    size_t Stride = 0;
    size_t Offset = 0;
    size_t ComponentSize = 0;
    Oot3dPicaVertexFormat Format = Oot3dPicaVertexFormat::Float;
    bool PerInstance = false;

    // False where the per-read checks (missing binding, zero stride, an
    // attribute outside its record) would refuse every read.
    bool Bind(const Oot3dPicaVulkanDrawPlan& plan, const Oot3dPicaVulkanVertexAttribute& attribute) {
        const auto* binding = FindVertexBinding(plan, attribute.Binding);
        if (!binding || !binding->ByteStride) return false;
        const size_t size = VertexFormatByteSize(attribute.Format);
        if (!size || attribute.ByteOffset > binding->ByteStride ||
            size*attribute.ComponentCount > binding->ByteStride-attribute.ByteOffset) return false;
        const auto bytes = binding->ResolvedBytes();
        Bytes = bytes.data();
        Stride = binding->ByteStride;
        Records = bytes.size() / Stride;
        Offset = attribute.ByteOffset;
        ComponentSize = size;
        Format = attribute.Format;
        PerInstance = binding->InputRate == Oot3dPicaVertexInputRate::PerInstance;
        return true;
    }

    // False when the record is outside the binding's bytes.
    bool Read(size_t vertex, size_t component, float& value) const {
        const size_t record = PerInstance ? 0 : vertex;
        if (record >= Records) return false;
        const uint8_t* at = Bytes + record * Stride + Offset + component * ComponentSize;
        switch (Format) {
        case Oot3dPicaVertexFormat::UnsignedByte: value = float(*at); return true;
        case Oot3dPicaVertexFormat::SignedByte: value = float(static_cast<int8_t>(*at)); return true;
        case Oot3dPicaVertexFormat::SignedShort: {
            int16_t raw; std::memcpy(&raw, at, 2); value = float(raw); return true;
        }
        case Oot3dPicaVertexFormat::Float: std::memcpy(&value, at, 4); return true;
        }
        return false;
    }
};

// The bone-index walk of CmbPaletteSlots: the palette slots the draw's
// vertices address, or nothing for an address it cannot prove.
std::optional<uint32_t> CmbPaletteVertexSlots(
    const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanVertexAttribute& left,
    const Oot3dPicaVulkanDrawPlan& current, const Oot3dPicaVulkanVertexAttribute& right, size_t influences) {
    const auto indices = current.ResolvedIndexBytes();
    const size_t indexSize = current.IndicesAre16Bit ? 2 : 1;
    if (current.Indexed && current.VertexCount > indices.size()/indexSize) return {};
    VisualAttributeReader from, to;
    // Every read fails alike when an attribute cannot be read at all (the
    // walk reads at least once: VertexCount and influences are nonzero).
    if (!from.Bind(previous, left) || !to.Bind(current, right)) return {};
    const uint8_t* const indexBytes = indices.data();
    const bool indexed = current.Indexed;
    const int64_t baseVertex = current.BaseVertex;
    const size_t vertexCount = current.VertexCount;
    uint32_t selected = 0;
    for (size_t i = 0; i < vertexCount; ++i) {
        int64_t vertex = static_cast<int64_t>(i);
        if (indexed) {
            vertex = indexBytes[i*indexSize];
            if (indexSize == 2) vertex |= int64_t(indexBytes[i*indexSize+1]) << 8;
            vertex += baseVertex;
        }
        if (vertex < 0) return {};
        for (size_t component = 0; component < influences; ++component) {
            float a = 0.0F, b = 0.0F;
            // Reject fractional, wrapped and out-of-palette addresses instead
            // of approximating PICA's MOVA/wrap semantics with a guessed span.
            if (!from.Read(size_t(vertex), component, a) || !to.Read(size_t(vertex), component, b) ||
                !std::isfinite(b) || a != b || b < 0 || b >= 10 || std::floor(b) != b) return {};
            selected |= 1U << static_cast<uint32_t>(b);
        }
    }
    return selected;
}

// CmbPaletteVertexSlots of the previous transition, by draw: a bone-index
// walk over the same immutable shared bytes (held here, so they cannot be
// freed and reused) with the same layout has the same result.
struct CmbSlotsMemo {
    std::shared_ptr<const std::vector<uint8_t>> PreviousBytes, CurrentBytes, Indices;
    std::array<uint64_t, 6> Layout{};
    std::optional<uint32_t> Result;
    bool Valid = false;
};

const std::shared_ptr<const std::vector<uint8_t>>* SharedAttributeBytes(
    const Oot3dPicaVulkanDrawPlan& plan, const Oot3dPicaVulkanVertexAttribute& attribute) {
    const auto* binding = FindVertexBinding(plan, attribute.Binding);
    return binding != nullptr && binding->SharedBytes != nullptr ? &binding->SharedBytes : nullptr;
}

std::array<uint64_t, 6> CmbSlotsLayout(
    const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanVertexAttribute& left,
    const Oot3dPicaVulkanDrawPlan& current, const Oot3dPicaVulkanVertexAttribute& right, size_t influences) {
    const auto binding = [](const Oot3dPicaVulkanDrawPlan& plan, const Oot3dPicaVulkanVertexAttribute& attribute) {
        const auto* found = FindVertexBinding(plan, attribute.Binding);
        return (uint64_t(found->ByteStride) << 8) | uint64_t(found->InputRate == Oot3dPicaVertexInputRate::PerInstance);
    };
    const auto attributeBits = [](const Oot3dPicaVulkanVertexAttribute& attribute) {
        return (uint64_t(attribute.Binding) << 48) | (uint64_t(static_cast<uint8_t>(attribute.Format)) << 40) |
               (uint64_t(attribute.ComponentCount) << 32) | uint64_t(attribute.ByteOffset);
    };
    return {binding(previous, left), binding(current, right), attributeBits(left), attributeBits(right),
            (uint64_t(current.VertexCount) << 8) | (current.Indexed ? 2U : 0U) | (current.IndicesAre16Bit ? 1U : 0U),
            (uint64_t(uint32_t(current.BaseVertex)) << 8) | uint64_t(influences)};
}

struct CmbSlotsCache {
    std::vector<CmbSlotsMemo> Previous, Current;
    void Begin(size_t draws) {
        std::swap(Previous, Current);
        for (auto& memo : Current) memo = {};
        Current.resize(draws);
    }
};

struct CmbSlotsContext {
    CmbSlotsCache* Cache = nullptr;
    size_t PreviousIndex = 0;
    size_t CurrentIndex = 0;
};

// Return a conservative exact mask of the slots referenced by this draw.
// A missing snapshot or an unfamiliar addressing mode disables factoring;
// selecting rigid-looking rows from an unknown active palette could separate
// a joint shared with an excluded, non-rigid matrix.
std::optional<uint32_t> CmbPaletteSlots(
    const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current,
    const CmbSlotsContext* context = nullptr) {
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
    if (context == nullptr || context->Cache == nullptr) {
        return CmbPaletteVertexSlots(previous, *left, current, *right, influences);
    }
    const auto* previousBytes = SharedAttributeBytes(previous, *left);
    const auto* currentBytes = SharedAttributeBytes(current, *right);
    const bool sharedIndices = !current.Indexed || current.SharedIndexBytes != nullptr;
    if (previousBytes == nullptr || currentBytes == nullptr || !sharedIndices) {
        return CmbPaletteVertexSlots(previous, *left, current, *right, influences);
    }
    auto& cache = *context->Cache;
    const auto layout = CmbSlotsLayout(previous, *left, current, *right, influences);
    const auto* indices = current.Indexed ? current.SharedIndexBytes.get() : nullptr;
    const auto same = [&](const CmbSlotsMemo& memo) {
        return memo.Valid && memo.PreviousBytes == *previousBytes && memo.CurrentBytes == *currentBytes &&
               memo.Indices.get() == indices && memo.Layout == layout;
    };
    // In steady state the previous transition walked the same bytes (the
    // draw's own buffers in both frames). A miss only costs the walk.
    const auto hit = [&](size_t index) -> const CmbSlotsMemo* {
        return index < cache.Previous.size() && same(cache.Previous[index]) ? &cache.Previous[index] : nullptr;
    };
    const CmbSlotsMemo* found = hit(context->PreviousIndex);
    if (found == nullptr && context->CurrentIndex != context->PreviousIndex) found = hit(context->CurrentIndex);
    auto& entry = cache.Current[context->CurrentIndex];
    entry.Result = found != nullptr ? found->Result
                                    : CmbPaletteVertexSlots(previous, *left, current, *right, influences);
    entry.PreviousBytes = *previousBytes;
    entry.CurrentBytes = *currentBytes;
    entry.Indices = current.Indexed ? current.SharedIndexBytes : nullptr;
    entry.Layout = layout;
    entry.Valid = true;
    return entry.Result;
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
// other draws of its actor, prepared once per transition (they do not depend
// on the sample's alpha). A draw of the actor that has no previous match
// carries a reconstructed previous palette instead of being shown at the new
// pose.
struct VisualFactoredPalette {
    bool Prepared = false;
    std::optional<uint32_t> Selected;
    std::optional<Fast::Renderer3ds::PicaRigidMotionRows> SharedMotion;
    std::optional<VisualUniformFloats> CarriedPrevious;
};

// One actor's draws that share an output space, and the bones they use.
struct VisualPaletteGroup {
    uint64_t Owner = 0;
    uint64_t Lifetime = 0;
    VisualMatrixBits Space{};
    VisualMatrixBits PriorSpace{};
    std::optional<Fast::Renderer3ds::PicaRigidMotionRows> Motion;
    // Current bone matrix -> its previous pose (Ambiguous when two draws
    // disagree), each distinct bone once in first-use order.
    struct Bone {
        VisualMatrixBits Current{};
        VisualMatrixBits Prior{};
        bool Ambiguous = false;
    };
    std::vector<Bone> Bones;
    std::vector<Fast::Renderer3ds::PicaAffineEndpoints> Matrices;

    const Bone* FindBone(const VisualMatrixBits& current) const {
        for (const auto& bone : Bones)
            if (bone.Current == current) return &bone;
        return nullptr;
    }
};

// ShareVisualPaletteMotions' storage, kept between transitions.
struct VisualPaletteScratch {
    std::vector<VisualPaletteGroup> Groups;   // the first GroupCount are live
    size_t GroupCount = 0;
    std::vector<int32_t> GroupOfDraw;
    CmbSlotsCache Slots;

    VisualPaletteGroup* Find(uint64_t owner, uint64_t lifetime, const VisualMatrixBits& space) {
        for (size_t index = 0; index < GroupCount; ++index) {
            auto& group = Groups[index];
            if (group.Owner == owner && group.Lifetime == lifetime && group.Space == space) return &group;
        }
        return nullptr;
    }
    VisualPaletteGroup& Add(uint64_t owner, uint64_t lifetime, const VisualMatrixBits& space) {
        if (GroupCount == Groups.size()) Groups.emplace_back();
        auto& group = Groups[GroupCount++];
        group.Owner = owner;
        group.Lifetime = lifetime;
        group.Space = space;
        group.PriorSpace = {};
        group.Motion.reset();
        group.Bones.clear();
        group.Matrices.clear();
        return group;
    }
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
// `result` is overwritten; its storage is reused (one entry per draw, ~1.7
// KiB each).
void ShareVisualPaletteMotions(
    const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
    const std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
    std::vector<VisualFactoredPalette>& result, VisualPaletteScratch& scratch) {
    const size_t drawCount = current.Draws.size();
    result.resize(drawCount);
    for (auto& entry : result) {
        entry.Prepared = false;
        entry.Selected.reset();
        entry.SharedMotion.reset();
        entry.CarriedPrevious.reset();
    }
    scratch.GroupCount = 0;
    scratch.GroupOfDraw.assign(drawCount, -1);
    scratch.Slots.Begin(drawCount);
    const auto endpoints = [](const VisualUniformFloats& before, const VisualUniformFloats& after, size_t first) {
        Fast::Renderer3ds::PicaAffineEndpoints matrix{};
        for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c) {
            matrix[0][row][c] = before[first + row][c];
            matrix[1][row][c] = after[first + row][c];
        }
        return matrix;
    };
    // The actor key of a draw: native owner and lifetime, and its output space.
    const auto actorKey = [](const Oot3dPicaVulkanDrawPlan& plan, VisualMatrixBits& space) {
        const auto& id = plan.Composition.TemporalIdentity;
        if (!id.Available() || id.Owner == 0) return false;
        const auto first = plan.VertexShader.TemporalProgram->AffineLayout.ModelView.FirstUniform;
        space = AffineBits(plan.VertexShader.Uniforms.Floats, first);
        return true;
    };
    const auto* const previousDraws = previous.Draws.data();
    const auto* const currentDraws = current.Draws.data();
    for (size_t index = 0; index < drawCount; ++index) {
        const auto& match = previousByCurrent[index];
        if (!match) continue;
        const auto& before = previousDraws[match->PreviousIndex];
        const auto& after = currentDraws[index];
        if (!CmbAffineLayouts(before, after)) continue;
        auto& entry = result[index];
        entry.Prepared = true;
        const CmbSlotsContext context{&scratch.Slots, match->PreviousIndex, index};
        entry.Selected = CmbPaletteSlots(before, after, &context);
        if (!entry.Selected) continue;
        VisualMatrixBits space;
        if (!actorKey(after, space)) continue;
        const auto& id = after.Composition.TemporalIdentity;
        const auto& priorId = before.Composition.TemporalIdentity;
        if (priorId.Owner != id.Owner || priorId.Lifetime != id.Lifetime) continue;
        const auto& layout = after.VertexShader.TemporalProgram->AffineLayout;
        const auto& beforeFloats = before.VertexShader.Uniforms.Floats;
        const auto& afterFloats = after.VertexShader.Uniforms.Floats;
        const auto priorSpace = AffineBits(beforeFloats, layout.ModelView.FirstUniform);
        auto* group = scratch.Find(id.Owner, id.Lifetime, space);
        if (group == nullptr) {
            group = &scratch.Add(id.Owner, id.Lifetime, space);
            group->PriorSpace = priorSpace;
        } else if (group->PriorSpace != priorSpace) {
            continue;
        }
        scratch.GroupOfDraw[index] = static_cast<int32_t>(group - scratch.Groups.data());
        const uint32_t selected = *entry.Selected;
        for (size_t slot = 0; slot < layout.Palette.MatrixCount; ++slot) {
            if (!(selected & (1U << slot))) continue;
            const size_t first = layout.Palette.FirstUniform + 3 * slot;
            const auto prior = AffineBits(beforeFloats, first);
            const auto bits = AffineBits(afterFloats, first);
            auto* bone = const_cast<VisualPaletteGroup::Bone*>(group->FindBone(bits));
            if (bone == nullptr) {
                group->Bones.push_back({bits, prior, false});
                group->Matrices.push_back(endpoints(beforeFloats, afterFloats, first));
            } else if (!bone->Ambiguous && bone->Prior != prior) {
                bone->Ambiguous = true;
            }
        }
    }
    for (size_t index = 0; index < scratch.GroupCount; ++index) {
        auto& group = scratch.Groups[index];
        group.Motion = Fast::Renderer3ds::PicaLeastResidualMotion(group.Matrices);
    }
    for (size_t index = 0; index < drawCount; ++index)
        if (scratch.GroupOfDraw[index] >= 0)
            result[index].SharedMotion = scratch.Groups[static_cast<size_t>(scratch.GroupOfDraw[index])].Motion;
    for (size_t index = 0; index < drawCount; ++index) {
        const auto& after = currentDraws[index];
        if (previousByCurrent[index] || !CmbAffineLayouts(after, after)) continue;
        VisualMatrixBits space;
        if (!actorKey(after, space)) continue;
        const auto& id = after.Composition.TemporalIdentity;
        const auto* found = scratch.Find(id.Owner, id.Lifetime, space);
        if (found == nullptr || !found->Motion) continue;
        const auto selected = CmbPaletteSlots(after, after);
        if (!selected) continue;
        const auto& group = *found;
        const auto& motion = *group.Motion;
        const auto& layout = after.VertexShader.TemporalProgram->AffineLayout;
        const auto& afterFloats = after.VertexShader.Uniforms.Floats;
        auto& entry = result[index];
        entry.CarriedPrevious.emplace(afterFloats);
        VisualUniformFloats& prior = *entry.CarriedPrevious;
        // The group's previous model-view: the piece shares the actor's space.
        for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c)
            prior[layout.ModelView.FirstUniform + row][c] = std::bit_cast<float>(group.PriorSpace[row * 4 + c]);
        for (size_t slot = 0; slot < layout.Palette.MatrixCount; ++slot) {
            if (!(*selected & (1U << slot))) continue;
            const size_t first = layout.Palette.FirstUniform + 3 * slot;
            const auto* bone = group.FindBone(AffineBits(afterFloats, first));
            if (bone != nullptr && !bone->Ambiguous) {
                for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c)
                    prior[first + row][c] = std::bit_cast<float>(bone->Prior[row * 4 + c]);
                continue;
            }
            // inverse(D) * N1 with D = [R | t]: R^T * (N1 - [0 | t]).
            for (size_t row = 0; row < 3; ++row) for (size_t c = 0; c < 4; ++c) {
                double value = 0.0;
                for (size_t k = 0; k < 3; ++k)
                    value += motion[k][row] * (double(afterFloats[first + k][c]) -
                                               (c == 3 ? motion[k][3] : 0.0));
                prior[first + row][c] = static_cast<float>(value);
            }
        }
        entry.Prepared = true;
        entry.Selected = selected;
        entry.SharedMotion = motion;
    }
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

// True when the model-view group was sampled as a factored rigid motion.
bool InterpolateVisualVertexUniforms(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms,
    const VisualFactoredPalette* palette = nullptr) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    const auto& before = previous.VertexShader.Uniforms.Floats;
    const auto& after = current.VertexShader.Uniforms.Floats;
    const bool endpoint = alpha == 0.0F || alpha == 1.0F;
    // `vertexUniforms` starts as the current draw's. An unchanged row keeps
    // its value bit for bit (x + (x - x) * alpha is x, and a non-finite x is
    // kept), except -0.0 strictly between the endpoints (-0 + +0 is +0).
    const auto unchanged = [&](size_t index) {
        if (std::memcmp(before[index].data(), after[index].data(), sizeof(before[index])) != 0 ||
            !SameBitsAsCurrent(vertexUniforms.Floats[index], after[index])) return false;
        if (endpoint) return true;
        for (const float value : after[index])
            if (std::bit_cast<uint32_t>(value) == 0x80000000U) return false;
        return true;
    };
    for (size_t index = 0; index < vertexUniforms.Floats.size(); ++index) {
        if (!IsContinuousVertexUniform(index)) continue;
        if (unchanged(index)) continue;
        for (size_t component = 0; component < 4; ++component)
            vertexUniforms.Floats[index][component] = alpha == 0.0F ? before[index][component]
                : alpha == 1.0F ? after[index][component]
                : InterpolateFinite(before[index][component],after[index][component],alpha);
    }
    if (!(alpha > 0.0F && alpha < 1.0F)) return false;
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
    if (!CmbAffineLayouts(previous, current)) return false;
    const auto selected = palette && palette->Prepared ? palette->Selected : CmbPaletteSlots(previous,current);
    if (!selected) return false;
    const auto& layout = current.VertexShader.TemporalProgram->AffineLayout;
    const auto* shared = palette && palette->SharedMotion ? &*palette->SharedMotion : nullptr;
    auto factored = vertexUniforms.Floats;
    // Palette outputs feed model-view inputs: two coordinate spaces, hence
    // separate motions. Commit both groups together or keep scalar fallback.
    if (Fast::Renderer3ds::InterpolatePicaFactoredUniformGroup(
            before,after,layout.Palette,*selected,alpha,factored,shared) &&
        Fast::Renderer3ds::InterpolatePicaFactoredUniformGroup(
            before,after,layout.ModelView,1U,alpha,factored)) {
        vertexUniforms.Floats = factored;
        return true;
    }
    return false;
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

bool InterpolateCompatibleVisualRenderState(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms,
    Oot3dPicaFragmentUniformState& fragmentUniforms,
    Oot3dPicaViewportState& viewport,
    std::array<float, 4>& blendConstantColor,
    const VisualFactoredPalette* palette = nullptr) {
    const bool rigidModelView = InterpolateVisualVertexUniforms(previous, current, alpha,
                                                                vertexUniforms, palette);
    InterpolateVisualFragmentUniforms(previous, current, alpha,
                                      fragmentUniforms);
    InterpolateVisualViewport(previous, current, alpha, viewport);
    InterpolateVisualBlendConstantColor(previous, current, alpha,
                                        blendConstantColor);
    return rigidModelView;
}

void InterpolateCompatibleVisualDraw(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current,
                                     float alpha, Oot3dPicaVulkanDrawPlan& output,
                                     VisualSceneLightInterpolation& scenePoints,
                                     const VisualFactoredPalette* palette = nullptr) {
    output = current;
    const bool rigidModelView = InterpolateCompatibleVisualRenderState(
        previous, current, alpha, output.VertexShader.Uniforms, output.FragmentShader.Uniforms,
        output.State.Viewport, output.State.OutputMerger.Blend.ConstantColor, palette);
    InterpolateVisualLightBinding(previous.LightBinding, current.LightBinding, alpha, output.LightBinding, scenePoints,
                                  rigidModelView);
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
    std::vector<VisualFactoredPalette> palettes;
    if (alpha > 0.0F && alpha < 1.0F) {
        VisualPaletteScratch paletteScratch;
        ShareVisualPaletteMotions(previous, current, matching.PreviousByCurrent, palettes, paletteScratch);
    }

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
                                       const std::vector<VisualDrawDeltaFlags>& drawDeltas,
                                       const std::vector<VisualFactoredPalette>* preparedPalettes = nullptr) {
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
    // The factored palettes (prepared once per transition) apply strictly
    // between the endpoints only.
    static const std::vector<VisualFactoredPalette> noPalettes;
    std::vector<VisualFactoredPalette> localPalettes;
    const std::vector<VisualFactoredPalette>* palettesSource = &noPalettes;
    if (alpha > 0.0F && alpha < 1.0F) {
        if (preparedPalettes != nullptr) {
            palettesSource = preparedPalettes;
        } else {
            VisualPaletteScratch paletteScratch;
            ShareVisualPaletteMotions(previous, current, matching.PreviousByCurrent, localPalettes, paletteScratch);
            palettesSource = &localPalettes;
        }
    }
    const auto& palettes = *palettesSource;

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
        bool rigidModelView = false;
        if (delta.VertexUniforms) {
            sampled.VertexUniforms.emplace(currentDraw.VertexShader.Uniforms);
            rigidModelView = InterpolateVisualVertexUniforms(
                previousDraw, currentDraw, alpha, *sampled.VertexUniforms,
                palettes.empty() ? nullptr : &palettes[currentIndex]);
        }
        if (delta.LightBinding) {
            sampled.LightBinding.emplace();
            InterpolateVisualLightBinding(previousDraw.LightBinding, currentDraw.LightBinding,
                                           alpha, *sampled.LightBinding, scenePoints, rigidModelView);
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
    // Storage kept between frames: preparing a transition allocates nothing
    // once the capacities cover the scene.
    VisualMatchingScratch MatchingScratch;
    std::vector<double> TopTargetDeltas;
    // The factored palettes of this transition: alpha-independent, built by
    // the first sample strictly between the endpoints and shared by the rest.
    mutable std::vector<VisualFactoredPalette> Palettes;
    mutable VisualPaletteScratch PaletteScratch;
    mutable bool PalettesReady = false;
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
    BuildVisualDrawMatching(previous, current, mImpl->Matching, mImpl->MatchingScratch);
    const auto analysisStart = std::chrono::steady_clock::now();
    mImpl->Transition = AnalyzeVisualTransitionWithMatching(
        previous, current, mImpl->Matching, &mImpl->DrawDeltas, &mImpl->TopTargetDeltas);
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
    if (alpha > 0.0F && alpha < 1.0F && !mImpl->PalettesReady) {
        ShareVisualPaletteMotions(previous, current, mImpl->Matching.PreviousByCurrent, mImpl->Palettes,
                                  mImpl->PaletteScratch);
        mImpl->PalettesReady = true;
    }
    const bool sampled = SampleVisualFrameViewWithMatching(
        previous, current, alpha, mImpl->Matching, output,
        mImpl->DrawDeltas, &mImpl->Palettes);
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
    // Cleared, not freed: the next transition reuses the storage.
    mImpl->Matching.PreviousByCurrent.clear();
    mImpl->Matching.StrictUniqueMatches = 0;
    mImpl->Matching.StrictOrdinalMatches = 0;
    mImpl->Matching.StructuralOrdinalMatches = 0;
    mImpl->Matching.PipelineOrdinalMatches = 0;
    mImpl->Matching.AmbiguousPrevious = 0;
    mImpl->Matching.AmbiguousCurrent = 0;
    mImpl->Matching.UnmatchedPrevious = 0;
    mImpl->Matching.UnmatchedCurrent = 0;
    mImpl->DrawDeltas.clear();
    mImpl->PalettesReady = false;
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

int64_t Oot3dPicaPreparedVisualTransition::PreviousIndexOf(
    size_t currentIndex, uint8_t* kind) const noexcept {
    if (mImpl == nullptr || !mImpl->Prepared ||
        currentIndex >= mImpl->Matching.PreviousByCurrent.size()) {
        return -1;
    }
    const auto& match = mImpl->Matching.PreviousByCurrent[currentIndex];
    if (!match) return -1;
    if (kind != nullptr) *kind = static_cast<uint8_t>(match->Kind);
    return static_cast<int64_t>(match->PreviousIndex);
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
