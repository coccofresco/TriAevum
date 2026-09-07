#include "fast/oot3d/pica_grass_texture_coordinates.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::vector<uint8_t> BuildTextureCoordinateUniforms() {
    constexpr size_t kFloatOffset =
        16U + 4U * 4U * sizeof(uint32_t);
    std::vector<uint8_t> bytes(
        kFloatOffset + 96U * 4U * sizeof(float));
    const uint32_t booleanMask = 2U | 64U;
    std::memcpy(bytes.data(), &booleanMask, sizeof(booleanMask));
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[10] = {1.0F, 0.0F, 0.0F, 0.25F};
    uniforms[11] = {0.0F, 1.0F, 0.0F, 0.125F};
    uniforms[89][0] = 0.0F;
    uniforms[91][0] = 1.0F / 32768.0F;
    uniforms[92][0] = 1.0F;
    uniforms[93][1] = 1.0F;
    uniforms[95][0] = 3.0F;
    uniforms[95][1] = 4.0F;
    std::memcpy(bytes.data() + kFloatOffset,
                uniforms.data(), sizeof(uniforms));
    return bytes;
}

Oot3d::Renderer::PicaShaderHookLayout BuildFragmentHooks(
    std::string_view source,
    Oot3d::Renderer::PicaTextureCoordinateOperation operation) {
    Oot3d::Renderer::PicaShaderHookLayout hooks;
    hooks.SchemaVersion =
        Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    hooks.SourceSize = source.size();
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::GlobalDeclarations)] = 0U;
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::MainEpilogue)] =
        source.find_last_of('}');
    hooks.Outputs = {
        Oot3d::Renderer::kPicaFragmentOutputContractSchemaVersion,
        1U,
        0U,
        Oot3d::Renderer::PicaFragmentDepthOutput::FixedFunction,
        false,
        false,
    };
    hooks.SampledTextureMask = 1U;
    hooks.TextureSamples[0] = {0U, operation};
    return hooks;
}

Oot3d::Renderer::PicaVertexShaderHookLayout BuildVertexHooks(
    std::string_view source) {
    using namespace Oot3d::Renderer;
    PicaVertexShaderHookLayout hooks;
    hooks.SchemaVersion = kPicaShaderHookSchemaVersion;
    hooks.SourceSize = source.size();
    hooks.Offsets.fill(0U);
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::MainBodyEnd)] = source.size();
    hooks.Semantics =
        PicaVertexShaderSemantic::TextureCoordinateProgram;
    auto& layout = hooks.TextureCoordinates[0U];
    layout.Operation =
        PicaVertexTextureCoordinateOperation::CmbAffine;
    layout.EnableBooleanUniform = 1U;
    layout.SourceSelectorUniform = 89U;
    layout.SourceSelectorComponent = 0U;
    layout.SourceSelectorConstantUniform = 93U;
    layout.SourceSelectorConstantComponents = {0U, 1U};
    layout.CoordinateModeUniform = 92U;
    layout.CoordinateModeComponent = 0U;
    layout.CoordinateModeConstantUniform = 95U;
    layout.CoordinateModeConstantComponents = {0U, 1U};
    layout.MatrixRowUUniform = 10U;
    layout.MatrixRowVUniform = 11U;
    layout.HomogeneousUniform = 93U;
    layout.HomogeneousComponent = 1U;
    layout.Sources[0U] = {3U, 91U, 0U, 6U};
    layout.Sources[1U] = {4U, 91U, 1U, 7U};
    layout.Sources[2U] = {5U, 91U, 2U, 8U};
    layout.SourceCount = 3U;
    return hooks;
}

} // namespace

TEST(Oot3dPicaGrassTextureCoordinates,
     UsesTypedNativeVFlipContractForCanonicalSamplerHelpers) {
    using namespace Fast::Oot3d;
    static constexpr std::string_view kVertex =
        "canonical vertex program; no GLSL contract parsing required";
    static constexpr std::string_view kFragment = R"glsl(
vec4 pica_sample_texture0(vec2 uv) { return textureLod(pica_texture0, uv, 0.0); }
void main() {
    pica_color = pica_sample_texture0(vec2(pica_texcoord0.x, 1.0 - pica_texcoord0.y));
}
)glsl";
    const auto uniforms = BuildTextureCoordinateUniforms();
    const auto vertexHooks = BuildVertexHooks(kVertex);
    const auto fragmentHooks = BuildFragmentHooks(
        kFragment,
        Oot3d::Renderer::PicaTextureCoordinateOperation::NativeVFlip);
    const auto transform = DecodePicaGrassTextureCoordinateTransform(
        kVertex, kFragment, uniforms, 3U,
        &vertexHooks, &fragmentHooks);
    ASSERT_TRUE(transform.Applied());
    const auto uv = ApplyPicaGrassTextureCoordinateTransform(
        transform, {8192.0F, 16384.0F});
    EXPECT_FLOAT_EQ(uv[0], 0.5F);
    EXPECT_FLOAT_EQ(uv[1], 0.375F);
}

TEST(Oot3dPicaGrassTextureCoordinates,
     RejectsTypedProjectiveSamplingWithoutGuessing) {
    using namespace Fast::Oot3d;
    static constexpr std::string_view kVertex =
        "canonical vertex program; no GLSL contract parsing required";
    static constexpr std::string_view kFragment =
        "void main() { pica_color = textureProj(pica_texture0, "
        "vec3(pica_texcoord0, pica_texcoord0_w)); }";
    const auto uniforms = BuildTextureCoordinateUniforms();
    const auto vertexHooks = BuildVertexHooks(kVertex);
    const auto fragmentHooks = BuildFragmentHooks(
        kFragment,
        Oot3d::Renderer::
            PicaTextureCoordinateOperation::ProjectedNativeVFlip);
    EXPECT_EQ(
        DecodePicaGrassTextureCoordinateTransform(
            kVertex, kFragment, uniforms, 3U,
            &vertexHooks, &fragmentHooks).Eligibility,
        PicaGrassTextureCoordinateEligibility::
            UnsupportedFragmentSampling);
}
