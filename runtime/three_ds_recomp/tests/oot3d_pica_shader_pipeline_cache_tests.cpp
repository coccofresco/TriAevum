#include "fast/renderer3ds/pica_fragment_balance.h"
#include "fast/extension/pica_attachment_contract.h"
#include "fast/extension/pica_directional_shadow_caster.h"
#include "fast/extension/pica_directional_shadow_lighting.h"
#include "fast/extension/pica_reactive_mask.h"
#include "fast/extension/pica_rigid_motion.h"
#include "fast/extension/pica_shader_pipeline_cache.h"
#include "fast/extension/pica_rim_light.h"
#include "fast/extension/pica_uber_effect_program.h"
#include "fast/renderer3ds/pica_surface_lighting.h"

#include <gtest/gtest.h>
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

#include <stdexcept>
#include <cmath>
#include <limits>

namespace {

TEST(Oot3dRimLight, ScopeUsesNativeActorOwnershipAndRejectsNonOpaquePasses) {
    using namespace Fast::Renderer3ds;
    PicaEnvironmentLighting environment;
    environment.Enabled = environment.Rim.Enabled = true;
    PicaCompositionAttribution composition;
    composition.Layer = PicaCompositionLayer::OpaqueWorld;
    NativeBlendState blend;
    auto domain = PicaCompositionDomain::Scene;
    auto appearance = PicaSurfaceAppearance::Relightable;
    bool depthTest = true, depthWrite = true;
    uint8_t operation = 0;
    const auto eligible = [&] { return Fast::Extension::PicaDrawReceivesRimLight(
        environment, domain, composition, blend, appearance, depthTest, depthWrite, operation); };
    EXPECT_FALSE(eligible()); // A room material is not an actor, even with the same shader.
    composition.NativeActorGeometry = true;
    EXPECT_TRUE(eligible()); // Rigid props and skinned characters use the same ownership.
    composition.NativeActorGeometry = false;
    environment.Rim.Scope = PicaRimLightScope::ActorsAndWorld;
    EXPECT_TRUE(eligible());
    for (auto layer : {PicaCompositionLayer::Unknown, PicaCompositionLayer::TransparentWorld,
                       PicaCompositionLayer::Atmosphere, PicaCompositionLayer::Ui}) {
        composition.Layer = layer;
        EXPECT_FALSE(eligible());
    }
    composition.Layer = PicaCompositionLayer::OpaqueWorld;
    for (auto excluded : {PicaCompositionDomain::Unknown, PicaCompositionDomain::Ui, PicaCompositionDomain::Screen}) {
        domain = excluded;
        EXPECT_FALSE(eligible());
    }
    domain = PicaCompositionDomain::Scene;
    blend.Enabled = true;
    EXPECT_TRUE(eligible()); // Native ONE/ZERO replacement blending.
    blend.SourceRgb = NativeBlendFactor::SourceAlpha;
    blend.DestRgb = NativeBlendFactor::OneMinusSourceAlpha;
    EXPECT_FALSE(eligible()); // Fades are excluded even if they write depth.
    blend.DestRgb = NativeBlendFactor::One;
    EXPECT_FALSE(eligible());
    blend = {};
    composition.IsolatedLighting = true; EXPECT_FALSE(eligible()); composition.IsolatedLighting = false;
    composition.NativeProjectedShadow = true; EXPECT_FALSE(eligible()); composition.NativeProjectedShadow = false;
    appearance = PicaSurfaceAppearance::Unlit; EXPECT_FALSE(eligible());
    appearance = PicaSurfaceAppearance::AdditiveComposition; EXPECT_FALSE(eligible());
    appearance = PicaSurfaceAppearance::Relightable;
    depthTest = false; EXPECT_FALSE(eligible()); depthTest = true;
    depthWrite = false; EXPECT_FALSE(eligible()); depthWrite = true;
    operation = 3; EXPECT_FALSE(eligible()); operation = 0;
    environment.Enabled = false; EXPECT_FALSE(eligible()); environment.Enabled = true;
    environment.Rim.Enabled = false; EXPECT_FALSE(eligible());
}

TEST(Oot3dRimLight, DisabledAndInvalidSettingsPublishCanonicalZeroUniforms) {
    using namespace Fast::Renderer3ds;
    PicaSurfaceLightingUniforms uniforms;
    PicaRimLightSettings rim;
    AssignPicaSurfaceRim(uniforms, rim);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
    rim.Enabled = true;
    AssignPicaSurfaceRim(uniforms, rim);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{1.0F, 4.0F, 1.0F, 0.25F}));
    EXPECT_EQ(uniforms.RimColor, (std::array<float, 4>{1.0F, 0.96F, 0.9F, 0.0F}));
    EXPECT_EQ(uniforms.RimDirection, (std::array<float, 4>{0, 1, 0, 0}));
    rim.Intensity = kMaximumPicaRimIntensity; rim.Azimuth = 90;
    AssignPicaSurfaceRim(uniforms, rim);
    EXPECT_FLOAT_EQ(uniforms.Rim[0], 8.0F);
    EXPECT_NEAR(uniforms.RimDirection[0], 1.0F, 1.e-6F);
    EXPECT_NEAR(uniforms.RimDirection[1], 0.0F, 1.e-6F);
    rim.Source = PicaRimLightSource::SceneLights;
    AssignPicaSurfaceRim(uniforms, rim);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{8, 4, 2, 0}));
    EXPECT_EQ(uniforms.RimColor, (std::array<float, 4>{}));
    EXPECT_EQ(uniforms.RimDirection, (std::array<float, 4>{}));
    rim.Enabled = false; rim.Intensity = 1; rim.Sharpness = 16; rim.Coverage = 0.7F;
    AssignPicaSurfaceRim(uniforms, rim);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
    EXPECT_EQ(uniforms.RimColor, (std::array<float, 4>{}));
    EXPECT_EQ(uniforms.RimDirection, (std::array<float, 4>{}));
    rim.Enabled = true;
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0F, 17.0F}) {
        auto invalid = rim; invalid.Sharpness = bad;
        AssignPicaSurfaceRim(uniforms, invalid);
        EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
    }
    for (auto member : {&PicaRimLightSettings::Intensity, &PicaRimLightSettings::Azimuth,
                        &PicaRimLightSettings::Coverage}) {
        auto invalid = rim; invalid.*member = std::numeric_limits<float>::quiet_NaN();
        AssignPicaSurfaceRim(uniforms, invalid);
        EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
    }
    auto invalid = rim; invalid.Color[1] = 1.1F;
    AssignPicaSurfaceRim(uniforms, invalid);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
    invalid = rim; invalid.Source = static_cast<PicaRimLightSource>(2);
    AssignPicaSurfaceRim(uniforms, invalid);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
    rim.Intensity = 0;
    AssignPicaSurfaceRim(uniforms, rim);
    EXPECT_EQ(uniforms.Rim, (std::array<float, 4>{}));
}

Oot3d::Renderer::PicaTemporalVertexProgramView
BaseTemporalVertexProgram();

constexpr std::string_view kShadowCasterVertexShader =
    "#version 450\n"
    "layout(location=0) in vec4 pica_input0;\n"
    "layout(location=6) out vec3 pica_view;\n"
    "layout(set=0,binding=0,std140) uniform PicaVertexUniforms {\n"
    "    vec4 f[96];\n"
    "} uniforms;\n"
    "vec4 pica_output0 = vec4(0.0);\n"
    "void exec_shader() { pica_output0 = pica_input0; }\n"
    "// void main in a comment must not be a structural anchor.\n"
    "void main() {\n"
    "    exec_shader();\n"
    "    if (pica_output0.w != 0.0) { pica_output0.x += 0.0; }\n"
    "    gl_Position = pica_output0;\n"
    "    pica_view = pica_output0.xyz;\n"
    "}\n"
    "void helper_after_main() {}\n";

Oot3d::Renderer::PicaVertexShaderHookLayout ShadowCasterHooks() {
    using Oot3d::Renderer::PicaVertexShaderHook;
    using Oot3d::Renderer::PicaVertexShaderHookLayout;
    using Oot3d::Renderer::PicaVertexShaderSemantic;
    PicaVertexShaderHookLayout hooks;
    hooks.SchemaVersion =
        Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    hooks.SourceSize = kShadowCasterVertexShader.size();
    const size_t state =
        kShadowCasterVertexShader.find("vec4 pica_output0");
    const size_t main = kShadowCasterVertexShader.find("void main() {");
    const size_t mainBody =
        kShadowCasterVertexShader.find('{', main) + 1U;
    const size_t mainEnd =
        kShadowCasterVertexShader.find("}\nvoid helper_after_main");
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::GlobalDeclarations)] = state;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::RegisterStateBegin)] = state;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::RegisterStateEnd)] = main;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::MainBodyBegin)] = mainBody;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::MainBodyEnd)] = mainEnd;
    hooks.Semantics =
        PicaVertexShaderSemantic::PicaRegisterState |
        PicaVertexShaderSemantic::VertexUniformState |
        PicaVertexShaderSemantic::ClipPositionOutput |
        PicaVertexShaderSemantic::ViewPositionOutput;
    return hooks;
}

Oot3d::Renderer::NativeBlendState SourceAlphaBlend() {
    using Factor = Oot3d::Renderer::NativeBlendFactor;
    Oot3d::Renderer::NativeBlendState blend;
    blend.Enabled = true;
    blend.SourceRgb = Factor::SourceAlpha;
    blend.DestRgb = Factor::OneMinusSourceAlpha;
    blend.SourceAlpha = Factor::SourceAlpha;
    blend.DestAlpha = Factor::OneMinusSourceAlpha;
    return blend;
}

Fast::Extension::PicaShaderPipelineRequest BaseRequest() {
    static constexpr std::string_view kVertexShader =
        "#version 450\n"
        "vec4 pica_output0 = vec4(0.0);\n"
        "void main() { gl_Position = pica_output0; }\n";
    static constexpr std::string_view kFragmentShader =
        "#version 450\n"
        "layout(location=0) out vec4 pica_color;\n"
        "void main() {\n"
        "    pica_color = vec4(1.0);\n"
        "}\n";
    Fast::Extension::PicaShaderPipelineRequest request;
    request.VertexShaderSource = kVertexShader;
    request.VertexShaderKey = 11U;
    request.FragmentShaderSource = kFragmentShader;
    request.FragmentShaderKey = 17U;
    request.Draw.FramebufferWidth = 400U;
    request.Draw.FramebufferHeight = 240U;
    request.Draw.DepthTestEnabled = true;
    request.Draw.DepthWriteEnabled = true;
    request.Draw.ColorWriteMask = 0xFU;
    request.Draw.PerspectiveProjection = true;
    request.Draw.CompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Scene;
    request.Draw.CompositionLayer = Fast::Renderer3ds::PicaCompositionLayer::OpaqueWorld;
    request.TemporalVertexProgram = BaseTemporalVertexProgram();
    request.FragmentShaderHooks =
        Fast::Extension::AnalyzePicaFragmentShaderHooks(kFragmentShader);
    return request;
}

Oot3d::Renderer::PicaTemporalVertexProgramView
BaseTemporalVertexProgram() {
    using Oot3d::Renderer::PicaVertexShaderHook;
    using Oot3d::Renderer::PicaVertexShaderSemantic;
    static constexpr std::string_view kSource =
        "#version 450\n"
        "vec4 pica_output0 = vec4(0.0);\n"
        "void main() { gl_Position = pica_output0; }\n";
    static constexpr std::string_view kPreviousState =
        "vec4 pica_output0_previous = vec4(0.0);\n";
    static constexpr std::string_view kPreviousMain =
        " gl_Position = pica_output0_previous; ";
    const size_t state = kSource.find("vec4 pica_output0 =");
    const size_t main = kSource.find("void main()");
    const size_t mainBody = kSource.find('{', main) + 1U;
    const size_t end = kSource.find_last_of('}');
    Oot3d::Renderer::PicaVertexShaderHookLayout hooks;
    hooks.SchemaVersion =
        Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    hooks.SourceSize = kSource.size();
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::GlobalDeclarations)] = state;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::RegisterStateBegin)] = state;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::RegisterStateEnd)] = main;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::MainBodyBegin)] = mainBody;
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::MainBodyEnd)] = end;
    hooks.Semantics =
        PicaVertexShaderSemantic::PicaRegisterState |
        PicaVertexShaderSemantic::VertexUniformState |
        PicaVertexShaderSemantic::ClipPositionOutput;
    return {hooks, kPreviousState, kPreviousMain};
}

TEST(Oot3dPicaDirectionalShadowCaster,
     UsesTypedVertexHooksInsteadOfTextualMainDiscovery) {
    using namespace Fast::Extension;
    const auto result = BuildPicaDirectionalShadowCasterShader(
        kShadowCasterVertexShader, 0x1234U, ShadowCasterHooks());
    ASSERT_TRUE(result.Applied());
    EXPECT_EQ(result.Status,
              PicaDirectionalShadowCasterShaderStatus::Applied);
    EXPECT_NE(result.Key, 0x1234U);
    const size_t overridePosition = result.Source.find(
        "gl_Position = oot3d_shadow.world_to_light_clip");
    const size_t helperPosition =
        result.Source.find("void helper_after_main");
    ASSERT_NE(overridePosition, std::string::npos);
    ASSERT_NE(helperPosition, std::string::npos);
    EXPECT_LT(overridePosition, helperPosition);
    EXPECT_EQ(result.Source.find(
                  "gl_Position = oot3d_shadow.world_to_light_clip",
                  overridePosition + 1U),
              std::string::npos);
    EXPECT_TRUE(result.Source.ends_with(
        "void helper_after_main() {}\n"));
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        result.Source, shaderc_vertex_shader,
        "typed_directional_shadow_caster.vert");
    EXPECT_EQ(compiled.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
#endif
}

TEST(Oot3dPicaDirectionalShadowCaster,
     RejectsMissingOrStaleFrontendSemantics) {
    using namespace Fast::Extension;
    auto hooks = ShadowCasterHooks();
    hooks.Semantics =
        static_cast<Oot3d::Renderer::PicaVertexShaderSemantic>(
            static_cast<uint32_t>(hooks.Semantics) &
            ~static_cast<uint32_t>(
                Oot3d::Renderer::PicaVertexShaderSemantic::
                    ViewPositionOutput));
    const auto missing = BuildPicaDirectionalShadowCasterShader(
        kShadowCasterVertexShader, 1U, hooks);
    EXPECT_FALSE(missing.Applied());
    EXPECT_EQ(missing.Status,
              PicaDirectionalShadowCasterShaderStatus::
                  MissingViewPosition);

    hooks = ShadowCasterHooks();
    ++hooks.SourceSize;
    const auto stale = BuildPicaDirectionalShadowCasterShader(
        kShadowCasterVertexShader, 1U, hooks);
    EXPECT_FALSE(stale.Applied());
    EXPECT_EQ(stale.Status,
              PicaDirectionalShadowCasterShaderStatus::
                  InvalidHookContract);
}

// Canonical-shaped fragment program: native samplers and fragment uniforms,
// TEV, the native alpha test, then depth and colour after BeforeDepth.
constexpr std::string_view kAlphaCasterFragmentShader =
    "#version 450\n"
    "layout(location=1) in vec2 pica_texcoord0;\n"
    "layout(set=0,binding=1) uniform sampler2D pica_texture0;\n"
    "layout(set=0,binding=2) uniform sampler2D pica_texture1;\n"
    "layout(set=0,binding=3) uniform sampler2D pica_texture2;\n"
    "layout(set=0,binding=4,std140) uniform PicaFragmentUniforms {\n"
    "    vec4 tev_constants[6];\n"
    "    int alpha_reference;\n"
    "    float depth_scale;\n"
    "} fragment_uniforms;\n"
    "layout(location=0) out vec4 pica_color;\n"
    "void main() {\n"
    "    vec4 combiner_output = texture(pica_texture0, pica_texcoord0);\n"
    "    // AFTER_TEV\n"
    "    int alpha_byte = int(combiner_output.a * 255.0);\n"
    "    if (alpha_byte < fragment_uniforms.alpha_reference) discard;\n"
    "    // BEFORE_DEPTH\n"
    "    gl_FragDepth = gl_FragCoord.z * fragment_uniforms.depth_scale;\n"
    "    // BEFORE_COLOR\n"
    "    pica_color = combiner_output + texture(pica_texture1, pica_texcoord0) +\n"
    "        texture(pica_texture2, pica_texcoord0);\n"
    "    // EPILOGUE\n"
    "}\n";

Oot3d::Renderer::PicaShaderHookLayout AlphaCasterFragmentHooks(std::string_view source) {
    using Oot3d::Renderer::PicaShaderHook;
    Oot3d::Renderer::PicaShaderHookLayout hooks;
    hooks.SchemaVersion = Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    hooks.SourceSize = source.size();
    hooks.Outputs = {Oot3d::Renderer::kPicaFragmentOutputContractSchemaVersion, 1U, 0U,
                     Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative, false, false};
    const auto at = [&](PicaShaderHook hook, std::string_view marker) {
        hooks.Offsets[static_cast<size_t>(hook)] = source.find(marker);
    };
    at(PicaShaderHook::GlobalDeclarations, "void main()");
    at(PicaShaderHook::MainPrologue, "    vec4 combiner_output");
    at(PicaShaderHook::AfterNativeTev, "    // AFTER_TEV");
    at(PicaShaderHook::BeforeDepth, "    // BEFORE_DEPTH");
    at(PicaShaderHook::BeforeNativeColor, "    // BEFORE_COLOR");
    at(PicaShaderHook::MainEpilogue, "    // EPILOGUE");
    return hooks;
}

TEST(Oot3dPicaDirectionalShadowCaster, CasterTransformLeavesTheNativeBindingsFree) {
    using namespace Fast::Extension;
    const auto result = BuildPicaDirectionalShadowCasterShader(
        kShadowCasterVertexShader, 0x1234U, ShadowCasterHooks());
    ASSERT_TRUE(result.Applied());
    EXPECT_EQ(kPicaShadowCasterTransformBinding, 30U);
    EXPECT_NE(result.Source.find("layout(set=0,binding=30,std140) uniform Oot3dDirectionalShadowTransform"),
              std::string::npos);
    EXPECT_EQ(result.Source.find("binding=1,std140"), std::string::npos);
}

TEST(Oot3dPicaDirectionalShadowCaster, AlphaCasterRunsTheNativeProgramUpToItsAlphaTest) {
    using namespace Fast::Extension;
    const auto hooks = AlphaCasterFragmentHooks(kAlphaCasterFragmentShader);
    ASSERT_TRUE(hooks.ValidFor(kAlphaCasterFragmentShader));
    const auto result = BuildPicaShadowCasterAlphaFragmentShader(kAlphaCasterFragmentShader, 0x77U, hooks);
    ASSERT_TRUE(result.Applied());
    EXPECT_NE(result.Key, 0x77U);
    // TEV and alpha test kept; nothing from the depth hook on.
    EXPECT_NE(result.Source.find("if (alpha_byte < fragment_uniforms.alpha_reference) discard;"),
              std::string::npos);
    EXPECT_EQ(result.Source.find("gl_FragDepth"), std::string::npos);
    EXPECT_EQ(result.Source.find("pica_color ="), std::string::npos);
    EXPECT_EQ(result.Source.find("BEFORE_DEPTH"), std::string::npos);
    EXPECT_TRUE(result.Source.ends_with("}\n"));
    // The map has no colour attachment: the native output is not exported.
    EXPECT_EQ(result.Source.find("out vec4 pica_color"), std::string::npos);
    EXPECT_NE(result.Source.find("\nvec4 pica_color;"), std::string::npos);
    // Native samplers follow the NRI split: textures 1-3, samplers 7-9.
    EXPECT_EQ(result.Source.find("uniform sampler2D pica_texture0;"), std::string::npos);
    EXPECT_NE(result.Source.find("binding=7"), std::string::npos);
    EXPECT_NE(result.Source.find("layout(set=0,binding=4,std140) uniform PicaFragmentUniforms"),
              std::string::npos);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        result.Source, shaderc_fragment_shader, "alpha_shadow_caster.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
#endif
}

TEST(Oot3dPicaDirectionalShadowCaster, AlphaCasterRequiresPublishedHooks) {
    using namespace Fast::Extension;
    using Oot3d::Renderer::PicaShaderHook;
    const std::string_view source = kAlphaCasterFragmentShader;
    auto hooks = AlphaCasterFragmentHooks(source);
    hooks.Offsets[static_cast<size_t>(PicaShaderHook::BeforeDepth)] =
        Oot3d::Renderer::PicaShaderHookLayout::Unavailable;
    EXPECT_EQ(BuildPicaShadowCasterAlphaFragmentShader(source, 1U, hooks).Status,
              PicaDirectionalShadowCasterShaderStatus::MissingAlphaTest);

    hooks = AlphaCasterFragmentHooks(source);
    ++hooks.SourceSize;
    EXPECT_EQ(BuildPicaShadowCasterAlphaFragmentShader(source, 1U, hooks).Status,
              PicaDirectionalShadowCasterShaderStatus::InvalidHookContract);

    hooks = AlphaCasterFragmentHooks(source);
    std::swap(hooks.Offsets[static_cast<size_t>(PicaShaderHook::AfterNativeTev)],
              hooks.Offsets[static_cast<size_t>(PicaShaderHook::BeforeDepth)]);
    EXPECT_EQ(BuildPicaShadowCasterAlphaFragmentShader(source, 1U, hooks).Status,
              PicaDirectionalShadowCasterShaderStatus::InvalidHookContract);

    // Only the canonical native colour output can stay unexported.
    hooks = AlphaCasterFragmentHooks(source);
    hooks.Outputs.ColorLocationMask = 3U;
    EXPECT_EQ(BuildPicaShadowCasterAlphaFragmentShader(source, 1U, hooks).Status,
              PicaDirectionalShadowCasterShaderStatus::OutputContract);
    constexpr std::string_view kOutput = "layout(location=0) out vec4 pica_color;\n";
    std::string undeclared(source);
    undeclared.erase(undeclared.find(kOutput), kOutput.size());
    EXPECT_EQ(BuildPicaShadowCasterAlphaFragmentShader(undeclared, 1U, AlphaCasterFragmentHooks(undeclared)).Status,
              PicaDirectionalShadowCasterShaderStatus::OutputContract);

    // Without a native sampler the NRI binding contract cannot be met.
    constexpr std::string_view kTexture2 = "layout(set=0,binding=3) uniform sampler2D pica_texture2;\n";
    std::string unsampled(source);
    unsampled.erase(unsampled.find(kTexture2), kTexture2.size());
    EXPECT_EQ(BuildPicaShadowCasterAlphaFragmentShader(unsampled, 1U, AlphaCasterFragmentHooks(unsampled)).Status,
              PicaDirectionalShadowCasterShaderStatus::SamplerContract);
}

TEST(Oot3dPicaAttachmentContract, CanonicalRenderingDeclaresOnlyNativeColor) {
    const Fast::Extension::PicaAttachmentRequirements requirements;
    EXPECT_TRUE(requirements.NativeColorOnly());
    EXPECT_EQ(requirements.ColorAttachmentCount(), 1U);
    EXPECT_EQ(requirements.Key(), 0U);
}

TEST(Oot3dPicaAttachmentContract,
     InstrumentedRenderingPreservesStableMrtAbi) {
    using Fast::Extension::PicaAuxiliaryOutput;
    const Fast::Extension::PicaAttachmentRequirements requirements{
        PicaAuxiliaryOutput::NormalGuide};
    EXPECT_FALSE(requirements.NativeColorOnly());
    EXPECT_TRUE(requirements.Requires(PicaAuxiliaryOutput::NormalGuide));
    EXPECT_FALSE(requirements.Requires(PicaAuxiliaryOutput::MaterialGuide));
    EXPECT_FALSE(requirements.Requires(
        PicaAuxiliaryOutput::RigidMotionGuide));
    EXPECT_FALSE(requirements.Requires(PicaAuxiliaryOutput::AmbientGuide));
    EXPECT_EQ(requirements.ColorAttachmentCount(),
              Fast::Extension::kPicaBaseInstrumentedColorAttachmentCount);
    // The GI albedo guide extends the layout only while requested.
    const Fast::Extension::PicaAttachmentRequirements withAlbedo{
        PicaAuxiliaryOutput::NormalGuide | PicaAuxiliaryOutput::DiffuseAlbedoGuide};
    EXPECT_EQ(withAlbedo.ColorAttachmentCount(), Fast::Extension::kPicaColorAttachmentCount);
    EXPECT_EQ(withAlbedo.BaseGuides(), requirements);
}

TEST(Oot3dPicaAttachmentContract,
     ReportsTheExactDeclaredAuxiliaryMask) {
    using Fast::Extension::PicaAuxiliaryOutput;
    const Fast::Extension::PicaAttachmentRequirements requirements{
        PicaAuxiliaryOutput::NormalGuide |
        PicaAuxiliaryOutput::MaterialGuide |
        PicaAuxiliaryOutput::AmbientGuide};
    EXPECT_TRUE(requirements.Requires(PicaAuxiliaryOutput::NormalGuide));
    EXPECT_TRUE(requirements.Requires(PicaAuxiliaryOutput::MaterialGuide));
    EXPECT_FALSE(requirements.Requires(
        PicaAuxiliaryOutput::RigidMotionGuide));
    EXPECT_TRUE(requirements.Requires(PicaAuxiliaryOutput::AmbientGuide));
}

TEST(Oot3dPicaShaderPipelineCache,
     CanonicalPathPreservesSourceAndSurvivesSettingsRevision) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    const auto request = BaseRequest();
    cache.BeginFrame(4U);

    bool firstHit = true;
    const auto& first = cache.Resolve(request, effects, &firstHit);
    bool secondHit = false;
    const auto& second = cache.Resolve(request, effects, &secondHit);

    EXPECT_FALSE(firstHit);
    EXPECT_TRUE(secondHit);
    EXPECT_EQ(&first, &second);
    EXPECT_TRUE(first.IsCanonical());
    EXPECT_EQ(first.VertexShaderSource, request.VertexShaderSource);
    EXPECT_EQ(first.FragmentShaderSource, request.FragmentShaderSource);
    EXPECT_EQ(first.VertexShaderKey, request.VertexShaderKey);
    EXPECT_EQ(first.FragmentShaderKey, request.FragmentShaderKey);
    EXPECT_EQ(first.RequestedFeatures,
              Fast::Extension::PicaShaderInstrumentationFeature::None);
    EXPECT_EQ(first.AppliedFeatures,
              Fast::Extension::PicaShaderInstrumentationFeature::None);

    cache.BeginFrame(5U);
    bool revisedHit = false;
    const auto& revised = cache.Resolve(request, effects, &revisedHit);
    EXPECT_TRUE(revisedHit);
    EXPECT_EQ(&first, &revised);
    const auto stats = cache.Stats();
    EXPECT_EQ(stats.CanonicalEntries, 1U);
    EXPECT_EQ(stats.CanonicalOutputAudits, 1U);
    EXPECT_EQ(stats.CanonicalOutputContractRejects, 0U);
    EXPECT_EQ(stats.CanonicalCompatibilityAnalyses, 0U);
    EXPECT_EQ(stats.InstrumentationEntries, 0U);
    EXPECT_EQ(stats.InstrumentationInvalidations, 1U);
}

// IsolatedLighting changes the guides through the draw's decisions, not the
// text: both draws share one program, which reads the decision from the
// effect block. It stays part of the variant key (the decisions differ): a
// warm cache returns exactly what a cold cache generates, whichever of the
// two draws comes first.
TEST(Oot3dPicaShaderPipelineCache, IsolatedLightingSelectsItsOwnDecisions) {
    using namespace Fast::Extension;
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    auto shared = BaseRequest();
    auto isolated = shared;
    isolated.Draw.IsolatedLighting = true;
    const auto cold = [&](const PicaShaderPipelineRequest& request) {
        PicaShaderPipelineCache cache;
        cache.BeginFrame(1U);
        return cache.Resolve(request, effects);
    };
    const auto sharedCold = cold(shared);
    const auto isolatedCold = cold(isolated);
    ASSERT_FALSE(sharedCold.IsCanonical());
    ASSERT_FALSE(isolatedCold.IsCanonical());
    EXPECT_EQ(sharedCold.FragmentShaderKey, isolatedCold.FragmentShaderKey);
    EXPECT_EQ(sharedCold.FragmentShaderSource, isolatedCold.FragmentShaderSource);
    EXPECT_TRUE(sharedCold.ReadsDrawControl);
    EXPECT_TRUE(isolatedCold.ReadsDrawControl);
    EXPECT_NE(isolatedCold.FragmentShaderSource.find("? vec4(0.0, 0.0, 0.0, 1.0)"), std::string::npos);
    EXPECT_NE(isolatedCold.FragmentShaderSource.find("uniform Oot3dDrawControl"), std::string::npos);
    EXPECT_NE(sharedCold.Decisions.Ambient, PicaEffectDecisions::AmbientPath::None);
    EXPECT_EQ(isolatedCold.Decisions.Ambient, PicaEffectDecisions::AmbientPath::None);
    // The program tests control.y's ambient field against None.
    const auto field = [](const PicaShaderPipelineResult& result) {
        return (PackPicaUberEffectBlock(result.Decisions, {}).Control[1] >> kPicaUberAmbientShift) &
               ((1U << kPicaUberFieldBits) - 1U);
    };
    EXPECT_EQ(field(isolatedCold), static_cast<uint32_t>(PicaEffectDecisions::AmbientPath::None));
    EXPECT_NE(field(sharedCold), static_cast<uint32_t>(PicaEffectDecisions::AmbientPath::None));

    for (const bool isolatedFirst : {false, true}) {
        PicaShaderPipelineCache cache;
        cache.BeginFrame(1U);
        const auto& first = cache.Resolve(isolatedFirst ? isolated : shared, effects);
        bool secondHit = true;
        const auto& second = cache.Resolve(isolatedFirst ? shared : isolated, effects, &secondHit);
        EXPECT_FALSE(secondHit);
        const auto& warmShared = isolatedFirst ? second : first;
        const auto& warmIsolated = isolatedFirst ? first : second;
        EXPECT_EQ(warmShared.FragmentShaderSource, sharedCold.FragmentShaderSource);
        EXPECT_EQ(warmShared.FragmentShaderKey, sharedCold.FragmentShaderKey);
        EXPECT_EQ(warmIsolated.FragmentShaderSource, isolatedCold.FragmentShaderSource);
        EXPECT_EQ(warmIsolated.FragmentShaderKey, isolatedCold.FragmentShaderKey);
        EXPECT_EQ(warmIsolated.Decisions.Ambient, PicaEffectDecisions::AmbientPath::None);
        EXPECT_NE(warmShared.Decisions.Ambient, PicaEffectDecisions::AmbientPath::None);
        EXPECT_EQ(cache.Stats().InstrumentationEntries, 2U);
    }
}

TEST(Oot3dPicaShaderPipelineCache,
     CanonicalIdentityIncludesExactSourceContent) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    cache.BeginFrame(7U);
    auto first = BaseRequest();
    auto second = BaseRequest();
    static constexpr std::string_view kDifferentSameLength =
        "#version 450\n"
        "vec4 pica_output0 = vec4(1.0);\n"
        "void main() { gl_Position = pica_output0; }\n";
    ASSERT_EQ(kDifferentSameLength.size(),
              first.VertexShaderSource.size());
    second.VertexShaderSource = kDifferentSameLength;

    (void)cache.Resolve(first, effects);
    bool secondHit = true;
    const auto& different = cache.Resolve(second, effects, &secondHit);

    EXPECT_FALSE(secondHit);
    EXPECT_EQ(different.VertexShaderSource, kDifferentSameLength);
    EXPECT_EQ(cache.Stats().CanonicalEntries, 2U);
}

TEST(Oot3dPicaShaderPipelineCache,
     AuditsPublishedSourceIdentityOnceAndUsesFastHits) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.VertexShaderSourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(
            request.VertexShaderSource);
    request.FragmentShaderSourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(
            request.FragmentShaderSource);
    cache.BeginFrame(8U);

    bool firstHit = true;
    (void)cache.Resolve(request, effects, &firstHit);
    bool secondHit = false;
    (void)cache.Resolve(request, effects, &secondHit);

    EXPECT_FALSE(firstHit);
    EXPECT_TRUE(secondHit);
    const auto stats = cache.Stats();
    EXPECT_EQ(stats.CanonicalSourceIdentityAudits, 2U);
    EXPECT_EQ(stats.CanonicalSourceIdentityFastHits, 2U);
    EXPECT_EQ(stats.CanonicalSourceIdentityRejects, 0U);
}

TEST(Oot3dPicaShaderPipelineCache,
     RejectsIncorrectPublishedSourceIdentity) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.VertexShaderSourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(
            request.VertexShaderSource);
    request.FragmentShaderSourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(
            request.FragmentShaderSource);
    ++request.FragmentShaderSourceIdentity.SecondaryHash;
    cache.BeginFrame(9U);

    EXPECT_THROW((void)cache.Resolve(request, effects),
                 std::logic_error);
    EXPECT_EQ(cache.Stats().CanonicalSourceIdentityRejects, 1U);
}

TEST(Oot3dPicaShaderPipelineCache,
     InstrumentationInvalidatesWithoutDiscardingCanonicalShaders) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(9U);

    const auto& instrumented = cache.Resolve(request, effects);
    EXPECT_EQ(instrumented.Domain,
              Fast::Extension::PicaShaderDomain::Instrumented);
    EXPECT_TRUE(Fast::Extension::HasPicaShaderInstrumentationFeature(
        instrumented.AppliedFeatures,
        Fast::Extension::PicaShaderInstrumentationFeature::TemporalVertex));
    EXPECT_TRUE(Fast::Extension::HasPicaShaderInstrumentationFeature(
        instrumented.AppliedFeatures,
        Fast::Extension::PicaShaderInstrumentationFeature::RigidMotionGuide));
    EXPECT_NE(instrumented.VertexShaderSource,
              request.VertexShaderSource);
    EXPECT_NE(instrumented.FragmentShaderSource,
              request.FragmentShaderSource);
    EXPECT_EQ(cache.Stats().CanonicalEntries, 1U);
    EXPECT_EQ(cache.Stats().InstrumentationEntries, 1U);

    cache.BeginFrame(10U);
    EXPECT_EQ(cache.Stats().CanonicalEntries, 1U);
    EXPECT_EQ(cache.Stats().InstrumentationEntries, 0U);
}

TEST(Oot3dPicaShaderPipelineCache,
     RejectsPreInstrumentedCanonicalFragmentSources) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    static constexpr std::string_view kContaminated =
        "#version 450\n"
        "layout(location=0) out vec4 pica_color;\n"
        "layout(location=2) out vec4 pica_material_guide;\n"
        "void main() {\n"
        "    pica_color = vec4(1.0);\n"
        "    pica_material_guide = vec4(0.0);\n"
        "}\n";
    request.FragmentShaderSource = kContaminated;
    request.FragmentShaderHooks =
        Fast::Extension::AnalyzePicaFragmentShaderHooks(kContaminated);
    cache.BeginFrame(1U);

    EXPECT_THROW((void)cache.Resolve(request, effects),
                 std::logic_error);
    const auto stats = cache.Stats();
    EXPECT_EQ(stats.CanonicalEntries, 0U);
    EXPECT_EQ(stats.CanonicalOutputAudits, 1U);
    EXPECT_EQ(stats.CanonicalOutputContractRejects, 1U);
}

TEST(Oot3dPicaShaderPipelineCache,
     RejectsDuplicateCanonicalColorLocations) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    static constexpr std::string_view kDuplicateLocation =
        "#version 450\n"
        "layout(location=0) out vec4 pica_color;\n"
        "layout(location=0) out vec4 accidental_color;\n"
        "void main() {\n"
        "    pica_color = vec4(1.0);\n"
        "    accidental_color = vec4(0.0);\n"
        "}\n";
    request.FragmentShaderSource = kDuplicateLocation;
    request.FragmentShaderHooks =
        Fast::Extension::AnalyzePicaFragmentShaderHooks(
            kDuplicateLocation);
    cache.BeginFrame(1U);

    EXPECT_THROW((void)cache.Resolve(request, effects),
                 std::logic_error);
    EXPECT_EQ(cache.Stats().CanonicalOutputContractRejects, 1U);
}

TEST(Oot3dPicaShaderPipelineCache,
     AuditsLegacyCanonicalHooksOnceBeforeCaching) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.FragmentShaderHooks = {};
    cache.BeginFrame(1U);

    const auto& first = cache.Resolve(request, effects);
    const auto& second = cache.Resolve(request, effects);
    EXPECT_TRUE(first.IsCanonical());
    EXPECT_EQ(&first, &second);
    EXPECT_FALSE(first.DirectCanonicalFragmentHooksUsed);
    const auto stats = cache.Stats();
    EXPECT_EQ(stats.CanonicalOutputAudits, 1U);
    EXPECT_EQ(stats.CanonicalCompatibilityAnalyses, 1U);
    EXPECT_EQ(stats.CanonicalOutputContractRejects, 0U);
}

TEST(Oot3dPicaShaderPipelineCache,
     LiveTypedContractSupersedesReplayInferenceAndCachedVariants) {
    using namespace Fast::Extension;
    constexpr std::string_view source = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
void main() {
    vec3 normal = vec3(0.25, 0.5, 1.0);
    pica_color = vec4(normal, 1.0);
}
)glsl";
    auto request = BaseRequest();
    request.FragmentShaderSource = source;
    request.FragmentShaderHooks = {};
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    PicaShaderPipelineCache cache;
    cache.BeginFrame(1U);
    const auto& replay = cache.Resolve(request, effects);
    ASSERT_TRUE(HasPicaShaderInstrumentationFeature(
        replay.AppliedFeatures, PicaShaderInstrumentationFeature::NormalGuide));

    // This native program uses "normal" as a color, not a surface normal.
    // The frontend contract is authoritative over legacy source inference.
    request.FragmentShaderHooks = AnalyzePicaFragmentShaderHooks(source);
    request.FragmentShaderHooks.Semantics = static_cast<PicaShaderSemantic>(
        static_cast<uint32_t>(request.FragmentShaderHooks.Semantics) &
        ~static_cast<uint32_t>(PicaShaderSemantic::MaterialNormal));
    const auto& live = cache.Resolve(request, effects);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(
        live.AppliedFeatures, PicaShaderInstrumentationFeature::NormalGuide));
    // Earlier recorded draws retain their variant until the frame boundary.
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        replay.AppliedFeatures, PicaShaderInstrumentationFeature::NormalGuide));
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(
        cache.Resolve(request, effects).AppliedFeatures,
        PicaShaderInstrumentationFeature::NormalGuide));
}

TEST(Oot3dPicaShaderPipelineCache,
     PublishesFallbackProvenanceWithoutTypedFrontendProgram) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.TemporalMotionEnabled = true;
    request.TemporalVertexProgram = {};
    cache.BeginFrame(1U);

    const auto& result = cache.Resolve(request, effects);
    EXPECT_FALSE(Fast::Extension::HasPicaShaderInstrumentationFeature(
        result.AppliedFeatures, Fast::Extension::PicaShaderInstrumentationFeature::TemporalVertex));
    EXPECT_TRUE(Fast::Extension::HasPicaShaderInstrumentationFeature(
        result.AppliedFeatures, Fast::Extension::PicaShaderInstrumentationFeature::RigidMotionGuide));
    EXPECT_EQ(result.FragmentShaderSource.find("layout(location=7) in"), std::string::npos);
    EXPECT_NE(result.FragmentShaderSource.find("vec4(oot3d_draw.rigid_motion.xyz, 0.0)"), std::string::npos);
    EXPECT_EQ(cache.Stats().TypedInstrumentationContractRejects, 1U);
}

TEST(Oot3dPicaShaderPipelineCache,
     NativeLightingRejectsDirectionalShadowsEvenWhenRequested) {
    using namespace Fast::Extension;

    EffectsSettings disabledSettings;
    auto enabledRequest = BaseRequest();
    enabledRequest.Draw.DirectionalShadowReceiver = true;
    PicaShaderPipelineCache enabledCache;
    enabledCache.BeginFrame(1U);
    const auto& enabled = enabledCache.Resolve(
        enabledRequest, disabledSettings);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(
        enabled.RequestedFeatures,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting));
    EXPECT_EQ(enabled.FragmentShaderKey, enabledRequest.FragmentShaderKey);

    EffectsSettings enabledSettings;
    enabledSettings.DirectionalShadows.Mode =
        DirectionalShadowMode::VirtualShadowMaps;
    auto disabledRequest = BaseRequest();
    disabledRequest.Draw.DirectionalShadowReceiver = false;
    PicaShaderPipelineCache disabledCache;
    disabledCache.BeginFrame(1U);
    const auto& disabled = disabledCache.Resolve(
        disabledRequest, enabledSettings);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(
        disabled.RequestedFeatures,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting));
}

TEST(Oot3dPicaShaderPipelineCache,
     UsesFrontendFragmentHooksWhenTheyMatchCanonicalSource) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.FragmentShaderHooks =
        Fast::Extension::AnalyzePicaFragmentShaderHooks(
            request.FragmentShaderSource);
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(11U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_TRUE(instrumented.DirectFragmentHooksUsed);
    EXPECT_TRUE(instrumented.RigidMotionApplied);
}

TEST(Oot3dPicaShaderPipelineCache,
     BuildsRequestedNormalGuideFromTypedMaterialNormal) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
void main() {
    vec3 normal = normalize(vec3(0.25, 0.5, 1.0));
    // OOT3D_PICA_MATERIAL_TOON_POINT
    pica_color = vec4(1.0);
}
)glsl";

    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    auto request = BaseRequest();
    request.FragmentShaderSource = kFragmentShader;
    request.FragmentShaderHooks =
        AnalyzePicaFragmentShaderHooks(kFragmentShader);
    cache.BeginFrame(11U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_TRUE(instrumented.DirectFragmentHooksUsed);
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        instrumented.RequestedFeatures,
        PicaShaderInstrumentationFeature::NormalGuide));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        instrumented.AppliedFeatures,
        PicaShaderInstrumentationFeature::NormalGuide));
    EXPECT_NE(instrumented.FragmentShaderSource.find(
                  "layout(location=1) out vec4 pica_normal_guide"),
              std::string::npos);
    EXPECT_NE(instrumented.FragmentShaderSource.find(
                  "pica_normal_guide = vec4(normal * 0.5 + 0.5, 1.0)"),
              std::string::npos);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        instrumented.FragmentShaderSource, shaderc_fragment_shader,
        "typed_pica_normal_guide.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
#endif
}

// A typed CMB-like program: the transform and surface layouts name the normal
// input and the uniforms the native program transforms it with.
constexpr std::string_view kTypedVertexShader = R"glsl(#version 450
layout(location=0) in vec4 pica_input0;
layout(location=1) in vec4 pica_input1;
layout(location=2) in vec4 pica_input2;
layout(set=0,binding=0,std140) uniform PicaVertexUniforms {
    uint b; int flip_viewport; uvec4 i[4]; vec4 f[96];
} uniforms;
vec4 pica_output0 = vec4(0.0);
void main() { pica_output0 = pica_input0; gl_Position = pica_output0; }
)glsl";

Oot3d::Renderer::PicaTemporalVertexProgramView TypedVertexProgram() {
    using namespace Oot3d::Renderer;
    const size_t state = kTypedVertexShader.find("vec4 pica_output0 =");
    const size_t main = kTypedVertexShader.find("void main()");
    PicaVertexShaderHookLayout hooks;
    hooks.SchemaVersion = kPicaShaderHookSchemaVersion;
    hooks.SourceSize = kTypedVertexShader.size();
    hooks.Offsets[static_cast<size_t>(PicaVertexShaderHook::GlobalDeclarations)] = state;
    hooks.Offsets[static_cast<size_t>(PicaVertexShaderHook::RegisterStateBegin)] = state;
    hooks.Offsets[static_cast<size_t>(PicaVertexShaderHook::RegisterStateEnd)] = main;
    hooks.Offsets[static_cast<size_t>(PicaVertexShaderHook::MainBodyBegin)] = kTypedVertexShader.find('{', main) + 1U;
    hooks.Offsets[static_cast<size_t>(PicaVertexShaderHook::MainBodyEnd)] = kTypedVertexShader.find_last_of('}');
    hooks.Semantics = PicaVertexShaderSemantic::PicaRegisterState | PicaVertexShaderSemantic::VertexUniformState |
                      PicaVertexShaderSemantic::ClipPositionOutput | PicaVertexShaderSemantic::TransformProgram |
                      PicaVertexShaderSemantic::SurfaceProgram;
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4, 0, 4, 4, 3, 20, 3, 0, 1};
    hooks.Surface.ViewNormalTemporary = 14;
    hooks.Surface.LightingBooleanUniform = 9;
    hooks.Surface.DiffuseUniform = 8;
    hooks.Surface.AmbientUniform = 9;
    hooks.Surface.TintBooleanUniform = 5;
    hooks.Surface.TintInputRegister = 2;
    hooks.Surface.TintScale = {90, 2};
    hooks.Surface.LightEnabledValue = {93, 1};
    hooks.Surface.LightDirectionUniforms = {80, 83, 86};
    return {hooks, "vec4 pica_output0_previous = vec4(0.0);\n", " gl_Position = pica_output0_previous; "};
}

// A vertex-lit world material: one colour texture, native LOD sampling, the
// quaternion and view inputs every frontend program declares.
constexpr std::string_view kVertexLitFragmentShader = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=1) in vec2 pica_texcoord0;
layout(location=5) in vec4 pica_normquat;
layout(location=6) in vec3 pica_view;
layout(set=0,binding=1) uniform sampler2D pica_texture0;
layout(set=0,binding=4,std140) uniform PicaFragmentUniforms { vec4 texture_lod_bias; } fragment_uniforms;
layout(location=0) out vec4 pica_color;
float pica_texture_lod(vec2 coord, vec2 size) {
    vec2 scaled = coord * size;
    vec2 delta = max(abs(dFdx(scaled)), abs(dFdy(scaled)));
    return log2(max(delta.x, delta.y));
}
vec4 pica_sample_texture0(vec2 coord) {
    float lod = pica_texture_lod(coord, vec2(textureSize(pica_texture0, 0)));
    return textureLod(pica_texture0, coord, lod + fragment_uniforms.texture_lod_bias[0]);
}
void main() {
    vec4 combiner_output = pica_sample_texture0(vec2(pica_texcoord0.x, 1.0 - pica_texcoord0.y)) * pica_primary_color;
    if (combiner_output.a < 0.5) discard;
    pica_color = combiner_output;
}
)glsl";

Fast::Extension::PicaShaderPipelineRequest VertexLitWorldRequest() {
    auto request = BaseRequest();
    request.VertexShaderSource = kTypedVertexShader;
    request.TemporalVertexProgram = TypedVertexProgram();
    request.FragmentShaderSource = kVertexLitFragmentShader;
    request.FragmentShaderHooks = Fast::Extension::AnalyzePicaFragmentShaderHooks(kVertexLitFragmentShader);
    return request;
}

#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
void ExpectCompiles(const std::string& source, shaderc_shader_kind kind, const char* name) {
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(source, kind, name);
    EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success) << compiled.GetErrorMessage();
}
#endif

TEST(Oot3dPicaUberEffectProgram, DecisionFieldsDecodeIndependently) {
    using namespace Fast::Extension;
    using Decision = PicaEffectDecisions;
    const auto decode = [](uint32_t packed, unsigned shift) {
        return (packed >> shift) & ((1U << kPicaUberFieldBits) - 1U);
    };
    // Every field at its largest value next to neighbours at theirs: a field
    // that read its neighbour's bits would not round-trip.
    Decision decisions;
    decisions.Bits = Decision::NormalGuide | Decision::Reactive;
    decisions.Normal = Decision::NormalPath::NativeRotate;
    decisions.NormalAt = Decision::NormalWriter::Outline;
    decisions.MaterialAt = Decision::MaterialWriter::Baseline;
    decisions.AlbedoCoverage = Decision::Coverage::Alpha;
    decisions.Albedo = Decision::AlbedoPath::Surface;
    decisions.Ambient = Decision::AmbientPath::Fallback;
    decisions.ReactiveKind = Decision::ReactiveCoverage::Full;
    const auto block = PackPicaUberEffectBlock(decisions, {});
    EXPECT_EQ(block.Control[0], decisions.Bits);
    const uint32_t y = block.Control[1];
    EXPECT_EQ(decode(y, kPicaUberNormalShift), static_cast<uint32_t>(decisions.Normal));
    EXPECT_EQ(decode(y, kPicaUberNormalAtShift), static_cast<uint32_t>(decisions.NormalAt));
    EXPECT_EQ(decode(y, kPicaUberMaterialAtShift), static_cast<uint32_t>(decisions.MaterialAt));
    EXPECT_EQ(decode(y, kPicaUberCoverageShift), static_cast<uint32_t>(decisions.AlbedoCoverage));
    EXPECT_EQ(decode(y, kPicaUberAlbedoShift), static_cast<uint32_t>(decisions.Albedo));
    EXPECT_EQ(decode(y, kPicaUberAmbientShift), static_cast<uint32_t>(decisions.Ambient));
    EXPECT_EQ(decode(y, kPicaUberReactiveShift), static_cast<uint32_t>(decisions.ReactiveKind));
    static_assert(sizeof(PicaUberEffectBlock) == 160U);
}

TEST(Oot3dPicaUberEffectProgram, DecisionsRecordTheDeclaredGuideOutputs) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Mxao;
    auto request = VertexLitWorldRequest();
    cache.BeginFrame(1U);
    const auto& variant = cache.Resolve(request, effects);
    ASSERT_FALSE(variant.IsCanonical());
    // The superset program declares every guide; its pipeline writes only
    // the attachments the specialized program declares.
    for (uint32_t location = 1U; location < 8U; ++location) {
        const bool declared = variant.FragmentShaderSource.find(
            "layout(location=" + std::to_string(location) + ") out") != std::string_view::npos;
        EXPECT_EQ((variant.Decisions.Outputs & (1U << location)) != 0U, declared) << location;
    }
    EXPECT_NE(variant.Decisions.Outputs & (1U << 1U), 0U);
}

TEST(Oot3dPicaShaderPipelineCache, NormalGuideReadsTheAuthoredVertexNormalInEveryLightingMode) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Mxao;
    auto request = VertexLitWorldRequest();
    cache.BeginFrame(1U);
    const auto typed = cache.Resolve(request, effects);
    // Native lighting: no per-pixel relighting, yet the guide no longer falls
    // back to the geometric normal of the depth derivatives.
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(typed.AppliedFeatures,
                                                     PicaShaderInstrumentationFeature::NativePerPixelLighting));
    EXPECT_TRUE(typed.TypedNormalGuide);
    EXPECT_NE(typed.VertexShaderSource.find("layout(location=9) out vec3 pica_surface_normal"), std::string::npos);
    EXPECT_NE(typed.FragmentShaderSource.find("layout(location=9) in vec3 pica_surface_normal"), std::string::npos);
    EXPECT_NE(typed.FragmentShaderSource.find(
                  "oot3d_normal_guide_typed(pica_surface_normal, oot3d_normal_guide_surface(pica_normquat, pica_view, "
                  "oot3d_view_dx, oot3d_view_dy))"),
              std::string::npos);
    // The native colour path is untouched.
    EXPECT_NE(typed.FragmentShaderSource.find("pica_color = combiner_output;"), std::string::npos);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    ExpectCompiles(typed.VertexShaderSource, shaderc_vertex_shader, "typed_normal_guide.vert");
    ExpectCompiles(typed.FragmentShaderSource, shaderc_fragment_shader, "typed_normal_guide.frag");
#endif

    // An untyped program keeps the quaternion/geometric guide and its key.
    auto untyped = request;
    untyped.TemporalVertexProgram = BaseTemporalVertexProgram();
    untyped.VertexShaderSource = BaseRequest().VertexShaderSource;
    const auto& fallback = cache.Resolve(untyped, effects);
    EXPECT_FALSE(fallback.TypedNormalGuide);
    EXPECT_NE(fallback.FragmentShaderSource.find(
                  "pica_normal_guide = vec4(oot3d_normal_guide_surface(pica_normquat, pica_view, oot3d_view_dx, "
                  "oot3d_view_dy) * 0.5 + 0.5, 1.0);"),
              std::string::npos);
    EXPECT_EQ(fallback.FragmentShaderSource.find("pica_surface_normal"), std::string::npos);
    const size_t derivatives = fallback.FragmentShaderSource.find(kViewDerivativesPrologue);
    ASSERT_NE(derivatives, std::string::npos);
    EXPECT_LT(derivatives, fallback.FragmentShaderSource.find("combiner_output"));
    EXPECT_EQ(fallback.FragmentShaderSource.find("dFdx(to_eye)"), std::string::npos);
    EXPECT_NE(fallback.FragmentShaderKey, typed.FragmentShaderKey);

    // Without a normal-guide reader the program stays canonical.
    effects.AmbientOcclusion = AmbientOcclusionMode::Off;
    cache.BeginFrame(2U);
    const auto& canonical = cache.Resolve(request, effects);
    EXPECT_TRUE(canonical.IsCanonical());
    EXPECT_EQ(canonical.VertexShaderSource, kTypedVertexShader);
}

TEST(Oot3dPicaShaderPipelineCache, FaceNormalReadersRequestTheGeometryGuide) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Mxao;
    effects.MxaoNormals = MxaoNormalSource::Depth;
    const auto request = VertexLitWorldRequest();
    cache.BeginFrame(1U);
    const auto depth = cache.Resolve(request, effects);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(depth.RequestedFeatures,
                                                     PicaShaderInstrumentationFeature::FaceNormalGuide));
    const auto depthKey = depth.FragmentShaderKey;

    effects.MxaoNormals = MxaoNormalSource::Geometry;
    cache.BeginFrame(2U);
    const auto face = cache.Resolve(request, effects);
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(face.RequestedFeatures,
                                                    PicaShaderInstrumentationFeature::FaceNormalGuide));
    EXPECT_NE(face.FragmentShaderKey, depthKey);

    // The debug views that show face normals read them too; MXAO is their
    // only reader.
    effects.MxaoNormals = MxaoNormalSource::Depth;
    effects.MxaoDebug = MxaoDebugView::FaceDisagreement;
    EXPECT_TRUE(EffectsReadFaceNormal(effects));
    effects.MxaoDebug = MxaoDebugView::FaceNormals;
    EXPECT_TRUE(EffectsReadFaceNormal(effects));
    effects.MxaoDebug = MxaoDebugView::DepthNormals;
    EXPECT_FALSE(EffectsReadFaceNormal(effects));
    effects.MxaoNormals = MxaoNormalSource::Geometry;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    EXPECT_FALSE(EffectsReadFaceNormal(effects));
}

TEST(Oot3dPicaShaderPipelineCache, AoTextureReliefBendsTheGuideFromTheColourTexture) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    effects.AoRelief = AoTextureRelief::Medium;
    auto request = VertexLitWorldRequest();
    request.ReliefTexture = 0U;
    cache.BeginFrame(1U);
    const auto relief = cache.Resolve(request, effects);
    ASSERT_TRUE(HasPicaShaderInstrumentationFeature(relief.AppliedFeatures,
                                                    PicaShaderInstrumentationFeature::NormalGuideTextureRelief));
    const auto& source = relief.FragmentShaderSource;
    const size_t taps = source.find("// OOT3D_AO_TEXTURE_RELIEF");
    const size_t alphaTest = source.find("discard;");
    const size_t bend = source.find("pica_normal_guide.rgb = oot3d_relief_normal(");
    ASSERT_NE(taps, std::string::npos);
    ASSERT_NE(bend, std::string::npos);
    // Taps and derivatives precede the native alpha test; the bend follows
    // the guide assignment it modifies.
    EXPECT_LT(taps, alphaTest);
    EXPECT_LT(source.find("pica_normal_guide = vec4("), bend);
    EXPECT_NE(source.find("fragment_uniforms.texture_lod_bias[0]"), std::string::npos);
    EXPECT_NE(source.find("2.000000 *"), std::string::npos);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    ExpectCompiles(source, shaderc_fragment_shader, "ao_texture_relief.frag");
#endif

    // Each level is its own variant; Off, no colour texture, a disabled AO,
    // a blended surface or an integer texture add nothing.
    effects.AoRelief = AoTextureRelief::Strong;
    cache.BeginFrame(2U);
    const auto strongKey = cache.Resolve(request, effects).FragmentShaderKey;
    EXPECT_NE(strongKey, relief.FragmentShaderKey);
    effects.AoRelief = AoTextureRelief::Off;
    cache.BeginFrame(3U);
    const auto off = cache.Resolve(request, effects);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(off.RequestedFeatures,
                                                     PicaShaderInstrumentationFeature::NormalGuideTextureRelief));
    EXPECT_EQ(off.FragmentShaderSource.find("oot3d_relief"), std::string::npos);
    effects.AoRelief = AoTextureRelief::Medium;
    cache.BeginFrame(4U);
    auto without = request;
    without.ReliefTexture = 0xffU;
    EXPECT_EQ(cache.Resolve(without, effects).FragmentShaderSource, off.FragmentShaderSource);
    auto blended = request;
    blended.Draw.DepthWriteEnabled = false;
    blended.Draw.Blend = SourceAlphaBlend();
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(cache.Resolve(blended, effects).RequestedFeatures,
                                                     PicaShaderInstrumentationFeature::NormalGuideTextureRelief));
    auto integer = request;
    std::string integerSource(kVertexLitFragmentShader);
    integerSource.replace(integerSource.find("uniform sampler2D pica_texture0"), 31, "uniform usampler2D pica_texture0");
    integer.FragmentShaderSource = integerSource;
    integer.FragmentShaderHooks = AnalyzePicaFragmentShaderHooks(integerSource);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(cache.Resolve(integer, effects).AppliedFeatures,
                                                     PicaShaderInstrumentationFeature::NormalGuideTextureRelief));
    effects.AmbientOcclusion = AmbientOcclusionMode::Off;
    effects.Reflections = ReflectionMode::HiZ;
    cache.BeginFrame(5U);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(cache.Resolve(request, effects).RequestedFeatures,
                                                     PicaShaderInstrumentationFeature::NormalGuideTextureRelief));
}

TEST(Oot3dPicaShaderPipelineCache,
     OutlineFogGuideBelongsOnlyToDepthWritingWorldDraws) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Toon = ToonMode::PostProcessPreview;
    effects.ToonStyle.OutlineEnabled = true;
    auto request = BaseRequest();
    cache.BeginFrame(1U);
    EXPECT_TRUE(cache.Resolve(request, effects).FragmentOutputs.WritesFogGuide);
    // A color-only program has no geometric normal capability.
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesOutlineGeometryGuide);
    constexpr std::string_view source = "#version 450\nlayout(location=5) in vec4 pica_normquat;\n"
                                        "layout(location=0) out vec4 pica_color;\n"
                                        "void main() { pica_color=vec4(1.0); }\n";
    request.FragmentShaderSource = source;
    request.FragmentShaderHooks = AnalyzePicaFragmentShaderHooks(source);
    EXPECT_TRUE(cache.Resolve(request, effects).FragmentOutputs.WritesOutlineGeometryGuide);
    const auto geometryKey = cache.Resolve(request, effects).FragmentShaderKey;
    request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Always;
    EXPECT_NE(cache.Resolve(request, effects).FragmentShaderKey, geometryKey);
    EXPECT_NE(cache.Resolve(request, effects).FragmentShaderSource.find("vec4(0.0, 0.0, 0.0, 1.0)"),
              std::string::npos);
    request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Less;
    EXPECT_EQ(cache.Resolve(request, effects).FragmentShaderKey, geometryKey);
    request.Draw.Blend = SourceAlphaBlend();
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesOutlineGeometryGuide);
    EXPECT_TRUE(cache.Resolve(request, effects).FragmentOutputs.SceneDomainTransparentDepthOverlay);
    request.Draw.Blend = {};
    request.Draw.DepthWriteEnabled = false;
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesFogGuide);
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesOutlineGeometryGuide);
    request.Draw.DepthWriteEnabled = true;
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Ui;
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesFogGuide);
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesOutlineGeometryGuide);
    request = BaseRequest();
    effects.ToonStyle.OutlineEnabled = false;
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesFogGuide);
    EXPECT_FALSE(cache.Resolve(request, effects).FragmentOutputs.WritesOutlineGeometryGuide);
}

TEST(Oot3dPicaShaderPipelineCache,
     BuildsRequestedNormalGuideFromNativeNormalQuaternion) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=5) in vec4 pica_normquat;
layout(location=0) out vec4 pica_color;
void main() {
    pica_color = vec4(1.0);
}
)glsl";

    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Toon = ToonMode::PostProcessPreview;
    effects.ToonStyle.OutlineEnabled = true;
    auto request = BaseRequest();
    request.FragmentShaderSource = kFragmentShader;
    request.FragmentShaderHooks =
        AnalyzePicaFragmentShaderHooks(kFragmentShader);
    cache.BeginFrame(11U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        instrumented.AppliedFeatures,
        PicaShaderInstrumentationFeature::NormalGuide));
    EXPECT_NE(instrumented.FragmentShaderSource.find(
                  "oot3d_normal_guide_rotate_z(pica_normquat)"),
              std::string::npos);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        instrumented.FragmentShaderSource, shaderc_fragment_shader,
        "native_quaternion_pica_normal_guide.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
#endif
}

TEST(Oot3dPicaShaderPipelineCache,
     MaterialToonConsumesTheTypedPicaNormal) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=5) in vec4 pica_normquat;
layout(location=6) in vec3 pica_view;
layout(location=0) out vec4 pica_color;
void main() {
    vec4 rounded_primary_color = pica_primary_color;
    vec4 primary_fragment_color = rounded_primary_color;
    vec4 secondary_fragment_color = vec4(0.25);
    vec3 normal = normalize(vec3(0.25, 0.5, 1.0));
    // OOT3D_PICA_MATERIAL_TOON_POINT
    vec4 combiner_output = primary_fragment_color + secondary_fragment_color;
    float pica_z_over_w = -gl_FragCoord.z;
    pica_color = combiner_output;
}
)glsl";

    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Toon = ToonMode::PicaMaterial;
    auto request = BaseRequest();
    request.FragmentShaderSource = kFragmentShader;
    request.FragmentShaderHooks =
        AnalyzePicaFragmentShaderHooks(kFragmentShader);
    cache.BeginFrame(12U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_TRUE(instrumented.DirectFragmentHooksUsed);
    EXPECT_TRUE(instrumented.ToonMaterialPath);
    EXPECT_NE(instrumented.FragmentShaderSource.find(
                  "oot3d_material_rim(normal)"),
              std::string::npos);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        instrumented.FragmentShaderSource, shaderc_fragment_shader,
        "typed_pica_material_toon.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
#endif
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedTemporalVertexProgramPreservesLegacySourceAndKey) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    const auto legacy = Fast::Extension::BuildPicaTemporalVertexVariant(
        request.VertexShaderSource, request.VertexShaderKey);
    ASSERT_TRUE(legacy.Applied);
    request.TemporalVertexProgram = BaseTemporalVertexProgram();
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(11U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_TRUE(instrumented.DirectTemporalVertexProgramUsed);
    EXPECT_EQ(instrumented.VertexShaderSource, legacy.Source);
    EXPECT_EQ(instrumented.VertexShaderKey, legacy.FragmentKey);
}

TEST(Oot3dPicaShaderPipelineCache,
     RejectsStaleFrontendFragmentHooksAndFallsBackToAnalysis) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.FragmentShaderHooks =
        Fast::Extension::AnalyzePicaFragmentShaderHooks(
            request.FragmentShaderSource);
    ++request.FragmentShaderHooks.SourceSize;
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(12U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_FALSE(instrumented.DirectFragmentHooksUsed);
    EXPECT_TRUE(instrumented.RigidMotionApplied);
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedToonCompositionUsesUniformParametersAndRasterEligibility) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=5) in vec4 pica_normquat;
layout(location=6) in vec3 pica_view;
layout(location=0) out vec4 pica_color;
void main() {
    vec4 rounded_primary_color = pica_primary_color;
    vec4 shaded_primary = rounded_primary_color;
    vec4 combiner_output = shaded_primary;
    float pica_z_over_w = -gl_FragCoord.z;
    pica_color = combiner_output;
}
)glsl";

    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Toon = ToonMode::PostProcessPreview;
    auto request = BaseRequest();
    request.FragmentShaderSource = kFragmentShader;
    request.FragmentShaderKey = 83U;
    request.FragmentShaderHooks =
        AnalyzePicaFragmentShaderHooks(kFragmentShader);
    cache.BeginFrame(13U);

    const PicaToonDrawInfo toonDraw{
        request.Draw.CompositionDomain,
        request.Draw.DepthTestEnabled,
        request.Draw.DepthWriteEnabled,
        request.Draw.Blend.Enabled,
        request.Draw.ColorWriteMask,
    };
    const auto expected = BuildPicaToonInstrumentation(
        request.FragmentShaderKey, toonDraw, effects.Toon, effects.ToonStyle,
        request.FragmentShaderHooks, true);
    ASSERT_TRUE(expected.Applied());

    const auto& composed = cache.Resolve(request, effects);

    EXPECT_TRUE(composed.DirectFragmentHooksUsed);
    EXPECT_EQ(composed.ToonEligibility,
              PicaToonEligibility::Eligible);
    EXPECT_NE(composed.FragmentShaderSource.find(expected.Declarations), std::string::npos);
    EXPECT_NE(composed.FragmentShaderSource.find(expected.Body), std::string::npos);
    EXPECT_NE(composed.FragmentShaderSource.find("binding=15,std140"), std::string::npos);
    EXPECT_EQ(composed.FragmentShaderKey, expected.FragmentKey);
    const auto originalSource = composed.FragmentShaderSource;
    const auto originalKey = composed.FragmentShaderKey;
    effects.ToonStyle.RimStrength = 0.1F;
    const auto& restyled = cache.Resolve(request, effects);
    EXPECT_EQ(restyled.FragmentShaderSource, originalSource);
    EXPECT_EQ(restyled.FragmentShaderKey, originalKey);

    // Eligibility must follow raster state, not the prior cache hit.
    request.Draw.DepthWriteEnabled = false;
    const auto& canvas = cache.Resolve(request, effects);
    EXPECT_EQ(canvas.ToonEligibility, PicaToonEligibility::NoDepth);
    EXPECT_EQ(canvas.FragmentShaderSource, kFragmentShader);
    request.Draw.DepthWriteEnabled = true;
    const auto& geometry = cache.Resolve(request, effects);
    EXPECT_EQ(geometry.ToonEligibility, PicaToonEligibility::Eligible);
    request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Always;
    request.Draw.PerspectiveProjection = false;
    for (auto mode : {ToonMode::PostProcessPreview, ToonMode::PicaMaterial}) {
        effects.Toon = mode;
        const auto& initialization = cache.Resolve(request, effects);
        EXPECT_EQ(initialization.ToonEligibility, PicaToonEligibility::NoDepth);
        EXPECT_EQ(initialization.FragmentShaderSource, kFragmentShader);
        EXPECT_EQ(initialization.FragmentShaderKey, request.FragmentShaderKey);
        request.Draw.PerspectiveProjection = true;
        EXPECT_EQ(cache.Resolve(request, effects).ToonEligibility, PicaToonEligibility::Eligible);
        request.Draw.PerspectiveProjection = false;
    }
}

TEST(Oot3dPicaShaderPipelineCache,
     NativeFidelityRejectsRequestedInstrumentation) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.TemporalMotionEnabled = true;
    request.RequireNativeFidelity = true;
    cache.BeginFrame(12U);

    EXPECT_THROW((void)cache.Resolve(request, effects),
                 std::logic_error);
    const auto stats = cache.Stats();
    EXPECT_EQ(stats.NativeFidelityConflicts, 1U);
    EXPECT_EQ(stats.CanonicalEntries, 1U);
    EXPECT_EQ(stats.InstrumentationEntries, 0U);
}

TEST(Oot3dPicaShaderPipelineCache,
     ReactiveMaskIsPartOfTemporalInstrumentationOnly) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    auto request = BaseRequest();
    request.Draw.Blend = SourceAlphaBlend();
    cache.BeginFrame(15U);

    const auto& canonical = cache.Resolve(request, effects);
    EXPECT_TRUE(canonical.IsCanonical());
    EXPECT_EQ(canonical.FragmentShaderSource.find(
                  "OOT3D reactive world draw"),
              std::string::npos);

    request.TemporalMotionEnabled = true;
    const auto& temporal = cache.Resolve(request, effects);
    EXPECT_TRUE(Fast::Extension::HasPicaShaderInstrumentationFeature(
        temporal.AppliedFeatures,
        Fast::Extension::PicaShaderInstrumentationFeature::ReactiveMask));
    EXPECT_NE(temporal.FragmentShaderSource.find(
                  "OOT3D reactive world draw"),
              std::string::npos);
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedReflectionOwnsMaterialGuideOutsideCanonicalSource) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Reflections = ReflectionMode::HiZ;
    auto request = BaseRequest();
    static constexpr std::string_view kFragment =
        "#version 450\n"
        "layout(location=0) out vec4 pica_color;\n"
        "void main() {\n"
        "    vec4 secondary_fragment_color = vec4(0.25);\n"
        "    vec4 combiner_output = secondary_fragment_color;\n"
        "    pica_color = combiner_output;\n"
        "}\n";
    request.FragmentShaderSource = kFragment;
    request.FragmentShaderHooks =
        AnalyzePicaFragmentShaderHooks(kFragment);
    cache.BeginFrame(16U);

    const auto& reflected = cache.Resolve(request, effects);
    EXPECT_EQ(request.FragmentShaderSource.find("pica_material_guide"),
              std::string::npos);
    EXPECT_EQ(reflected.ReflectionEligibility,
              PicaReflectionMaterialEligibility::Eligible);
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        reflected.AppliedFeatures,
        PicaShaderInstrumentationFeature::ReflectionMaterialGuide));
    EXPECT_NE(reflected.FragmentShaderSource.find(
                  "layout(location=2) out vec4 pica_material_guide"),
              std::string::npos);
    EXPECT_NE(reflected.FragmentShaderSource.find(
                  "OOT3D calibrated specular material"),
              std::string::npos);
}

TEST(Oot3dPicaShaderPipelineCache,
     OpaqueGuideWritersOverwriteTheMaterialGuide) {
    // The material attachment is in every opaque guide writer's write mask;
    // a draw without reflectivity must still overwrite it, or an earlier
    // presented sample's values survive where it draws.
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Reflections = ReflectionMode::HiZ;
    auto request = BaseRequest();
    // A typed fragment whose TEV never reads the specular colour.
    static constexpr std::string_view kFragment =
        "#version 450\n"
        "layout(location=0) out vec4 pica_color;\n"
        "void main() {\n"
        "    vec4 secondary_fragment_color = vec4(0.25);\n"
        "    vec4 combiner_output = vec4(0.5);\n"
        "    pica_color = combiner_output;\n"
        "}\n";
    request.FragmentShaderSource = kFragment;
    request.FragmentShaderHooks = AnalyzePicaFragmentShaderHooks(kFragment);
    cache.BeginFrame(18U);
    const auto& opaque = cache.Resolve(request, effects);
    EXPECT_NE(opaque.ReflectionEligibility, PicaReflectionMaterialEligibility::Eligible);
    EXPECT_NE(opaque.FragmentShaderSource.find("layout(location=2) out vec4 pica_material_guide"),
              std::string::npos);
    EXPECT_NE(opaque.FragmentShaderSource.find("OOT3D material guide baseline"), std::string::npos);
    const uint64_t opaqueKey = opaque.FragmentShaderKey;

    // Nothing to overwrite without depth writes; no material consumer, no output.
    request.Draw.DepthWriteEnabled = false;
    EXPECT_EQ(cache.Resolve(request, effects).FragmentShaderSource.find("pica_material_guide"),
              std::string::npos);
    request.Draw.DepthWriteEnabled = true;
    effects.Reflections = ReflectionMode::Off;
    const auto& plain = cache.Resolve(request, effects);
    EXPECT_EQ(plain.FragmentShaderSource.find("pica_material_guide"), std::string::npos);
    EXPECT_NE(plain.FragmentShaderKey, opaqueKey);
}

TEST(Oot3dPicaShaderPipelineCache,
     ExplicitReflectionProfileDoesNotRequireTevSpecular) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Reflections = ReflectionMode::HiZ;
    auto request = BaseRequest();
    request.ReflectionProfile =
        ReflectionMaterialParameters{0.78F, 0.10F};
    cache.BeginFrame(17U);

    const auto& reflected = cache.Resolve(request, effects);
    EXPECT_EQ(reflected.ReflectionEligibility,
              PicaReflectionMaterialEligibility::
                  ExplicitTextureProfile);
    EXPECT_NE(reflected.FragmentShaderSource.find(
                  "layout(location=2) out vec4 pica_material_guide"),
              std::string::npos);
    EXPECT_NE(reflected.FragmentShaderSource.find(
                  "OOT3D explicit texture material"),
              std::string::npos);
}

TEST(Oot3dPicaShaderPipelineCache,
     CanonicalMetadataClassifiesGrassWithoutShaderMutation) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    cache.BeginFrame(18U);
    auto request = BaseRequest();
    static constexpr std::string_view kGrassFragment =
        "#version 450\n"
        "uniform sampler2D pica_texture0;\n"
        "layout(location=1) in vec2 pica_texcoord0;\n"
        "layout(location=0) out vec4 pica_color;\n"
        "vec4 pica_sample_texture0(vec2 uv) {\n"
        "    return texture(pica_texture0, uv);\n"
        "}\n"
        "void main() {\n"
        "    pica_color = pica_sample_texture0(vec2(\n"
        "        pica_texcoord0.x, 1.0 - pica_texcoord0.y));\n"
        "}\n";
    request.FragmentShaderSource = kGrassFragment;
    request.FragmentShaderKey = 29U;
    request.FragmentShaderHooks =
        Fast::Extension::AnalyzePicaFragmentShaderHooks(kGrassFragment);

    const auto& result = cache.Resolve(request, effects);
    EXPECT_TRUE(result.IsCanonical());
    EXPECT_TRUE(result.GrassTexture0Sampled);
    EXPECT_EQ(result.FragmentShaderSource, kGrassFragment);
    EXPECT_EQ(cache.Stats().InstrumentationEntries, 0U);
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedSamplerUsageIgnoresUncalledTextureHelpers) {
    Fast::Extension::PicaShaderPipelineCache cache;
    Fast::Extension::EffectsSettings effects;
    cache.BeginFrame(19U);
    auto request = BaseRequest();
    static constexpr std::string_view kFragment =
        "#version 450\n"
        "uniform sampler2D pica_texture0;\n"
        "vec4 pica_sample_texture0(vec2 uv) {\n"
        "    return textureLod(pica_texture0, uv, 0.0);\n"
        "}\n"
        "layout(location=0) out vec4 pica_color;\n"
        "void main() { pica_color = vec4(1.0); }\n";
    request.FragmentShaderSource = kFragment;
    request.FragmentShaderKey = 31U;
    request.FragmentShaderHooks.SchemaVersion =
        Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    request.FragmentShaderHooks.SourceSize = kFragment.size();
    request.FragmentShaderHooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::GlobalDeclarations)] = 0U;
    request.FragmentShaderHooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::MainEpilogue)] =
        kFragment.find_last_of('}');
    request.FragmentShaderHooks.Outputs =
        Fast::Extension::AnalyzePicaFragmentOutputContract(kFragment);

    const auto& unused = cache.Resolve(request, effects);
    EXPECT_FALSE(unused.GrassTexture0Sampled);

    request.FragmentShaderKey = 32U;
    request.FragmentShaderHooks.SampledTextureMask = 1U;
    request.FragmentShaderHooks.TextureSamples[0] = {
        0U,
        Oot3d::Renderer::PicaTextureCoordinateOperation::NativeVFlip};
    const auto& sampled = cache.Resolve(request, effects);
    EXPECT_TRUE(sampled.GrassTexture0Sampled);
}

TEST(Oot3dPicaShaderInstrumentation,
     AnalyzesTextureRoutingOnlyFromTheMainProgram) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragment =
        "#version 450\n"
        "uniform sampler2D pica_texture0;\n"
        "vec4 pica_sample_texture0(vec2 uv) {\n"
        "    return textureLod(pica_texture0, uv, 0.0);\n"
        "}\n"
        "layout(location=0) out vec4 pica_color;\n"
        "void main() {\n"
        "    pica_color = pica_sample_texture0(vec2(\n"
        "        pica_texcoord0.x, 1.0 - pica_texcoord0.y));\n"
        "}\n";
    const auto hooks = AnalyzePicaFragmentShaderHooks(kFragment);
    const auto* sample = hooks.TextureSample(0U);
    ASSERT_NE(sample, nullptr);
    EXPECT_TRUE(hooks.SamplesTexture(0U));
    EXPECT_EQ(sample->Coordinate, 0U);
    EXPECT_EQ(sample->Operation,
              Oot3d::Renderer::
                  PicaTextureCoordinateOperation::NativeVFlip);

    static constexpr std::string_view kUnusedHelper =
        "#version 450\n"
        "uniform sampler2D pica_texture0;\n"
        "vec4 pica_sample_texture0(vec2 uv) {\n"
        "    return textureLod(pica_texture0, uv, 0.0);\n"
        "}\n"
        "layout(location=0) out vec4 pica_color;\n"
        "void main() { pica_color = vec4(1.0); }\n";
    const auto unused = AnalyzePicaFragmentShaderHooks(kUnusedHelper);
    EXPECT_FALSE(unused.SamplesTexture(0U));
    EXPECT_EQ(unused.TextureSample(0U), nullptr);
}

TEST(Oot3dPicaShaderInstrumentation,
     OverlaysAttenuateTheGlobalIlluminationCoverage) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
void main() {
    vec3 normal = vec3(0.0, 0.0, 1.0);
    // OOT3D_PICA_MATERIAL_TOON_POINT
    pica_color = vec4(1.0, 1.0, 1.0, 0.5);
}
)glsl";
    const auto hooks = AnalyzePicaFragmentShaderHooks(kFragmentShader);
    PicaFragmentInstrumentationRequest request{
        kFragmentShader,
        92U,
        PicaShaderInstrumentationFeature::SceneDomainGuide,
        {320U, 240U, 0U, true, false, SourceAlphaBlend(), 0x0fU,
         Oot3d::Renderer::PicaCompositionDomain::Scene},
        {false, false, true},
        nullptr,
        &hooks,
    };
    const auto blended = BuildPicaFragmentInstrumentationVariant(request);
    ASSERT_TRUE(blended.Applied());
    EXPECT_TRUE(blended.Outputs.SceneDomainAlbedoOverlay);
    EXPECT_FALSE(blended.Outputs.SceneDomainAmbientOverlay);
    EXPECT_NE(blended.Source.find("layout(location=7) out vec4 pica_diffuse_albedo_guide"), std::string::npos);
    EXPECT_NE(blended.Source.find("pica_diffuse_albedo_guide = vec4(0.0, 0.0, 0.0, pica_color.a)"),
              std::string::npos);

    // An opaque overlay (no depth write) replaces the surface: coverage 0.
    request.Draw.Blend = {};
    const auto opaque = BuildPicaFragmentInstrumentationVariant(request);
    EXPECT_TRUE(opaque.Outputs.SceneDomainAlbedoOverlay);
    EXPECT_NE(opaque.Source.find("pica_diffuse_albedo_guide = vec4(0.0, 0.0, 0.0, 0.0)"), std::string::npos);
    EXPECT_NE(opaque.FragmentKey, blended.FragmentKey);

    // Without GI the overlay leaves the albedo guide alone.
    request.SceneDomainFeatures = {true, false, false};
    request.Draw.Blend = SourceAlphaBlend();
    const auto ambientOnly = BuildPicaFragmentInstrumentationVariant(request);
    EXPECT_FALSE(ambientOnly.Outputs.SceneDomainAlbedoOverlay);
    EXPECT_EQ(ambientOnly.Source.find("pica_diffuse_albedo_guide"), std::string::npos);
}

TEST(Oot3dPicaShaderInstrumentation,
     PublishesTypedSceneOverlayOutputs) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 pica_normal_guide;
layout(location=4) out vec4 pica_ambient_guide;
void main() {
    vec3 normal = vec3(0.0, 0.0, 1.0);
    // OOT3D_PICA_MATERIAL_TOON_POINT
    pica_color = vec4(1.0, 1.0, 1.0, 0.5);
    pica_normal_guide = vec4(normal, 1.0);
    pica_ambient_guide = vec4(1.0);
}
)glsl";
    const auto hooks = AnalyzePicaFragmentShaderHooks(kFragmentShader);
    const auto result = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        90U,
        PicaShaderInstrumentationFeature::SceneDomainGuide,
        {320U, 240U, 0U, true, false, SourceAlphaBlend(), 0x0fU,
         Oot3d::Renderer::PicaCompositionDomain::Scene},
        {true, true},
        nullptr,
        &hooks,
    });

    ASSERT_TRUE(result.Applied());
    EXPECT_EQ(result.SceneDomainEligibility,
              PicaSceneDomainGuideEligibility::AppliedBlendedOverlay);
    EXPECT_TRUE(result.Outputs.SceneDomainBlendedOverlay);
    EXPECT_TRUE(result.Outputs.SceneDomainNormalOverlay);
    EXPECT_TRUE(result.Outputs.SceneDomainAmbientOverlay);
    EXPECT_TRUE(result.Outputs.SceneDomainTransparentDepthOverlay);
    EXPECT_TRUE(result.Outputs.WritesAmbientGuide);
    EXPECT_NE(result.Source.find("pica_transparent_depth_guide"),
              std::string::npos);

    const auto ui = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        91U,
        PicaShaderInstrumentationFeature::SceneDomainGuide,
        {400U, 240U, 0U, true, false, SourceAlphaBlend(), 0x0fU,
         Oot3d::Renderer::PicaCompositionDomain::Ui},
        {true, true},
        nullptr,
        &hooks,
    });
    EXPECT_FALSE(ui.Applied());
    EXPECT_EQ(ui.SceneDomainEligibility,
              PicaSceneDomainGuideEligibility::OutsideScene);
}

TEST(Oot3dPicaShaderInstrumentation,
     SharesRigidMotionOutputWithTransparentDepthGuide) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 pica_normal_guide;
void main() {
    vec3 normal = vec3(0.0, 0.0, 1.0);
    // OOT3D_PICA_MATERIAL_TOON_POINT
    pica_color = vec4(1.0, 1.0, 1.0, 0.5);
    pica_normal_guide = vec4(normal, 1.0);
}
)glsl";
    const auto hooks = AnalyzePicaFragmentShaderHooks(kFragmentShader);
    const auto result = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        92U,
        PicaShaderInstrumentationFeature::SceneDomainGuide |
            PicaShaderInstrumentationFeature::RigidMotionGuide,
        {320U, 240U, 0U, true, false, SourceAlphaBlend(), 0x0fU,
         Oot3d::Renderer::PicaCompositionDomain::Scene},
        {false, true},
        nullptr,
        &hooks,
    });

    ASSERT_TRUE(result.Applied());
    EXPECT_TRUE(result.RigidMotionApplied);
    EXPECT_TRUE(result.Outputs.SceneDomainTransparentDepthOverlay);
    EXPECT_EQ(result.Source.find("pica_transparent_depth_guide"),
              std::string::npos);
    const size_t firstLocation =
        result.Source.find("layout(location=3) out vec4");
    ASSERT_NE(firstLocation, std::string::npos);
    EXPECT_EQ(result.Source.find("layout(location=3) out vec4",
                                 firstLocation + 1U),
              std::string::npos);
    const size_t motionWrite = result.Source.find(
        "pica_rigid_motion_guide = oot3d_motion");
    const size_t depthWrite = result.Source.find(
        "pica_rigid_motion_guide.a = pica_color.a");
    ASSERT_NE(motionWrite, std::string::npos);
    ASSERT_NE(depthWrite, std::string::npos);
    EXPECT_LT(motionWrite, depthWrite);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        result.Source, shaderc_fragment_shader,
        "rigid_motion_transparent_depth.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
#endif
}

TEST(Oot3dPicaShaderInstrumentation, NativeOutlineCoverageFollowsBlendAndWindowDepth) {
    using namespace Fast::Extension;
    for (bool explicitDepth : {false, true}) {
        const std::string source = std::string(
            "#version 450\nlayout(location=5) in vec4 pica_normquat;\n"
            "layout(location=0) out vec4 pica_color;\n"
            "void main() { pica_color=vec4(0.5);\n") +
            (explicitDepth ? "gl_FragDepth=0.625;\n" : "") + "}\n";
        PicaFragmentInstrumentationRequest request;
        request.Source = source;
        request.RequestedFeatures = PicaShaderInstrumentationFeature::NormalGuide |
            PicaShaderInstrumentationFeature::OutlineGeometryGuide |
            PicaShaderInstrumentationFeature::SceneDomainGuide;
        request.SceneDomainFeatures = {false, true};
        request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
        request.Draw.DepthTestEnabled = true;
        request.Draw.DepthWriteEnabled = true;
        request.Draw.ColorWriteMask = 15U;
        const auto opaque = BuildPicaFragmentInstrumentationVariant(request);
        EXPECT_TRUE(opaque.Outputs.WritesOutlineGeometryGuide);
        EXPECT_FALSE(opaque.Outputs.SceneDomainTransparentDepthOverlay);
        EXPECT_NE(opaque.Source.find("pica_transparent_depth_guide = vec4(0.0)"), std::string::npos);

        // Alpha compositing remains transparent even when it writes native depth.
        request.Draw.Blend = SourceAlphaBlend();
        const auto transparent = BuildPicaFragmentInstrumentationVariant(request);
        EXPECT_FALSE(transparent.Outputs.WritesOutlineGeometryGuide);
        EXPECT_TRUE(transparent.Outputs.SceneDomainTransparentDepthOverlay);
        EXPECT_NE(transparent.FragmentKey, opaque.FragmentKey);
        EXPECT_NE(transparent.Source.find(explicitDepth ? "clamp(1.0 - gl_FragDepth" :
            "clamp(1.0 - gl_FragCoord.z"), std::string::npos);
        request.Draw.DepthTestEnabled = false;
        const auto screen = BuildPicaFragmentInstrumentationVariant(request);
        EXPECT_FALSE(screen.Outputs.SceneDomainTransparentDepthOverlay);
        request.Draw.DepthTestEnabled = true;
        request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Always;
        const auto canvas = BuildPicaFragmentInstrumentationVariant(request);
        EXPECT_FALSE(canvas.Outputs.SceneDomainTransparentDepthOverlay);
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
        shaderc::Compiler compiler;
        for (const auto* result : {&opaque, &transparent, &screen, &canvas}) {
            const auto compiled = compiler.CompileGlslToSpv(result->Source, shaderc_fragment_shader,
                "native_outline_coverage.frag");
            EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success)
                << compiled.GetErrorMessage();
        }
#endif
    }
}

TEST(Oot3dPicaShaderPipelineCache,
     UiDomainCannotRequestSceneExtensionInstrumentation) {
    using namespace Fast::Extension;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Toon = ToonMode::PicaMaterial;
    effects.ToonStyle.OutlineEnabled = true;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    effects.Reflections = ReflectionMode::FidelityFxSssr;
    auto request = BaseRequest();
    request.Draw.CompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Ui;
    request.Draw.DirectionalShadowReceiver = true;
    request.TemporalMotionEnabled = true;

    cache.BeginFrame(17U);
    const auto& result = cache.Resolve(request, effects);
    EXPECT_TRUE(result.IsCanonical());
    EXPECT_EQ(result.RequestedFeatures,
              PicaShaderInstrumentationFeature::None);
    EXPECT_EQ(result.ToonEligibility, PicaToonEligibility::Disabled);
    EXPECT_EQ(result.SceneDomainEligibility,
              PicaSceneDomainGuideEligibility::Disabled);
    EXPECT_EQ(result.ReflectionEligibility,
              PicaReflectionMaterialEligibility::Disabled);
}

TEST(Oot3dPicaShaderInstrumentation,
     ResolvesStablePrewarmProfileFeatures) {
    using namespace Fast::Extension;
    EffectsSettings effects;
    effects.Toon = ToonMode::PicaMaterial;
    effects.ToonStyle.OutlineEnabled = true;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    effects.Reflections = ReflectionMode::FidelityFxSssr;
    effects.SurfaceLighting = SurfaceLightingMode::PerPixel;
    const auto features = ResolvePicaShaderProfileFeatures(
        effects, true, true);

    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features, PicaShaderInstrumentationFeature::Toon));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features, PicaShaderInstrumentationFeature::TemporalVertex));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features,
        PicaShaderInstrumentationFeature::AmbientOcclusionGuide));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features, PicaShaderInstrumentationFeature::SceneDomainGuide));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features,
        PicaShaderInstrumentationFeature::ReflectionMaterialGuide));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features, PicaShaderInstrumentationFeature::ReactiveMask));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features, PicaShaderInstrumentationFeature::RigidMotionGuide));
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        features, PicaShaderInstrumentationFeature::NormalGuide));
}

TEST(Oot3dPicaShaderInstrumentation,
     MainEpilogueHookIgnoresLaterFunctionsAndNestedBlocks) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
void main() {
    if (true) { pica_color = vec4(1.0); }
}
void helper() { }
)glsl";
    const auto hooks = AnalyzePicaFragmentShaderHooks(kFragmentShader);
    ASSERT_TRUE(hooks.Valid());
    const size_t helper = kFragmentShader.find("void helper");
    ASSERT_NE(helper, std::string_view::npos);
    EXPECT_LT(hooks.Offset(PicaShaderHook::MainEpilogue), helper);

    const auto composed = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        73U,
        PicaShaderInstrumentationFeature::AmbientOcclusionGuide,
        {},
        {},
        nullptr,
    });
    ASSERT_TRUE(composed.Applied());
    const size_t assignment = composed.Source.find(
        ": vec4(1.0, 1.0, 1.0, 1.0);");
    const size_t composedHelper = composed.Source.find("void helper");
    ASSERT_NE(assignment, std::string::npos);
    ASSERT_NE(composedHelper, std::string::npos);
    EXPECT_LT(assignment, composedHelper);
}

TEST(Oot3dPicaShaderPipelineCache, AtmosphereDoesNotInheritWorldRelightingFromSharedPrograms) {
    using namespace Fast::Extension;
    using namespace Fast::Renderer3ds;
    auto request = BaseRequest();
    request.SurfaceLightingAvailable = true;
    request.Draw.DirectionalShadowReceiver = true;
    request.Pbr.TextureSlot = 0;
    auto& surface = request.TemporalVertexProgram.Hooks.Surface;
    surface.ViewNormalTemporary = 14;
    surface.LightingBooleanUniform = 9;
    surface.DiffuseUniform = 8;
    surface.AmbientUniform = 9;
    surface.TintBooleanUniform = 5;
    surface.TintInputRegister = 2;
    surface.TintScale = {90, 2};
    surface.LightEnabledValue = {93, 1};
    surface.LightDirectionUniforms = {80, 83, 86};
    request.FragmentShaderHooks.Offsets[static_cast<size_t>(PicaShaderHook::PicaLighting)] = 0;
    request.FragmentShaderHooks.Offsets[static_cast<size_t>(PicaShaderHook::AfterNativeTev)] = 0;
    request.FragmentShaderHooks.Semantics |= PicaShaderSemantic::PrimaryColorInput | PicaShaderSemantic::ViewVector;
    EffectsSettings effects;
    effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;
    const auto relights = [&] {
        return HasPicaShaderInstrumentationFeature(ResolvePicaDrawInstrumentationFeatures(request, effects),
                                                   PicaShaderInstrumentationFeature::NativePerPixelLighting);
    };
    ASSERT_TRUE(relights());
    request.Draw.CompositionLayer = PicaCompositionLayer::Atmosphere;
    const auto atmosphere = ResolvePicaDrawInstrumentationFeatures(request, effects);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(atmosphere, PicaShaderInstrumentationFeature::NativePerPixelLighting));
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(atmosphere, PicaShaderInstrumentationFeature::PbrMaterial));
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(atmosphere, PicaShaderInstrumentationFeature::DirectionalShadowLighting));
    request.Draw.CompositionLayer = PicaCompositionLayer::TransparentWorld;
    EXPECT_TRUE(relights());
    request.Draw.CompositionLayer = PicaCompositionLayer::Unknown;
    EXPECT_FALSE(relights());
}

TEST(Oot3dPicaShaderInstrumentation,
     DirectionalShadowLightingCannotModifyBakedLighting) {
    using namespace Fast::Extension;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=5) in vec4 pica_normquat;
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 unrelated_output;
void main() { pica_color = vec4(1.0); }
)glsl";
    const auto composed = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        79U,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting,
        {},
        {},
        nullptr,
    });
    EXPECT_FALSE(composed.Applied());
    EXPECT_TRUE(composed.Source.empty());
    EXPECT_EQ(composed.FragmentKey, 79U);
    EXPECT_EQ(composed.DirectionalShadowEligibility,
              PicaDirectionalShadowLightingEligibility::Disabled);
}



TEST(Oot3dPicaShaderInstrumentation, SurfaceShadowsExcludePointsAndPreserveSelectedLightSlots) {
    using namespace Fast::Extension;
    auto view = IdentityDirectionalShadowMatrix();
    view[0] = view[5] = 0.0F;
    view[1] = 1.0F;
    view[4] = -1.0F;
    PicaDirectionalShadowHistoryState history;
    history.ProducedFrameId = 1U;
    history.Virtual = Fast::Renderer3ds::BuildVirtualShadowLayout({}, {0, 1, 0}, 2048, 65536);
    history.WorldLightDirectionTowardSource = {0.0F, 1.0F, 0.0F};
    history.NormalBias = 1.5F;
    std::array<PicaSurfaceShadowLight, 4> lights{{
        {{1.0F, 0.0F, 0.0F}, 0U, true, true},
        {{1.0F, 0.0F, 0.0F}, 2U, true, false},
        {{1.0F, 0.0F, 0.0F}, 5U, false, false},
        {{0.0F, 1.0F, 0.0F}, 7U, true, false},
    }};
    const auto binding = BuildPicaSurfaceShadowReceiverBinding(view, lights, history);
    ASSERT_TRUE(binding.Bound());
    EXPECT_EQ(binding.Uniforms.Receiver[3], 4.0F);
    // The smooth receivers' normal bias.
    EXPECT_EQ(binding.Uniforms.Receiver[0], 1.5F);
    lights[1].Enabled = false;
    EXPECT_FALSE(BuildPicaSurfaceShadowReceiverBinding(view, lights, history).Bound());
}

TEST(Oot3dPicaShaderInstrumentation, AuthoredLightStaysWorldFixedWithCameraFoldedIntoBones) {
    using namespace Fast::Renderer3ds;
    auto hooks = ShadowCasterHooks();
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4,
                       0, 4, 4, 3, 20, 3, 0, 1};
    hooks.Surface.ViewNormalTemporary = 14;
    hooks.Surface.LightingBooleanUniform = 9;
    hooks.Surface.DiffuseUniform = 8;
    hooks.Surface.AmbientUniform = 9;
    hooks.Surface.TintBooleanUniform = 5;
    hooks.Surface.TintInputRegister = 2;
    hooks.Surface.TintScale = {90, 2};
    hooks.Surface.LightEnabledValue = {93, 1};
    hooks.Surface.LightDirectionUniforms = {80, 83, 86};
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[4][0] = uniforms[5][1] = uniforms[6][2] = 1.0F;
    uniforms[93][1] = 1.0F;
    const std::span<const uint8_t> bytes{
        reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms)};
    PicaEnvironmentLighting environment;
    environment.Enabled = true;
    environment.Lights[0].DirectionTowardSource = {1, 0, 0};
    environment.Lights[0].Intensity = 1;
    // The native post-transform stays identity while the camera is baked into
    // the model/bone palette. A +X world-facing surface must remain fully lit.
    for (float yaw : {0.0F, 1.57079632679F, 3.14159265359F}) {
        auto camera = Fast::Extension::IdentityDirectionalShadowMatrix();
        const float c = std::cos(yaw), s = std::sin(yaw);
        camera[0] = camera[10] = c;
        camera[2] = -s;
        camera[8] = s;
        const auto view = BuildPicaSurfaceLightingView(camera);
        ASSERT_TRUE(view);
        const auto light = BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, environment, &*view);
        ASSERT_TRUE(light);
        const auto& direction = light->EnvironmentDirections[0];
        const float diffuse = c * direction[0] - s * direction[2];
        EXPECT_NEAR(diffuse, 1.0F, 1.0e-6F);
        // Shadow matching must recover the same world-space key, not a
        // camera-relative direction that changes with the actor's view.
        Fast::Extension::DirectionalShadowMatrix inverse{};
        ASSERT_TRUE(Fast::Extension::InvertDirectionalShadowMatrix(camera, inverse));
        const std::array<Fast::Extension::PicaSurfaceShadowLight, 1> sources{{
            {{direction[0], direction[1], direction[2]}, 8, true, false}}};
        Fast::Extension::PicaDirectionalShadowHistoryState history;
        history.ProducedFrameId = 1;
        history.Virtual = Fast::Renderer3ds::BuildVirtualShadowLayout({}, {1, 0, 0}, 2048, 65536);
        history.WorldLightDirectionTowardSource = {1, 0, 0};
        EXPECT_TRUE(Fast::Extension::BuildPicaSurfaceShadowReceiverBinding(inverse, sources, history).Bound());
    }
    EXPECT_FALSE(BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, environment, nullptr));
    environment.Enabled = false;
    EXPECT_TRUE(BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, environment, nullptr));
}

// A four-light vertex program (MM3D's GzwShader) supplies the normal and
// material without its vertex lights: a draw lit per vertex keeps its native
// lighting, and a scene light the draw's native fragment lights already
// apply is not added again (its shadow applies to the native light).
TEST(Oot3dPicaShaderInstrumentation, FourLightSurfaceKeepsNativeLightsAndLightsFragmentDrawsOnce) {
    using namespace Fast::Renderer3ds;
    auto hooks = ShadowCasterHooks();
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4,
                       0, 4, 4, 3, 20, 3, 0, 1};
    hooks.Surface.ViewNormalTemporary = 14;
    hooks.Surface.LightingBooleanUniform = 9;
    hooks.Surface.DiffuseUniform = 8;
    hooks.Surface.AmbientUniform = 9;
    hooks.Surface.TintBooleanUniform = 5;
    hooks.Surface.TintInputRegister = 3;
    hooks.Surface.TintScale = {90, 2};
    EXPECT_FALSE(hooks.Surface.Available());
    hooks.Surface.VertexLightsReconstructed = false;
    ASSERT_TRUE(hooks.Surface.Available());
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[8] = {1.0F, 1.0F, 1.0F, 1.0F};
    const std::span<const uint8_t> bytes{
        reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms)};
    PicaNativeLightBinding binding;
    PicaSceneDirectionalLights directionals;
    directionals.Lights[0] = {1, {1.0F, 0.0F, 0.0F}, {0.8F, 0.7F, 0.6F}};
    directionals.Lights[1] = {2, {0.0F, 1.0F, 0.0F}, {0.3F, 0.3F, 0.3F}};
    directionals.Count = 2;
    binding.SceneDirectionals = std::make_shared<const PicaSceneDirectionalLights>(directionals);
    const auto view = BuildPicaSurfaceLightingView(Fast::Extension::IdentityDirectionalShadowMatrix());
    ASSERT_TRUE(view);
    PicaEnvironmentLighting environment;
    EXPECT_FALSE(BuildPicaSurfaceLightingUniforms(hooks, 1U << 9, bytes, binding, environment, &*view))
        << "a draw lit per vertex by four lights must stay native";
    auto light = BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, binding, environment, &*view);
    ASSERT_TRUE(light);
    EXPECT_EQ(light->Control[0], 0U);
    ASSERT_EQ(light->Control[2], 2U);
    // The native fragment light toward +X (half-float, not unit length).
    const std::array<std::array<float, 3>, 1> native{{{0.937F, 0.004F, -0.01F}}};
    EXPECT_EQ(ExcludePicaSceneDirectionalsLitNatively(*light, native), 1U);
    ASSERT_EQ(light->Control[2], 1U);
    EXPECT_FLOAT_EQ(light->SceneDirections[0][1], 1.0F);
    EXPECT_FLOAT_EQ(light->SceneDirectionalColors[0][0], 0.3F);
    EXPECT_EQ(light->SceneDirections[1], (std::array<float, 4>{}));
    // The shadow block is the adapter's: the legacy body has none.
    EXPECT_NE(std::string(PicaSurfaceLightingBody(false, false, true)).find(
                  "fragment_uniforms.lighting_diffuse[pica_light]"), std::string::npos);
    EXPECT_EQ(std::string(PicaSurfaceLightingBody()).find("fragment_uniforms.lighting_diffuse[pica_light]"),
              std::string::npos);
    EXPECT_EQ(PicaSurfaceLightingBody(), PicaSurfaceLightingBody(false, false, false));
}

TEST(Oot3dPicaShaderInstrumentation, AuthoredLightingPreservesNativeMaterialReflectance) {
    using namespace Fast::Renderer3ds;
    auto hooks = ShadowCasterHooks();
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4,
                       0, 4, 4, 3, 20, 3, 0, 1};
    hooks.Surface.ViewNormalTemporary = 14;
    hooks.Surface.LightingBooleanUniform = 9;
    hooks.Surface.DiffuseUniform = 8;
    hooks.Surface.AmbientUniform = 9;
    hooks.Surface.TintBooleanUniform = 5;
    hooks.Surface.TintInputRegister = 2;
    hooks.Surface.TintScale = {90, 2};
    hooks.Surface.LightEnabledValue = {93, 1};
    hooks.Surface.LightDirectionUniforms = {80, 83, 86};
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[8] = {0.5F, 0.25F, 0.0F, 1.0F};
    uniforms[9] = {0.4F, 0.4F, 0.4F, 0.0F};
    uniforms[93][1] = 1.0F;
    const std::span<const uint8_t> bytes{
        reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms)};
    PicaEnvironmentLighting environment;
    environment.Enabled = true;
    environment.AmbientIntensity = 0.2F;
    environment.Lights[0].DirectionTowardSource = {1, 0, 0};
    environment.Lights[0].Color = {1.0F, 0.8F, 0.6F};
    environment.Lights[0].Intensity = 1.0F;
    const auto view = BuildPicaSurfaceLightingView(Fast::Extension::IdentityDirectionalShadowMatrix());
    ASSERT_TRUE(view);
    const auto evaluate = [&] {
        const auto light = BuildPicaSurfaceLightingUniforms(hooks, 512U, bytes, {}, environment, &*view);
        EXPECT_TRUE(light);
        std::array<float, 3> color{};
        if (light) {
            for (size_t channel = 0; channel < color.size(); ++channel)
                // Facing the key, texture 0.25, native TEV gain 2.
                color[channel] = 0.5F * (light->EnvironmentColors[0][channel] *
                    light->EnvironmentDirections[0][3] + light->EnvironmentAmbient[channel]);
        }
        return color;
    };
    const auto lit = evaluate();
    EXPECT_NEAR(lit[0], 0.29F, 1.0e-6F);
    EXPECT_NEAR(lit[1], 0.14F, 1.0e-6F);
    EXPECT_NEAR(lit[2], 0.04F, 1.0e-6F); // A black diffuse channel must not become white.
    uniforms[8] = {};
    uniforms[9] = {1.0F, 1.0F, 1.0F, 0.0F};
    const auto ambientOnly = evaluate();
    EXPECT_NEAR(ambientOnly[0], 0.6F, 1.0e-6F);
    EXPECT_NEAR(ambientOnly[1], 0.5F, 1.0e-6F);
    EXPECT_NEAR(ambientOnly[2], 0.4F, 1.0e-6F);
    uniforms[9] = {};
    EXPECT_EQ(evaluate(), (std::array<float, 3>{}));
}


TEST(Oot3dPicaShaderInstrumentation, FragmentLambertKeepsPrimaryMaterialAndPlacedCount) {
    using namespace Fast::Renderer3ds;
    auto hooks = ShadowCasterHooks();
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4, 0, 4, 4, 3, 20, 3, 0, 1};
    auto& s = hooks.Surface;
    s.ViewNormalTemporary = 14; s.LightingBooleanUniform = 9;
    s.DiffuseUniform = 8; s.AmbientUniform = 9;
    s.TintBooleanUniform = 5; s.TintInputRegister = 3; s.TintScale = {90, 2};
    s.VertexLightsReconstructed = false;
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[8] = {0.5F, 0.25F, 0.0F, 1.0F};
    uniforms[9] = {0.25F, 0.25F, 0.25F, 0.0F};
    const std::span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms)};
    const auto view = BuildPicaSurfaceLightingView(Fast::Extension::IdentityDirectionalShadowMatrix());
    ASSERT_TRUE(view);
    PicaEnvironmentLighting e;
    e.Enabled = true; e.PreserveSceneBrightness = true;
    e.AmbientIntensity = 0.2F;
    e.PlacedLightCount = 1;
    e.PlacedLights[0].Kind = PicaPlacedLightKind::Point;
    e.PlacedLights[0].Reach = 300.0F;
    e.PlacedLights[0].Color = {0.8F, 0.4F, 0.2F};
    e.PlacedLights[0].PositionWorld = {-280.0F, 170.0F, -1630.0F};
    auto lambert = BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, e, &*view, nullptr, true);
    ASSERT_TRUE(lambert);
    EXPECT_EQ(lambert->Control[3], 0x103U); // count survives both low flags
    EXPECT_FLOAT_EQ(lambert->Placed[0].Color[0], 0.8F);
    EXPECT_FLOAT_EQ(lambert->Placed[0].Color[2], 0.2F); // primary owns black material channel
    EXPECT_FLOAT_EQ(lambert->Placed[0].PositionReach[1], 170.0F);
    EXPECT_FLOAT_EQ(lambert->Placed[0].AxisKind[3], 1.0F);
    EXPECT_FALSE(BuildPicaSurfaceLightingUniforms(hooks, 1U << 9, bytes, {}, e, &*view, nullptr, true));
    auto pbr = BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, e, &*view);
    ASSERT_TRUE(pbr);
    EXPECT_EQ(pbr->Control[3], 0x101U);
    EXPECT_FLOAT_EQ(pbr->Placed[0].Color[0], 0.4F);
    EXPECT_FLOAT_EQ(pbr->Placed[0].Color[2], 0.0F);
    FoldPicaSurfaceMaterialForPbr(*lambert);
    EXPECT_EQ(lambert->Control, pbr->Control);
    EXPECT_EQ(lambert->EnvironmentAmbient, pbr->EnvironmentAmbient);
    EXPECT_EQ(lambert->ResolvedAmbient, pbr->ResolvedAmbient);
    EXPECT_EQ(lambert->EnvironmentColors, pbr->EnvironmentColors);
    EXPECT_EQ(lambert->Placed[0].Color, pbr->Placed[0].Color);
    FoldPicaSurfaceMaterialForPbr(*lambert); // idempotent, no second material multiplication
    EXPECT_EQ(lambert->Placed[0].Color, pbr->Placed[0].Color);
    e.Enabled = false;
    auto off = BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, e, &*view, nullptr, true);
    ASSERT_TRUE(off);
    EXPECT_EQ(off->Control[3] >> 8U, 0U);
    EXPECT_EQ(off->Placed[0].Color, (std::array<float, 4>{}));
    e.Enabled = true;
    EXPECT_FALSE(BuildPicaSurfaceLightingUniforms(hooks, 0, bytes, {}, e, nullptr, nullptr, true));
}


TEST(Oot3dPicaShaderInstrumentation, MixedWaterBoundsKeepDryFragmentIrradiance) {
    using namespace Fast::Renderer3ds;
    // Captured MM room-decal bounds: one draw contains separated wall/floor
    // pieces. Its AABB reaches water, although its wall fragments are dry.
    const std::array<float, 3> minimum{-963.284F, -18.10252F, -2039.396F};
    const std::array<float, 3> maximum{487.968F, 995.0F, -264.2556F};
    auto water = std::make_shared<PicaSceneWaterBodies>();
    water->Count = 2U;
    water->Bodies[0] = {{-924.0F, -1000.0F}, {-764.0F, -480.0F}, -20.0F, 257U};
    water->Bodies[1] = {{207.0F, -1000.0F}, {367.0F, -480.0F}, -20.0F, 257U};
    EXPECT_TRUE(PicaWaterSurfaceReaches(minimum, maximum, water->Bodies[0]));
    EXPECT_TRUE(PicaWaterSurfaceReaches(minimum, maximum, water->Bodies[1]));
    EXPECT_FALSE(MatchPicaWaterSurface(minimum, maximum, *water));
    const bool operand = PicaNativeFragmentOperandWaterEligible(true, water->Count, true, true, false);
    ASSERT_TRUE(operand); // Previously false: routed the whole draw to primary-colour replacement.

    auto hooks = ShadowCasterHooks();
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4, 0, 4, 4, 3, 20, 3, 0, 1};
    auto& surface = hooks.Surface;
    surface.ViewNormalTemporary = 14; surface.LightingBooleanUniform = 9;
    surface.DiffuseUniform = 8; surface.AmbientUniform = 9;
    surface.TintBooleanUniform = 5; surface.TintInputRegister = 3; surface.TintScale = {90, 2};
    surface.VertexLightsReconstructed = false;
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[8] = {0.498F, 0.498F, 0.498F, 1.0F};
    uniforms[9] = {0.498F, 0.498F, 0.498F, 1.0F};
    const std::span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms)};
    const auto view = BuildPicaSurfaceLightingView(Fast::Extension::IdentityDirectionalShadowMatrix());
    ASSERT_TRUE(view);
    PicaNativeLightBinding binding; binding.Water = water;
    PicaEnvironmentLighting environment;
    environment.Enabled = true; environment.PreserveSceneBrightness = false;
    environment.NativeAmbientStrength = 0.0F; environment.NativeLocalStrength = 0.0F;
    environment.NativeDirectStrength = 0.0F; environment.AmbientIntensity = 0.5F;
    environment.AmbientColor = {1.0F, 1.0F, 1.0F};
    auto resolved = BuildPicaSurfaceLightingUniforms(hooks, 0U, bytes, binding, environment, &*view, nullptr, operand);
    ASSERT_TRUE(resolved);
    AssignPicaSurfaceWater(*resolved, *view, *water, minimum, maximum, 0.0F, false);
    EXPECT_EQ(resolved->Control[0], 0U);
    EXPECT_EQ(resolved->Control[3] & 2U, 2U); // Native primary still owns the decal material.
    EXPECT_FLOAT_EQ(resolved->ResolvedAmbient[0], 0.5F); // Irradiance, not 0.498 * irradiance.
    EXPECT_FLOAT_EQ(resolved->WaterViewTranslation[3], 2.0F); // Both footprints reach the fragment classifier.
    EXPECT_FLOAT_EQ(resolved->WaterHeights[0], -20.0F);
    EXPECT_FLOAT_EQ(resolved->WaterHeights[1], -20.0F);
    const auto body = PicaSurfaceLightingBody(true);
    // These existing shader guards prevent native-off and added irradiance
    // from reaching wet fragments, while dry fragments keep native primary.
    EXPECT_NE(body.find("(pica_surface.control.w & 2u) != 0u && !surface_water"), std::string::npos);
    EXPECT_NE(body.find("if (!surface_fragment_operand &&"), std::string::npos);
    EXPECT_NE(body.find("if (surface_fragment_operand) {"), std::string::npos);
    EXPECT_FALSE(PicaNativeFragmentOperandWaterEligible(true, 2U, false, false, false));
    EXPECT_FALSE(PicaNativeFragmentOperandWaterEligible(true, 2U, true, true, true));
    EXPECT_FALSE(PicaNativeFragmentOperandWaterEligible(true, kMaximumPicaSurfaceWaterBodies + 1U, true, true, false));
    EXPECT_FALSE(PicaNativeFragmentOperandWaterEligible(false, 2U, true, true, false));
}



TEST(PicaFragmentBalance, SourceIdentityNotPermutationOrDirectionOwnsTheKey) {
    using namespace Fast::Renderer3ds;
    PicaFragmentLightingLayout layout;
    layout.SchemaVersion=kPicaFragmentLightingLayoutSchemaVersion;
    layout.ActiveLightCount=2;layout.LightPermutation={0,1};
    layout.Lights[0].Directional=layout.Lights[1].Directional=true;
    auto scene=std::make_shared<PicaSceneDirectionalLights>();
    scene->Count=2;scene->ShadowCasterIdentity=134537748;
    scene->Lights[0]={134537748,{-0.265F,0.907F,0.151F},{220/255.F,195/255.F,180/255.F},true};
    scene->Lights[1]={134537772,{0.265F,-0.907F,-0.151F},{20/255.F,45/255.F,72/255.F},true};
    PicaNativeLightBinding binding;binding.SceneDirectionals=scene;
    binding.Sources[0].Kind=binding.Sources[1].Kind=PicaNativeLightSource::Type::Directional;
    binding.Sources[0].SourceIdentity=134537772;binding.Sources[1].SourceIdentity=134537748;
    PicaEnvironmentLighting e;e.Enabled=true;e.KeyDirection=PicaEnvironmentKeyDirection::SceneMain;
    e.NativeDirectStrength=0;e.NativeLocalStrength=1;e.NativeAmbientStrength=.6F;
    e.AmbientIntensity=0;e.Lights[0].Intensity=1;
    const auto a=BuildPicaFragmentBalanceUniforms(true,layout,binding,e);ASSERT_TRUE(a);
    EXPECT_EQ(a->Control[1],1U);EXPECT_EQ(a->Control[2],1U);
    EXPECT_FLOAT_EQ(a->Gains[0][0],1); // Retain native opposite fill, even below horizon.
    EXPECT_FLOAT_EQ(a->Gains[1][0],0); // No duplicate main direct.
    EXPECT_FLOAT_EQ(a->Parameters[0],1); // Actual OoT preserve-ambient policy.
    layout.LightPermutation={1,0};
    const auto b=BuildPicaFragmentBalanceUniforms(true,layout,binding,e);ASSERT_TRUE(b);
    EXPECT_EQ(a->Control,b->Control);EXPECT_EQ(a->Gains,b->Gains);
    EXPECT_FALSE(BuildPicaFragmentBalanceUniforms(false,layout,binding,e));
    e.PreserveSceneBrightness=false;
    const auto fractional=BuildPicaFragmentBalanceUniforms(true,layout,binding,e);ASSERT_TRUE(fractional);
    EXPECT_FLOAT_EQ(fractional->Parameters[0],.6F);
    EXPECT_FLOAT_EQ(fractional->Gains[0][0],0);
    EXPECT_FLOAT_EQ(fractional->Gains[0][2],.6F);
    e.PreserveSceneBrightness=true;
    binding.Sources[0].SourceIdentity=134537748;
    EXPECT_FALSE(BuildPicaFragmentBalanceUniforms(true,layout,binding,e));
    binding.Sources[0].SourceIdentity=134537772;
    layout.LightPermutation={1,1};EXPECT_FALSE(BuildPicaFragmentBalanceUniforms(true,layout,binding,e));
    layout.LightPermutation={1,0};binding.Sources[1]={};
    EXPECT_FALSE(BuildPicaFragmentBalanceUniforms(true,layout,binding,e));
    binding.Sources[1].Kind=PicaNativeLightSource::Type::Point;
    binding.Sources[1].SourceIdentity=134537748;
    EXPECT_FALSE(BuildPicaFragmentBalanceUniforms(true,layout,binding,e));
}

TEST(PicaFragmentBalance, ExplicitProfileAndFractionalAmbientEndpoints) {
    using namespace Fast::Renderer3ds;
    PicaEnvironmentLighting e;e.Enabled=true;e.KeyDirection=PicaEnvironmentKeyDirection::SceneMain;
    e.AmbientIntensity=0;e.Lights[0].Intensity=1;
    ASSERT_TRUE(PicaFragmentBalanceProfileSupported(e));
    for(float fraction:{0.F,.25F,.6F,1.F}) {
        e.NativeAmbientStrength=fraction;e.NativeLocalStrength=fraction;
        EXPECT_TRUE(PicaFragmentBalanceProfileSupported(e));
    }
    const auto good=e;
    e.NativeDirectStrength=1;EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));e=good;
    e.AmbientIntensity=.5F;EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));e=good;
    e.Lights[0].Intensity=2;EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));e=good;
    e.Lights[0].Color[0]=.9F;EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));e=good;
    e.KeyElevationOffset=.01F;EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));e=good;
    e.NativeAmbientStrength=std::nanf("");EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));e=good;
    e.Enabled=false;EXPECT_FALSE(PicaFragmentBalanceProfileSupported(e));
}

TEST(PicaFragmentBalance, LegacyDeclarationsAndWaterBodyAreUnchanged) {
    using namespace Fast::Renderer3ds;
    EXPECT_EQ(PicaSurfaceLightingDeclarations(false,false),PicaSurfaceLightingDeclarations());
    EXPECT_EQ(PicaSurfaceLightingBody(false,false),PicaSurfaceLightingBody());
    EXPECT_EQ(PicaSurfaceLightingDeclarations(true,false),PicaSurfaceLightingDeclarations(true));
    EXPECT_EQ(PicaSurfaceLightingBody(true,false),PicaSurfaceLightingBody(true));
    EXPECT_EQ(sizeof(PicaSurfaceLightingUniforms),2992U);
    EXPECT_EQ(offsetof(PicaBalancedSurfaceUniforms,Balance),2992U);
    EXPECT_EQ(sizeof(PicaBalancedSurfaceUniforms),3152U);
    const auto body=PicaSurfaceLightingBody(true,true);
    EXPECT_EQ(body.find("primary_fragment_color.rgb + added"),std::string::npos);
    EXPECT_EQ(body.find("fragment_uniforms.lighting_diffuse[pica_light]"),std::string::npos);
    EXPECT_NE(body.find("if (!surface_water)"),std::string::npos);
    EXPECT_NE(body.find("pica_surface_shadow(8)"),std::string::npos);
    EXPECT_NE(body.find("if (!identity)"),std::string::npos);
    Fast::Extension::PicaEffectDecisions d;d.FragmentLightingOperand=true;
    EXPECT_FALSE(Fast::Extension::PicaUberEffectSupports(d,{}));
}

TEST(PicaFragmentBalance, PreClampTransferLeavesAmbientFillAndAlphaAlone) {
    // Independent ledger fixture: includes an unclipped sum over1, a fill
    // term and secondary specular. Post-clamp Lambert subtraction gives a
    // different answer; global output multiplication darkens ambient/fill.
    const std::array<float,3> ambient{.6F,.2F,.1F}, fill{.3F,.1F,.2F}, key{.5F,.4F,.3F};
    const std::array<float,3> fillSpec{.2F,.1F,.05F}, keySpec{.3F,.2F,.1F};
    const auto clamp=[](float x){return std::clamp(x,0.F,1.F);};
    for(size_t c=0;c<3;++c) {
        const float native=clamp(ambient[c]+fill[c]+key[c]);
        const float transferred=clamp(ambient[c]+fill[c]+key[c]*1.F);
        EXPECT_FLOAT_EQ(native,transferred);
        const float shadowed=clamp(ambient[c]+fill[c]+key[c]*.25F);
        EXPECT_LE(shadowed,native);
        EXPECT_FLOAT_EQ(fillSpec[c]+keySpec[c]*.25F,
                        fillSpec[c]+keySpec[c]-(1.F-.25F)*keySpec[c]);
    }
    EXPECT_FLOAT_EQ(clamp(.6F+.3F+.5F*.25F),1.F);
    EXPECT_NE(clamp(.6F+.3F+.5F*.25F),clamp(1.F-.5F*.75F));
}

TEST(PicaFragmentBalance, CapabilityFalseKeepsCanonicalKeysAndNativeMemoryInert) {
    using namespace Fast::Extension;
    auto request=BaseRequest();
    EffectsSettings effects;
    PicaShaderPipelineCache cache;
    const auto before=cache.Resolve(request,effects);
    request.FragmentShaderHooks.NativeLightingCapture={1,1,2,3,4};
    request.FragmentLightingBalance=false;
    const auto after=cache.Resolve(request,effects);
    EXPECT_EQ(before.FragmentShaderSource,after.FragmentShaderSource);
    EXPECT_EQ(before.FragmentShaderKey,after.FragmentShaderKey);
    EXPECT_EQ(before.VertexShaderSource,after.VertexShaderSource);
    EXPECT_EQ(before.VertexShaderKey,after.VertexShaderKey);
    EXPECT_EQ(before.RequestedFeatures,after.RequestedFeatures);
    request.RequireNativeFidelity=true;
    request.SurfaceLightingAvailable=true;
    request.FragmentLightingOperand=true;request.FragmentLightingBalance=true;
    const auto native=cache.Resolve(request,effects);
    EXPECT_EQ(native.FragmentShaderSource,request.FragmentShaderSource);
    EXPECT_EQ(native.FragmentShaderKey,request.FragmentShaderKey);
    EXPECT_FALSE(HasPicaShaderInstrumentationFeature(native.AppliedFeatures,
        PicaShaderInstrumentationFeature::FragmentLightingBalance));
}

// The plain (no balance operand) surface source is pinned: a change here is a
// change to every title's canonical lighting shader, not to the balance variant.
// Pinned at the beta.1.2 alignment (rim and baked-reference fields present;
// the native fragment-lit shadow block is the MM3D adapter's, not legacy).
TEST(Oot3dPicaShaderInstrumentation, LegacySurfaceSourceIsPinned) {
    using namespace Fast::Renderer3ds;
    const auto hash = [](std::string_view text) {
        uint64_t value = 14695981039346656037ULL;
        for (const unsigned char byte : text) value = (value ^ byte) * 1099511628211ULL;
        return value;
    };
    EXPECT_EQ(PicaSurfaceLightingDeclarations().size(), 38472U);
    EXPECT_EQ(hash(PicaSurfaceLightingDeclarations()), 17643374038356268985ULL);
    EXPECT_EQ(PicaSurfaceLightingBody().size(), 3332U);
    EXPECT_EQ(hash(PicaSurfaceLightingBody()), 6543872953540395030ULL);
    EXPECT_NE(PicaSurfaceLightingBody(true), PicaSurfaceLightingBody());
    EXPECT_NE(PicaSurfaceLightingBody(false, false, true), PicaSurfaceLightingBody());
    Fast::Extension::PicaEffectDecisions decisions;
    decisions.FragmentLightingOperand = true;
    EXPECT_FALSE(Fast::Extension::PicaUberEffectSupports(decisions, {}));
    Fast::Extension::PicaEffectDecisions shadows;
    shadows.NativeFragmentLitShadows = true;
    EXPECT_FALSE(Fast::Extension::PicaUberEffectSupports(shadows, {}));
}

} // namespace
