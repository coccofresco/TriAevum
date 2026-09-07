#include "fast/renderer3ds/pica_semantic_scene_view.h"

namespace Fast::Renderer3ds {
namespace {

[[nodiscard]] PicaVersionedReference GeometryReference(
    const PicaResolvedDrawRecord& draw) noexcept {
    return {
        .Identity = draw.GeometryIdentity,
        .ContentVersion = draw.GeometryContentVersion,
        .NativeHandle = draw.GeometryBuffer.NativeHandle,
        .IdentityAvailable = draw.GeometryIdentityAvailable,
        .ContentVersionAvailable =
            draw.GeometryIdentityAvailable &&
            draw.GeometryContentVersion != 0U,
        .NativeHandleAvailable = draw.GeometryBuffer.NativeHandle != 0U,
    };
}

[[nodiscard]] PicaMaterialReference MaterialReference(
    const PicaResolvedDrawRecord& draw) noexcept {
    return {
        .DescriptorSchemaVersion = draw.CanonicalDescriptorSchemaVersion,
        .VertexProgramId = draw.CanonicalVertexProgramId,
        .FragmentProgramId = draw.CanonicalFragmentProgramId,
        .RasterStateId = draw.CanonicalRasterStateId,
        .PipelineId = draw.CanonicalPipelineId,
        .DynamicStateId = draw.CanonicalDynamicStateId,
        .FullRegisterStateId = draw.CanonicalFullRegisterStateId,
        .ResolvedState = draw.Material.Available()
            ? &draw.Material
            : nullptr,
    };
}

[[nodiscard]] PicaTextureReference TextureReference(
    const PicaSceneTextureBinding& texture, uint8_t slot) noexcept {
    PicaTextureReference result;
    result.Slot = slot;
    result.Bound = texture.Bound;
    result.PhysicalAddress = texture.PhysicalAddress;
    result.NativeBaseLevelContentHash =
        texture.NativeBaseLevelContentHash;
    result.NativeBaseLevelContentHashAvailable =
        texture.NativeBaseLevelContentHashAvailable;
    result.ReplacementContentHash = texture.ReplacementContentHash;
    result.CustomReplacement = texture.CustomReplacement;
    if (!texture.Bound) {
        return result;
    }
    result.Image = {
        .Identity = static_cast<uint64_t>(texture.NativeImageHandle),
        .ContentVersion = texture.NativeContentHash,
        .NativeHandle = texture.NativeImageHandle,
        .IdentityAvailable = texture.NativeImageHandle != 0U,
        .ContentVersionAvailable = texture.NativeContentHashAvailable,
        .NativeHandleAvailable = texture.NativeImageHandle != 0U,
    };
    return result;
}

[[nodiscard]] PicaBufferSliceReference UniformReference(
    const PicaSceneBufferResource& buffer, uint64_t offset,
    uint64_t size, uint64_t contentVersion) noexcept {
    return {
        .NativeHandle = buffer.NativeHandle,
        .Offset = offset,
        .Size = size,
        .ContentVersion = contentVersion,
        .NativeHandleAvailable = buffer.NativeHandle != 0U,
        .ContentVersionAvailable = contentVersion != 0U,
    };
}

[[nodiscard]] bool HeaderAvailable(
    const PicaSemanticSceneView& view) noexcept {
    return view.SchemaVersion == kPicaSemanticSceneViewSchemaVersion &&
           view.FrameId != 0U && view.Generation != 0U &&
           view.DrawStream.SchemaVersion ==
               kPicaResolvedDrawStreamSchemaVersion &&
           view.DrawStream.FrameId == view.FrameId;
}

} // namespace

bool PicaSemanticSceneView::Available() const noexcept {
    return HeaderAvailable(*this) && DrawStream.Available();
}

bool PicaSemanticSceneView::TemporalSampleAvailable() const noexcept {
    return HeaderAvailable(*this) && TemporalSample != nullptr &&
           TemporalSample->Available();
}

bool PicaSemanticSceneView::CurrentViewFamilyAvailable() const noexcept {
    return HeaderAvailable(*this) && CurrentViewFamily != nullptr &&
           CurrentViewFamily->Available();
}

bool PicaSemanticSceneView::ViewFamilyHistoryAvailable() const noexcept {
    if (!CurrentViewFamilyAvailable() || PreviousViewFamily == nullptr ||
        !PreviousViewFamily->Available() ||
        CurrentViewFamily->FamilyId != PreviousViewFamily->FamilyId ||
        CurrentViewFamily->ViewCount != PreviousViewFamily->ViewCount) {
        return false;
    }
    for (size_t index = 0U; index < CurrentViewFamily->ViewCount; ++index) {
        if (CurrentViewFamily->Views[index].ViewId !=
            PreviousViewFamily->Views[index].ViewId) {
            return false;
        }
    }
    return true;
}

std::span<const PicaResolvedDrawRecord>
PicaSemanticSceneView::Draws() const noexcept {
    return HeaderAvailable(*this)
        ? DrawStream.Draws
        : std::span<const PicaResolvedDrawRecord>{};
}

std::optional<PicaSemanticDrawView>
PicaSemanticSceneView::DescribeDraw(size_t drawIndex) const noexcept {
    const auto draws = Draws();
    if (drawIndex >= draws.size()) {
        return std::nullopt;
    }
    const auto& draw = draws[drawIndex];
    const size_t firstBinding = draw.FirstVertexBinding;
    const size_t bindingCount = draw.VertexBindingCount;
    if (firstBinding > DrawStream.VertexBindings.size() ||
        bindingCount >
            DrawStream.VertexBindings.size() - firstBinding) {
        return std::nullopt;
    }
    PicaSemanticDrawView result;
    result.PicaDraw = &draw;
    result.SubmissionId = draw.SubmissionId;
    result.Composition = &draw.Composition;
    result.Raster.ResolvedState = draw.Raster.Available()
        ? &draw.Raster
        : nullptr;
    result.RenderTarget.ResolvedState = draw.RenderTarget.Available()
        ? &draw.RenderTarget
        : nullptr;
    result.Geometry = GeometryReference(draw);
    result.Material = MaterialReference(draw);
    for (size_t textureIndex = 0U;
         textureIndex < draw.Textures.size(); ++textureIndex) {
        result.Textures[textureIndex] = TextureReference(
            draw.Textures[textureIndex],
            static_cast<uint8_t>(textureIndex));
    }
    result.VertexUniforms = UniformReference(
        draw.UniformBuffer, draw.VertexUniformOffset,
        draw.VertexUniformSize, draw.VertexUniformContentVersion);
    if (draw.PreviousVertexUniformHistoryAvailable) {
        result.PreviousVertexUniforms = UniformReference(
            draw.UniformBuffer, draw.PreviousVertexUniformOffset,
            draw.PreviousVertexUniformSize,
            draw.PreviousVertexUniformContentVersion);
    }
    result.FragmentUniforms = UniformReference(
        draw.UniformBuffer, draw.FragmentUniformOffset,
        draw.FragmentUniformSize, draw.FragmentUniformContentVersion);
    if (draw.NativeLighting.Available) {
        result.Lighting = &draw.NativeLighting;
        result.NativeLight = PicaSemanticAvailability::Available;
    }
    if (draw.NativeFragmentLighting.Available) {
        result.FragmentLighting = &draw.NativeFragmentLighting;
        result.NativeFragmentLight = PicaSemanticAvailability::Available;
    }
    if (draw.NativeDepth.Valid) {
        result.Depth = &draw.NativeDepth;
    }
    if (draw.NativeFog.Available) {
        result.Fog = &draw.NativeFog;
        result.NativeFog = PicaSemanticAvailability::Available;
    }
    if (draw.NativeTransform.ProgramAvailable) {
        result.Transform = &draw.NativeTransform;
        if (draw.NativeTransform.CurrentAvailable) {
            result.CurrentTransform = PicaSemanticAvailability::Available;
        }
        if (draw.NativeTransform.PreviousAvailable) {
            result.PreviousTransform = PicaSemanticAvailability::Available;
        }
    }
    if (draw.NativeSkeleton.ProgramAvailable) {
        result.Skeleton = &draw.NativeSkeleton;
        if (draw.NativeSkeleton.CurrentAvailable) {
            result.CurrentSkeleton = PicaSemanticAvailability::Available;
        }
        if (draw.NativeSkeleton.PreviousAvailable) {
            result.PreviousSkeleton = PicaSemanticAvailability::Available;
        }
    }
    return result;
}

} // namespace Fast::Renderer3ds
