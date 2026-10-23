// Offline replay of recorded PICA variant requests (issue #76 strategy).
//
// A session pairs the raw PICA state of each canonical pipeline
// (OOT3D_PICA_RAW_DRAWS) with the distinct variant requests the renderer
// resolved (OOT3D_PICA_VARIANT_REQUESTS). This tool rebuilds each canonical
// plan with this build's native generators, binds the regenerated sources,
// hooks and typed vertex program to the recorded request and calls the real
// PicaShaderPipelineCache::Resolve with the recorded effects. It writes the
// effective shader inventory the AOT compiler consumes and, given the
// session's pipeline inventories, rewrites their recipes to the regenerated
// sources. No game, GPU or shader compiler runs.
//
// With an unchanged generator every regenerated identity must equal the
// recorded witness (--require-witness). With a changed generator the
// witnesses only link recipes; every request must still resolve.
//
// --infer-report is a dry run of inferred coverage (strategy §4): each
// recorded request is resolved again with one unresolved per-draw semantic
// changed at a time (surface availability, receivers, isolation, temporal
// motion, relief, reflection), and the report counts the modules that are not
// already observed. Nothing it finds reaches the inventory: widening the corpus
// is a budgeted decision taken on this report.
//
// --synthesize-cache closes the corpus over programs no session drew (fight
// effects, enemies, cutscenes): every fragment record of a Citra transferable
// cache is transplanted onto a recorded draw (its rasterizer, texturing, TEV,
// lighting and output-merger registers replace the donor's; the vertex side is
// the donor's, and the title has one canonical vertex program). The rebuilt
// canonical program must have the record's own identity, or the record is
// rejected. It is then resolved under every context class the sessions
// recorded for the target effects (--synthesize-effects: the effects every
// given session recorded, i.e. the product profile), with the draw state that
// derives from the record's registers. Synthesized modules carry no pipeline
// recipe: the runtime prepares their pipelines in the background.
//
// Every resolved request, recorded or synthesized, is checked for an
// always-available program (strategy A.1): a parametric base, the effect
// family covering its decisions, a built-in vertex program, the NRI contract
// and a built-in fragment artifact. A draw without one would stall while its
// specialized program compiles; --require-uber makes any such gap a failure.
//
// Alpha-tested shadow casters run their draw's fragment program up to the
// native alpha test. A draw with a parametric (always-available) form derives
// its caster from that parametric base, which the built-in caster family holds
// for the bases it enumerates; any other derives its caster from the canonical
// program. The replay adds every caster the family does not hold as an NRI
// fragment of the corpus.

#include "oot3d_native_pica_draw_state.h"
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_raw_draw_record.h"
#include "oot3d_native_pica_transferable_cache.h"
#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "oot3d_native_vertex_binaries.h"

#include "fast/oot3d/pica_caster_fragment_binaries.h"
#include "fast/oot3d/pica_directional_shadow_caster.h"
#include "fast/oot3d/pica_uber_effect_program.h"
#include "fast/oot3d/pica_uber_fragment_binaries.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/renderer3ds/pica_vertex_artifact.h"

#include "fast/oot3d/pica_aot_shader_pack.h"
#include "fast/oot3d/pica_nri_shader_contract.h"
#include "fast/oot3d/pica_pipeline_manifest.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"
#include "fast/oot3d/pica_variant_request_record.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;
using namespace Oot3dNativeGame;
using Fast::Oot3d::PicaShaderPipelineRequest;
using Fast::Oot3d::PicaVariantShaderIdentity;

// PicaCompareFunction::Always in the alpha test register.
constexpr uint32_t kPicaCompareAlways = 1U;

std::string Hex64(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

PicaVariantShaderIdentity Identity(const Oot3d::Renderer::PicaShaderSourceIdentity& identity) {
    return {identity.Id, identity.SecondaryHash, identity.Size};
}

PicaVariantShaderIdentity Identity(const Fast::Oot3d::PicaAotShaderSourceIdentity& identity) {
    return {identity.Id, identity.SecondaryHash, identity.Size};
}

// A regenerated canonical plan, owning what a request view borrows.
struct CanonicalPlan {
    uint32_t DescriptorSchemaVersion = 0;
    uint64_t VertexKey = 0;
    uint64_t FragmentKey = 0;
    std::string VertexSource;
    std::string FragmentSource;
    Oot3d::Renderer::PicaShaderSourceIdentity VertexIdentity;
    Oot3d::Renderer::PicaShaderSourceIdentity FragmentIdentity;
    std::shared_ptr<const Oot3dPicaTemporalVertexProgram> Temporal;
    Oot3d::Renderer::PicaShaderHookLayout FragmentHooks;
    // The parametric base of the always-available program (empty: none).
    std::string UberSource;
    Oot3d::Renderer::PicaShaderSourceIdentity UberIdentity;
    Oot3d::Renderer::PicaShaderHookLayout UberHooks;
};

// A program as a pipeline recipe names it: output source identities and the
// effective shader keys. One source pair can carry several keys.
using ProgramKey = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;

struct Regenerated {
    PicaVariantShaderIdentity Vertex;
    PicaVariantShaderIdentity Fragment;
    PicaVariantShaderIdentity NriFragment;
    bool NriAvailable = false;
    uint64_t VertexKey = 0;
    uint64_t FragmentKey = 0;
    Fast::Oot3d::PicaShaderInstrumentationFeature Requested{};
    Fast::Oot3d::PicaShaderInstrumentationFeature Applied{};
    Fast::Oot3d::PicaFragmentInstrumentationOutputLayout Outputs;
    bool Canonical = true;
};

struct Options {
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> Sessions;
    std::vector<std::filesystem::path> Pipelines;
    std::filesystem::path ShaderInventory;
    std::filesystem::path PipelineManifest;
    std::filesystem::path Report;
    std::filesystem::path InferReport;
    std::vector<std::filesystem::path> SynthesizeCaches;
    std::vector<std::filesystem::path> SynthesizeEffects;
    std::filesystem::path SynthesisTrace;
    bool RequireWitness = false;
    // Every resolved request has an always-available program (strategy A.1).
    bool RequireUber = false;
    std::filesystem::path UberGapSources;
};

// The request a draw of this canonical plan makes in a recorded context: the
// regenerated sources, hooks and typed program are bound to it.
PicaShaderPipelineRequest BindCanonical(const PicaShaderPipelineRequest& recorded, const CanonicalPlan& canonical) {
    PicaShaderPipelineRequest request = recorded;
    request.VertexShaderSource = canonical.VertexSource;
    request.FragmentShaderSource = canonical.FragmentSource;
    request.VertexShaderKey = canonical.VertexKey;
    request.FragmentShaderKey = canonical.FragmentKey;
    request.VertexShaderSourceIdentity = canonical.VertexIdentity;
    request.FragmentShaderSourceIdentity = canonical.FragmentIdentity;
    if (canonical.Temporal != nullptr)
        request.TemporalVertexProgram = {canonical.Temporal->Hooks, canonical.Temporal->PreviousRegisterState,
                                         canonical.Temporal->PreviousMainBody};
    request.FragmentShaderHooks = canonical.FragmentHooks;
    return request;
}

CanonicalPlan CanonicalFromPlan(const Oot3dPicaVulkanDrawPlan& plan) {
    CanonicalPlan canonical;
    canonical.DescriptorSchemaVersion = plan.CanonicalIdentity.SchemaVersion;
    canonical.VertexKey = plan.VertexShader.StateKey;
    canonical.FragmentKey = plan.FragmentShader.StateKey;
    canonical.VertexSource = std::string(plan.ResolvedVertexShaderSource());
    canonical.FragmentSource = std::string(plan.ResolvedFragmentShaderSource());
    canonical.VertexIdentity = plan.VertexShader.SourceIdentity;
    canonical.FragmentIdentity = plan.FragmentShader.SourceIdentity;
    canonical.Temporal = plan.VertexShader.TemporalProgram;
    canonical.FragmentHooks = plan.FragmentShader.Hooks;
    if (plan.SharedUberFragmentShaderSource != nullptr) {
        canonical.UberSource = *plan.SharedUberFragmentShaderSource;
        canonical.UberIdentity = plan.UberFragmentShader.SourceIdentity;
        canonical.UberHooks = plan.UberFragmentShader.Hooks;
    }
    return canonical;
}

// Why a draw of this variant would have no always-available program, tested
// in the order of GfxRenderingAPIVulkan::ResolveUberNativePicaProgram
// (gfx_vulkan_shaders.cpp) against the built-in families this build links;
// "available" when it has one. The device is assumed to have fragment
// primitive IDs (PBR seams), as the recording machines do.
class UberAvailability {
  public:
    const char* Classify(const Fast::Oot3d::PicaShaderPipelineResult& variant, const CanonicalPlan& canonical) {
        if (canonical.UberSource.empty()) return "no_parametric_program";
        const bool instrumented =
            (static_cast<uint32_t>(variant.AppliedFeatures) &
             ~static_cast<uint32_t>(Fast::Oot3d::PicaShaderInstrumentationFeature::TemporalVertex)) != 0U;
        const Fast::Oot3d::PicaUberEffectProgramRequest superset{
            canonical.UberSource, canonical.UberHooks, variant.TemporalVertexApplied, variant.SurfaceVertexApplied,
            variant.SurfaceVertexApplied};
        if (instrumented && !Fast::Oot3d::PicaUberEffectSupports(variant.Decisions, superset))
            return "effect_not_covered";
        if (Fast::Renderer3ds::FindPicaVertexArtifact(TitleVertexArtifacts(), variant.VertexShaderSource).Spirv.empty())
            return "vertex_not_built_in";
        const auto key = std::make_tuple(canonical.UberIdentity.Id, canonical.UberIdentity.SecondaryHash,
                                         canonical.UberIdentity.Size,
                                         instrumented ? 1U | (superset.TemporalInputs ? 2U : 0U) |
                                                            (superset.SurfaceInputs ? 4U : 0U)
                                                      : 0U);
        auto found = mFragments.find(key);
        if (found == mFragments.end()) {
            const auto nri = Fast::Oot3d::BuildPicaNriFragmentShaderVariant(
                instrumented ? Fast::Oot3d::BuildPicaUberEffectProgram(superset) : canonical.UberSource);
            const char* gap = !nri.Applied ? "no_nri_contract"
                : Fast::Renderer3ds::FindPicaFragmentArtifact(
                      instrumented ? Fast::Oot3d::UberFragmentArtifacts() : Fast::Renderer3ds::NativeFragmentArtifacts(),
                      nri.Source, true).empty() ? "fragment_not_built_in" : "available";
            found = mFragments.emplace(key, gap).first;
        }
        return found->second;
    }

  private:
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, unsigned>, const char*> mFragments;
};

// The draw state a request takes from the draw's own output-merger registers
// (oot3d_native_pica_vulkan_bridge.cpp builds the runtime draw view the same way).
void AssignOutputMergerState(PicaShaderPipelineRequest& request, const Oot3dPicaOutputMergerState& merger) {
    auto& draw = request.Draw;
    draw.FragmentOperationMode = merger.FragmentOperationMode;
    draw.ColorWriteMask = merger.ColorWriteMask;
    draw.DepthTestEnabled = merger.Depth.TestEnabled;
    draw.DepthWriteEnabled = merger.Depth.WriteEnabled;
    draw.DepthCompare = static_cast<Oot3d::Renderer::PicaCompareFunction>(merger.Depth.Compare);
    draw.Blend.Enabled = merger.Blend.Enabled;
    draw.Blend.EquationRgb = static_cast<Oot3d::Renderer::NativeBlendEquation>(merger.Blend.ColorEquation);
    draw.Blend.EquationAlpha = static_cast<Oot3d::Renderer::NativeBlendEquation>(merger.Blend.AlphaEquation);
    draw.Blend.SourceRgb = static_cast<Oot3d::Renderer::NativeBlendFactor>(merger.Blend.SourceColor);
    draw.Blend.DestRgb = static_cast<Oot3d::Renderer::NativeBlendFactor>(merger.Blend.DestinationColor);
    draw.Blend.SourceAlpha = static_cast<Oot3d::Renderer::NativeBlendFactor>(merger.Blend.SourceAlpha);
    draw.Blend.DestAlpha = static_cast<Oot3d::Renderer::NativeBlendFactor>(merger.Blend.DestinationAlpha);
    std::copy(merger.Blend.ConstantColor.begin(), merger.Blend.ConstantColor.end(), draw.Blend.ConstantColor);
}

// A recorded request with everything that derives from its own program and
// output-merger registers cleared: what remains is the context it was drawn in.
PicaShaderPipelineRequest ContextOf(const PicaShaderPipelineRequest& recorded) {
    PicaShaderPipelineRequest context;
    context.Draw = recorded.Draw;
    AssignOutputMergerState(context, {});
    context.TemporalMotionEnabled = recorded.TemporalMotionEnabled;
    context.ReflectionProfile = recorded.ReflectionProfile;
    context.RequireNativeFidelity = recorded.RequireNativeFidelity;
    context.SurfaceLightingAvailable = recorded.SurfaceLightingAvailable;
    context.Pbr = recorded.Pbr;
    context.ReliefTexture = recorded.ReliefTexture;
    return context;
}

// One unresolved per-draw semantic of a variant request, changed in place.
struct InferredSemantic {
    const char* Name;
    void (*Apply)(PicaShaderPipelineRequest& request);
};

constexpr InferredSemantic kInferredSemantics[] = {
    {"surface_lighting", [](PicaShaderPipelineRequest& r) { r.SurfaceLightingAvailable = !r.SurfaceLightingAvailable; }},
    {"directional_shadow_receiver",
     [](PicaShaderPipelineRequest& r) { r.Draw.DirectionalShadowReceiver = !r.Draw.DirectionalShadowReceiver; }},
    {"point_shadow_receiver",
     [](PicaShaderPipelineRequest& r) { r.Draw.PointShadowReceiver = !r.Draw.PointShadowReceiver; }},
    {"isolated_lighting", [](PicaShaderPipelineRequest& r) { r.Draw.IsolatedLighting = !r.Draw.IsolatedLighting; }},
    {"temporal_motion", [](PicaShaderPipelineRequest& r) { r.TemporalMotionEnabled = !r.TemporalMotionEnabled; }},
    {"relief_texture", [](PicaShaderPipelineRequest& r) { r.ReliefTexture = r.ReliefTexture == 0xffU ? 0U : 0xffU; }},
    {"reflection",
     [](PicaShaderPipelineRequest& r) {
         if (r.ReflectionProfile.has_value())
             r.ReflectionProfile.reset();
         else
             r.ReflectionProfile = Fast::Oot3d::ReflectionMaterialParameters{};
     }},
};

int Usage() {
    std::cerr << "usage: oot3d_native_pica_variant_inventory --session RAW_DRAWS VARIANT_REQUESTS [...]\n"
                 "       [--pipelines SESSION_PIPELINE_INVENTORY ...] --shader-inventory OUT\n"
                 "       [--pipeline-manifest OUT] --report OUT [--require-witness] [--require-uber]\n"
                 "       [--uber-gap-sources DIR]\n"
                 "       [--infer-report OUT] [--synthesize-cache CACHE ... --synthesize-effects REQUESTS ...\n"
                 "       [--synthesis-trace OUT]]\n";
    return 64;
}

int Run(const Options& options) {
    if (Fast::Oot3d::PicaFaceNormalGuideDiagnosticRequested()) {
        std::cerr << "OOT3D_GRAPHICS_FACE_NORMAL_GUIDE is set: it changes generated variants; unset it\n";
        return 2;
    }
    Fast::Oot3d::PicaEffectiveShaderInventory inventory;
    std::string error;
    if (!inventory.Configure(options.ShaderInventory, &error)) throw std::runtime_error(error);

    Json sessions = Json::array();
    // Recorded programs (as the session's pipeline inventories name them)
    // -> regenerated programs, for recipe rewriting.
    std::map<ProgramKey, std::set<ProgramKey>> recipeLinks;
    std::map<ProgramKey, Regenerated> regeneratedByIdentity;
    uint64_t totalFailures = 0, totalWitnessMismatches = 0;
    // Inferred coverage dry run: (stage, source id) observed vs. reachable.
    struct InferredEntry {
        uint64_t Candidates = 0, Rejected = 0, SourceBytes = 0;
        std::set<std::pair<uint32_t, uint64_t>> Modules;
    };
    std::set<std::pair<uint32_t, uint64_t>> observedModules, inferredModules;
    uint64_t observedBytes = 0, inferredBytes = 0;
    std::map<std::string, InferredEntry> inferred;
    UberAvailability uber;
    uint64_t totalUberGaps = 0;
    // One sample per distinct gap configuration.
    std::map<std::tuple<std::string, uint64_t, uint32_t, bool, bool>, Json> uberGapSamples;
    const auto countUber = [&](Json& counts, const Fast::Oot3d::PicaShaderPipelineResult& result,
                               const CanonicalPlan& canonical) {
        const std::string gap = uber.Classify(result, canonical);
        counts[gap] = counts.value(gap, uint64_t{0}) + 1U;
        if (gap == "available") return;
        ++totalUberGaps;
        const auto applied = static_cast<uint32_t>(result.AppliedFeatures);
        auto& sample = uberGapSamples[{gap, canonical.UberIdentity.Id, applied, result.TemporalVertexApplied,
                                       result.SurfaceVertexApplied}];
        if (sample.is_null() && !options.UberGapSources.empty()) {
            // The gap's base next to the family base of its configuration.
            std::filesystem::create_directories(options.UberGapSources);
            const bool integer = canonical.UberSource.find("usampler2D pica_texture0") != std::string::npos;
            for (const auto lighting : kOot3dPicaParametricLightings) {
                Oot3dPicaGeneratedFragmentShader family;
                std::string familyError;
                if (GenerateOot3dPicaParametricBaseFragmentShader(lighting, integer, family, &familyError))
                    std::ofstream(options.UberGapSources /
                                  ("family-" + std::to_string(static_cast<unsigned>(lighting)) +
                                   std::to_string(integer) + ".frag")) << family.Source;
            }
            std::ofstream(options.UberGapSources / (Hex64(canonical.UberIdentity.Id) + ".frag"))
                << canonical.UberSource;
        }
        if (sample.is_null())
            sample = {{"gap", gap}, {"base", Hex64(canonical.UberIdentity.Id)},
                      {"canonical_fragment", Hex64(canonical.FragmentIdentity.Id)},
                      {"applied_features", applied}, {"decisions", result.Decisions.Bits},
                      {"temporal_inputs", result.TemporalVertexApplied},
                      {"surface_inputs", result.SurfaceVertexApplied},
                      {"base_explicit_depth", canonical.UberSource.find("gl_FragDepth = pica_depth;") != std::string::npos},
                      {"base_lighting", canonical.UberSource.find("usampler2D pica_lighting_lut;") != std::string::npos},
                      {"base_integer_texture0", canonical.UberSource.find("usampler2D pica_texture0") != std::string::npos},
                      {"base_bytes", canonical.UberSource.size()}, {"count", 0U}};
        sample["count"] = sample["count"].get<uint64_t>() + 1U;
    };

    // Synthesis inputs: the target effects (recorded by every given session),
    // the context classes recorded under them, and a donor draw.
    const bool synthesize = !options.SynthesizeCaches.empty();
    if (synthesize && options.SynthesizeEffects.empty())
        throw std::runtime_error("--synthesize-cache needs --synthesize-effects");
    std::map<std::string, Fast::Oot3d::EffectsSettings> targetEffects;
    for (size_t index = 0; index < options.SynthesizeEffects.size(); ++index) {
        Fast::Oot3d::PicaVariantRequestSession session;
        if (!Fast::Oot3d::LoadPicaVariantRequests(options.SynthesizeEffects[index], session, &error))
            throw std::runtime_error(error);
        std::map<std::string, Fast::Oot3d::EffectsSettings> found;
        for (const auto& [id, effects] : session.Effects)
            found.emplace(Fast::Oot3d::EncodePicaVariantEffects(effects).dump(), effects);
        if (index == 0) {
            targetEffects = std::move(found);
        } else {
            std::erase_if(targetEffects, [&](const auto& entry) { return !found.contains(entry.first); });
        }
    }
    // Effects text -> context classes recorded under those effects.
    std::map<std::string, std::map<std::string, PicaShaderPipelineRequest>> contexts;
    std::optional<Oot3dPicaDrawPacket> donor;
    Oot3dPicaTevMode donorTevMode = Oot3dPicaTevMode::Specialized;
    bool donorUberFragments = false;

    for (const auto& [rawPath, requestPath] : options.Sessions) {
        Oot3dPicaRawDrawSession raw;
        if (!LoadOot3dPicaRawDraws(rawPath, raw, &error)) throw std::runtime_error(error);
        Fast::Oot3d::PicaVariantRequestSession requests;
        if (!Fast::Oot3d::LoadPicaVariantRequests(requestPath, requests, &error)) throw std::runtime_error(error);
        if (requests.FaceNormalGuideDiagnostic)
            throw std::runtime_error(requestPath.string() +
                                     ": recorded with OOT3D_GRAPHICS_FACE_NORMAL_GUIDE; record it again without");

        Oot3dPicaVulkanShaderSourceCache shaderCache;
        shaderCache.TevMode = raw.ParametricTev ? Oot3dPicaTevMode::Parametric : Oot3dPicaTevMode::Specialized;
        shaderCache.UberFragments = raw.UberFragments;
        std::map<uint64_t, CanonicalPlan> plans;
        Json planFailures = Json::array();
        uint64_t canonicalWitnessMismatches = 0, casterFragments = 0, alphaTested = 0, alphaTestedParametric = 0,
                 parametricCastersOutsideFamily = 0;
        for (const auto& draw : raw.Draws) {
            Oot3dPicaDrawSubmission submission;
            submission.Packet = draw.Packet;
            Oot3dPicaVulkanDrawPlan plan;
            if (!DecodeOot3dPicaDrawState(submission.Packet, submission.State, &error) ||
                !BuildOot3dPicaVulkanPipelinePlan(submission, plan, &error, &shaderCache)) {
                if (planFailures.size() < 32U)
                    planFailures.push_back({{"pipeline_id", Hex64(draw.CanonicalPipelineId)}, {"error", error}});
                ++totalFailures;
                continue;
            }
            auto canonical = CanonicalFromPlan(plan);
            const auto& witness = draw.Witness;
            if (canonical.VertexKey != witness.VertexShaderKey ||
                canonical.FragmentKey != witness.FragmentShaderKey ||
                canonical.VertexIdentity.Id != witness.VertexSourceId ||
                canonical.FragmentIdentity.Id != witness.FragmentSourceId)
                ++canonicalWitnessMismatches;
            // Canonical recipes link through their canonical identities.
            regeneratedByIdentity.try_emplace(
                ProgramKey{witness.VertexSourceId, witness.FragmentSourceId, witness.VertexShaderKey,
                           witness.FragmentShaderKey},
                Regenerated{Identity(canonical.VertexIdentity), Identity(canonical.FragmentIdentity), {}, false,
                            canonical.VertexKey, canonical.FragmentKey});
            // The caster the shadow passes derive (gfx_vulkan_pica_draw.cpp:
            // alpha-tested, not Always): from the parametric base when the draw
            // has a valid one, else from the canonical program.
            const auto alphaTest = DecodeOot3dPicaAlphaTest(plan.FragmentShader.Uniforms.FragmentControl[1]);
            const bool parametric = plan.SharedUberFragmentShaderSource != nullptr &&
                                    plan.UberFragmentShader.Hooks.ValidFor(*plan.SharedUberFragmentShaderSource);
            if (alphaTest.Enabled && alphaTest.Compare != kPicaCompareAlways) {
                ++alphaTested;
                if (parametric) ++alphaTestedParametric;
                const auto caster = parametric
                    ? Fast::Oot3d::BuildPicaShadowCasterAlphaFragmentShader(*plan.SharedUberFragmentShaderSource,
                                                                            plan.UberFragmentShader.StateKey,
                                                                            plan.UberFragmentShader.Hooks)
                    : Fast::Oot3d::BuildPicaShadowCasterAlphaFragmentShader(
                          canonical.FragmentSource, canonical.FragmentKey, canonical.FragmentHooks);
                // The runtime resolves a caster from the built-in family first;
                // any other compiles unless the corpus carries it.
                const bool builtIn = parametric && caster.Applied() &&
                    !Fast::Renderer::FindPassShaderArtifact(Fast::Oot3d::CasterFragmentArtifacts(), caster.Source,
                                                            Fast::Renderer::SpirvStage::Fragment, {}).empty();
                if (caster.Applied() && !builtIn) {
                    inventory.Observe(Fast::Oot3d::PicaAotShaderStage::NriFragment, caster.Source,
                                      canonical.DescriptorSchemaVersion, draw.CanonicalPipelineId,
                                      parametric ? plan.UberFragmentShader.StateKey : canonical.FragmentKey, 0U);
                    ++casterFragments;
                    if (parametric) ++parametricCastersOutsideFamily;
                }
            }
            plans.emplace(draw.CanonicalPipelineId, std::move(canonical));
        }

        if (synthesize) {
            if (!donor && !raw.Draws.empty()) {
                donor = raw.Draws.front().Packet;
                donorTevMode = shaderCache.TevMode;
                donorUberFragments = raw.UberFragments;
            }
            // Frame facts (native fidelity, temporal motion, receivers) follow
            // from the effects: a context is replayed only with its own effects.
            std::map<uint32_t, std::string> targeted;
            for (const auto& [id, effects] : requests.Effects)
                if (auto text = Fast::Oot3d::EncodePicaVariantEffects(effects).dump(); targetEffects.contains(text))
                    targeted.emplace(id, std::move(text));
            for (const auto& record : requests.Requests) {
                const auto effects = targeted.find(record.EffectsId);
                if (effects == targeted.end()) continue;
                const auto context = ContextOf(record.Request);
                contexts[effects->second].try_emplace(
                    Fast::Oot3d::EncodePicaVariantRequest(0U, 0U, 0U, context, {}).dump(), context);
            }
        }

        // Variants depend on the effects: resolve one effects value at a time,
        // as the runtime cache does per settings revision.
        std::vector<const Fast::Oot3d::PicaVariantRequestRecord*> ordered;
        for (const auto& request : requests.Requests) ordered.push_back(&request);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto* left, const auto* right) { return left->EffectsId < right->EffectsId; });
        Fast::Oot3d::PicaShaderPipelineCache cache;
        uint64_t unresolvedRaw = 0, resolveFailures = 0, outputWitnessMismatches = 0, instrumented = 0;
        Json failures = Json::array();
        Json uberCounts = Json::object();
        // The recorded request bound to this build's regenerated canonical plan.
        const auto bind = [](const Fast::Oot3d::PicaVariantRequestRecord& record, const CanonicalPlan& canonical) {
            PicaShaderPipelineRequest request = record.Request;
            request.VertexShaderSource = canonical.VertexSource;
            request.FragmentShaderSource = canonical.FragmentSource;
            request.VertexShaderKey = canonical.VertexKey;
            request.FragmentShaderKey = canonical.FragmentKey;
            request.VertexShaderSourceIdentity = canonical.VertexIdentity;
            request.FragmentShaderSourceIdentity = canonical.FragmentIdentity;
            if (canonical.Temporal != nullptr)
                request.TemporalVertexProgram = {canonical.Temporal->Hooks,
                                                 canonical.Temporal->PreviousRegisterState,
                                                 canonical.Temporal->PreviousMainBody};
            request.FragmentShaderHooks = canonical.FragmentHooks;
            return request;
        };
        for (const auto* record : ordered) {
            const auto plan = plans.find(record->CanonicalPipelineId);
            if (plan == plans.end()) {
                ++unresolvedRaw;
                continue;
            }
            const auto& canonical = plan->second;
            const PicaShaderPipelineRequest request = bind(*record, canonical);
            const auto& effects = requests.Effects.at(record->EffectsId);
            cache.BeginFrame(record->EffectsId);
            const Fast::Oot3d::PicaShaderPipelineResult* result = nullptr;
            try {
                result = &cache.Resolve(request, effects);
            } catch (const std::exception& exception) {
                ++resolveFailures;
                if (failures.size() < 32U)
                    failures.push_back({{"pipeline_id", Hex64(record->CanonicalPipelineId)},
                                        {"error", exception.what()}});
                continue;
            }
            const auto witness = Fast::Oot3d::WitnessPicaVariantRequest(request, *result);
            if (!(witness == record->Witness)) ++outputWitnessMismatches;
            if (!result->IsCanonical()) ++instrumented;
            countUber(uberCounts, *result, canonical);
            const auto schema = canonical.DescriptorSchemaVersion;
            inventory.Observe(Fast::Oot3d::PicaAotShaderStage::Vertex, result->VertexShaderSource, schema,
                              record->CanonicalPipelineId, result->VertexShaderKey, record->SettingsRevision);
            inventory.Observe(Fast::Oot3d::PicaAotShaderStage::Fragment, result->FragmentShaderSource, schema,
                              record->CanonicalPipelineId, result->FragmentShaderKey, record->SettingsRevision);
            Regenerated regenerated{Identity(result->VertexShaderSourceIdentity),
                                    Identity(result->FragmentShaderSourceIdentity),
                                    {},
                                    false,
                                    result->VertexShaderKey,
                                    result->FragmentShaderKey,
                                    result->RequestedFeatures,
                                    result->AppliedFeatures,
                                    result->FragmentOutputs,
                                    result->IsCanonical()};
            const auto nri = Fast::Oot3d::BuildPicaNriFragmentShaderVariant(result->FragmentShaderSource);
            observedModules.insert({1U, regenerated.Vertex.Id});
            observedModules.insert({2U, regenerated.Fragment.Id});
            if (nri.Applied) {
                regenerated.NriAvailable = true;
                regenerated.NriFragment = Identity(Fast::Oot3d::IdentifyPicaAotShaderSource(nri.Source));
                inventory.Observe(Fast::Oot3d::PicaAotShaderStage::NriFragment, nri.Source, schema,
                                  record->CanonicalPipelineId, result->FragmentShaderKey, record->SettingsRevision);
                observedModules.insert({3U, regenerated.NriFragment.Id});
                observedBytes += nri.Source.size();
            }
            observedBytes += result->VertexShaderSource.size() + result->FragmentShaderSource.size();
            const ProgramKey recorded{record->Witness.Vertex.Id, record->Witness.Fragment.Id,
                                      record->Witness.VertexKey, record->Witness.FragmentKey};
            const ProgramKey current{regenerated.Vertex.Id, regenerated.Fragment.Id, regenerated.VertexKey,
                                     regenerated.FragmentKey};
            recipeLinks[recorded].insert(current);
            regeneratedByIdentity.insert_or_assign(current, regenerated);
        }
        if (!options.InferReport.empty()) {
            for (const auto& semantic : kInferredSemantics) {
                auto& entry = inferred[semantic.Name];
                for (const auto* record : ordered) {
                    const auto plan = plans.find(record->CanonicalPipelineId);
                    if (plan == plans.end()) continue;
                    auto request = bind(*record, plan->second);
                    semantic.Apply(request);
                    ++entry.Candidates;
                    cache.BeginFrame(record->EffectsId);
                    try {
                        const auto& result = cache.Resolve(request, requests.Effects.at(record->EffectsId));
                        const auto add = [&](uint32_t stage, std::string_view source) {
                            const auto id = Fast::Oot3d::IdentifyPicaAotShaderSource(source).Id;
                            if (observedModules.contains({stage, id})) return;
                            if (entry.Modules.insert({stage, id}).second) entry.SourceBytes += source.size();
                            if (inferredModules.insert({stage, id}).second) inferredBytes += source.size();
                        };
                        add(1U, result.VertexShaderSource);
                        add(2U, result.FragmentShaderSource);
                        if (const auto nri = Fast::Oot3d::BuildPicaNriFragmentShaderVariant(result.FragmentShaderSource);
                            nri.Applied)
                            add(3U, nri.Source);
                    } catch (const std::exception&) {
                        ++entry.Rejected;
                    }
                }
            }
        }
        totalFailures += unresolvedRaw + resolveFailures;
        totalWitnessMismatches += canonicalWitnessMismatches + outputWitnessMismatches;
        sessions.push_back({{"raw_draws", rawPath.string()},
                            {"variant_requests", requestPath.string()},
                            {"parametric_tev", raw.ParametricTev},
                            {"uber_fragments", raw.UberFragments},
                            {"canonical_plans", plans.size()},
                            {"plan_failures", planFailures},
                            {"canonical_witness_mismatches", canonicalWitnessMismatches},
                            {"shadow_caster_fragments", casterFragments},
                            {"alpha_tested_draws", alphaTested},
                            {"alpha_tested_parametric", alphaTestedParametric},
                            {"parametric_casters_outside_family", parametricCastersOutsideFamily},
                            {"effects", requests.Effects.size()},
                            {"requests", requests.Requests.size()},
                            {"instrumented_requests", instrumented},
                            {"requests_without_raw_draw", unresolvedRaw},
                            {"resolve_failures", failures},
                            {"uber_programs", uberCounts},
                            {"output_witness_mismatches", outputWitnessMismatches}});
    }

    Json synthesis = nullptr;
    if (synthesize) {
        if (!donor) throw std::runtime_error("--synthesize-cache needs a session with a raw draw");
        if (targetEffects.empty()) throw std::runtime_error("--synthesize-effects: no effects common to every file");
        Oot3dPicaVulkanShaderSourceCache shaderCache;
        shaderCache.TevMode = donorTevMode;
        shaderCache.UberFragments = donorUberFragments;
        Fast::Oot3d::PicaShaderPipelineCache cache;
        std::set<std::tuple<uint64_t, uint64_t, uint64_t>> programs;
        uint64_t records = 0, duplicates = 0, sourceFailures = 0, transplantFailures = 0, identityMismatches = 0,
                 resolves = 0, resolveRejects = 0, newModules = 0;
        Json rejected = Json::array();
        Json uberCounts = Json::object();
        // One line per resolved combination, for sizing the closure.
        std::ofstream trace;
        if (!options.SynthesisTrace.empty()) {
            trace.open(options.SynthesisTrace);
            if (!trace) throw std::runtime_error("cannot write " + options.SynthesisTrace.string());
            for (const auto& [effects, classes] : contexts)
                for (const auto& [key, context] : classes) trace << Json{{"context", key}}.dump() << '\n';
        }
        for (const auto& path : options.SynthesizeCaches) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) throw std::runtime_error("cannot open cache: " + path.string());
            for (const auto& record : ReadPicaTransferableCache(stream, PicaTransferableDialect::CitraLegacy)) {
                if (record.Stage != PicaTransferableStage::Fragment) continue;
                ++records;
                // The record's own canonical program, as the cache importer builds it.
                Oot3dPicaDrawPacket alone;
                alone.Registers = record.Registers;
                Oot3dPicaDecodedDrawState aloneState;
                Oot3dPicaGeneratedFragmentShader aloneShader;
                if (!DecodeOot3dPicaDrawState(alone, aloneState, &error) ||
                    !GenerateOot3dPicaFragmentShader(alone, aloneState, aloneShader, &error,
                                                     Oot3dPicaShaderBuildPurpose::OfflineSource)) {
                    ++sourceFailures;
                    continue;
                }
                const auto program = Oot3d::Renderer::IdentifyPicaShaderSource(aloneShader.Source);
                const bool recordRasterDepth = Oot3dPicaFragmentDepthIsRasterDepth(aloneState);
                if (!programs.insert({program.Id, program.SecondaryHash, program.Size}).second) {
                    ++duplicates;
                    continue;
                }
                // The specialized text leaves depth to fixed function only for
                // Z-buffering with range -1 and near plane 0
                // (Oot3dPicaFragmentDepthIsRasterDepth), and the record holds
                // the viewport of one draw only: emit both depth forms.
                for (const bool rasterDepth : {true, false}) {
                    const auto depthForm = [rasterDepth](Oot3dPicaDrawPacket& packet) {
                        packet.Registers[0x04DU] = 0xBF0000U;  // depth range -1.0 (float24)
                        packet.Registers[0x04EU] = 0U;         // near plane 0.0
                        packet.Registers[0x06DU] = (packet.Registers[0x06DU] & ~1U) | (rasterDepth ? 1U : 0U);
                    };
                    depthForm(alone);
                    if (!DecodeOot3dPicaDrawState(alone, aloneState, &error) ||
                        !GenerateOot3dPicaFragmentShader(alone, aloneState, aloneShader, &error,
                                                         Oot3dPicaShaderBuildPurpose::OfflineSource)) {
                        ++sourceFailures;
                        continue;
                    }
                    const auto expected = Oot3d::Renderer::IdentifyPicaShaderSource(aloneShader.Source);
                    // Transplant: everything below the geometry pipeline (0x200) is the record's.
                    Oot3dPicaDrawSubmission submission;
                    submission.Packet = *donor;
                    std::copy(record.Registers.begin() + 0x040, record.Registers.begin() + 0x200,
                              submission.Packet.Registers.begin() + 0x040);
                    depthForm(submission.Packet);
                    Oot3dPicaVulkanDrawPlan plan;
                    if (!DecodeOot3dPicaDrawState(submission.Packet, submission.State, &error) ||
                        !BuildOot3dPicaVulkanPipelinePlan(submission, plan, &error, &shaderCache)) {
                        ++transplantFailures;
                        if (rejected.size() < 32U)
                            rejected.push_back({{"program", Hex64(program.Id)}, {"error", error}});
                        continue;
                    }
                    if (plan.FragmentShader.SourceIdentity.Id != expected.Id ||
                        plan.FragmentShader.SourceIdentity.SecondaryHash != expected.SecondaryHash ||
                        plan.FragmentShader.SourceIdentity.Size != expected.Size) {
                        ++identityMismatches;
                        if (rejected.size() < 32U)
                            rejected.push_back({{"program", Hex64(program.Id)}, {"error", "transplanted identity differs"}});
                        continue;
                    }
                    const auto canonical = CanonicalFromPlan(plan);
                    const auto schema = canonical.DescriptorSchemaVersion;
                    const uint64_t pipelineId = plan.CanonicalIdentity.PipelineId;
                    uint64_t effectsIndex = 0;
                    uint64_t contextIndex = 0;
                    for (const auto& [text, effects] : targetEffects) {
                        cache.BeginFrame(++effectsIndex);
                        for (const auto& [key, context] : contexts[text]) {
                            ++contextIndex;
                            auto request = BindCanonical(context, canonical);
                            AssignOutputMergerState(request, plan.State.OutputMerger);
                            ++resolves;
                            const Fast::Oot3d::PicaShaderPipelineResult* result = nullptr;
                            try {
                                result = &cache.Resolve(request, effects);
                            } catch (const std::exception&) {
                                ++resolveRejects;
                                continue;
                            }
                            countUber(uberCounts, *result, canonical);
                            const auto observe = [&](uint32_t stage, Fast::Oot3d::PicaAotShaderStage aot,
                                                     std::string_view source, uint64_t shaderKey) {
                                const auto id = Fast::Oot3d::IdentifyPicaAotShaderSource(source).Id;
                                if (observedModules.insert({stage, id}).second) ++newModules;
                                inventory.Observe(aot, source, schema, pipelineId, shaderKey, 0U);
                            };
                            observe(1U, Fast::Oot3d::PicaAotShaderStage::Vertex, result->VertexShaderSource,
                                    result->VertexShaderKey);
                            observe(2U, Fast::Oot3d::PicaAotShaderStage::Fragment, result->FragmentShaderSource,
                                    result->FragmentShaderKey);
                            const auto nri = Fast::Oot3d::BuildPicaNriFragmentShaderVariant(result->FragmentShaderSource);
                            if (nri.Applied)
                                observe(3U, Fast::Oot3d::PicaAotShaderStage::NriFragment, nri.Source,
                                        result->FragmentShaderKey);
                            if (trace.is_open()) {
                                Json merger = Json::array();
                                for (uint32_t index = 0x100U; index <= 0x107U; ++index)
                                    merger.push_back(record.Registers[index]);
                                trace << Json{{"program", Hex64(program.Id)},
                                              {"record_raster_depth", recordRasterDepth},
                                              {"raster_depth", rasterDepth},
                                              {"effects", effectsIndex},
                                              {"context", contextIndex},
                                              {"lighting", record.Registers[0x08FU]},
                                              {"merger", merger},
                                              {"fragment", Hex64(Fast::Oot3d::IdentifyPicaAotShaderSource(
                                                                     result->FragmentShaderSource).Id)},
                                              {"nri", nri.Applied
                                                          ? Json(Hex64(Fast::Oot3d::IdentifyPicaAotShaderSource(
                                                                           nri.Source).Id))
                                                          : Json(nullptr)}}
                                             .dump()
                                      << '\n';
                            }
                        }
                    }
                }
            }
        }
        synthesis = {{"caches", options.SynthesizeCaches.size()},
                     {"target_effects", targetEffects.size()},
                     {"contexts", std::accumulate(contexts.begin(), contexts.end(), size_t{0},
                                                  [](size_t total, const auto& entry) {
                                                      return total + entry.second.size();
                                                  })},
                     {"fragment_records", records},
                     {"duplicate_programs", duplicates},
                     {"programs", programs.size()},
                     {"source_failures", sourceFailures},
                     {"transplant_failures", transplantFailures},
                     {"identity_mismatches", identityMismatches},
                     {"resolves", resolves},
                     {"resolve_rejects", resolveRejects},
                     {"new_modules", newModules},
                     {"uber_programs", uberCounts},
                     {"rejected", rejected}};
    }
    if (!inventory.Finish(&error)) throw std::runtime_error(error);

    Json recipes = nullptr;
    if (!options.Pipelines.empty()) {
        if (options.PipelineManifest.empty()) throw std::runtime_error("--pipelines needs --pipeline-manifest");
        std::map<uint64_t, Fast::Oot3d::PicaGraphicsPipelineManifestEntry> rewritten;
        uint32_t schema = 0;
        uint64_t input = 0, unlinked = 0, contractChanged = 0, ambiguous = 0;
        for (const auto& path : options.Pipelines) {
            Fast::Oot3d::PicaGraphicsPipelineManifest manifest;
            if (!manifest.Load(path, &error)) throw std::runtime_error(error);
            schema = manifest.DescriptorSchemaVersion();
            for (auto entry : manifest.Entries()) {
                ++input;
                const ProgramKey recorded{entry.VertexSource.Id, entry.FragmentSource.Id, entry.VertexShaderKey,
                                          entry.FragmentShaderKey};
                std::set<ProgramKey> targets;
                if (const auto link = recipeLinks.find(recorded); link != recipeLinks.end()) targets = link->second;
                else if (regeneratedByIdentity.contains(recorded)) targets.insert(recorded);
                if (targets.empty()) {
                    ++unlinked;
                    continue;
                }
                ambiguous += targets.size() > 1U ? 1U : 0U;
                for (const auto& target : targets) {
                    auto copy = entry;
                    const auto found = regeneratedByIdentity.find(target);
                    if (found == regeneratedByIdentity.end()) continue;
                    const auto& program = found->second;
                    copy.VertexShaderKey = program.VertexKey;
                    copy.FragmentShaderKey = program.FragmentKey;
                    copy.VertexSource = {program.Vertex.Id, program.Vertex.SecondaryHash, program.Vertex.Size};
                    copy.FragmentSource = {program.Fragment.Id, program.Fragment.SecondaryHash,
                                           program.Fragment.Size};
                    if (!program.Canonical || program.NriAvailable) {
                        copy.NriFragmentSource = {program.NriFragment.Id, program.NriFragment.SecondaryHash,
                                                  program.NriFragment.Size};
                        copy.NriFragmentAvailable = program.NriAvailable;
                    }
                    if (!program.Canonical) {
                        copy.RequestedFeatures = program.Requested;
                        copy.AppliedFeatures = program.Applied;
                        if (!(copy.ShaderOutputs == program.Outputs)) ++contractChanged;
                        copy.ShaderOutputs = program.Outputs;
                    }
                    rewritten.insert_or_assign(copy.StructuralId(), copy);
                }
            }
        }
        std::vector<Fast::Oot3d::PicaGraphicsPipelineManifestEntry> entries;
        for (auto& [id, entry] : rewritten) entries.push_back(std::move(entry));
        if (!Fast::Oot3d::WritePicaGraphicsPipelineManifest(options.PipelineManifest, schema, entries, &error))
            throw std::runtime_error(error);
        totalFailures += unlinked;
        recipes = {{"input", input},
                   {"written", entries.size()},
                   {"unlinked", unlinked},
                   {"ambiguous", ambiguous},
                   {"output_contract_changed", contractChanged}};
    }

    const Json report = {{"format", "oot3d_pica_variant_replay_v1"},
                         {"sessions", sessions},
                         {"effective_shaders", inventory.EntryCount()},
                         {"recipes", recipes},
                         {"synthesis", synthesis},
                         {"failures", totalFailures},
                         {"witness_mismatches", totalWitnessMismatches},
                         {"require_witness", options.RequireWitness},
                         {"uber_gaps", totalUberGaps},
                         {"uber_gap_samples", [&] {
                              Json samples = Json::array();
                              for (const auto& [key, sample] : uberGapSamples) samples.push_back(sample);
                              return samples;
                          }()},
                         {"require_uber", options.RequireUber}};
    std::ofstream(options.Report) << report.dump(2) << '\n';
    if (!options.InferReport.empty()) {
        Json semantics = Json::object();
        for (const auto& [name, entry] : inferred) {
            Json byStage = {{"vertex", 0}, {"fragment", 0}, {"nri_fragment", 0}};
            for (const auto& [stage, id] : entry.Modules)
                byStage[stage == 1U ? "vertex" : stage == 2U ? "fragment" : "nri_fragment"] =
                    byStage[stage == 1U ? "vertex" : stage == 2U ? "fragment" : "nri_fragment"].get<uint64_t>() + 1U;
            semantics[name] = {{"candidates", entry.Candidates}, {"rejected", entry.Rejected},
                               {"new_modules", byStage}, {"new_source_bytes", entry.SourceBytes}};
        }
        const Json inferredReport = {
            {"format", "oot3d_pica_inferred_coverage_dry_run_v1"},
            {"observed_modules", observedModules.size()},
            {"observed_source_bytes", observedBytes},
            {"inferred_new_modules", inferredModules.size()},
            {"inferred_new_source_bytes", inferredBytes},
            {"semantics", semantics}};
        std::ofstream(options.InferReport) << inferredReport.dump(2) << '\n';
    }
    std::cout << report.dump(2) << '\n';
    if (totalFailures != 0U) return 3;
    if (options.RequireWitness && totalWitnessMismatches != 0U) return 4;
    if (options.RequireUber && totalUberGaps != 0U) return 5;
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto next = [&]() -> std::filesystem::path {
            if (index + 1 >= argc) throw std::runtime_error(argument + " needs a value");
            return argv[++index];
        };
        if (argument == "--session") {
            auto raw = next();
            options.Sessions.emplace_back(raw, next());
        } else if (argument == "--pipelines") {
            options.Pipelines.push_back(next());
        } else if (argument == "--shader-inventory") {
            options.ShaderInventory = next();
        } else if (argument == "--pipeline-manifest") {
            options.PipelineManifest = next();
        } else if (argument == "--report") {
            options.Report = next();
        } else if (argument == "--require-witness") {
            options.RequireWitness = true;
        } else if (argument == "--require-uber") {
            options.RequireUber = true;
        } else if (argument == "--uber-gap-sources") {
            options.UberGapSources = next();
        } else if (argument == "--infer-report") {
            options.InferReport = next();
        } else if (argument == "--synthesize-cache") {
            options.SynthesizeCaches.push_back(next());
        } else if (argument == "--synthesize-effects") {
            options.SynthesizeEffects.push_back(next());
        } else if (argument == "--synthesis-trace") {
            options.SynthesisTrace = next();
        } else {
            return Usage();
        }
    }
    if (options.Sessions.empty() || options.ShaderInventory.empty() || options.Report.empty()) return Usage();
    try {
        return Run(options);
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_variant_inventory: " << exception.what() << '\n';
        return 1;
    }
}
