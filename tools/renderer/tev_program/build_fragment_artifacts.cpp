// Developer build: a finite interface family derived from the maintained PICA
// equations. No ROM, register capture, gameplay inventory or driver cache input.
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/renderer3ds/pica_nri_shader_contract.h"
#include "fast/extension/pica_directional_shadow_caster.h"
#include "fast/extension/pica_shader_instrumentation.h"
#include "fast/extension/pica_shader_pipeline_cache.h"
#include "fast/extension/toon_surface_response.h"
#include "fast/extension/pica_temporal_fragment_binaries.h"
#include "fast/extension/pica_toon_fragment_artifact.h"
#include "fast/extension/pica_uber_effect_program.h"
#include "fast/extension/pica_uber_fragment_binaries.h"
#include "fast/extension/pica_caster_fragment_binaries.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/extension/pica_nri_shader_contract.h"
#include "spirv_pack_file.h"
#include <shaderc/shaderc.hpp>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
#include <unordered_set>

using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;

static void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

static constexpr Fast::Extension::PicaReactiveCoverage kTemporalCoverage[]{
    Fast::Extension::PicaReactiveCoverage::None, Fast::Extension::PicaReactiveCoverage::SourceAlpha,
    Fast::Extension::PicaReactiveCoverage::SourceColor, Fast::Extension::PicaReactiveCoverage::Full};

static void ConfigureTemporal(Fast::Extension::PicaFragmentInstrumentationRequest& request, unsigned kind) {
    using namespace Fast::Extension;
    using Factor = ::Oot3d::Renderer::NativeBlendFactor;
    request.RequestedFeatures |= PicaShaderInstrumentationFeature::RigidMotionGuide |
        PicaShaderInstrumentationFeature::ReactiveMask;
    request.Draw.Blend.Enabled = kind != 0;
    request.Draw.Blend.SourceRgb = kind == 1 ? Factor::SourceAlpha :
        kind == 2 ? Factor::SourceColor : Factor::One;
    request.Draw.Blend.DestRgb = Factor::One;
}

// Always-available effect family: one superset program per parametric
// structural configuration (lighting, integer texture 0) and vertex interface
// (temporal clip, surface normal and tint). Shadow-buffer draws keep the
// native family. NRI separate-sampler interface only.
// Alpha-tested shadow casters of the parametric bases: the map discards the
// fragments the native draw discards with the parametric TEV and alpha test,
// one program per base. Pass compiler target (Vulkan 1.2).
static int BuildCasterFamily(int argc, char** argv) {
    Check(argc == 2, "usage: build_fragment_artifacts --casters pica_caster_fragment.spvpack | --check");
    const bool verify = std::string_view(argv[1]) == "--check";
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    SpirvPackFile pack;
    unsigned modules = 0;
    for (const auto lighting : kOot3dPicaParametricLightings)
    for (unsigned integerTexture = 0; integerTexture < 2; ++integerTexture) {
        Oot3dPicaGeneratedFragmentShader shader;
        std::string error;
        Check(GenerateOot3dPicaParametricBaseFragmentShader(lighting, integerTexture != 0, shader, &error), error);
        const auto caster = Fast::Extension::BuildPicaShadowCasterAlphaFragmentShader(
            shader.Source, shader.StateKey, shader.Hooks);
        Check(caster.Applied(), "parametric alpha caster derivation failed");
        const auto compiled = compiler.CompileGlslToSpv(caster.Source, shaderc_fragment_shader,
                                                       "alpha_caster", options);
        Check(compiled.GetCompilationStatus() == shaderc_compilation_status_success, compiled.GetErrorMessage());
        if (verify) {
            const auto stored = Fast::Renderer::FindPassShaderArtifact(
                Fast::Extension::CasterFragmentArtifacts(), caster.Source, Fast::Renderer::SpirvStage::Fragment, {});
            Check(stored.size() == size_t(compiled.cend() - compiled.cbegin()) &&
                      std::equal(stored.begin(), stored.end(), compiled.cbegin()),
                  "stale/missing alpha caster: regenerate with --casters");
        }
        auto& entry = pack.Add(Fast::Renderer::SpirvPackKind::Pass, {compiled.cbegin(), compiled.cend()});
        entry.Stage = uint32_t(Fast::Renderer::SpirvStage::Fragment);
        entry.VulkanMinor = 2;
        entry.Source = pack.Keep(caster.Source);
        ++modules;
    }
    if (!verify) pack.Write(argv[1]);
    std::cout << "caster_fragment_artifacts=" << modules << '\n';
    return 0;
}

static int BuildUberFamily(int argc, char** argv, bool nativeLighting = false) {
    const bool reuseBuiltins = argc > 1 && std::string_view(argv[1]) == "--reuse-builtins";
    if (reuseBuiltins) { --argc; ++argv; }
    const bool verify = argc == 2 && std::string_view(argv[1]) == "--check";
    const bool countOnly = argc == 2 && std::string_view(argv[1]) == "--count";
    Check(argc == 2, "usage: build_fragment_artifacts --uber|--native-lighting-uber output.spvpack | --check | --count");
    std::unordered_set<std::string> emitted;
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    SpirvPackFile pack;
    unsigned modules = 0;
    for (const auto lighting : kOot3dPicaParametricLightings)
    for (unsigned integerTexture = 0; integerTexture < 2; ++integerTexture) {
        Oot3dPicaGeneratedFragmentShader shader;
        std::string error;
        Check(GenerateOot3dPicaParametricBaseFragmentShader(lighting, integerTexture != 0, shader, &error), error);
        // Native policy bits: operand, balance, shadows, GGX balance, native-unlit.
        // The original 36 programs remain byte-identical in their own pack.
        const std::vector<unsigned> policies = nativeLighting ? std::vector<unsigned>{1, 3, 4, 5, 7, 10, 16}
                                                             : std::vector<unsigned>{0};
        for (unsigned policy : policies)
        for (unsigned temporal = 0; temporal < 2; ++temporal)
        for (unsigned surface = nativeLighting ? 1U : 0U; surface < 3; ++surface) {
            if (nativeLighting && policy != 16 && !shader.Hooks.Has(::Oot3d::Renderer::PicaShaderSemantic::MaterialNormal)) continue;
            const auto uber = Fast::Extension::BuildPicaUberEffectProgram({shader.Source, shader.Hooks,
                temporal != 0, surface != 0, surface == 2,
                (policy & 1U) != 0, (policy & 2U) != 0, (policy & 4U) != 0,
                (policy & 8U) != 0, (policy & 16U) != 0});
            Check(!uber.empty(), "uber effect composition rejected a parametric base");
            const auto nri = Fast::Extension::BuildPicaNriFragmentShaderVariant(uber);
            Check(nri.Applied, nri.Error);
            if (!emitted.insert(nri.Source).second) continue;
            if (const char* dump = std::getenv("TRIAEVUM_UBER_SOURCES")) {
                std::ofstream file(std::filesystem::path(dump) / ("uber" + std::to_string(modules) + ".frag"));
                file << nri.Source;
            }
            if (countOnly) { ++modules; continue; }
            const auto id = IdentifyPicaShaderSource(nri.Source);
            if (verify) {
                Check(!FindPicaFragmentArtifact(Fast::Extension::UberFragmentArtifacts(), nri.Source, true).empty(),
                      "uber effect source changed: regenerate artifacts");
                ++modules;
                continue;
            }
            if (reuseBuiltins) {
                const auto stored = FindPicaFragmentArtifact(Fast::Extension::UberFragmentArtifacts(), nri.Source, true);
                if (!stored.empty()) {
                    auto& entry = pack.Add(Fast::Renderer::SpirvPackKind::PicaFragment, {stored.begin(), stored.end()});
                    entry.Flags = Fast::Renderer::kSpirvPackSeparateSamplers;
                    entry.Identity = {id.Id, id.SecondaryHash, id.Size};
                    ++modules;
                    continue;
                }
            }
            const auto compiled = compiler.CompileGlslToSpv(nri.Source, shaderc_fragment_shader,
                                                           "uber_fragment", options);
            Check(compiled.GetCompilationStatus() == shaderc_compilation_status_success,
                  compiled.GetErrorMessage());
            auto& entry = pack.Add(Fast::Renderer::SpirvPackKind::PicaFragment, {compiled.cbegin(), compiled.cend()});
            entry.Flags = Fast::Renderer::kSpirvPackSeparateSamplers;
            entry.Identity = {id.Id, id.SecondaryHash, id.Size};
            ++modules;
        }
    }
    Check(modules == (nativeLighting ? 104U : 36U), "unexpected uber effect family size");
    if (countOnly || verify) {
        std::cout << "uber_fragment_artifacts=" << modules << '\n';
        return 0;
    }
    pack.Write(argv[1]);
    std::cout << "uber_fragment_artifacts=" << modules << '\n';
    return 0;
}

// The native-fragment balance is not an uber decision: validate its actual
// generated capture/body composition separately without changing old packs.
static int CheckBalanceFamily() {
    using namespace Fast::Extension;
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    unsigned modules=0;
    for (const auto lighting : kOot3dPicaParametricLightings) {
        if (lighting == kOot3dPicaParametricLightings[0]) continue; // No native lighting to capture.
        for (bool integerTexture : {false,true}) for (bool ggx : {false,true}) {
            if (integerTexture && ggx) continue; // Installed maps require float sampling.
            Oot3dPicaGeneratedFragmentShader shader;
            std::string error;
            Check(GenerateOot3dPicaParametricBaseFragmentShader(lighting, integerTexture, shader, &error), error);
            PicaFragmentInstrumentationRequest request;
            request.Source=shader.Source;request.Hooks=&shader.Hooks;
            request.RequestedFeatures=PicaShaderInstrumentationFeature::NativePerPixelLighting |
                PicaShaderInstrumentationFeature::FragmentLightingOperand |
                PicaShaderInstrumentationFeature::FragmentLightingBalance;
            if (ggx) {
                // Parametric TEV normally leaves sampling runtime-selected.
                // Bind a synthetic typed slot for this installed-map compile fixture.
                shader.Hooks.SampledTextureMask=1;
                shader.Hooks.TextureSamples[0]={0,Fast::Renderer3ds::PicaTextureCoordinateOperation::NativeVFlip};
                request.RequestedFeatures |= PicaShaderInstrumentationFeature::PbrMaterial;
                request.Pbr.TextureSlot=0;
                request.Pbr.NativeFragmentLighting=true;
                request.Pbr.NativeBalance=true;
                request.Pbr.NativeAttenuationLuts=true;
            }
            const auto balanced=BuildPicaFragmentInstrumentationVariant(request);
            if (ggx) Check(HasPicaShaderInstrumentationFeature(balanced.AppliedFeatures,
                PicaShaderInstrumentationFeature::PbrMaterial), "GGX balance not composed");
            Check(HasPicaShaderInstrumentationFeature(balanced.AppliedFeatures,
                PicaShaderInstrumentationFeature::FragmentLightingBalance), "balance capture not composed");
            const auto nri=Fast::Extension::BuildPicaNriFragmentShaderVariant(balanced.Source);
            Check(nri.Applied,nri.Error);
            const auto compiled=compiler.CompileGlslToSpv(nri.Source,shaderc_fragment_shader,"fragment_balance",options);
            Check(compiled.GetCompilationStatus()==shaderc_compilation_status_success,compiled.GetErrorMessage());
            ++modules;
        }
    }
    Check(modules==6,"unexpected balance family size");
    std::cout << "balance_fragment_checks=" << modules << '\n';
    return 0;
}

// Exercise native decoded-normal ownership and installed normal-map writers.
static int CheckNormalGuides() {
    using namespace Fast::Extension;
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    unsigned cases = 0;
    for (unsigned lighting = 1; lighting <= 2; ++lighting)
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto packet = std::make_unique<Oot3dPicaDrawPacket>();
        Oot3dPicaDecodedDrawState state{};
        packet->Registers[0x8f] = 1;
        if (lighting == 2) packet->Registers[0x1c4] = 0xFF7BFF00U;
        packet->Registers[0x80] = 1;
        packet->Registers[0xc0] = 0x00030003U; // native texture 0 RGB/alpha TEV operands
        state.Textures[0].Enabled = true;
        Oot3dPicaGeneratedFragmentShader shader;
        std::string error;
        Check(GenerateOot3dPicaFragmentShader(*packet, state, shader, &error,
            Oot3dPicaShaderBuildPurpose::OfflineSource, Oot3dPicaTevMode::Specialized), error);
        Check(shader.Hooks.Has(::Oot3d::Renderer::PicaShaderSemantic::MaterialNormal),
              "native frontend did not publish its decoded normal");
        PicaFragmentInstrumentationRequest request;
        request.Source = shader.Source;
        request.Hooks = &shader.Hooks;
        request.RequestedFeatures = PicaShaderInstrumentationFeature::NormalGuide;
        if (mode != 0) request.RequestedFeatures |= PicaShaderInstrumentationFeature::NativePerPixelLighting;
        if (mode == 1) request.RequestedFeatures |= PicaShaderInstrumentationFeature::FragmentLightingOperand;
        if (mode == 2) request.RequestedFeatures |= PicaShaderInstrumentationFeature::NativeFragmentLitShadows;
        if (mode == 3) {
            request.RequestedFeatures |= PicaShaderInstrumentationFeature::PbrMaterial;
            request.Pbr.TextureSlot = 0;
            request.Pbr.NativeFragmentLighting = true;
            request.Pbr.NativeAttenuationLuts = lighting == 1;
        }
        const auto guide = BuildPicaFragmentInstrumentationVariant(request);
        Check(HasPicaShaderInstrumentationFeature(guide.AppliedFeatures,
              PicaShaderInstrumentationFeature::NormalGuide), "normal guide was not emitted");
        Check(guide.Decisions.Normal == (mode == 3 ? PicaEffectDecisions::NormalPath::Pbr
                                                 : PicaEffectDecisions::NormalPath::Material),
              "normal guide selected the wrong writer: lighting=" + std::to_string(lighting) +
                  " mode=" + std::to_string(mode) + " actual=" + std::to_string(unsigned(guide.Decisions.Normal)));
        const auto compiled = compiler.CompileGlslToSpv(guide.Source, shaderc_fragment_shader,
                                                       "native_normal_guide", options);
        Check(compiled.GetCompilationStatus() == shaderc_compilation_status_success, compiled.GetErrorMessage());
        ++cases;
        if (mode == 1 || mode == 2) {
            // A textual identifier is not sufficient: an unproved frontend
            // contract retains the prior surface path and a different key.
            auto unknown = shader.Hooks;
            using Semantic = ::Oot3d::Renderer::PicaShaderSemantic;
            unknown.Semantics = static_cast<Semantic>(static_cast<uint32_t>(unknown.Semantics) &
                ~static_cast<uint32_t>(Semantic::MaterialNormal));
            request.Hooks = &unknown;
            const auto fallback = BuildPicaFragmentInstrumentationVariant(request);
            Check(fallback.Decisions.Normal == PicaEffectDecisions::NormalPath::Surface &&
                  fallback.FragmentKey != guide.FragmentKey, "unproved normal did not retain an isolated fallback");
            const auto fallbackBinary = compiler.CompileGlslToSpv(fallback.Source, shaderc_fragment_shader,
                                                                 "unknown_native_normal", options);
            Check(fallbackBinary.GetCompilationStatus() == shaderc_compilation_status_success,
                  fallbackBinary.GetErrorMessage());
            ++cases;
        }
    }
    std::cout << "native_normal_guide_compile_cases=" << cases << '\n';
    return 0;
}

int main(int argc, char** argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "--balance-check") return CheckBalanceFamily();
    if (argc == 2 && std::string_view(argv[1]) == "--normal-guide-check") return CheckNormalGuides();
    if (argc > 1 && std::string_view(argv[1]) == "--uber") return BuildUberFamily(argc - 1, argv + 1);
    if (argc > 1 && std::string_view(argv[1]) == "--native-lighting-uber") return BuildUberFamily(argc - 1, argv + 1, true);
    if (argc > 1 && std::string_view(argv[1]) == "--casters") return BuildCasterFamily(argc - 1, argv + 1);
    const bool temporal = argc > 1 && std::string_view(argv[1]) == "--temporal";
    const bool toon = argc > 1 && std::string_view(argv[1]) == "--toon";
    if (temporal || toon) { --argc; ++argv; }
    const bool reuseBuiltins = argc > 1 && std::string_view(argv[1]) == "--reuse-builtins";
    if (reuseBuiltins) { --argc; ++argv; }
    const bool verify = argc == 2 && std::string_view(argv[1]) == "--check";
    const bool dumpSources = argc == 3 && std::string_view(argv[1]) == "--sources";
    const bool countOnly = dumpSources || (argc == 2 && std::string_view(argv[1]) == "--count");
    const bool exact = argc == 3 && std::string_view(argv[1]) == "--check-exact";
    Check(!(exact && reuseBuiltins), "exact compiler verification must rebuild all programs");
    Check(argc == 2 || exact || dumpSources, "usage: build_fragment_artifacts [--temporal|--toon] output.spvpack | --check | --count | --sources directory | --check-exact output.spvpack");
    if (dumpSources) std::filesystem::create_directories(argv[2]);
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    SpirvPackFile pack;
    const std::span<const PicaFragmentArtifact> storedArtifacts = toon ? Fast::Extension::ToonFragmentArtifacts()
        : temporal ? Fast::Extension::TemporalFragmentArtifacts() : NativeFragmentArtifacts();
    std::unordered_set<std::string> emittedSources;
    unsigned modules = 0;
    unsigned materialCases = 0, toonStyleCases = 0;
    // The always-available canonical family holds every parametric lighting
    // configuration (Oot3dPicaParametricLighting); the temporal and toon
    // families only lighting with and without its tables bound.
    for (unsigned primaryColor = 0; primaryColor < (toon ? 2U : 1U); ++primaryColor)
    for (unsigned lighting = 0; lighting < (temporal || toon ? 2U : 3U); ++lighting)
    for (unsigned integerTexture = 0; integerTexture < 2; ++integerTexture)
    for (unsigned shadowWrite = 0; shadowWrite < 2; ++shadowWrite) {
        if (toon && shadowWrite) continue;
        auto packet = std::make_unique<Oot3dPicaDrawPacket>();
        Oot3dPicaDecodedDrawState state{};
        packet->Registers[0x8f] = lighting != 0 ? 1U : 0U;
        // Lighting without lookup tables (GenerateOot3dPicaParametricBaseFragmentShader).
        if (lighting == 2) packet->Registers[0x1c4] = 0xFF7BFF00U;
        packet->Registers[0x80] = 1;
        packet->Registers[0x83] = integerTexture ? 2U << 28 : 0;
        state.Textures[0].Enabled = true;
        state.Textures[0].Type = integerTexture ? 2 : 0;
        state.OutputMerger.FragmentOperationMode = shadowWrite ? 3 : 0;
        Oot3dPicaGeneratedFragmentShader shader;
        std::string error;
        Check(GenerateOot3dPicaFragmentShader(*packet, state, shader, &error,
              Oot3dPicaShaderBuildPurpose::OfflineSource, Oot3dPicaTevMode::Parametric), error);
        // Check that material data does not silently introduce another program.
        for (unsigned variant = 0; variant < 16; ++variant) {
            ++materialCases;
            packet->Registers[0xe0] = (variant & 1) ? 5 : 0;
            packet->Registers[0x104] = (variant & 7) << 4 | 1;
            packet->Registers[0xc0] = (variant & 1) ? 0x00030003 : 0;
            packet->Registers[0xc2] = (variant % 6) | (variant % 6) << 16;
            packet->Registers[0xc4] = (variant % 3) | (variant % 3) << 16;
            Oot3dPicaGeneratedFragmentShader candidate;
            Check(GenerateOot3dPicaFragmentShader(*packet, state, candidate, &error,
                  Oot3dPicaShaderBuildPurpose::OfflineSource, Oot3dPicaTevMode::Parametric), error);
            Check(candidate.Source == shader.Source, "material data changed finite fragment family");
            Check(candidate.Hooks.Has(::Oot3d::Renderer::PicaShaderSemantic::NativeFogFactor) &&
                  shader.Hooks.Has(::Oot3d::Renderer::PicaShaderSemantic::NativeFogFactor),
                  "parametric fog hook changed with material activation");
        }
        if (verify && !temporal && !toon && !shadowWrite) {
            // Alpha-tested shadow casters run this program up to its alpha test.
            const auto caster = Fast::Extension::BuildPicaShadowCasterAlphaFragmentShader(
                shader.Source, shader.StateKey, shader.Hooks);
            Check(caster.Applied(), "alpha-tested shadow caster variant was not built");
            const auto binary = compiler.CompileGlslToSpv(caster.Source, shaderc_fragment_shader,
                                                          "alpha_shadow_caster.frag", options);
            Check(binary.GetCompilationStatus() == shaderc_compilation_status_success,
                  "alpha-tested shadow caster variant: " + binary.GetErrorMessage());
        }
        if (toon && primaryColor != 0) {
            // Native TEV may omit vertex color. It is a discrete toon response
            // contract, independent of material identity and continuous style.
            using Semantic = ::Oot3d::Renderer::PicaShaderSemantic;
            shader.Hooks.Semantics = static_cast<Semantic>(
                static_cast<uint32_t>(shader.Hooks.Semantics) &
                ~static_cast<uint32_t>(Semantic::PrimaryColorConsumed));
        }
        std::vector<std::string> sources;
        std::unordered_set<std::string> nriOnlySources;
        if (!temporal && !shadowWrite) {
            using namespace Fast::Extension;
            ToonStyleSettings style;
            PicaFragmentInstrumentationRequest request;
            request.Source = shader.Source;
            request.Hooks = &shader.Hooks;
            request.RequestedFeatures = PicaShaderInstrumentationFeature::Toon;
            request.Draw.DepthTestEnabled = request.Draw.DepthWriteEnabled = true;
            request.Draw.ColorWriteMask = 15;
            request.Draw.CompositionDomain = ::Oot3d::Renderer::PicaCompositionDomain::Scene;
            request.Toon = ToonMode::PicaMaterial;
            request.ToonStyle = &style;
            request.ToonParametersUniform = true;
            const auto uniform = BuildPicaFragmentInstrumentationVariant(request);
            Check(uniform.Applied() && uniform.UsedProvidedHooks, "uniform toon missing typed instrumentation");
            for (unsigned variant = 0; variant < 256; ++variant) {
                ++toonStyleCases;
                style.LightBands = 2 + variant % 5;
                style.CustomLightBands = (variant & 1) != 0;
                style.BandSoftness = float(variant) / 255;
                style.Saturation = float(variant) / 128;
                style.RimWidth = float(variant + 1) / 256;
                style.RimStrength = float(variant) / 256;
                style.ShadowStrength = float(variant) / 255;
                style.ShadowTint = {0.1f, float(variant) / 256, 0.4f};
                style.RimTint = {float(variant) / 256, 0.3f, 0.7f};
                style.LightBandLevels[1] = float(variant) / 512;
                style.LightBandThresholds[0] = float(variant) / 768;
                const auto candidate = BuildPicaFragmentInstrumentationVariant(request);
                Check(candidate.Source == uniform.Source && candidate.FragmentKey == uniform.FragmentKey,
                      "continuous toon style still creates shader variants");
            }
            const auto binary = compiler.CompileGlslToSpv(uniform.Source, shaderc_fragment_shader,
                                                         "uniform_toon", options);
            Check(binary.GetCompilationStatus() == shaderc_compilation_status_success,
                  binary.GetErrorMessage());
            request.Toon = ToonMode::Off;
            const auto off = BuildPicaFragmentInstrumentationVariant(request);
            Check(!off.Applied(), "disabled toon changed the canonical shader");
        }
        if (toon) {
            using namespace Fast::Extension;
            ToonStyleSettings style;
            for (auto mode : {ToonMode::PicaMaterial, ToonMode::PostProcessPreview}) {
                PicaFragmentInstrumentationRequest request;
                request.Source = shader.Source;
                request.Hooks = &shader.Hooks;
                request.RequestedFeatures = PicaShaderInstrumentationFeature::Toon;
                request.Draw.DepthTestEnabled = request.Draw.DepthWriteEnabled = true;
                request.Draw.ColorWriteMask = 15;
                request.Draw.CompositionDomain = ::Oot3d::Renderer::PicaCompositionDomain::Scene;
                request.Toon = mode;
                request.ToonStyle = &style;
                request.ToonParametersUniform = true;
                const auto result = BuildPicaFragmentInstrumentationVariant(request);
                Check(result.Applied() && result.UsedProvidedHooks, "toon family lacks typed hooks");
                sources.push_back(result.Source);
                for (unsigned kind = 0; kind < std::size(kTemporalCoverage); ++kind) {
                    ConfigureTemporal(request, kind);
                    const auto combined = BuildPicaFragmentInstrumentationVariant(request);
                    Check(combined.UsedProvidedHooks && combined.RigidMotionApplied &&
                          HasPicaShaderInstrumentationFeature(combined.AppliedFeatures,
                              PicaShaderInstrumentationFeature::Toon),
                          "toon/temporal family lost typed effect composition");
                    Check(combined.ReactiveCoverage == kTemporalCoverage[kind],
                          "toon/temporal family lost native reactive coverage");
                    for (unsigned variation = 0; variation < 8; ++variation) {
                        auto variedStyle = style;
                        variedStyle.Saturation = float(variation) / 4;
                        variedStyle.LightBands = 2 + variation % 5;
                        variedStyle.CustomLightBands = (variation & 1) != 0;
                        auto variedRequest = request;
                        variedRequest.ToonStyle = &variedStyle;
                        const auto varied = BuildPicaFragmentInstrumentationVariant(variedRequest);
                        Check(varied.Source == combined.Source && varied.FragmentKey == combined.FragmentKey,
                              "temporal composition reintroduced continuous toon variants");
                        ++toonStyleCases;
                    }
                    sources.push_back(combined.Source);
                }
                // The outline consumes typed normal/fog/occlusion guides, not
                // just toon color. Enumerate semantic eligibility predicates;
                // dimensions, material colors, masks and texture identities do
                // not belong to this program family.
                for (unsigned bits = 0; bits < 64; ++bits)
                for (unsigned blendKind = 0; blendKind < 5; ++blendKind)
                for (bool motion : {false, true}) {
                    auto outlined = request;
                    outlined.SceneDomainFeatures.Outline = true;
                    outlined.Draw.CompositionDomain = bits & 1 ? ::Oot3d::Renderer::PicaCompositionDomain::Scene
                        : ::Oot3d::Renderer::PicaCompositionDomain::Ui;
                    outlined.Draw.DepthTestEnabled = (bits & 2) != 0;
                    outlined.Draw.DepthWriteEnabled = (bits & 4) != 0;
                    outlined.Draw.ColorWriteMask = bits & 8 ? 15 : 0;
                    outlined.Draw.DepthCompare = bits & 16 ? ::Oot3d::Renderer::PicaCompareFunction::Always
                        : ::Oot3d::Renderer::PicaCompareFunction::Less;
                    outlined.Draw.PerspectiveProjection = (bits & 32) != 0;
                    outlined.Draw.Blend = {};
                    using Factor = ::Oot3d::Renderer::NativeBlendFactor;
                    outlined.Draw.Blend.Enabled = blendKind != 0;
                    outlined.Draw.Blend.SourceRgb = blendKind == 2 ? Factor::SourceAlpha :
                        blendKind == 3 ? Factor::SourceColor : Factor::One;
                    outlined.Draw.Blend.DestRgb = blendKind == 1 ? Factor::Zero : Factor::One;
                    PicaShaderPipelineRequest drawRequest;
                    drawRequest.Draw.CompositionDomain = outlined.Draw.CompositionDomain;
                    drawRequest.Draw.DepthTestEnabled = outlined.Draw.DepthTestEnabled;
                    drawRequest.Draw.DepthWriteEnabled = outlined.Draw.DepthWriteEnabled;
                    drawRequest.Draw.ColorWriteMask = outlined.Draw.ColorWriteMask;
                    drawRequest.Draw.DepthCompare = outlined.Draw.DepthCompare;
                    drawRequest.Draw.Blend = outlined.Draw.Blend;
                    drawRequest.TemporalMotionEnabled = motion;
                    EffectsSettings effects;
                    effects.Toon = mode;
                    effects.ToonStyle.OutlineEnabled = true;
                    outlined.RequestedFeatures = ResolvePicaDrawInstrumentationFeatures(drawRequest, effects);
                    const auto outlinedShader = BuildPicaFragmentInstrumentationVariant(outlined);
                    Check(outlinedShader.UsedProvidedHooks, "outline family lost typed hooks");
                    if (outlined.Draw.CompositionDomain == ::Oot3d::Renderer::PicaCompositionDomain::Ui)
                        Check(!outlinedShader.Applied(), "offline effect preparation instrumented the UI");
                    if (!outlinedShader.Applied()) continue;
                    sources.push_back(outlinedShader.Source);
                    nriOnlySources.insert(outlinedShader.Source);
                }
            }
        } else if (!temporal) {
            sources.push_back(shader.Source);
        } else {
            using namespace Fast::Extension;
            // Temporal-only rendering always requests rigid motion and reactivity.
            // All native blend states reduce to these four coverage equations.
            for (unsigned kind = 0; kind < std::size(kTemporalCoverage); ++kind) {
                PicaFragmentInstrumentationRequest request;
                request.Source = shader.Source;
                request.Hooks = &shader.Hooks;
                request.Draw.FragmentOperationMode = state.OutputMerger.FragmentOperationMode;
                request.Draw.DepthTestEnabled = true;
                request.Draw.ColorWriteMask = 15;
                request.Draw.CompositionDomain = ::Oot3d::Renderer::PicaCompositionDomain::Scene;
                ConfigureTemporal(request, kind);
                const auto instrumented = BuildPicaFragmentInstrumentationVariant(request);
                Check(instrumented.Applied() && instrumented.UsedProvidedHooks && instrumented.RigidMotionApplied,
                      "temporal family did not use typed motion hooks");
                Check(instrumented.ReactiveCoverage == (shadowWrite ? PicaReactiveCoverage::None : kTemporalCoverage[kind]),
                      "native reactive coverage no longer matches the finite family");
                sources.push_back(instrumented.Source);
            }
        }
        for (const auto& canonicalSource : sources) {
        const auto nri = BuildPicaNriFragmentShaderVariant(canonicalSource);
        Check(nri.Applied, nri.Error);
        for (unsigned separate = 0; separate < 2; ++separate) {
            // New outline binaries target the owned NRI Vulkan backend. The
            // compatibility backend retains its existing source fallback.
            if (!separate && nriOnlySources.contains(canonicalSource)) continue;
            const auto& source = separate ? nri.Source : canonicalSource;
            if (!emittedSources.insert(source).second) continue;
            if (countOnly) {
                if (dumpSources) {
                    std::ofstream file(std::filesystem::path(argv[2]) / (std::to_string(modules) + ".glsl"));
                    Check(bool(file << source), "cannot export generated program source");
                }
                ++modules; continue;
            }
            const auto id = IdentifyPicaShaderSource(source);
            if (verify) {
                const auto stored = FindPicaFragmentArtifact(storedArtifacts, source, separate != 0);
                Check(!stored.empty(), "fragment source/interface changed: regenerate artifacts");
                bool fragmentEntry = false;
                for (size_t pos = 5; pos < stored.size();) {
                    const auto length = stored[pos] >> 16;
                    Check(length > 0 && length <= stored.size() - pos, "invalid stored SPIR-V instruction");
                    if ((stored[pos] & 0xffff) == 15 && length >= 4 && stored[pos+1] == 4)
                        fragmentEntry = true;
                    pos += length;
                }
                Check(fragmentEntry, "stored artifact is not a fragment program");
                ++modules;
                continue;
            }
            std::vector<uint32_t> binary;
            if (reuseBuiltins) {
                const auto stored = FindPicaFragmentArtifact(storedArtifacts, source, separate != 0);
                binary.assign(stored.begin(), stored.end());
            }
            if (binary.empty()) {
                const auto compiled = compiler.CompileGlslToSpv(source, shaderc_fragment_shader,
                                                               "native_fragment", options);
                Check(compiled.GetCompilationStatus() == shaderc_compilation_status_success,
                      compiled.GetErrorMessage());
                binary.assign(compiled.begin(), compiled.end());
            }
            auto& entry = pack.Add(Fast::Renderer::SpirvPackKind::PicaFragment, std::move(binary));
            entry.Flags = separate ? Fast::Renderer::kSpirvPackSeparateSamplers : 0U;
            entry.Identity = {id.Id, id.SecondaryHash, id.Size};
            ++modules;
        }
        }
    }
    if (countOnly) { std::cout << "unique_modules=" << modules << '\n'; return 0; }
    Check(modules == (toon ? 348U : temporal ? 40U : 24U), "unexpected finite fragment family size");
    if (exact)
        Check(pack.Matches(argv[2]), "fragment artifacts are stale; regenerate with the current compiler and generators");
    else if (!verify)
        pack.Write(argv[1]);
    std::cout << (toon ? "toon_fragment_artifacts=" : temporal ? "temporal_fragment_artifacts=" : "native_fragment_artifacts=")
              << modules << " material_invariance_cases=" << materialCases
              << " toon_style_invariance_cases=" << toonStyleCases << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
