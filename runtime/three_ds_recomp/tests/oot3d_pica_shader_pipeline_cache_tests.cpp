#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/oot3d/pica_directional_shadow_caster.h"
#include "fast/oot3d/pica_directional_shadow_lighting.h"
#include "fast/oot3d/pica_reactive_mask.h"
#include "fast/oot3d/pica_rigid_motion.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"

#include <gtest/gtest.h>
#ifdef OOT3D_PIPELINE_CACHE_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

#include <stdexcept>

namespace {

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

Fast::Oot3d::PicaShaderPipelineRequest BaseRequest() {
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
    Fast::Oot3d::PicaShaderPipelineRequest request;
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
    request.TemporalVertexProgram = BaseTemporalVertexProgram();
    request.FragmentShaderHooks =
        Fast::Oot3d::AnalyzePicaFragmentShaderHooks(kFragmentShader);
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
    using namespace Fast::Oot3d;
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
    using namespace Fast::Oot3d;
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

TEST(Oot3dPicaAttachmentContract, CanonicalRenderingDeclaresOnlyNativeColor) {
    const Fast::Oot3d::PicaAttachmentRequirements requirements;
    EXPECT_TRUE(requirements.NativeColorOnly());
    EXPECT_EQ(requirements.ColorAttachmentCount(), 1U);
    EXPECT_EQ(requirements.Key(), 0U);
}

TEST(Oot3dPicaAttachmentContract,
     InstrumentedRenderingPreservesStableMrtAbi) {
    using Fast::Oot3d::PicaAuxiliaryOutput;
    const Fast::Oot3d::PicaAttachmentRequirements requirements{
        PicaAuxiliaryOutput::NormalGuide};
    EXPECT_FALSE(requirements.NativeColorOnly());
    EXPECT_TRUE(requirements.Requires(PicaAuxiliaryOutput::NormalGuide));
    EXPECT_FALSE(requirements.Requires(PicaAuxiliaryOutput::MaterialGuide));
    EXPECT_FALSE(requirements.Requires(
        PicaAuxiliaryOutput::RigidMotionGuide));
    EXPECT_FALSE(requirements.Requires(PicaAuxiliaryOutput::AmbientGuide));
    EXPECT_EQ(requirements.ColorAttachmentCount(),
              Fast::Oot3d::kPicaColorAttachmentCount);
}

TEST(Oot3dPicaAttachmentContract,
     ReportsTheExactDeclaredAuxiliaryMask) {
    using Fast::Oot3d::PicaAuxiliaryOutput;
    const Fast::Oot3d::PicaAttachmentRequirements requirements{
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
              Fast::Oot3d::PicaShaderInstrumentationFeature::None);
    EXPECT_EQ(first.AppliedFeatures,
              Fast::Oot3d::PicaShaderInstrumentationFeature::None);

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

TEST(Oot3dPicaShaderPipelineCache,
     CanonicalIdentityIncludesExactSourceContent) {
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
    auto request = BaseRequest();
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(9U);

    const auto& instrumented = cache.Resolve(request, effects);
    EXPECT_EQ(instrumented.Domain,
              Fast::Oot3d::PicaShaderDomain::Instrumented);
    EXPECT_TRUE(Fast::Oot3d::HasPicaShaderInstrumentationFeature(
        instrumented.AppliedFeatures,
        Fast::Oot3d::PicaShaderInstrumentationFeature::TemporalVertex));
    EXPECT_TRUE(Fast::Oot3d::HasPicaShaderInstrumentationFeature(
        instrumented.AppliedFeatures,
        Fast::Oot3d::PicaShaderInstrumentationFeature::RigidMotionGuide));
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
        Fast::Oot3d::AnalyzePicaFragmentShaderHooks(kContaminated);
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
        Fast::Oot3d::AnalyzePicaFragmentShaderHooks(
            kDuplicateLocation);
    cache.BeginFrame(1U);

    EXPECT_THROW((void)cache.Resolve(request, effects),
                 std::logic_error);
    EXPECT_EQ(cache.Stats().CanonicalOutputContractRejects, 1U);
}

TEST(Oot3dPicaShaderPipelineCache,
     AuditsLegacyCanonicalHooksOnceBeforeCaching) {
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
     RejectsTemporalInstrumentationWithoutTypedFrontendProgram) {
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
    auto request = BaseRequest();
    request.TemporalMotionEnabled = true;
    request.TemporalVertexProgram = {};
    cache.BeginFrame(1U);

    EXPECT_THROW((void)cache.Resolve(request, effects),
                 std::logic_error);
    EXPECT_EQ(cache.Stats().TypedInstrumentationContractRejects, 1U);
}

TEST(Oot3dPicaShaderPipelineCache,
     DirectionalShadowLightingFollowsCompiledPassRequest) {
    using namespace Fast::Oot3d;

    EffectsSettings disabledSettings;
    auto enabledRequest = BaseRequest();
    enabledRequest.Draw.DirectionalShadowReceiver = true;
    PicaShaderPipelineCache enabledCache;
    enabledCache.BeginFrame(1U);
    const auto& enabled = enabledCache.Resolve(
        enabledRequest, disabledSettings);
    EXPECT_TRUE(HasPicaShaderInstrumentationFeature(
        enabled.RequestedFeatures,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting));

    EffectsSettings enabledSettings;
    enabledSettings.DirectionalShadows.Mode =
        DirectionalShadowMode::SingleCascade;
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
    auto request = BaseRequest();
    request.FragmentShaderHooks =
        Fast::Oot3d::AnalyzePicaFragmentShaderHooks(
            request.FragmentShaderSource);
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(11U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_TRUE(instrumented.DirectFragmentHooksUsed);
    EXPECT_TRUE(instrumented.RigidMotionApplied);
}

TEST(Oot3dPicaShaderPipelineCache,
     BuildsRequestedNormalGuideFromTypedMaterialNormal) {
    using namespace Fast::Oot3d;
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
    effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
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

TEST(Oot3dPicaShaderPipelineCache,
     OutlineFogGuideBelongsOnlyToDepthWritingWorldDraws) {
    using namespace Fast::Oot3d;
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
    using namespace Fast::Oot3d;
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
    using namespace Fast::Oot3d;
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
    auto request = BaseRequest();
    const auto legacy = Fast::Oot3d::BuildPicaTemporalVertexVariant(
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
    auto request = BaseRequest();
    request.FragmentShaderHooks =
        Fast::Oot3d::AnalyzePicaFragmentShaderHooks(
            request.FragmentShaderSource);
    ++request.FragmentShaderHooks.SourceSize;
    request.TemporalMotionEnabled = true;
    cache.BeginFrame(12U);

    const auto& instrumented = cache.Resolve(request, effects);

    EXPECT_FALSE(instrumented.DirectFragmentHooksUsed);
    EXPECT_TRUE(instrumented.RigidMotionApplied);
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedToonCompositionPreservesLegacySourceAndKey) {
    using namespace Fast::Oot3d;
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
    const auto legacy = BuildPicaToonShaderVariant(
        kFragmentShader, request.FragmentShaderKey, toonDraw,
        effects.Toon, effects.ToonStyle);
    ASSERT_TRUE(legacy.Applied());

    const auto& composed = cache.Resolve(request, effects);

    EXPECT_TRUE(composed.DirectFragmentHooksUsed);
    EXPECT_EQ(composed.ToonEligibility,
              PicaToonEligibility::Eligible);
    EXPECT_EQ(composed.FragmentShaderSource, legacy.Source);
    EXPECT_EQ(composed.FragmentShaderKey, legacy.FragmentKey);
}

TEST(Oot3dPicaShaderPipelineCache,
     NativeFidelityRejectsRequestedInstrumentation) {
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
    EXPECT_TRUE(Fast::Oot3d::HasPicaShaderInstrumentationFeature(
        temporal.AppliedFeatures,
        Fast::Oot3d::PicaShaderInstrumentationFeature::ReactiveMask));
    EXPECT_NE(temporal.FragmentShaderSource.find(
                  "OOT3D reactive world draw"),
              std::string::npos);
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedReflectionOwnsMaterialGuideOutsideCanonicalSource) {
    using namespace Fast::Oot3d;
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
     ExplicitReflectionProfileDoesNotRequireTevSpecular) {
    using namespace Fast::Oot3d;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Reflections = ReflectionMode::HiZ;
    auto request = BaseRequest();
    request.ReflectionProfile =
        ReflectionMaterialParameters{0.78F, 0.10F, 0.75F};
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
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
        Fast::Oot3d::AnalyzePicaFragmentShaderHooks(kGrassFragment);

    const auto& result = cache.Resolve(request, effects);
    EXPECT_TRUE(result.IsCanonical());
    EXPECT_TRUE(result.GrassTexture0Sampled);
    EXPECT_EQ(result.FragmentShaderSource, kGrassFragment);
    EXPECT_EQ(cache.Stats().InstrumentationEntries, 0U);
}

TEST(Oot3dPicaShaderPipelineCache,
     TypedSamplerUsageIgnoresUncalledTextureHelpers) {
    Fast::Oot3d::PicaShaderPipelineCache cache;
    Fast::Oot3d::EffectsSettings effects;
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
        Fast::Oot3d::AnalyzePicaFragmentOutputContract(kFragment);

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
    using namespace Fast::Oot3d;
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
     TypedComposerMatchesTheSupportedLegacyFragmentSequence) {
    using namespace Fast::Oot3d;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=5) in vec4 pica_normquat;
layout(location=0) out vec4 pica_color;
layout(location=2) out vec4 pica_material_guide;
void main() {
    vec4 primary_fragment_color = vec4(0.75);
    vec4 secondary_fragment_color = vec4(0.25);
    // OOT3D_PICA_MATERIAL_TOON_POINT
    secondary_fragment_color.rgb *= vec3(1.0);
    float oot3d_specular_signal = clamp(max(max(secondary_fragment_color.r, secondary_fragment_color.g), secondary_fragment_color.b) * 4.0, 0.0, 1.0);
    pica_material_guide = vec4(oot3d_specular_signal, clamp(1.0 - oot3d_specular_signal * 0.75, 0.08, 1.0), 1.0, 0.0);
    vec3 oot3d_ao_response_rgb = vec3(0.75);
    // OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY
    pica_color = vec4(1.0);
}
)glsl";

    const auto composed = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        71U,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting |
            PicaShaderInstrumentationFeature::AmbientOcclusionGuide |
            PicaShaderInstrumentationFeature::SceneDomainGuide |
            PicaShaderInstrumentationFeature::ReflectionMaterialGuide |
            PicaShaderInstrumentationFeature::ReactiveMask |
            PicaShaderInstrumentationFeature::RigidMotionGuide,
        {400U, 240U, 0U, true, true, SourceAlphaBlend(), 0xFU,
         Oot3d::Renderer::PicaCompositionDomain::Scene},
        {true, false},
        nullptr,
    });

    ASSERT_TRUE(composed.Applied());
    EXPECT_NE(composed.Source.find(
                  "uniform sampler2D oot3d_directional_shadow_map"),
              std::string::npos);
    EXPECT_NE(composed.Source.find(
                  "primary_fragment_color.rgb *= "
                  "oot3d_directional_shadow_visibility()"),
              std::string::npos);
    EXPECT_NE(composed.Source.find("pica_ambient_guide"),
              std::string::npos);
    EXPECT_NE(composed.Source.find("pica_material_guide"),
              std::string::npos);
    EXPECT_NE(composed.Source.find("pica_rigid_motion_guide"),
              std::string::npos);
    EXPECT_NE(composed.FragmentKey, 71U);
    EXPECT_EQ(composed.DirectionalShadowEligibility,
              PicaDirectionalShadowLightingEligibility::FragmentPrimary);
    EXPECT_EQ(composed.AmbientGuideEligibility,
              PicaAmbientOcclusionGuideEligibility::AppliedExact);
    EXPECT_EQ(composed.ReflectionEligibility,
              PicaReflectionMaterialEligibility::Eligible);
    EXPECT_EQ(composed.ReactiveCoverage,
              PicaReactiveCoverage::SourceAlpha);
    EXPECT_TRUE(composed.Reactive);
    EXPECT_TRUE(composed.RigidMotionApplied);
    EXPECT_TRUE(composed.Outputs.WritesAmbientGuide);
    EXPECT_FALSE(composed.Outputs.SceneDomainBlendedOverlay);
}

TEST(Oot3dPicaShaderInstrumentation,
     PublishesTypedSceneOverlayOutputs) {
    using namespace Fast::Oot3d;
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
    using namespace Fast::Oot3d;
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
        "pica_rigid_motion_guide = vec4(oot3d_motion.xy");
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
    using namespace Fast::Oot3d;
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
    using namespace Fast::Oot3d;
    PicaShaderPipelineCache cache;
    EffectsSettings effects;
    effects.Toon = ToonMode::PicaMaterial;
    effects.ToonStyle.OutlineEnabled = true;
    effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
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
    using namespace Fast::Oot3d;
    EffectsSettings effects;
    effects.Toon = ToonMode::PicaMaterial;
    effects.ToonStyle.OutlineEnabled = true;
    effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    effects.Reflections = ReflectionMode::FidelityFxSssr;
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
    using namespace Fast::Oot3d;
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
        "pica_ambient_guide = vec4(1.0, 1.0, 1.0, 1.0)");
    const size_t composedHelper = composed.Source.find("void helper");
    ASSERT_NE(assignment, std::string::npos);
    ASSERT_NE(composedHelper, std::string::npos);
    EXPECT_LT(assignment, composedHelper);
}

TEST(Oot3dPicaShaderInstrumentation,
     DirectionalShadowLightingRequiresATypedLightingHook) {
    using namespace Fast::Oot3d;
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
              PicaDirectionalShadowLightingEligibility::UnsupportedShader);
}

TEST(Oot3dPicaShaderInstrumentation,
     DirectionalShadowLightingModulatesVertexPrimaryBeforeTev) {
    using namespace Fast::Oot3d;
    static constexpr std::string_view kFragmentShader = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=0) out vec4 pica_color;
void main() {
    vec4 rounded_primary_color = pica_primary_color;
    vec4 combiner_output = rounded_primary_color;
    pica_color = combiner_output;
}
)glsl";
    const auto hooks = AnalyzePicaFragmentShaderHooks(kFragmentShader);
    ASSERT_TRUE(hooks.Valid());
    EXPECT_TRUE(hooks.Supports(PicaShaderHook::PicaLighting));
    EXPECT_TRUE(hooks.Has(PicaShaderSemantic::VertexLightingPoint));

    const auto composed = BuildPicaFragmentInstrumentationVariant({
        kFragmentShader,
        83U,
        PicaShaderInstrumentationFeature::DirectionalShadowLighting,
        {},
        {},
        nullptr,
    });
    ASSERT_TRUE(composed.Applied());
    EXPECT_EQ(composed.DirectionalShadowEligibility,
              PicaDirectionalShadowLightingEligibility::VertexPrimary);
    const size_t shadow = composed.Source.find(
        "rounded_primary_color.rgb *= "
        "oot3d_directional_shadow_visibility()");
    const size_t tev = composed.Source.find(
        "vec4 combiner_output = rounded_primary_color");
    ASSERT_NE(shadow, std::string::npos);
    ASSERT_NE(tev, std::string::npos);
    EXPECT_LT(shadow, tev);
    EXPECT_NE(composed.Source.find(
                  "oot3d_directional_shadow_axis_weight"),
              std::string::npos);
    EXPECT_NE(composed.Source.find("weighted_occlusion"),
              std::string::npos);
    EXPECT_EQ(composed.Source.find("samples += 1.0"),
              std::string::npos);
    EXPECT_EQ(composed.Source.find("pica_normal_guide"),
              std::string::npos);
}

TEST(Oot3dPicaShaderInstrumentation,
     DirectionalShadowReceiverUsesTypedHistoryAndCurrentView) {
    using namespace Fast::Oot3d;
    auto currentViewToWorld = IdentityDirectionalShadowMatrix();
    currentViewToWorld[12] = 5.0F;
    PicaDirectionalShadowHistoryState history;
    history.WorldToShadowTexture = IdentityDirectionalShadowMatrix();
    history.ProducedFrameId = 7U;
    history.Resolution = 1024U;
    history.Strength = 0.8F;
    history.DepthBias = 0.002F;
    history.PcfRadius = 4U;
    history.WorldLightDirectionTowardSource = {0.0F, 1.0F, 0.0F};
    PicaNativeLightingState lighting;
    lighting.Available = true;
    lighting.Enabled = true;
    lighting.ActiveLightCount = 1U;
    lighting.Lights[0].Valid = true;
    lighting.Lights[0].DirectionViewTowardSource = {0.0F, 1.0F, 0.0F};
    lighting.Lights[0].Diffuse = {};
    lighting.Lights[0].Ambient = {0.25F, 0.50F, 0.75F};

    const auto binding = BuildPicaDirectionalShadowReceiverBinding(
        currentViewToWorld, lighting, false, history);
    ASSERT_TRUE(binding.Bound());
    EXPECT_EQ(binding.Classification,
              PicaDirectionalShadowReceiverClass::AmbientOnly);
    EXPECT_EQ(binding.Contribution,
              PicaDirectionalShadowReceiverContribution::BakedRigidMaterial);
    const auto& uniforms = binding.Uniforms;
    EXPECT_FLOAT_EQ(uniforms.ViewToShadowTexture[12], 5.0F);
    EXPECT_FLOAT_EQ(uniforms.ShadowState[0], 0.8F);
    EXPECT_FLOAT_EQ(uniforms.ShadowState[1], 1.0F / 1024.0F);
    EXPECT_FLOAT_EQ(uniforms.ShadowState[2], 0.0F);
    EXPECT_FLOAT_EQ(uniforms.ShadowState[3], 0.002F);
    EXPECT_FLOAT_EQ(uniforms.ShadowedDirectFraction[0], 1.0F);
    EXPECT_FLOAT_EQ(uniforms.ShadowedDirectFraction[1], 1.0F);
    EXPECT_FLOAT_EQ(uniforms.ShadowedDirectFraction[2], 1.0F);
    EXPECT_EQ(uniforms.Extent[0], 1024U);
    EXPECT_EQ(uniforms.Extent[1], 1024U);
    EXPECT_EQ(uniforms.Extent[2], 2U);
    EXPECT_EQ(uniforms.Extent[3], 1U);

    const auto skinned = BuildPicaDirectionalShadowReceiverBinding(
        currentViewToWorld, lighting, true, history);
    EXPECT_FALSE(skinned.Bound());
    EXPECT_EQ(skinned.Classification,
              PicaDirectionalShadowReceiverClass::AmbientOnly);

    lighting.Lights[0].Diffuse = {0.75F, 0.50F, 0.25F};
    const auto nativeDirect = BuildPicaDirectionalShadowReceiverBinding(
        currentViewToWorld, lighting, false, history);
    EXPECT_FALSE(nativeDirect.Bound());
    EXPECT_EQ(nativeDirect.Classification,
              PicaDirectionalShadowReceiverClass::DirectMatched);

    history = {};
    const auto neutral = BuildPicaDirectionalShadowReceiverBinding(
        currentViewToWorld, lighting, false, history);
    EXPECT_FALSE(neutral.Bound());
    EXPECT_EQ(neutral.Uniforms.Extent[3], 0U);
}

} // namespace
