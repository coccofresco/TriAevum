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
// --synthesize-recorded adds the sessions' own draws as program sources (the
// first register snapshot of each canonical fragment program), for a title
// without a faithful Citra cache; they go through the same transplant and
// identity check after any cache records.
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
#if defined(TRIAEVUM_VARIANT_REPLAY_MM3D)
#include "mm3d_builtin_shader_packs.h"
#endif

#include "fast/extension/pica_caster_fragment_binaries.h"
#include "fast/extension/pica_directional_shadow_caster.h"
#include "fast/extension/pica_uber_effect_program.h"
#include "fast/extension/pica_uber_fragment_binaries.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/renderer3ds/pica_vertex_artifact.h"

#include "fast/extension/pica_aot_shader_pack.h"
#include "fast/extension/pica_nri_shader_contract.h"
#include "fast/extension/pica_pipeline_manifest.h"
#include "fast/extension/pica_shader_pipeline_cache.h"
#include "fast/extension/pica_variant_request_record.h"

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
using Fast::Extension::PicaShaderPipelineRequest;
using Fast::Extension::PicaVariantShaderIdentity;

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

PicaVariantShaderIdentity Identity(const Fast::Extension::PicaAotShaderSourceIdentity& identity) {
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
    Fast::Extension::PicaShaderInstrumentationFeature Requested{};
    Fast::Extension::PicaShaderInstrumentationFeature Applied{};
    Fast::Extension::PicaFragmentInstrumentationOutputLayout Outputs;
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
    // Program source for synthesis besides Citra caches: the register
    // snapshots of the recorded sessions' own draws (one per canonical program).
    bool SynthesizeRecorded = false;
    // Only the vertex donors of the recorded draws, for cache programs (EoT's
    // sources: the cache alone), on a title with several vertex programs.
    bool SynthesizeRecordedDonors = false;
    // Synthesized programs contribute their NRI fragment module only (MC's renderer path).
    bool SynthesizeNriOnly = false;
    // Cache programs the sessions already drew are not synthesized again.
    bool SynthesizeCacheUncovered = false;
    // A cache program is resolved only under the N context classes the
    // sessions drew most often per target effects (0: every class).
    unsigned SynthesizeCacheContexts = 0;
    // The same bound for the recorded programs (--synthesize-recorded).
    unsigned SynthesizeRecordedContexts = 0;
    // Recorded requests with one dynamic per-draw semantic flipped
    // (kInferredSemantics names), added to the inventory (NRI module).
    std::vector<std::string> Neighbours;
    bool RequireWitness = false;
    // Recover only output-equivalent legacy contexts, never synthesize new
    // coverage from unknown inputs. Original witnesses remain mandatory.
    bool RecoverLegacyFragmentContext = false;
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
    const char* Classify(const Fast::Extension::PicaShaderPipelineResult& variant, const CanonicalPlan& canonical) {
        if (canonical.UberSource.empty()) return "no_parametric_program";
        const bool instrumented =
            (static_cast<uint32_t>(variant.AppliedFeatures) &
             ~static_cast<uint32_t>(Fast::Extension::PicaShaderInstrumentationFeature::TemporalVertex)) != 0U;
        const auto superset = Fast::Extension::WithPicaUberLightingPolicy({
            canonical.UberSource, canonical.UberHooks, variant.TemporalVertexApplied, variant.SurfaceVertexApplied,
            variant.SurfaceVertexApplied}, variant.Decisions);
        if (instrumented && !Fast::Extension::PicaUberEffectSupports(variant.Decisions, superset))
            return "effect_not_covered";
        if (Fast::Renderer3ds::FindPicaVertexArtifact(TitleVertexArtifacts(), variant.VertexShaderSource).Spirv.empty())
            return "vertex_not_built_in";
        const auto key = std::make_tuple(canonical.UberIdentity.Id, canonical.UberIdentity.SecondaryHash,
                                         canonical.UberIdentity.Size,
                                         instrumented ? 256U | Fast::Extension::PicaUberEffectVariantKey(superset)
                                                      : 0U);
        auto found = mFragments.find(key);
        if (found == mFragments.end()) {
            const auto nri = Fast::Extension::BuildPicaNriFragmentShaderVariant(
                instrumented ? Fast::Extension::BuildPicaUberEffectProgram(superset) : canonical.UberSource);
            const char* gap = !nri.Applied ? "no_nri_contract"
                : Fast::Renderer3ds::FindPicaFragmentArtifact(
                      instrumented ? Fast::Extension::UberFragmentArtifacts() : Fast::Renderer3ds::NativeFragmentArtifacts(),
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
    context.FragmentLightingOperand = recorded.FragmentLightingOperand;
    context.FragmentLightingBalance = recorded.FragmentLightingBalance;
    context.NativeFragmentLitShadows = recorded.NativeFragmentLitShadows;
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
    context.ShadowProbe = recorded.ShadowProbe;
#endif
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
             r.ReflectionProfile = Fast::Extension::ReflectionMaterialParameters{};
     }},
};

int Usage() {
    std::cerr << "usage: oot3d_native_pica_variant_inventory --session RAW_DRAWS VARIANT_REQUESTS [...]\n"
                 "       [--pipelines SESSION_PIPELINE_INVENTORY ...] --shader-inventory OUT\n"
                 "       [--pipeline-manifest OUT] --report OUT [--require-witness] [--require-uber]\n"
                 "       [--uber-gap-sources DIR]\n"
                 "       [--infer-report OUT] [--synthesize-cache CACHE ... [--synthesize-recorded-donors] | --synthesize-recorded]\n"
                 "       [--synthesize-effects REQUESTS ... [--synthesis-trace OUT]]\n";
    return 64;
}

int Run(const Options& options) {
    if (Fast::Extension::PicaFaceNormalGuideDiagnosticRequested()) {
        std::cerr << "OOT3D_GRAPHICS_FACE_NORMAL_GUIDE is set: it changes generated variants; unset it\n";
        return 2;
    }
    Fast::Extension::PicaEffectiveShaderInventory inventory;
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
    const auto countUber = [&](Json& counts, const Fast::Extension::PicaShaderPipelineResult& result,
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
                      {"base_bytes", canonical.UberSource.size()},
                      // Which PicaUberEffectSupports condition can apply.
                      {"policy", {{"operand", result.Decisions.FragmentLightingOperand},
                                  {"balance", result.Decisions.FragmentLightingBalance},
                                  {"native_shadows", result.Decisions.NativeFragmentLitShadows},
                                  {"pbr_balance", result.Decisions.PbrNativeBalance},
                                  {"pbr_unlit", result.Decisions.PbrNativeUnlit}}},
                      {"paths", {{"normal", static_cast<unsigned>(result.Decisions.Normal)},
                                 {"albedo", static_cast<unsigned>(result.Decisions.Albedo)},
                                 {"ambient", static_cast<unsigned>(result.Decisions.Ambient)}}},
                      {"base_material_normal", canonical.UberHooks.Has(Fast::Renderer3ds::PicaShaderSemantic::MaterialNormal)},
                      {"base_capture_valid", canonical.UberHooks.NativeLightingCapture.Valid(
                          canonical.UberSource.size(), canonical.UberHooks.Offset(Fast::Renderer3ds::PicaShaderHook::PicaLighting))},
                      {"count", 0U}};
        sample["count"] = sample["count"].get<uint64_t>() + 1U;
    };

    // Synthesis inputs: the target effects (recorded by every given session),
    // the context classes recorded under them, and a donor draw.
    const bool synthesize = !options.SynthesizeCaches.empty() || options.SynthesizeRecorded;
    if (synthesize && options.SynthesizeEffects.empty())
        throw std::runtime_error("--synthesize-cache/--synthesize-recorded needs --synthesize-effects");
    // --synthesize-recorded: the first draw of each canonical fragment program
    // the sessions recorded, as a register snapshot (the program source a
    // cache record would be). Programs a session drew only under other effects
    // are thereby resolved under the target effects too.
    // Each recorded source keeps its own draw as donor (its own vertex program:
    // a title with several, e.g. MM3D's 14, has no single canonical one).
    std::vector<Oot3dPicaDrawPacket> recordedPrograms;
    // With --synthesize-recorded, cache records take as donor the draw of the
    // most frequently recorded canonical vertex program instead of the first draw.
    std::map<uint64_t, std::pair<uint64_t, Oot3dPicaDrawPacket>> recordedVertexDonors;
    std::set<std::tuple<uint64_t, uint64_t, uint64_t>> recordedProgramIds;
    std::map<std::string, Fast::Extension::EffectsSettings> targetEffects;
    for (size_t index = 0; index < options.SynthesizeEffects.size(); ++index) {
        Fast::Extension::PicaVariantRequestSession session;
        if (!Fast::Extension::LoadPicaVariantRequests(options.SynthesizeEffects[index], session, &error))
            throw std::runtime_error(error);
        std::map<std::string, Fast::Extension::EffectsSettings> found;
        for (const auto& [id, effects] : session.Effects)
            found.emplace(Fast::Extension::EncodePicaVariantEffects(effects).dump(), effects);
        if (index == 0) {
            targetEffects = std::move(found);
        } else {
            std::erase_if(targetEffects, [&](const auto& entry) { return !found.contains(entry.first); });
        }
    }
    // Effects text -> context classes recorded under those effects.
    std::map<std::string, std::map<std::string, PicaShaderPipelineRequest>> contexts;
    std::map<std::string, std::map<std::string, uint64_t>> contextUses;
    uint64_t neighbourResolves = 0, neighbourModules = 0, cacheCovered = 0;
    std::optional<Oot3dPicaDrawPacket> donor;
    Oot3dPicaTevMode donorTevMode = Oot3dPicaTevMode::Specialized;
    bool donorUberFragments = false;

    for (const auto& [rawPath, requestPath] : options.Sessions) {
        Oot3dPicaRawDrawSession raw;
        if (!LoadOot3dPicaRawDraws(rawPath, raw, &error)) throw std::runtime_error(error);
        Fast::Extension::PicaVariantRequestSession requests;
        if (!Fast::Extension::LoadPicaVariantRequests(requestPath, requests, &error)) throw std::runtime_error(error);
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
                    ? Fast::Extension::BuildPicaShadowCasterAlphaFragmentShader(*plan.SharedUberFragmentShaderSource,
                                                                            plan.UberFragmentShader.StateKey,
                                                                            plan.UberFragmentShader.Hooks)
                    : Fast::Extension::BuildPicaShadowCasterAlphaFragmentShader(
                          canonical.FragmentSource, canonical.FragmentKey, canonical.FragmentHooks);
                // The runtime resolves a caster from the built-in family first;
                // any other compiles unless the corpus carries it.
                const bool builtIn = parametric && caster.Applied() &&
                    !Fast::Renderer::FindPassShaderArtifact(Fast::Extension::CasterFragmentArtifacts(), caster.Source,
                                                            Fast::Renderer::SpirvStage::Fragment, {}).empty();
                if (caster.Applied() && !builtIn) {
                    inventory.Observe(Fast::Extension::PicaAotShaderStage::NriFragment, caster.Source,
                                      canonical.DescriptorSchemaVersion, draw.CanonicalPipelineId,
                                      parametric ? plan.UberFragmentShader.StateKey : canonical.FragmentKey, 0U);
                    ++casterFragments;
                    if (parametric) ++parametricCastersOutsideFamily;
                }
            }
            if (options.SynthesizeRecorded || options.SynthesizeRecordedDonors)
                ++recordedVertexDonors.try_emplace(canonical.VertexIdentity.Id, 0U, draw.Packet).first->second.first;
            plans.emplace(draw.CanonicalPipelineId, std::move(canonical));
        }

        if (synthesize) {
            if (!donor && !raw.Draws.empty()) {
                donor = raw.Draws.front().Packet;
                donorTevMode = shaderCache.TevMode;
                donorUberFragments = raw.UberFragments;
            }
            if (options.SynthesizeRecorded || options.SynthesizeCacheUncovered) {
                for (const auto& draw : raw.Draws) {
                    Oot3dPicaDrawPacket alone;
                    alone.Registers = draw.Packet.Registers;
                    Oot3dPicaDecodedDrawState aloneState;
                    Oot3dPicaGeneratedFragmentShader aloneShader;
                    if (!DecodeOot3dPicaDrawState(alone, aloneState, &error) ||
                        !GenerateOot3dPicaFragmentShader(alone, aloneState, aloneShader, &error,
                                                         Oot3dPicaShaderBuildPurpose::OfflineSource))
                        continue;  // counted as a source failure if synthesis meets it again
                    const auto id = Oot3d::Renderer::IdentifyPicaShaderSource(aloneShader.Source);
                    if (recordedProgramIds.insert({id.Id, id.SecondaryHash, id.Size}).second &&
                        options.SynthesizeRecorded)
                        recordedPrograms.push_back(draw.Packet);
                }
            }
            // Frame facts (native fidelity, temporal motion, receivers) follow
            // from the effects: a context is replayed only with its own effects.
            std::map<uint32_t, std::string> targeted;
            for (const auto& [id, effects] : requests.Effects)
                if (auto text = Fast::Extension::EncodePicaVariantEffects(effects).dump(); targetEffects.contains(text))
                    targeted.emplace(id, std::move(text));
            for (const auto& record : requests.Requests) {
                const auto effects = targeted.find(record.EffectsId);
                if (effects == targeted.end()) continue;
                const auto context = ContextOf(record.Request);
                auto key = Fast::Extension::EncodePicaVariantRequest(0U, 0U, 0U, context, {}).dump();
                ++contextUses[effects->second][key];
                contexts[effects->second].try_emplace(std::move(key), context);
            }
        }

        // Variants depend on the effects: resolve one effects value at a time,
        // as the runtime cache does per settings revision.
        std::vector<const Fast::Extension::PicaVariantRequestRecord*> ordered;
        for (const auto& request : requests.Requests) ordered.push_back(&request);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto* left, const auto* right) { return left->EffectsId < right->EffectsId; });
        Fast::Extension::PicaShaderPipelineCache cache;
        uint64_t unresolvedRaw = 0, resolveFailures = 0, outputWitnessMismatches = 0, instrumented = 0;
        Json failures = Json::array();
        Json recovered = Json::array();
        Json mismatchSamples = Json::array();
        Json uberCounts = Json::object();
        // The recorded request bound to this build's regenerated canonical plan.
        const auto bind = [](const Fast::Extension::PicaVariantRequestRecord& record, const CanonicalPlan& canonical) {
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
            PicaShaderPipelineRequest request = bind(*record, canonical);
            const auto& effects = requests.Effects.at(record->EffectsId);
            cache.BeginFrame(record->EffectsId);
            const Fast::Extension::PicaShaderPipelineResult* result = nullptr;
            try {
                result = &cache.Resolve(request, effects);
            } catch (const std::exception& exception) {
                ++resolveFailures;
                if (failures.size() < 32U)
                    failures.push_back({{"pipeline_id", Hex64(record->CanonicalPipelineId)},
                                        {"error", exception.what()}});
                continue;
            }
            if (options.RecoverLegacyFragmentContext && !record->HasFragmentContext &&
                !(Fast::Extension::WitnessPicaVariantRequest(request, *result) == record->Witness)) {
                // These three booleans were omitted from v1. Enumerate the
                // bounded space; accept only the COMPLETE original witness,
                // including source sizes, both hashes, keys and feature masks.
                // Several contexts can be equivalent under these effects:
                // report all, but do not export guessed inputs for synthesis.
                Json matches = Json::array();
                const auto original = request;
                for (unsigned bits = 0; bits < 8; ++bits) {
                    auto candidate = original;
                    candidate.FragmentLightingOperand = (bits & 1U) != 0;
                    candidate.FragmentLightingBalance = (bits & 2U) != 0;
                    candidate.NativeFragmentLitShadows = (bits & 4U) != 0;
                    const auto& resolved = cache.Resolve(candidate, effects);
                    if (Fast::Extension::WitnessPicaVariantRequest(candidate, resolved) == record->Witness) {
                        if (matches.empty()) { request = candidate; result = &resolved; }
                        matches.push_back(bits);
                    }
                }
                if (!matches.empty()) recovered.push_back({{"pipeline_id", Hex64(record->CanonicalPipelineId)},
                    {"effects", record->EffectsId}, {"matching_context_masks", matches}});
            }
            const auto witness = Fast::Extension::WitnessPicaVariantRequest(request, *result);
            if (!(witness == record->Witness)) {
                ++outputWitnessMismatches;
                if (mismatchSamples.size() < 16U) mismatchSamples.push_back({
                    {"pipeline_id", Hex64(record->CanonicalPipelineId)},
                    {"recorded", Fast::Extension::EncodePicaVariantRequest(0, 0, 0, request, record->Witness)["witness"]},
                    {"replayed", Fast::Extension::EncodePicaVariantRequest(0, 0, 0, request, witness)["witness"]}});
            }
            if (!result->IsCanonical()) ++instrumented;
            countUber(uberCounts, *result, canonical);
            const auto schema = canonical.DescriptorSchemaVersion;
            inventory.Observe(Fast::Extension::PicaAotShaderStage::Vertex, result->VertexShaderSource, schema,
                              record->CanonicalPipelineId, result->VertexShaderKey, record->SettingsRevision);
            inventory.Observe(Fast::Extension::PicaAotShaderStage::Fragment, result->FragmentShaderSource, schema,
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
            const auto nri = Fast::Extension::BuildPicaNriFragmentShaderVariant(result->FragmentShaderSource);
            observedModules.insert({1U, regenerated.Vertex.Id});
            observedModules.insert({2U, regenerated.Fragment.Id});
            if (nri.Applied) {
                regenerated.NriAvailable = true;
                regenerated.NriFragment = Identity(Fast::Extension::IdentifyPicaAotShaderSource(nri.Source));
                inventory.Observe(Fast::Extension::PicaAotShaderStage::NriFragment, nri.Source, schema,
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
        for (const auto& name : options.Neighbours) {
            const auto* semantic = std::find_if(std::begin(kInferredSemantics), std::end(kInferredSemantics),
                                                [&](const auto& item) { return name == item.Name; });
            if (semantic == std::end(kInferredSemantics)) throw std::runtime_error("unknown --neighbour " + name);
            for (const auto* record : ordered) {
                const auto plan = plans.find(record->CanonicalPipelineId);
                if (plan == plans.end()) continue;
                auto request = bind(*record, plan->second);
                semantic->Apply(request);
                cache.BeginFrame(record->EffectsId);
                const Fast::Extension::PicaShaderPipelineResult* result = nullptr;
                try {
                    result = &cache.Resolve(request, requests.Effects.at(record->EffectsId));
                } catch (const std::exception&) {
                    continue;
                }
                ++neighbourResolves;
                countUber(uberCounts, *result, plan->second);
                const auto schema = plan->second.DescriptorSchemaVersion;
                const auto observe = [&](uint32_t stage, Fast::Extension::PicaAotShaderStage aot, std::string_view source,
                                         uint64_t key) {
                    if (observedModules.insert({stage, Fast::Extension::IdentifyPicaAotShaderSource(source).Id}).second)
                        ++neighbourModules;
                    inventory.Observe(aot, source, schema, record->CanonicalPipelineId, key, record->SettingsRevision);
                };
                observe(1U, Fast::Extension::PicaAotShaderStage::Vertex, result->VertexShaderSource, result->VertexShaderKey);
                if (const auto nri = Fast::Extension::BuildPicaNriFragmentShaderVariant(result->FragmentShaderSource);
                    nri.Applied)
                    observe(3U, Fast::Extension::PicaAotShaderStage::NriFragment, nri.Source, result->FragmentShaderKey);
                else
                    observe(2U, Fast::Extension::PicaAotShaderStage::Fragment, result->FragmentShaderSource,
                            result->FragmentShaderKey);
            }
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
                            const auto id = Fast::Extension::IdentifyPicaAotShaderSource(source).Id;
                            if (observedModules.contains({stage, id})) return;
                            if (entry.Modules.insert({stage, id}).second) entry.SourceBytes += source.size();
                            if (inferredModules.insert({stage, id}).second) inferredBytes += source.size();
                        };
                        add(1U, result.VertexShaderSource);
                        add(2U, result.FragmentShaderSource);
                        if (const auto nri = Fast::Extension::BuildPicaNriFragmentShaderVariant(result.FragmentShaderSource);
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
                            {"output_witness_mismatches", outputWitnessMismatches},
                            {"witness_mismatch_samples", mismatchSamples},
                            {"legacy_context_recoveries", recovered}});
    }

    Json synthesis = nullptr;
    if (synthesize) {
        if (!donor) throw std::runtime_error("--synthesize-cache needs a session with a raw draw");
        if (targetEffects.empty()) throw std::runtime_error("--synthesize-effects: no effects common to every file");
        Oot3dPicaVulkanShaderSourceCache shaderCache;
        shaderCache.TevMode = donorTevMode;
        shaderCache.UberFragments = donorUberFragments;
        Fast::Extension::PicaShaderPipelineCache cache;
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
        // One program source (a cache record's or a recorded draw's registers).
        // The cache programs' context classes: the N most used per effects.
        std::map<std::string, std::set<std::string>> cacheContexts, recordedContexts;
        Json contextRanking = Json::array();
        for (const auto& [text, uses] : contextUses) {
            std::vector<std::pair<uint64_t, std::string>> ranked;
            for (const auto& [key, count] : uses) ranked.push_back({count, key});
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            Json counts = Json::array();
            for (size_t index = 0; index < ranked.size(); ++index) {
                counts.push_back(ranked[index].first);
                if (options.SynthesizeCacheContexts == 0U || index < options.SynthesizeCacheContexts)
                    cacheContexts[text].insert(ranked[index].second);
                if (options.SynthesizeRecordedContexts == 0U || index < options.SynthesizeRecordedContexts)
                    recordedContexts[text].insert(ranked[index].second);
            }
            contextRanking.push_back(std::move(counts));
        }
        const auto synthesizeProgram = [&](const std::array<uint32_t, 0x300>& registers,
                                           const Oot3dPicaDrawPacket& donorPacket, bool fromCache = false) {
            {
                const struct { const std::array<uint32_t, 0x300>& Registers; } record{registers};
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
                    return;
                }
                const auto program = Oot3d::Renderer::IdentifyPicaShaderSource(aloneShader.Source);
                const bool recordRasterDepth = Oot3dPicaFragmentDepthIsRasterDepth(aloneState);
                if (fromCache && options.SynthesizeCacheUncovered &&
                    recordedProgramIds.contains({program.Id, program.SecondaryHash, program.Size})) {
                    ++cacheCovered;
                    return;
                }
                if (!programs.insert({program.Id, program.SecondaryHash, program.Size}).second) {
                    ++duplicates;
                    return;
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
                    submission.Packet = donorPacket;
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
                            if (!(fromCache ? cacheContexts : recordedContexts)[text].contains(key)) continue;
                            auto request = BindCanonical(context, canonical);
                            AssignOutputMergerState(request, plan.State.OutputMerger);
                            ++resolves;
                            const Fast::Extension::PicaShaderPipelineResult* result = nullptr;
                            try {
                                result = &cache.Resolve(request, effects);
                            } catch (const std::exception&) {
                                ++resolveRejects;
                                continue;
                            }
                            countUber(uberCounts, *result, canonical);
                            const auto observe = [&](uint32_t stage, Fast::Extension::PicaAotShaderStage aot,
                                                     std::string_view source, uint64_t shaderKey) {
                                const auto id = Fast::Extension::IdentifyPicaAotShaderSource(source).Id;
                                if (observedModules.insert({stage, id}).second) ++newModules;
                                inventory.Observe(aot, source, schema, pipelineId, shaderKey, 0U);
                            };
                            observe(1U, Fast::Extension::PicaAotShaderStage::Vertex, result->VertexShaderSource,
                                    result->VertexShaderKey);
                            const auto nri = Fast::Extension::BuildPicaNriFragmentShaderVariant(result->FragmentShaderSource);
                            // The combined-sampler fragment module serves only the non-NRI
                            // fallback and recipe prewarm; a synthesized program has no
                            // recipe, so with --synthesize-nri-only an NRI module replaces it.
                            if (!options.SynthesizeNriOnly || !nri.Applied)
                                observe(2U, Fast::Extension::PicaAotShaderStage::Fragment, result->FragmentShaderSource,
                                        result->FragmentShaderKey);
                            if (nri.Applied)
                                observe(3U, Fast::Extension::PicaAotShaderStage::NriFragment, nri.Source,
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
                                              {"fragment", Hex64(Fast::Extension::IdentifyPicaAotShaderSource(
                                                                     result->FragmentShaderSource).Id)},
                                              {"nri", nri.Applied
                                                          ? Json(Hex64(Fast::Extension::IdentifyPicaAotShaderSource(
                                                                           nri.Source).Id))
                                                          : Json(nullptr)}}
                                             .dump()
                                      << '\n';
                            }
                        }
                    }
                }
            }
        };
        const Oot3dPicaDrawPacket* cacheDonor = &*donor;
        uint64_t donorDraws = 0;
        for (const auto& [vertex, entry] : recordedVertexDonors)
            if (entry.first > donorDraws) donorDraws = entry.first, cacheDonor = &entry.second;
        // Recorded sources first: a cache record of a program the sessions drew
        // is then a duplicate, resolved with the draw (and vertex program) the
        // program was recorded with rather than with the shared cache donor.
        for (const auto& packet : recordedPrograms) synthesizeProgram(packet.Registers, packet);
        // A cache-only program takes the most recorded vertex program whose
        // composition with it is a built-in vertex artifact (the vertex text
        // follows the fragment's interface); no vertex program is invented.
        std::vector<const Oot3dPicaDrawPacket*> donorsByUse;
        for (const auto& [vertex, entry] : recordedVertexDonors) donorsByUse.push_back(&entry.second);
        std::stable_sort(donorsByUse.begin(), donorsByUse.end(), [&](const auto* a, const auto* b) {
            const auto use = [&](const Oot3dPicaDrawPacket* packet) {
                for (const auto& [vertex, entry] : recordedVertexDonors)
                    if (&entry.second == packet) return entry.first;
                return uint64_t{0};
            };
            return use(a) > use(b);
        });
        const auto donorFor = [&](const std::array<uint32_t, 0x300>& registers) -> const Oot3dPicaDrawPacket& {
            for (const auto* candidate : donorsByUse) {
                Oot3dPicaDrawSubmission submission;
                submission.Packet = *candidate;
                std::copy(registers.begin() + 0x040, registers.begin() + 0x200,
                          submission.Packet.Registers.begin() + 0x040);
                Oot3dPicaVulkanShaderSourceCache probeCache;
                probeCache.TevMode = donorTevMode;
                probeCache.UberFragments = donorUberFragments;
                Oot3dPicaVulkanDrawPlan plan;
                std::string probeError;
                if (!DecodeOot3dPicaDrawState(submission.Packet, submission.State, &probeError) ||
                    !BuildOot3dPicaVulkanPipelinePlan(submission, plan, &probeError, &probeCache))
                    continue;
                if (!Fast::Renderer3ds::FindPicaVertexArtifact(TitleVertexArtifacts(),
                                                              plan.ResolvedVertexShaderSource()).Spirv.empty())
                    return *candidate;
            }
            return *cacheDonor;
        };
        for (const auto& path : options.SynthesizeCaches) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) throw std::runtime_error("cannot open cache: " + path.string());
            for (const auto& record : ReadPicaTransferableCache(stream, PicaTransferableDialect::CitraLegacy))
                if (record.Stage == PicaTransferableStage::Fragment)
                    synthesizeProgram(record.Registers,
                                      donorsByUse.empty() ? *cacheDonor : donorFor(record.Registers), true);
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
        if (options.SynthesizeRecorded) synthesis["recorded_programs"] = recordedPrograms.size();
        if (options.SynthesizeCacheUncovered) synthesis["cache_programs_covered_by_sessions"] = cacheCovered;
        if (options.SynthesizeCacheContexts) synthesis["cache_contexts_per_effects"] = options.SynthesizeCacheContexts;
        if (options.SynthesizeRecordedContexts)
            synthesis["recorded_contexts_per_effects"] = options.SynthesizeRecordedContexts;
        if (options.SynthesizeCacheContexts || options.SynthesizeRecordedContexts)
            synthesis["context_uses_ranked"] = contextRanking;
    }
    if (!inventory.Finish(&error)) throw std::runtime_error(error);

    Json recipes = nullptr;
    if (!options.Pipelines.empty()) {
        if (options.PipelineManifest.empty()) throw std::runtime_error("--pipelines needs --pipeline-manifest");
        std::map<uint64_t, Fast::Extension::PicaGraphicsPipelineManifestEntry> rewritten;
        uint32_t schema = 0;
        uint64_t input = 0, unlinked = 0, contractChanged = 0, ambiguous = 0;
        for (const auto& path : options.Pipelines) {
            Fast::Extension::PicaGraphicsPipelineManifest manifest;
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
        std::vector<Fast::Extension::PicaGraphicsPipelineManifestEntry> entries;
        for (auto& [id, entry] : rewritten) entries.push_back(std::move(entry));
        if (!Fast::Extension::WritePicaGraphicsPipelineManifest(options.PipelineManifest, schema, entries, &error))
            throw std::runtime_error(error);
        totalFailures += unlinked;
        recipes = {{"input", input},
                   {"written", entries.size()},
                   {"unlinked", unlinked},
                   {"ambiguous", ambiguous},
                   {"output_contract_changed", contractChanged}};
    }

    Json report = {{"format", "oot3d_pica_variant_replay_v1"},
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
    if (!options.Neighbours.empty())
        report["neighbours"] = {{"semantics", options.Neighbours}, {"resolves", neighbourResolves},
                                {"new_modules", neighbourModules}};
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
#if defined(TRIAEVUM_VARIANT_REPLAY_MM3D)
    // The vertex families the dual-title runtime has: MM3D's built-in family is
    // installed exactly as the MM3D title session does before renderer use.
    if (!Oot3dNativeGame::InstallTitleBuiltInVertexPack(Oot3dNativeGame::Mm3dBuiltInVertexPack())) {
        std::fprintf(stderr, "MM3D built-in vertex pack installed after first use\n");
        return 2;
    }
#endif
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
        } else if (argument == "--recover-legacy-fragment-context") {
            options.RecoverLegacyFragmentContext = true;
        } else if (argument == "--require-uber") {
            options.RequireUber = true;
        } else if (argument == "--uber-gap-sources") {
            options.UberGapSources = next();
        } else if (argument == "--infer-report") {
            options.InferReport = next();
        } else if (argument == "--synthesize-cache") {
            options.SynthesizeCaches.push_back(next());
        } else if (argument == "--synthesize-recorded") {
            options.SynthesizeRecorded = true;
        } else if (argument == "--synthesize-recorded-donors") {
            options.SynthesizeRecordedDonors = true;
        } else if (argument == "--synthesize-nri-only") {
            options.SynthesizeNriOnly = true;
        } else if (argument == "--synthesize-cache-uncovered") {
            options.SynthesizeCacheUncovered = true;
        } else if (argument == "--synthesize-cache-contexts") {
            options.SynthesizeCacheContexts = static_cast<unsigned>(std::stoul(next()));
        } else if (argument == "--synthesize-recorded-contexts") {
            options.SynthesizeRecordedContexts = static_cast<unsigned>(std::stoul(next()));
        } else if (argument == "--neighbour") {
            options.Neighbours.push_back(next());
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
        if (options.RecoverLegacyFragmentContext &&
            (!options.RequireWitness || !options.SynthesizeCaches.empty() || options.SynthesizeRecorded ||
             !options.InferReport.empty()))
            throw std::runtime_error("legacy recovery requires --require-witness and forbids synthesis/inference");
        return Run(options);
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_variant_inventory: " << exception.what() << '\n';
        return 1;
    }
}
