// Developer build: a finite interface family derived from the maintained PICA
// equations. No ROM, register capture, gameplay inventory or driver cache input.
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/renderer3ds/pica_nri_shader_contract.h"
#include "fast/oot3d/pica_shader_instrumentation.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"
#include "fast/oot3d/toon_surface_response.h"
#include "fast/oot3d/pica_temporal_fragment_binaries.h"
#include "fast/oot3d/pica_toon_fragment_binaries.h"
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <unordered_set>

using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;

static void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

static constexpr Fast::Oot3d::PicaReactiveCoverage kTemporalCoverage[]{
    Fast::Oot3d::PicaReactiveCoverage::None, Fast::Oot3d::PicaReactiveCoverage::SourceAlpha,
    Fast::Oot3d::PicaReactiveCoverage::SourceColor, Fast::Oot3d::PicaReactiveCoverage::Full};

static void ConfigureTemporal(Fast::Oot3d::PicaFragmentInstrumentationRequest& request, unsigned kind) {
    using namespace Fast::Oot3d;
    using Factor = ::Oot3d::Renderer::NativeBlendFactor;
    request.RequestedFeatures |= PicaShaderInstrumentationFeature::RigidMotionGuide |
        PicaShaderInstrumentationFeature::ReactiveMask;
    request.Draw.Blend.Enabled = kind != 0;
    request.Draw.Blend.SourceRgb = kind == 1 ? Factor::SourceAlpha :
        kind == 2 ? Factor::SourceColor : Factor::One;
    request.Draw.Blend.DestRgb = Factor::One;
}

int main(int argc, char** argv) try {
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
    Check(argc == 2 || exact || dumpSources, "usage: build_fragment_artifacts [--temporal|--toon] output.h | --check | --count | --sources directory | --check-exact output.h");
    if (dumpSources) std::filesystem::create_directories(argv[2]);
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    std::ostringstream output, table;
    output << "// Generated from maintained PICA equations, not captured game shaders.\n"
              "// Regenerate with tools/renderer/tev_program/build_fragment_artifacts.\n"
              "// Vulkan 1.1, shaderc performance optimization; canonical and NRI interfaces.\n";
    output << ((temporal || toon) ? "// Optional effect family, isolated from canonical programs.\n"
                          "#pragma once\n#include \"fast/renderer3ds/pica_fragment_artifact.h\"\n"
                        : "#pragma once\n#include \"pica_fragment_artifact.h\"\n");
    output << ((temporal || toon) ? "namespace Fast::Oot3d {\n" : "namespace Fast::Renderer3ds {\n");
    const char* symbol = toon ? "kToonFragment" : temporal ? "kTemporalFragment" : "kNativeFragment";
    const std::span<const PicaFragmentArtifact> storedArtifacts = toon
        ? std::span<const PicaFragmentArtifact>(Fast::Oot3d::kToonFragmentArtifacts) : temporal
        ? std::span<const PicaFragmentArtifact>(Fast::Oot3d::kTemporalFragmentArtifacts)
        : std::span<const PicaFragmentArtifact>(kNativeFragmentArtifacts);
    std::unordered_set<std::string> emittedSources;
    unsigned modules = 0;
    unsigned materialCases = 0, toonStyleCases = 0;
    for (unsigned primaryColor = 0; primaryColor < (toon ? 2U : 1U); ++primaryColor)
    for (unsigned lighting = 0; lighting < 2; ++lighting)
    for (unsigned integerTexture = 0; integerTexture < 2; ++integerTexture)
    for (unsigned shadowWrite = 0; shadowWrite < 2; ++shadowWrite) {
        if (toon && shadowWrite) continue;
        auto packet = std::make_unique<Oot3dPicaDrawPacket>();
        Oot3dPicaDecodedDrawState state{};
        packet->Registers[0x8f] = lighting;
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
            using namespace Fast::Oot3d;
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
            using namespace Fast::Oot3d;
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
            using namespace Fast::Oot3d;
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
            output << "inline constexpr uint32_t " << symbol << "Spirv" << modules << "[] = {\n";
            unsigned column = 0;
            for (auto word : binary) {
                output << "0x" << std::hex << word << "U,";
                if (++column % 12 == 0) output << '\n';
            }
            output << std::dec << "\n};\n";
            table << "{{" << id.Id << "ULL," << id.SecondaryHash << "ULL," << id.Size
                  << "ULL}," << (separate ? "true" : "false") << "," << symbol << "Spirv" << modules << "},\n";
            ++modules;
        }
        }
    }
    if (countOnly) { std::cout << "unique_modules=" << modules << '\n'; return 0; }
    Check(modules == (toon ? 348U : temporal ? 40U : 16U), "unexpected finite fragment family size");
    output << ((temporal || toon) ? "inline const Fast::Renderer3ds::PicaFragmentArtifact "
                        : "inline const PicaFragmentArtifact ")
           << symbol << "Artifacts[] = {\n" << table.str()
           << ((temporal || toon) ? "};\n} // namespace Fast::Oot3d\n"
                        : "};\n} // namespace Fast::Renderer3ds\n");
    if (exact) {
        std::ifstream file(argv[2], std::ios::binary);
        Check(bool(file), "cannot read fragment artifacts");
        const std::string existing((std::istreambuf_iterator<char>(file)), {});
        Check(existing == output.str(), "fragment artifacts are stale; regenerate with the current compiler and generators");
    } else if (!verify) {
        std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
        Check(bool(file) && bool(file << output.str()), "cannot write fragment artifacts");
    }
    std::cout << (toon ? "toon_fragment_artifacts=" : temporal ? "temporal_fragment_artifacts=" : "native_fragment_artifacts=")
              << modules << " material_invariance_cases=" << materialCases
              << " toon_style_invariance_cases=" << toonStyleCases << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
