#include "oot3d_native_pica_vulkan_bridge.h"

#include "fast/oot3d/pica_uniform_layout.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

namespace Oot3dNativeGame {
namespace {

template <typename Destination, typename Source>
Destination SameValueEnum(Source source) {
    return static_cast<Destination>(static_cast<uint8_t>(source));
}

std::array<uint8_t, Fast::Oot3d::kPicaPackedVertexUniformSize>
PackVertexUniforms(
    const Oot3dPicaVertexUniformState& uniforms, bool flipViewport) {
    std::array<uint8_t, Fast::Oot3d::kPicaPackedVertexUniformSize> bytes{};
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedVertexBooleanMaskOffset,
                &uniforms.BooleanMask,
                sizeof(uniforms.BooleanMask));
    const int32_t flip = flipViewport ? 1 : 0;
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedVertexFlipViewportOffset,
                &flip, sizeof(flip));
    std::memcpy(bytes.data() + Fast::Oot3d::kPicaPackedVertexIntegerOffset,
                uniforms.Integers.data(),
                sizeof(uniforms.Integers));
    std::memcpy(bytes.data() + Fast::Oot3d::kPicaPackedVertexFloatOffset,
                uniforms.Floats.data(),
                sizeof(uniforms.Floats));
    return bytes;
}

std::array<uint8_t, Fast::Oot3d::kPicaPackedFragmentUniformSize>
PackFragmentUniforms(
    const Oot3dPicaFragmentUniformState& uniforms,
    const Oot3dPicaViewportState& viewport) {
    std::array<uint8_t, Fast::Oot3d::kPicaPackedFragmentUniformSize> bytes{};
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentTevConstantsOffset,
                uniforms.TevConstants.data(),
                sizeof(uniforms.TevConstants));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentCombinerBufferOffset,
                uniforms.CombinerBufferColor.data(),
                sizeof(uniforms.CombinerBufferColor));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentAlphaReferenceOffset,
                &uniforms.AlphaReference, sizeof(uniforms.AlphaReference));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentDepthScaleOffset,
                &viewport.DepthRange,
                sizeof(viewport.DepthRange));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentDepthOffsetOffset,
                &viewport.NearPlane,
                sizeof(viewport.NearPlane));
    const int32_t wBuffering = viewport.ZBuffering ? 0 : 1;
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentWBufferingOffset,
                &wBuffering,
                sizeof(wBuffering));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentFogColorOffset,
                uniforms.FogColor.data(),
                sizeof(uniforms.FogColor));
    for (size_t pair = 0; pair < 64U; ++pair) {
        std::array<float, 4> packed{
            uniforms.FogLut[pair * 2U][0],
            uniforms.FogLut[pair * 2U][1],
            uniforms.FogLut[pair * 2U + 1U][0],
            uniforms.FogLut[pair * 2U + 1U][1]};
        std::memcpy(bytes.data() +
                        Fast::Oot3d::kPicaPackedFragmentFogLutOffset +
                        pair * sizeof(packed),
                    packed.data(), sizeof(packed));
    }
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentTextureLodBiasOffset,
                uniforms.TextureLodBias.data(),
                sizeof(uniforms.TextureLodBias));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingSpecular0Offset,
                uniforms.Lighting.Specular0.data(),
                sizeof(uniforms.Lighting.Specular0));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingSpecular1Offset,
                uniforms.Lighting.Specular1.data(),
                sizeof(uniforms.Lighting.Specular1));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingDiffuseOffset,
                uniforms.Lighting.Diffuse.data(),
                sizeof(uniforms.Lighting.Diffuse));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingAmbientOffset,
                uniforms.Lighting.Ambient.data(),
                sizeof(uniforms.Lighting.Ambient));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingPositionOffset,
                uniforms.Lighting.Position.data(),
                sizeof(uniforms.Lighting.Position));
    std::memcpy(
        bytes.data() +
            Fast::Oot3d::kPicaPackedFragmentLightingSpotDirectionOffset,
        uniforms.Lighting.SpotDirection.data(),
        sizeof(uniforms.Lighting.SpotDirection));
    std::memcpy(bytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingAttenuationOffset,
                uniforms.Lighting.Attenuation.data(),
                sizeof(uniforms.Lighting.Attenuation));
    std::memcpy(
        bytes.data() +
            Fast::Oot3d::kPicaPackedFragmentLightingGlobalAmbientOffset,
        uniforms.Lighting.GlobalAmbient.data(),
        sizeof(uniforms.Lighting.GlobalAmbient));
    std::memcpy(
        bytes.data() +
            Fast::Oot3d::kPicaPackedFragmentShadowTextureBiasOffset,
        &uniforms.ShadowTextureBias, sizeof(uniforms.ShadowTextureBias));
    std::memcpy(
        bytes.data() +
            Fast::Oot3d::kPicaPackedFragmentShadowOrthographicOffset,
        &uniforms.ShadowOrthographic, sizeof(uniforms.ShadowOrthographic));
    std::memcpy(
        bytes.data() +
            Fast::Oot3d::kPicaPackedFragmentShadowBiasConstantOffset,
        &uniforms.ShadowBiasConstant, sizeof(uniforms.ShadowBiasConstant));
    std::memcpy(
        bytes.data() +
            Fast::Oot3d::kPicaPackedFragmentShadowBiasLinearOffset,
        &uniforms.ShadowBiasLinear, sizeof(uniforms.ShadowBiasLinear));
    return bytes;
}

} // namespace

bool SubmitOot3dPicaVulkanDrawPlan(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaVulkanDrawPlan& plan, std::string* error,
    uint64_t renderTargetNamespace,
    const Oot3dPicaVulkanDrawOverrides* overrides,
    Oot3dPicaVulkanDrawBridgeTiming* timing) {
    using Clock = std::chrono::steady_clock;
    auto phaseStart =
        timing != nullptr ? Clock::now() : Clock::time_point{};
    const auto& vertexUniformState = overrides != nullptr && overrides->VertexUniforms != nullptr
                                         ? *overrides->VertexUniforms
                                         : plan.VertexShader.Uniforms;
    const auto& fragmentUniformState = overrides != nullptr && overrides->FragmentUniforms != nullptr
                                           ? *overrides->FragmentUniforms
                                           : plan.FragmentShader.Uniforms;
    const auto& viewport =
        overrides != nullptr && overrides->Viewport != nullptr ? *overrides->Viewport : plan.State.Viewport;
    const auto& vertexBindings =
        overrides != nullptr && overrides->VertexBindings != nullptr ? *overrides->VertexBindings : plan.VertexBindings;
    const auto& blendConstantColor = overrides != nullptr && overrides->BlendConstantColor != nullptr
                                         ? *overrides->BlendConstantColor
                                         : plan.State.OutputMerger.Blend.ConstantColor;
    const auto vertexUniforms = PackVertexUniforms(vertexUniformState, plan.State.Framebuffer.Flipped);
    const auto fragmentUniforms =
        PackFragmentUniforms(fragmentUniformState, viewport);
    if (timing != nullptr) {
        const auto now = Clock::now();
        timing->UniformPackingSeconds +=
            std::chrono::duration<double>(now - phaseStart).count();
        phaseStart = now;
    }

    std::array<Oot3d::Renderer::PicaVertexBindingView, 16> bindings{};
    if (vertexBindings.size() > bindings.size()) {
        if (error != nullptr) {
            *error = "native PICA draw exceeds the 16 vertex binding limit";
        }
        return false;
    }
    size_t bindingCount = 0;
    for (const auto& binding : vertexBindings) {
      bindings[bindingCount++] = {binding.Binding, binding.ByteStride,
                                  binding.InputRate ==
                                      Oot3dPicaVertexInputRate::PerInstance,
                                  binding.ResolvedBytes()};
    }
    std::array<Oot3d::Renderer::PicaVertexAttributeView, 16> attributes{};
    if (plan.VertexAttributes.size() > attributes.size()) {
        if (error != nullptr) {
            *error = "native PICA draw exceeds the 16 vertex attribute limit";
        }
        return false;
    }
    size_t attributeCount = 0;
    for (const auto& attribute : plan.VertexAttributes) {
        attributes[attributeCount++] = {
            attribute.Location, attribute.Binding,
            SameValueEnum<Oot3d::Renderer::PicaVertexFormat>(attribute.Format),
            attribute.ComponentCount, attribute.ByteOffset};
    }
    std::array<Oot3d::Renderer::PicaTextureView, 3> textures{};
    if (plan.Textures.size() > textures.size()) {
        if (error != nullptr) {
            *error = "native PICA draw exceeds the three texture unit limit";
        }
        return false;
    }
    size_t textureCount = 0;
    for (const auto& texture : plan.Textures) {
        textures[textureCount++] = {
            texture.Slot, texture.State.Width, texture.State.Height,
            texture.State.Format, texture.State.Type, texture.State.WrapS,
            texture.State.WrapT, texture.State.MinLinear,
            texture.State.MagLinear, texture.State.MipLinear,
            texture.State.LodBiasRaw, texture.State.MinMipLevel,
            texture.State.MaxMipLevel, texture.State.PhysicalAddress,
            texture.ResolvedNativeBytes(), texture.NativeContentHash,
            texture.NativeContentHashAvailable,
            texture.NativeBaseLevelContentHash,
            texture.NativeBaseLevelContentHashAvailable};
    }

    Oot3d::Renderer::PicaDrawView draw;
    draw.SubmissionId = plan.SubmissionId;
    draw.RenderTargetNamespace = renderTargetNamespace;
    draw.CommandListAddress = plan.CommandListAddress;
    draw.CommandListOffsetWords = plan.CommandListOffsetWords;
    draw.CompositionDomain =
        SameValueEnum<Oot3d::Renderer::PicaCompositionDomain>(
            plan.CompositionDomain);
    draw.Composition = {
        SameValueEnum<Oot3d::Renderer::PicaCompositionLayer>(
            plan.Composition.Layer),
        SameValueEnum<Oot3d::Renderer::PicaCompositionProvenance>(
            plan.Composition.Provenance),
        plan.Composition.SourcePc,
        plan.Composition.NativeValue,
    };
    draw.CanonicalDescriptorSchemaVersion =
        plan.CanonicalIdentity.SchemaVersion;
    draw.CanonicalVertexProgramId =
        plan.CanonicalIdentity.VertexProgramId;
    draw.CanonicalFragmentProgramId =
        plan.CanonicalIdentity.FragmentProgramId;
    draw.CanonicalRasterStateId = plan.CanonicalIdentity.RasterStateId;
    draw.CanonicalPipelineId = plan.CanonicalIdentity.PipelineId;
    draw.CanonicalDynamicStateId = plan.CanonicalIdentity.DynamicStateId;
    draw.CanonicalFullRegisterStateId =
        plan.CanonicalIdentity.FullRegisterStateId;
    draw.VertexShaderKey = plan.VertexShader.StateKey;
    draw.FragmentShaderKey = plan.FragmentShader.StateKey;
    draw.VertexShaderSource = plan.ResolvedVertexShaderSource();
    draw.FragmentShaderSource = plan.ResolvedFragmentShaderSource();
    draw.VertexShaderSourceIdentity = plan.VertexShader.SourceIdentity;
    draw.FragmentShaderSourceIdentity =
        plan.FragmentShader.SourceIdentity;
    if (plan.VertexShader.TemporalProgram != nullptr) {
        draw.TemporalVertexProgram = {
            plan.VertexShader.TemporalProgram->Hooks,
            plan.VertexShader.TemporalProgram->PreviousRegisterState,
            plan.VertexShader.TemporalProgram->PreviousMainBody,
        };
    }
    draw.FragmentShaderHooks = plan.FragmentShader.Hooks;
    draw.FragmentFeatures.SchemaVersion =
        plan.FragmentFeatures.FragmentLighting.Available()
            ? Oot3d::Renderer::kPicaFragmentFeatureSchemaVersion
            : Oot3d::Renderer::kPicaFragmentFeatureLegacySchemaVersion;
    draw.FragmentFeatures.FragmentLightingEnabled =
        plan.FragmentFeatures.FragmentLightingEnabled;
    draw.FragmentFeatures.FogEnabled =
        plan.FragmentFeatures.FogEnabled;
    draw.FragmentFeatures.FogFlip = plan.FragmentFeatures.FogFlip;
    draw.FragmentFeatures.FogMode = plan.FragmentFeatures.FogMode;
    draw.FragmentFeatures.FragmentLighting =
        plan.FragmentFeatures.FragmentLighting;
    draw.VertexUniformBytes = vertexUniforms;
    draw.FragmentUniformBytes = fragmentUniforms;
    draw.VertexBindings =
        std::span<const Oot3d::Renderer::PicaVertexBindingView>(
        bindings.data(), bindingCount);
    draw.VertexAttributes =
        std::span<const Oot3d::Renderer::PicaVertexAttributeView>(
            attributes.data(), attributeCount);
    draw.GeometryIdentity = plan.GeometryIdentity;
    draw.GeometryContentVersion = plan.GeometryContentVersion;
    draw.GeometryIdentityAvailable =
        plan.GeometryIdentityAvailable &&
        (overrides == nullptr || overrides->VertexBindings == nullptr);
    const auto semantics =
        ResolveOot3dCmbVertexSemanticLocations(plan.State);
    draw.PositionAttributeLocation = semantics.Position;
    draw.TexCoord0AttributeLocation = semantics.TexCoord0;
    draw.IndexBytes = plan.ResolvedIndexBytes();
    draw.Indexed = plan.Indexed;
    draw.IndicesAre16Bit = plan.IndicesAre16Bit;
    draw.BaseVertex = plan.BaseVertex;
    draw.VertexCount = plan.VertexCount;
    draw.Textures = std::span<const Oot3d::Renderer::PicaTextureView>(
        textures.data(), textureCount);
    if (plan.LightingLuts != nullptr) {
        draw.LightingLut.PackedEntries = plan.LightingLuts->PackedEntries;
        draw.LightingLut.ContentHash = plan.LightingLuts->ContentHash;
        draw.LightingLut.ContentHashAvailable =
            plan.LightingLuts->ContentHashAvailable;
    }
    draw.Topology = SameValueEnum<Oot3d::Renderer::PicaTopology>(
        plan.State.Topology);
    draw.CullMode = SameValueEnum<Oot3d::Renderer::NativeCullMode>(
        plan.State.CullMode);
    draw.ViewportX = static_cast<float>(viewport.CornerX);
    draw.ViewportY = static_cast<float>(viewport.CornerY);
    draw.ViewportWidth = viewport.HalfWidth * 2.0F;
    draw.ViewportHeight = viewport.HalfHeight * 2.0F;
    draw.DepthRange = viewport.DepthRange;
    draw.NearPlane = viewport.NearPlane;
    draw.WBuffering = !viewport.ZBuffering;
    draw.FramebufferFlipped = plan.State.Framebuffer.Flipped;
    draw.FramebufferWidth = plan.State.Framebuffer.Width;
    draw.FramebufferHeight = plan.State.Framebuffer.Height;
    draw.FramebufferColorPhysicalAddress =
        plan.State.Framebuffer.ColorPhysicalAddress;
    draw.FramebufferDepthPhysicalAddress =
        plan.State.Framebuffer.DepthPhysicalAddress;
    draw.FramebufferColorFormat = plan.State.Framebuffer.ColorFormat;
    draw.FramebufferDepthFormat = plan.State.Framebuffer.DepthFormat;
    draw.ScissorMode = static_cast<uint8_t>(plan.State.Scissor.Mode);
    draw.ScissorX1 = plan.State.Scissor.X1;
    draw.ScissorY1 = plan.State.Scissor.Y1;
    draw.ScissorX2 = plan.State.Scissor.X2;
    draw.ScissorY2 = plan.State.Scissor.Y2;
    draw.ColorWriteMask = plan.State.OutputMerger.ColorWriteMask;
    draw.FragmentOperationMode =
        plan.State.OutputMerger.FragmentOperationMode;
    draw.LogicOperation =
        SameValueEnum<Oot3d::Renderer::PicaLogicOperation>(
            plan.State.OutputMerger.LogicOperation);
    draw.Blend.Enabled = plan.State.OutputMerger.Blend.Enabled;
    draw.Blend.EquationRgb =
        SameValueEnum<Oot3d::Renderer::NativeBlendEquation>(
        plan.State.OutputMerger.Blend.ColorEquation);
    draw.Blend.EquationAlpha =
        SameValueEnum<Oot3d::Renderer::NativeBlendEquation>(
        plan.State.OutputMerger.Blend.AlphaEquation);
    draw.Blend.SourceRgb =
        SameValueEnum<Oot3d::Renderer::NativeBlendFactor>(
        plan.State.OutputMerger.Blend.SourceColor);
    draw.Blend.DestRgb =
        SameValueEnum<Oot3d::Renderer::NativeBlendFactor>(
        plan.State.OutputMerger.Blend.DestinationColor);
    draw.Blend.SourceAlpha =
        SameValueEnum<Oot3d::Renderer::NativeBlendFactor>(
        plan.State.OutputMerger.Blend.SourceAlpha);
    draw.Blend.DestAlpha =
        SameValueEnum<Oot3d::Renderer::NativeBlendFactor>(
        plan.State.OutputMerger.Blend.DestinationAlpha);
    std::copy(blendConstantColor.begin(), blendConstantColor.end(),
              draw.Blend.ConstantColor);
    draw.DepthTestEnabled = plan.State.OutputMerger.Depth.TestEnabled;
    draw.DepthWriteEnabled = plan.State.OutputMerger.Depth.WriteEnabled;
    draw.DepthCompare =
        SameValueEnum<Oot3d::Renderer::PicaCompareFunction>(
            plan.State.OutputMerger.Depth.Compare);
    const auto& stencil = plan.State.OutputMerger.Stencil;
    draw.Stencil.Enabled = stencil.Enabled;
    draw.Stencil.Compare =
        SameValueEnum<Oot3d::Renderer::PicaCompareFunction>(stencil.Compare);
    draw.Stencil.Reference = stencil.Reference;
    draw.Stencil.CompareMask = stencil.CompareMask;
    draw.Stencil.WriteMask = stencil.WriteMask;
    draw.Stencil.Fail =
        SameValueEnum<Oot3d::Renderer::PicaStencilAction>(stencil.Fail);
    draw.Stencil.DepthFail =
        SameValueEnum<Oot3d::Renderer::PicaStencilAction>(stencil.DepthFail);
    draw.Stencil.Pass =
        SameValueEnum<Oot3d::Renderer::PicaStencilAction>(stencil.Pass);
    if (timing != nullptr) {
        const auto now = Clock::now();
        timing->ViewConstructionSeconds +=
            std::chrono::duration<double>(now - phaseStart).count();
        phaseStart = now;
    }
    const bool submitted =
        renderingApi.SubmitPicaDraw(draw, error);
    if (timing != nullptr) {
        timing->BackendSubmissionSeconds +=
            std::chrono::duration<double>(Clock::now() - phaseStart).count();
    }
    return submitted;
}

bool SubmitOot3dPicaVulkanDisplayTransfer(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaDisplayTransferSubmission& submission,
    bool present, std::string* error, uint64_t renderTargetNamespace,
    Oot3d::Renderer::PicaPresentationMode presentationMode) {
    const auto& transfer = submission.Transfer;
    Oot3d::Renderer::PicaDisplayTransferView view;
    view.CompletionId = submission.CompletionId;
    view.RenderTargetNamespace = renderTargetNamespace;
    view.InputPhysicalAddress = submission.InputPhysicalAddress;
    view.OutputPhysicalAddress = submission.OutputPhysicalAddress;
    view.InputWidth = static_cast<uint16_t>(transfer.InputSize & 0xFFFFU);
    view.InputHeight = static_cast<uint16_t>(transfer.InputSize >> 16U);
    const uint16_t declaredOutputWidth =
        static_cast<uint16_t>(transfer.OutputSize & 0xFFFFU);
    const uint16_t declaredOutputHeight =
        static_cast<uint16_t>(transfer.OutputSize >> 16U);
    const uint32_t scaling = (transfer.Flags >> 24U) & 3U;
    view.OutputWidth = static_cast<uint16_t>(
        declaredOutputWidth >> (scaling != 0U ? 1U : 0U));
    view.OutputHeight = static_cast<uint16_t>(
        declaredOutputHeight >> (scaling == 2U ? 1U : 0U));
    view.Flags = transfer.Flags;
    view.Present = present;
    view.PresentationMode = presentationMode;
    return renderingApi.SubmitPicaDisplayTransfer(view, error);
}

bool ClearOot3dPicaVulkanRenderTarget(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    uint64_t renderTargetNamespace,
    uint32_t colorPhysicalAddress, std::string* error) {
    return renderingApi.ClearPicaRenderTarget(
        renderTargetNamespace, colorPhysicalAddress, error);
}

bool SubmitOot3dPicaVulkanMemoryFill(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaMemoryFillSubmission& submission,
    std::string* error, uint64_t renderTargetNamespace) {
    Oot3d::Renderer::PicaMemoryFillView view;
    view.RenderTargetNamespace = renderTargetNamespace;
    view.StartPhysicalAddress = submission.StartPhysicalAddress;
    view.EndPhysicalAddress = submission.EndPhysicalAddress;
    view.Value = submission.Value;
    view.Control = submission.Control;
    return renderingApi.SubmitPicaMemoryFill(view, error);
}

} // namespace Oot3dNativeGame
