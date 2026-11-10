#include "fast/extension/pica_nri_pipeline_state.h"
#include "fast/extension/pica_alpha_coverage_policy.h"
#include "fast/extension/pica_attachment_formats.h"
#include "fast/extension/outline_occlusion_pass.h"
#include <cstdlib>
#include <stdexcept>

namespace Fast::Extension {
using Renderer3ds::kPicaColorAttachmentCount;
VkFormat FindPicaDepthFormat(VkPhysicalDevice physicalDevice) {
    for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) == required) return format;
    }
    throw std::runtime_error("Vulkan device has no sampleable depth attachment format");
}
VkCompareOp ToNativeVkCompare(Renderer3ds::PicaCompareFunction compare) {
    switch (compare) {
        case Renderer3ds::PicaCompareFunction::Never:
            return VK_COMPARE_OP_NEVER;
        case Renderer3ds::PicaCompareFunction::Always:
            return VK_COMPARE_OP_ALWAYS;
        case Renderer3ds::PicaCompareFunction::Equal:
            return VK_COMPARE_OP_EQUAL;
        case Renderer3ds::PicaCompareFunction::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case Renderer3ds::PicaCompareFunction::Less:
            return VK_COMPARE_OP_LESS;
        case Renderer3ds::PicaCompareFunction::LessOrEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case Renderer3ds::PicaCompareFunction::Greater:
            return VK_COMPARE_OP_GREATER;
        case Renderer3ds::PicaCompareFunction::GreaterOrEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
    throw std::runtime_error("invalid native PICA compare function");
}

VkStencilOp ToNativeVkStencil(Renderer3ds::PicaStencilAction action) {
    switch (action) {
        case Renderer3ds::PicaStencilAction::Keep:
            return VK_STENCIL_OP_KEEP;
        case Renderer3ds::PicaStencilAction::Zero:
            return VK_STENCIL_OP_ZERO;
        case Renderer3ds::PicaStencilAction::Replace:
            return VK_STENCIL_OP_REPLACE;
        case Renderer3ds::PicaStencilAction::Increment:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case Renderer3ds::PicaStencilAction::Decrement:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case Renderer3ds::PicaStencilAction::Invert:
            return VK_STENCIL_OP_INVERT;
        case Renderer3ds::PicaStencilAction::IncrementWrap:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case Renderer3ds::PicaStencilAction::DecrementWrap:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    throw std::runtime_error("invalid native PICA stencil action");
}

VkLogicOp ToNativeVkLogic(Renderer3ds::PicaLogicOperation operation) {
    switch (operation) {
        case Renderer3ds::PicaLogicOperation::Clear:
            return VK_LOGIC_OP_CLEAR;
        case Renderer3ds::PicaLogicOperation::And:
            return VK_LOGIC_OP_AND;
        case Renderer3ds::PicaLogicOperation::AndReverse:
            return VK_LOGIC_OP_AND_REVERSE;
        case Renderer3ds::PicaLogicOperation::Copy:
            return VK_LOGIC_OP_COPY;
        case Renderer3ds::PicaLogicOperation::Set:
            return VK_LOGIC_OP_SET;
        case Renderer3ds::PicaLogicOperation::CopyInverted:
            return VK_LOGIC_OP_COPY_INVERTED;
        case Renderer3ds::PicaLogicOperation::NoOp:
            return VK_LOGIC_OP_NO_OP;
        case Renderer3ds::PicaLogicOperation::Invert:
            return VK_LOGIC_OP_INVERT;
        case Renderer3ds::PicaLogicOperation::Nand:
            return VK_LOGIC_OP_NAND;
        case Renderer3ds::PicaLogicOperation::Or:
            return VK_LOGIC_OP_OR;
        case Renderer3ds::PicaLogicOperation::Nor:
            return VK_LOGIC_OP_NOR;
        case Renderer3ds::PicaLogicOperation::Xor:
            return VK_LOGIC_OP_XOR;
        case Renderer3ds::PicaLogicOperation::Equivalent:
            return VK_LOGIC_OP_EQUIVALENT;
        case Renderer3ds::PicaLogicOperation::AndInverted:
            return VK_LOGIC_OP_AND_INVERTED;
        case Renderer3ds::PicaLogicOperation::OrReverse:
            return VK_LOGIC_OP_OR_REVERSE;
        case Renderer3ds::PicaLogicOperation::OrInverted:
            return VK_LOGIC_OP_OR_INVERTED;
    }
    throw std::runtime_error("invalid native PICA logic operation");
}

VkPrimitiveTopology ToNativeVkTopology(Renderer3ds::PicaTopology topology) {
    switch (topology) {
        case Renderer3ds::PicaTopology::TriangleList:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Renderer3ds::PicaTopology::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Renderer3ds::PicaTopology::TriangleFan:
            throw std::runtime_error(
                "native PICA triangle fan requires index expansion");
        case Renderer3ds::PicaTopology::GeometryShader:
            // PICA topology 3 selects the programmable primitive setup path,
            // whose assembled output is a triangle list. Geometry shader
            // enablement is tracked independently by pipeline.use_gs.
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    throw std::runtime_error("invalid native PICA topology");
}

bool UsesTranslucentBlend(const Renderer3ds::NativeBlendState& blend) {
    return !Renderer3ds::PicaBlendReplacesDestination(blend);
}

VkBlendOp ToNativeVkBlendOperation(Renderer3ds::NativeBlendEquation equation) {
    switch (equation) {
        case Renderer3ds::NativeBlendEquation::Add:
            return VK_BLEND_OP_ADD;
        case Renderer3ds::NativeBlendEquation::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case Renderer3ds::NativeBlendEquation::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case Renderer3ds::NativeBlendEquation::Min:
            return VK_BLEND_OP_MIN;
        case Renderer3ds::NativeBlendEquation::Max:
            return VK_BLEND_OP_MAX;
    }
    throw std::runtime_error("invalid native PICA blend equation");
}

VkBlendFactor ToNativeVkBlendFactor(Renderer3ds::NativeBlendFactor factor) {
    switch (factor) {
        case Renderer3ds::NativeBlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case Renderer3ds::NativeBlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case Renderer3ds::NativeBlendFactor::SourceColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case Renderer3ds::NativeBlendFactor::OneMinusSourceColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case Renderer3ds::NativeBlendFactor::DestColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case Renderer3ds::NativeBlendFactor::OneMinusDestColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case Renderer3ds::NativeBlendFactor::SourceAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case Renderer3ds::NativeBlendFactor::OneMinusSourceAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Renderer3ds::NativeBlendFactor::DestAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case Renderer3ds::NativeBlendFactor::OneMinusDestAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case Renderer3ds::NativeBlendFactor::ConstantColor:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case Renderer3ds::NativeBlendFactor::OneMinusConstantColor:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case Renderer3ds::NativeBlendFactor::ConstantAlpha:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case Renderer3ds::NativeBlendFactor::OneMinusConstantAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case Renderer3ds::NativeBlendFactor::SourceAlphaSaturate:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    throw std::runtime_error("invalid native PICA blend factor");
}


Renderer3ds::NriPicaGraphicsPipelineDesc BuildPicaNriPipelineState(
    const Renderer3ds::PicaDrawView& draw,
    const PicaFragmentInstrumentationOutputLayout& outputs,
    Renderer3ds::PicaAttachmentRequirements attachments,
    VkSampleCountFlagBits samples, VkFormat depthFormat,
    bool writesReactiveMask, bool outlineOcclusionOnly, bool geometryGuideAtReset) {
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = draw.DepthTestEnabled ||
                                   draw.DepthWriteEnabled;
    depthStencil.depthWriteEnable = draw.DepthWriteEnabled;
    depthStencil.depthCompareOp = draw.DepthTestEnabled
                                      ? ToNativeVkCompare(draw.DepthCompare)
                                      : VK_COMPARE_OP_ALWAYS;
    depthStencil.stencilTestEnable = draw.Stencil.Enabled;
    const VkStencilOpState stencil{
        ToNativeVkStencil(draw.Stencil.Fail),
        ToNativeVkStencil(draw.Stencil.Pass),
        ToNativeVkStencil(draw.Stencil.DepthFail),
        ToNativeVkCompare(draw.Stencil.Compare), draw.Stencil.CompareMask,
        draw.Stencil.WriteMask, draw.Stencil.Reference};
    depthStencil.front = stencil;
    depthStencil.back = stencil;

    VkPipelineColorBlendAttachmentState attachment{};
    const bool shadowProducer = draw.FragmentOperationMode == 3U;
    attachment.blendEnable = !shadowProducer && draw.Blend.Enabled;
    attachment.colorBlendOp =
        ToNativeVkBlendOperation(draw.Blend.EquationRgb);
    attachment.alphaBlendOp =
        ToNativeVkBlendOperation(draw.Blend.EquationAlpha);
    attachment.srcColorBlendFactor =
        ToNativeVkBlendFactor(draw.Blend.SourceRgb);
    attachment.dstColorBlendFactor =
        ToNativeVkBlendFactor(draw.Blend.DestRgb);
    attachment.srcAlphaBlendFactor =
        ToNativeVkBlendFactor(draw.Blend.SourceAlpha);
    attachment.dstAlphaBlendFactor =
        ToNativeVkBlendFactor(draw.Blend.DestAlpha);
    attachment.colorWriteMask = 0;
    if (!shadowProducer && (draw.ColorWriteMask & 1U) != 0U) {
        attachment.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    }
    if (!shadowProducer && (draw.ColorWriteMask & 2U) != 0U) {
        attachment.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    }
    if (!shadowProducer && (draw.ColorWriteMask & 4U) != 0U) {
        attachment.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    }
    if (!shadowProducer && (draw.ColorWriteMask & 8U) != 0U) {
        attachment.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendAttachmentState guideAttachment{};
    const bool writesNormalGuide = !shadowProducer && draw.DepthTestEnabled &&
                                   draw.DepthWriteEnabled &&
                                   (draw.ColorWriteMask & 7U) != 0U;
    const bool sceneDomainBlendedOverlay =
        outputs.SceneDomainBlendedOverlay;
    const bool sceneDomainNormalOverlay =
        outputs.SceneDomainNormalOverlay;
    const bool sceneDomainAmbientOverlay =
        outputs.SceneDomainAmbientOverlay;
    const bool sceneDomainTransparentDepthOverlay =
        outputs.SceneDomainTransparentDepthOverlay;
    const auto configureOverlayCoverage =
        [sceneDomainBlendedOverlay, &draw](
            VkPipelineColorBlendAttachmentState& state) {
            state.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
            state.blendEnable =
                sceneDomainBlendedOverlay ? VK_TRUE : VK_FALSE;
            if (sceneDomainBlendedOverlay) {
                state.alphaBlendOp = VK_BLEND_OP_ADD;
                state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                // Coverage follows native RGB destination transmission, not
                // output opacity: additive particles must retain the scene.
                state.dstAlphaBlendFactor =
                    ToNativeVkBlendFactor(draw.Blend.DestRgb);
            }
        };
    if (sceneDomainNormalOverlay) {
        configureOverlayCoverage(guideAttachment);
    } else {
        guideAttachment.colorWriteMask = writesNormalGuide
            ? VK_COLOR_COMPONENT_R_BIT |
                  VK_COLOR_COMPONENT_G_BIT |
                  VK_COLOR_COMPONENT_B_BIT |
                  VK_COLOR_COMPONENT_A_BIT
            : 0U;
    }
    VkPipelineColorBlendAttachmentState materialAttachment{};
    materialAttachment.colorWriteMask = writesNormalGuide
        ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        : 0U;
    if (writesReactiveMask) {
        materialAttachment.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
        materialAttachment.blendEnable = VK_TRUE;
        materialAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        materialAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        materialAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        materialAttachment.alphaBlendOp = VK_BLEND_OP_MAX;
        materialAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        materialAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    VkPipelineColorBlendAttachmentState rigidMotionAttachment{};
    if (sceneDomainTransparentDepthOverlay) {
        // Only the isolated coverage pass may publish native occlusion. The
        // primary color pass tests mixed depth, which can contain extension Grass.
        rigidMotionAttachment.colorWriteMask = 0;
        rigidMotionAttachment.blendEnable = VK_TRUE;
        rigidMotionAttachment.alphaBlendOp = VK_BLEND_OP_MAX;
        rigidMotionAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        rigidMotionAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else {
        rigidMotionAttachment.colorWriteMask = writesNormalGuide
            ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            : 0U;
    }
    VkPipelineColorBlendAttachmentState ambientAttachment{};
    if (sceneDomainAmbientOverlay) {
        configureOverlayCoverage(ambientAttachment);
    } else if (outputs.WritesAmbientGuide) {
        ambientAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendAttachmentState outlineGeometryAttachment{};
    // Integer channels, never blended: R/A native normal and depth, G/B the
    // visible surface's face normal and kind.
    outlineGeometryAttachment.colorWriteMask =
        (outputs.WritesOutlineGeometryGuide ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT : 0U) |
        (outputs.WritesFaceNormalGuide ? VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0U);
    // A depth-initializing atmosphere draw (the title's sky: depth compare
    // Always with depth writes, the instrumentation's raster initializer) is no
    // geometric surface: its specialized program stores the cleared geometry
    // guide (normal 0, kind 0, depth 1.0) in the channels it writes (R|A for
    // outlines, G|B for the face normal). When the guide already holds those
    // values everywhere (geometryGuideAtReset: a full colour fill reset it and
    // nothing wrote it since - MM3D fills the scene colour right before its
    // sky), the write is a partial read-modify-write of 16 B per pixel that
    // changes nothing, and is masked. (The always-available program would
    // also store a face normal and kind 1; masked, it keeps the cleared values
    // its specialized program writes.) A sky that initializes the guide itself
    // (no fill before it, as in OoT3D) keeps the write.
    // TRIAEVUM_ATMOSPHERE_GEOMETRY_WRITE=1 keeps it always (developer A/B).
    static const bool atmosphereGeometryWrite = std::getenv("TRIAEVUM_ATMOSPHERE_GEOMETRY_WRITE") != nullptr;
    if (geometryGuideAtReset && !atmosphereGeometryWrite &&
        draw.Composition.Layer == Renderer3ds::PicaCompositionLayer::Atmosphere && draw.DepthWriteEnabled &&
        draw.DepthCompare == Renderer3ds::PicaCompareFunction::Always &&
        (outputs.WritesOutlineGeometryGuide || outputs.WritesFaceNormalGuide))
        outlineGeometryAttachment.colorWriteMask = 0U;
    VkPipelineColorBlendAttachmentState fogAttachment{};
    fogAttachment.colorWriteMask = outputs.WritesFogGuide ? 0xfU : 0U;
    VkPipelineColorBlendAttachmentState albedoAttachment{};
    if (outputs.SceneDomainAlbedoOverlay)
        configureOverlayCoverage(albedoAttachment);
    else
        albedoAttachment.colorWriteMask = outputs.WritesDiffuseAlbedoGuide ? 0xfU : 0U;
    std::array<VkPipelineColorBlendAttachmentState, kPicaColorAttachmentCount> colorAttachments{
        attachment,        guideAttachment, materialAttachment,       rigidMotionAttachment,
        ambientAttachment, fogAttachment,   outlineGeometryAttachment, albedoAttachment
    };
    if (outlineOcclusionOnly) ConfigureOutlineOcclusionPass(depthStencil, colorAttachments);

    Renderer3ds::NriPicaGraphicsPipelineDesc result;
    result.Topology = ToNativeVkTopology(draw.Topology);
    result.CullMode = draw.CullMode == Renderer3ds::NativeCullMode::KeepAll
        ? VK_CULL_MODE_NONE
        : (draw.FramebufferFlipped ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
    result.FrontFace = draw.CullMode == Renderer3ds::NativeCullMode::KeepCounterClockwise
        ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    result.Samples = samples;
    result.AlphaToCoverage = ResolvePicaAlphaCoveragePolicy(
        samples, draw.AlphaTestEnabled, draw.DepthTestEnabled, draw.DepthWriteEnabled,
        UsesTranslucentBlend(draw.Blend), draw.ColorWriteMask).Enable;
    result.DepthTest = depthStencil.depthTestEnable != VK_FALSE;
    result.DepthWrite = depthStencil.depthWriteEnable != VK_FALSE;
    result.DepthCompare = depthStencil.depthCompareOp;
    result.StencilTest = depthStencil.stencilTestEnable != VK_FALSE;
    result.FrontStencil = depthStencil.front;
    result.BackStencil = depthStencil.back;
    // An unbound guide declares no format and no writes.
    result.ColorFormats = BoundPicaColorAttachmentFormats(attachments);
    for (size_t index = 0; index < colorAttachments.size(); ++index)
        if (result.ColorFormats[index] == VK_FORMAT_UNDEFINED) colorAttachments[index] = {};
    result.Colors = colorAttachments;
    result.LogicOpEnabled = !shadowProducer && !draw.Blend.Enabled;
    result.LogicOp = ToNativeVkLogic(draw.LogicOperation);
    result.ColorAttachmentCount = attachments.ColorAttachmentCount();
    result.DepthStencilFormat = depthFormat;
    return result;
}
} // namespace Fast::Extension
