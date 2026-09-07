#include "fast/oot3d/pica_scene_semantics.h"
#include "fast/renderer/content_hash.h"

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/pica_uniform_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fast::Oot3d {
namespace {

constexpr std::array<uint32_t, kPicaNativeLightCount>
    kLightDirectionSlots{0x50U, 0x53U, 0x56U};
constexpr size_t kFragmentFogLutBytes =
    (kPicaNativeFogLutEntryCount / 2U) * kPicaPackedVec4Bytes;

bool ReadBytes(std::span<const uint8_t> bytes, size_t offset,
               void* output, size_t size) noexcept {
    if (output == nullptr || offset > bytes.size() ||
        size > bytes.size() - offset) {
        return false;
    }
    std::memcpy(output, bytes.data() + offset, size);
    return true;
}

template <size_t Size>
bool Finite(const std::array<float, Size>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float component) {
        return std::isfinite(component);
    });
}

std::array<float, 3> NormalizeTowardSource(
    const std::array<float, 4>& nativeDirection) noexcept {
    std::array<float, 3> result{
        -nativeDirection[0], -nativeDirection[1], -nativeDirection[2]};
    const float lengthSquared =
        result[0] * result[0] + result[1] * result[1] +
        result[2] * result[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F) {
        return {};
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    for (float& component : result) {
        component *= inverseLength;
    }
    return result;
}

bool ValidateFogLut(std::span<const uint8_t> bytes) noexcept {
    if (kPicaPackedFragmentFogLutOffset > bytes.size() ||
        kFragmentFogLutBytes >
            bytes.size() - kPicaPackedFragmentFogLutOffset) {
        return false;
    }
    for (size_t pair = 0U;
         pair < kPicaNativeFogLutEntryCount / 2U; ++pair) {
        std::array<float, 4> packed{};
        if (!ReadBytes(bytes, kPicaPackedFragmentFogLutOffset +
                                  pair * kPicaPackedVec4Bytes,
                       packed.data(), sizeof(packed)) ||
            !Finite(packed)) {
            return false;
        }
    }
    return true;
}

bool ValidateVertexUniformRows(std::span<const uint8_t> bytes, uint8_t first, uint8_t count) noexcept {
    if (first >= 96U || count == 0U || static_cast<uint16_t>(first) + count > 96U) {
        return false;
    }
    for (uint8_t row = 0U; row < count; ++row) {
        std::array<float, 4> value{};
        if (!ReadBytes(bytes, kPicaPackedVertexFloatOffset +
                                  static_cast<size_t>(first + row) *
                                      kPicaPackedVec4Bytes,
                       value.data(),
                       sizeof(value)) ||
            !Finite(value)) {
            return false;
        }
    }
    return true;
}

bool ReadBooleanUniforms(std::span<const uint8_t> bytes, uint32_t& value) noexcept {
    return ReadBytes(bytes, 0U, &value, sizeof(value));
}

bool BooleanUniform(uint32_t mask, uint8_t uniform) noexcept {
    return uniform < 16U && (mask & (1U << uniform)) != 0U;
}

bool ReadPackedFragmentVector(std::span<const uint8_t> bytes,
                              size_t arrayOffset, size_t index,
                              std::array<float, 4>& value) noexcept {
    return ReadBytes(bytes,
                     arrayOffset + index * kPicaPackedVec4Bytes,
                     value.data(), sizeof(value)) &&
           Finite(value);
}

void DecodeNativeFragmentLighting(
    std::span<const uint8_t> packedFragmentUniforms,
    const ::Fast::Renderer3ds::PicaFragmentFeatureView& features,
    PicaNativeFragmentLightingState& result) noexcept {
    if (!features.Valid() ||
        !features.FragmentLighting.Available() ||
        !features.FragmentLighting.Valid()) {
        return;
    }

    result.Layout = features.FragmentLighting;
    result.Enabled = features.FragmentLightingEnabled;
    if (!result.Enabled) {
        result.Available = true;
        return;
    }

    std::array<float, 4> globalAmbient{};
    if (!ReadPackedFragmentVector(
            packedFragmentUniforms,
            kPicaPackedFragmentLightingGlobalAmbientOffset, 0U,
            globalAmbient)) {
        return;
    }
    std::copy_n(globalAmbient.begin(), result.GlobalAmbient.size(),
                result.GlobalAmbient.begin());

    std::array<bool, ::Fast::Renderer3ds::kPicaFragmentLightCount>
        activeNativeLights{};
    for (size_t slot = 0U; slot < result.Layout.ActiveLightCount;
         ++slot) {
        activeNativeLights[result.Layout.LightPermutation[slot]] = true;
    }

    for (size_t nativeIndex = 0U;
         nativeIndex < activeNativeLights.size(); ++nativeIndex) {
        if (!activeNativeLights[nativeIndex]) {
            continue;
        }
        std::array<float, 4> specular0{};
        std::array<float, 4> specular1{};
        std::array<float, 4> diffuse{};
        std::array<float, 4> ambient{};
        std::array<float, 4> position{};
        std::array<float, 4> spotDirection{};
        std::array<float, 4> attenuation{};
        if (!ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingSpecular0Offset, nativeIndex,
                specular0) ||
            !ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingSpecular1Offset, nativeIndex,
                specular1) ||
            !ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingDiffuseOffset, nativeIndex,
                diffuse) ||
            !ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingAmbientOffset, nativeIndex,
                ambient) ||
            !ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingPositionOffset, nativeIndex,
                position) ||
            !ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingSpotDirectionOffset,
                nativeIndex, spotDirection) ||
            !ReadPackedFragmentVector(
                packedFragmentUniforms,
                kPicaPackedFragmentLightingAttenuationOffset,
                nativeIndex, attenuation)) {
            result.Lights = {};
            result.GlobalAmbient = {};
            return;
        }

        auto& light = result.Lights[nativeIndex];
        std::copy_n(specular0.begin(), light.Specular0.size(),
                    light.Specular0.begin());
        std::copy_n(specular1.begin(), light.Specular1.size(),
                    light.Specular1.begin());
        std::copy_n(diffuse.begin(), light.Diffuse.size(),
                    light.Diffuse.begin());
        std::copy_n(ambient.begin(), light.Ambient.size(),
                    light.Ambient.begin());
        std::copy_n(position.begin(), light.PositionOrDirectionView.size(),
                    light.PositionOrDirectionView.begin());
        std::copy_n(spotDirection.begin(), light.SpotDirectionView.size(),
                    light.SpotDirectionView.begin());
        light.DistanceAttenuationBias = attenuation[0];
        light.DistanceAttenuationScale = attenuation[1];
        light.Active = true;
        light.ValuesAvailable = true;
    }
    result.Available = true;
}

bool DecodeTransformSpaces(
    std::span<const uint8_t> bytes,
    const ::Fast::Renderer3ds::PicaVertexTransformLayout& layout,
    PicaNativeMatrix4& viewToWorld,
    bool& viewToWorldAvailable,
    PicaNativeMatrix4& clipToWorld) noexcept {
    DirectionalShadowMatrix projection{};
    DirectionalShadowMatrix view = IdentityDirectionalShadowMatrix();
    const auto decodeRows = [&](uint8_t first, uint8_t count,
                                DirectionalShadowMatrix& matrix) {
        for (uint8_t row = 0U; row < count; ++row) {
            std::array<float, 4> values{};
            if (!ReadBytes(bytes,
                           kPicaPackedVertexFloatOffset +
                               static_cast<size_t>(first + row) *
                                   kPicaPackedVec4Bytes,
                           values.data(), sizeof(values)) ||
                !Finite(values)) {
                return false;
            }
            for (size_t column = 0U; column < values.size(); ++column) {
                matrix[column * 4U + row] = values[column];
            }
        }
        return true;
    };
    if (!decodeRows(layout.ProjectionFirstUniform,
                    layout.ProjectionRowCount, projection) ||
        !decodeRows(layout.ViewFirstUniform,
                    layout.ViewRowCount, view)) {
        return false;
    }

    int32_t flipViewport = 0;
    if (!ReadBytes(bytes, kPicaPackedVertexFlipViewportOffset, &flipViewport,
                   sizeof(flipViewport))) {
        return false;
    }

    // The generated native vertex shader applies these conversions after the
    // PICA projection. Publish the inverse of the actual Vulkan clip transform,
    // rather than the inverse of the pre-conversion PICA matrix.
    for (size_t column = 0U; column < 4U; ++column) {
        projection[column * 4U + 2U] = -projection[column * 4U + 2U];
        if (flipViewport != 0) {
            projection[column * 4U + 1U] =
                -projection[column * 4U + 1U];
        }
    }

    viewToWorldAvailable =
        InvertDirectionalShadowMatrix(view, viewToWorld);
    const auto worldToClip =
        MultiplyDirectionalShadowMatrices(projection, view);
    return InvertDirectionalShadowMatrix(worldToClip, clipToWorld);
}

uint8_t InfluenceCount(uint32_t booleanUniforms,
                       const ::Fast::Renderer3ds::PicaVertexSkeletonLayout&
                           layout) noexcept {
    if (!BooleanUniform(booleanUniforms, layout.EnableBooleanUniform)) {
        return 0U;
    }
    return BooleanUniform(booleanUniforms, layout.MultipleInfluenceBooleanUniform) ? layout.MaximumInfluences : 1U;
}

} // namespace

uint64_t HashPicaUniformBytes(std::span<const uint8_t> bytes) noexcept {
    if (bytes.empty()) {
        return 0U;
    }
    const uint64_t hash = ::Fast::Renderer::ContentHash64(bytes);
    return hash == 0U ? 1U : hash;
}

PicaNativeDrawEnvironment DecodePicaNativeDrawEnvironment(
    std::span<const uint8_t> packedVertexUniforms,
    std::span<const uint8_t> packedFragmentUniforms,
    const ::Fast::Renderer3ds::PicaFragmentFeatureView&
        fragmentFeatures) noexcept {
    PicaNativeDrawEnvironment result;

    uint32_t booleanUniforms = 0U;
    if (ReadBytes(packedVertexUniforms, 0U, &booleanUniforms,
                  sizeof(booleanUniforms))) {
        result.Lighting.Available = true;
        result.Lighting.Enabled =
            (booleanUniforms & (1U << 9U)) != 0U;
        if (result.Lighting.Enabled) {
            const size_t requiredBytes =
                kPicaPackedVertexFloatOffset +
                (static_cast<size_t>(kLightDirectionSlots.back()) + 3U) *
                    kPicaPackedVec4Bytes;
            if (packedVertexUniforms.size() < requiredBytes) {
                result.Lighting.Available = false;
            } else {
                for (const uint32_t directionSlot :
                     kLightDirectionSlots) {
                    const size_t directionOffset =
                        kPicaPackedVertexFloatOffset +
                        static_cast<size_t>(directionSlot) *
                            kPicaPackedVec4Bytes;
                    std::array<float, 4> direction{};
                    std::array<float, 4> diffuse{};
                    std::array<float, 4> ambient{};
                    if (!ReadBytes(packedVertexUniforms, directionOffset,
                                   direction.data(), sizeof(direction)) ||
                        !ReadBytes(packedVertexUniforms,
                                   directionOffset + kPicaPackedVec4Bytes,
                                   diffuse.data(), sizeof(diffuse)) ||
                        !ReadBytes(packedVertexUniforms,
                                   directionOffset +
                                       2U * kPicaPackedVec4Bytes,
                                   ambient.data(), sizeof(ambient)) ||
                        !Finite(direction) || !Finite(diffuse) ||
                        !Finite(ambient)) {
                        result.Lighting.Available = false;
                        result.Lighting.ActiveLightCount = 0U;
                        result.Lighting.Lights = {};
                        break;
                    }
                    const auto towardSource =
                        NormalizeTowardSource(direction);
                    if (towardSource == std::array<float, 3>{}) {
                        continue;
                    }
                    auto& light = result.Lighting.Lights[
                        result.Lighting.ActiveLightCount++];
                    light.DirectionViewTowardSource = towardSource;
                    std::copy_n(diffuse.begin(), 3U,
                                light.Diffuse.begin());
                    std::copy_n(ambient.begin(), 3U,
                                light.Ambient.begin());
                    light.Valid = true;
                }
            }
        }
    }

    int32_t wBuffering = 0;
    if (ReadBytes(packedFragmentUniforms,
                  kPicaPackedFragmentDepthScaleOffset,
                  &result.Depth.Scale, sizeof(result.Depth.Scale)) &&
        ReadBytes(packedFragmentUniforms,
                  kPicaPackedFragmentDepthOffsetOffset,
                  &result.Depth.Offset, sizeof(result.Depth.Offset)) &&
        ReadBytes(packedFragmentUniforms,
                  kPicaPackedFragmentWBufferingOffset,
                  &wBuffering, sizeof(wBuffering)) &&
        std::isfinite(result.Depth.Scale) &&
        std::isfinite(result.Depth.Offset)) {
        result.Depth.WBuffering = wBuffering != 0;
        result.Depth.Valid = true;
    }

    if (!fragmentFeatures.Valid()) {
        return result;
    }
    result.FragmentLightingEnabled =
        fragmentFeatures.FragmentLightingEnabled;
    DecodeNativeFragmentLighting(packedFragmentUniforms,
                                 fragmentFeatures,
                                 result.FragmentLighting);
    result.Fog.Available = true;
    result.Fog.Mode = fragmentFeatures.FogMode;
    result.Fog.Enabled = fragmentFeatures.FogEnabled;
    result.Fog.Flip = fragmentFeatures.FogFlip;
    if (!result.Fog.Enabled) {
        return result;
    }

    std::array<float, 4> fogColor{};
    if (!ReadBytes(packedFragmentUniforms,
                   kPicaPackedFragmentFogColorOffset,
                   fogColor.data(), sizeof(fogColor)) ||
        !Finite(fogColor) || !ValidateFogLut(packedFragmentUniforms)) {
        result.Fog.Available = false;
        return result;
    }
    std::copy_n(fogColor.begin(), 3U, result.Fog.Color.begin());
    result.Fog.LutEntryCount =
        static_cast<uint16_t>(kPicaNativeFogLutEntryCount);
    result.Fog.LutContentVersion = HashPicaUniformBytes(
        packedFragmentUniforms.subspan(
            kPicaPackedFragmentFogLutOffset, kFragmentFogLutBytes));
    return result;
}

PicaNativeVertexState DecodePicaNativeVertexState(std::span<const uint8_t> packedVertexUniforms,
                                                  std::span<const uint8_t> packedPreviousVertexUniforms,
                                                  bool previousVertexUniformsAvailable,
                                                  const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks) noexcept {
    PicaNativeVertexState result;
    if (!hooks.Valid() || !hooks.Has(::Fast::Renderer3ds::PicaVertexShaderSemantic::TransformProgram) ||
        hooks.TransformProgram() == nullptr) {
        return result;
    }

    result.Transform.Layout = *hooks.TransformProgram();
    result.Transform.ProgramAvailable = true;
    if (hooks.Has(::Fast::Renderer3ds::PicaVertexShaderSemantic::SkeletonProgram) && hooks.SkeletonProgram() != nullptr) {
        result.Skeleton.Layout = *hooks.SkeletonProgram();
        result.Skeleton.ProgramAvailable = true;
    }

    const auto decode = [&](std::span<const uint8_t> bytes,
                            bool& transformAvailable,
                            bool& viewToWorldAvailable,
                            DirectionalShadowMatrix& viewToWorld,
                            bool& clipToWorldAvailable,
                            DirectionalShadowMatrix& clipToWorld,
                            bool& usesSkeleton,
                            bool& skeletonAvailable,
                            uint8_t& influenceCount) {
        uint32_t booleanUniforms = 0U;
        if (!ReadBooleanUniforms(bytes, booleanUniforms) ||
            !ValidateVertexUniformRows(bytes, result.Transform.Layout.ProjectionFirstUniform,
                                       result.Transform.Layout.ProjectionRowCount) ||
            !ValidateVertexUniformRows(bytes, result.Transform.Layout.ViewFirstUniform,
                                       result.Transform.Layout.ViewRowCount)) {
            return;
        }
        usesSkeleton = result.Skeleton.ProgramAvailable &&
                       BooleanUniform(booleanUniforms, result.Skeleton.Layout.EnableBooleanUniform);
        const uint8_t first =
            usesSkeleton ? result.Skeleton.Layout.PaletteFirstUniform : result.Transform.Layout.ModelFirstUniform;
        const uint8_t rows =
            usesSkeleton ? result.Skeleton.Layout.RowsPerMatrix : result.Transform.Layout.ModelRowCount;
        if (!ValidateVertexUniformRows(bytes, first, rows)) {
            return;
        }
        transformAvailable = true;
        clipToWorldAvailable = DecodeTransformSpaces(
            bytes, result.Transform.Layout, viewToWorld,
            viewToWorldAvailable, clipToWorld);
        if (usesSkeleton) {
            influenceCount = InfluenceCount(booleanUniforms, result.Skeleton.Layout);
            skeletonAvailable = influenceCount != 0U;
        }
    };

    decode(packedVertexUniforms, result.Transform.CurrentAvailable,
           result.Transform.CurrentViewToWorldAvailable,
           result.Transform.CurrentViewToWorld,
           result.Transform.CurrentClipToWorldAvailable,
           result.Transform.CurrentClipToWorld,
           result.Transform.CurrentUsesSkeleton,
           result.Skeleton.CurrentAvailable, result.Skeleton.CurrentInfluenceCount);
    if (previousVertexUniformsAvailable) {
        decode(packedPreviousVertexUniforms, result.Transform.PreviousAvailable,
               result.Transform.PreviousViewToWorldAvailable,
               result.Transform.PreviousViewToWorld,
               result.Transform.PreviousClipToWorldAvailable,
               result.Transform.PreviousClipToWorld,
               result.Transform.PreviousUsesSkeleton,
               result.Skeleton.PreviousAvailable, result.Skeleton.PreviousInfluenceCount);
    }
    return result;
}

bool DecodePicaNativeFogLut(
    std::span<const uint8_t> packedFragmentUniforms,
    const PicaNativeFogState& fog,
    std::span<std::array<float, 2>> output) noexcept {
    if (!fog.Available || !fog.Enabled ||
        fog.LutEntryCount != kPicaNativeFogLutEntryCount ||
        output.size() < kPicaNativeFogLutEntryCount ||
        !ValidateFogLut(packedFragmentUniforms)) {
        return false;
    }
    for (size_t entry = 0U;
         entry < kPicaNativeFogLutEntryCount; ++entry) {
        const size_t pair = entry / 2U;
        const size_t component = (entry % 2U) * 2U;
        std::array<float, 4> packed{};
        ReadBytes(packedFragmentUniforms,
                  kPicaPackedFragmentFogLutOffset + pair * sizeof(packed),
                  packed.data(), sizeof(packed));
        output[entry] = {packed[component], packed[component + 1U]};
    }
    return true;
}

} // namespace Fast::Oot3d
