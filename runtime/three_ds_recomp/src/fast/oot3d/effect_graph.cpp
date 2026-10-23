#include "fast/oot3d/effect_graph.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Fast::Oot3d {
namespace {

[[nodiscard]] constexpr uint8_t StageOrder(EffectStage stage) noexcept {
    return static_cast<uint8_t>(stage);
}

[[nodiscard]] constexpr bool IsPicaGuideOutput(EffectResource resource) noexcept {
    return resource == EffectResource::NormalGuide || resource == EffectResource::MaterialGuide ||
           resource == EffectResource::RigidMotionGuide || resource == EffectResource::AmbientGuide ||
           resource == EffectResource::FogGuide || resource == EffectResource::OutlineGeometryGuide ||
           resource == EffectResource::DiffuseAlbedoGuide;
}

[[nodiscard]] constexpr bool IsExternalResource(EffectResource resource) noexcept {
    return resource == EffectResource::PicaSceneFrame || resource == EffectResource::NativeSceneView ||
           resource == EffectResource::PreparedShadowCasters ||
           resource == EffectResource::SceneColor || resource == EffectResource::SceneBeforeUi ||
           resource == EffectResource::NativeDepth ||
           resource == EffectResource::NativeShadow2D ||
           resource == EffectResource::DirectionalShadowHistory ||
           resource == EffectResource::DirectionalShadowStaticHistory ||
           resource == EffectResource::PointShadowHistory ||
           resource == EffectResource::ContactShadowHistory;
}

[[nodiscard]] constexpr bool IsSemanticResource(EffectResource resource) noexcept {
    return resource == EffectResource::PicaSceneFrame || resource == EffectResource::NativeSceneView ||
           resource == EffectResource::PreparedShadowCasters ||
           resource == EffectResource::ExtensionGeometry || resource == EffectResource::SceneDistanceField;
}

inline constexpr uint64_t kOot3dEffectResourceNamespace =
    0x4F4F543344524553ULL; // OOT3DRES
inline constexpr uint32_t kOot3dEffectResourceSchemaVersion = 1U;

[[nodiscard]] constexpr ::Fast::Renderer::ExtensionResourceIdentity
BuildEffectResourceIdentity(EffectResource resource) noexcept {
    if (resource == EffectResource::Count) {
        return {};
    }
    return {
        kOot3dEffectResourceNamespace,
        static_cast<uint64_t>(resource) + 1U,
        kOot3dEffectResourceSchemaVersion,
    };
}

[[nodiscard]] std::optional<EffectResource> ResolveEffectResource(
    const ::Fast::Renderer::ExtensionResourceIdentity& identity) noexcept {
    if (identity.Namespace != kOot3dEffectResourceNamespace ||
        identity.SchemaVersion != kOot3dEffectResourceSchemaVersion ||
        identity.Resource == 0U ||
        identity.Resource > static_cast<uint64_t>(EffectResource::Count)) {
        return std::nullopt;
    }
    return static_cast<EffectResource>(identity.Resource - 1U);
}

[[nodiscard]] ::Fast::Renderer::ExtensionResourceDeclaration
BuildEffectResourceDeclaration(EffectResource resource) noexcept {
    const bool external = IsExternalResource(resource);
    return {
        BuildEffectResourceIdentity(resource),
        external
            ? ::Fast::Renderer::ExtensionResourceOrigin::External
            : ::Fast::Renderer::ExtensionResourceOrigin::GraphProduced,
        external && !IsSemanticResource(resource)
            ? ::Fast::Renderer::ExtensionInitialBarrierPolicy::Managed
            : ::Fast::Renderer::ExtensionInitialBarrierPolicy::None,
    };
}

[[nodiscard]] constexpr PicaAuxiliaryOutput AuxiliaryOutputFor(EffectResource resource) noexcept {
    switch (resource) {
        case EffectResource::NormalGuide:
            return PicaAuxiliaryOutput::NormalGuide;
        case EffectResource::MaterialGuide:
            return PicaAuxiliaryOutput::MaterialGuide;
        case EffectResource::RigidMotionGuide:
            return PicaAuxiliaryOutput::RigidMotionGuide;
        case EffectResource::AmbientGuide:
            return PicaAuxiliaryOutput::AmbientGuide;
        case EffectResource::FogGuide:
            return PicaAuxiliaryOutput::FogGuide;
        case EffectResource::OutlineGeometryGuide:
            return PicaAuxiliaryOutput::OutlineGeometryGuide;
        case EffectResource::DiffuseAlbedoGuide:
            return PicaAuxiliaryOutput::DiffuseAlbedoGuide;
        default:
            return PicaAuxiliaryOutput::None;
    }
}

[[nodiscard]] bool ValidateContract(const EffectPass& pass, std::string& error) {
    // A fallback producer reads only imported data (checked at compile), so
    // its primary anchor may be as early as the opaque boundary.
    if (pass.FallbackStage &&
        (pass.Contract != EffectContractKind::AuxiliaryOutput ||
         StageOrder(pass.Stage) < StageOrder(EffectStage::BeforeOpaque) ||
         StageOrder(*pass.FallbackStage) <= StageOrder(pass.Stage) ||
         StageOrder(*pass.FallbackStage) > StageOrder(EffectStage::BeforeUi))) {
        error = "invalid auxiliary export fallback stage: " + pass.Name;
        return false;
    }
    bool writesResource = false;
    std::unordered_set<EffectResource> declaredResources;
    for (const auto& use : pass.Resources) {
        if (!declaredResources.insert(use.Resource).second) {
            error = "duplicate resource declaration in effect pass " + pass.Name;
            return false;
        }
        if (!WritesEffectResource(use.Access))
            continue;
        writesResource = true;
        if (pass.Contract == EffectContractKind::Observer) {
            error = "observer effect pass writes a resource: " + pass.Name;
            return false;
        }
        if (pass.Contract == EffectContractKind::AuxiliaryOutput &&
            (IsExternalResource(use.Resource) || IsSemanticResource(use.Resource) ||
             use.Resource == EffectResource::PresentationOutput || use.Resource == EffectResource::Count)) {
            error = "auxiliary-output pass writes a native, semantic or presentation resource: " + pass.Name;
            return false;
        }
        if (pass.Contract == EffectContractKind::AuxiliaryOutput && IsPicaGuideOutput(use.Resource) &&
            pass.Stage != EffectStage::NativeLighting) {
            error = "PICA guide output must be written at NativeLighting: " + pass.Name;
            return false;
        }
    }

    switch (pass.Contract) {
        case EffectContractKind::Observer:
            return true;
        case EffectContractKind::AuxiliaryOutput:
            if (!writesResource) {
                error = "auxiliary-output pass must write an extension resource: " + pass.Name;
                return false;
            }
            return true;
        case EffectContractKind::GeometryProvider:
            if (pass.Stage != EffectStage::BeforeDepth && pass.Stage != EffectStage::BeforeOpaque &&
                pass.Stage != EffectStage::BeforeTransparent) {
                error = "geometry provider has an invalid stage: " + pass.Name;
                return false;
            }
            if (std::none_of(pass.Resources.begin(), pass.Resources.end(),
                             [](const EffectResourceUse& use) {
                                 return use.Resource == EffectResource::ExtensionGeometry &&
                                        WritesEffectResource(use.Access);
                             })) {
                error = "geometry provider must write extension geometry: " + pass.Name;
                return false;
            }
            if (std::any_of(pass.Resources.begin(), pass.Resources.end(),
                            [](const EffectResourceUse& use) {
                                return WritesEffectResource(use.Access) &&
                                       use.Resource != EffectResource::ExtensionGeometry;
                            })) {
                error = "geometry provider writes a non-geometry resource: " + pass.Name;
                return false;
            }
            return true;
        case EffectContractKind::LightingContributor:
            if (pass.Stage != EffectStage::NativeLighting) {
                error = "lighting contributor must run at NativeLighting: " + pass.Name;
                return false;
            }
            if (writesResource) {
                error = "lighting contributor must not write a graph resource: " + pass.Name;
                return false;
            }
            return true;
        case EffectContractKind::WorldPassReplacement:
            if (StageOrder(pass.Stage) < StageOrder(EffectStage::BeforeDepth) ||
                StageOrder(pass.Stage) > StageOrder(EffectStage::AfterTransparent)) {
                error = "world replacement has an invalid stage: " + pass.Name;
                return false;
            }
            return true;
        case EffectContractKind::ComposerPass:
            if (StageOrder(pass.Stage) < StageOrder(EffectStage::AfterOpaque)) {
                error = "composer pass runs before the compositing domain: " + pass.Name;
                return false;
            }
            return true;
    }
    return false;
}

[[nodiscard]] EffectResourceUse Read(EffectResource resource) {
    return { resource, EffectResourceAccess::Read };
}

[[nodiscard]] EffectResourceUse Write(EffectResource resource) {
    return { resource, EffectResourceAccess::Write };
}

} // namespace

::Fast::Renderer::ExtensionResourceIdentity
BuildOot3dEffectResourceIdentity(EffectResource resource) noexcept {
    return BuildEffectResourceIdentity(resource);
}

std::optional<EffectResource> ResolveOot3dEffectResourceIdentity(
    const ::Fast::Renderer::ExtensionResourceIdentity& identity) noexcept {
    return ResolveEffectResource(identity);
}

bool CompiledEffectPass::ReadsResource(
    EffectResource resource) const noexcept {
    return std::any_of(
        Resources.begin(), Resources.end(),
        [resource](const EffectResourceUse& use) {
            return use.Resource == resource &&
                   ReadsEffectResource(use.Access);
        });
}

const CompiledEffectPass* CompiledEffectGraph::FindPass(std::string_view name) const noexcept {
    const auto found = std::find_if(Passes.begin(), Passes.end(),
                                    [name](const CompiledEffectPass& pass) { return pass.Name == name; });
    return found == Passes.end() ? nullptr : &*found;
}

std::optional<EffectResourceLifetime> CompiledEffectGraph::FindLifetime(EffectResource resource) const noexcept {
    const auto found =
        std::find_if(ResourceLifetimes.begin(), ResourceLifetimes.end(),
                     [resource](const EffectResourceLifetime& lifetime) { return lifetime.Resource == resource; });
    return found == ResourceLifetimes.end() ? std::nullopt : std::optional<EffectResourceLifetime>(*found);
}

bool CompiledEffectGraph::ExportsResource(EffectResource resource) const noexcept {
    return std::find(Exports.begin(), Exports.end(), resource) != Exports.end();
}

size_t CompiledEffectGraph::DeclaredBindingCount() const noexcept {
    size_t count = 0U;
    for (const auto& pass : Passes) {
        count += pass.Resources.size();
    }
    return count;
}

bool EffectGraph::Add(EffectPass pass) {
    if (pass.Name.empty())
        return false;
    const std::string name = pass.Name;
    if (!mPasses.emplace(name, std::move(pass)).second)
        return false;
    mInsertionOrder.push_back(name);
    return true;
}

bool EffectGraph::Export(EffectResource resource) {
    if (resource == EffectResource::Count ||
        std::find(mExports.begin(), mExports.end(), resource) != mExports.end()) {
        return false;
    }
    mExports.push_back(resource);
    return true;
}

void EffectGraph::Clear() {
    mPasses.clear();
    mInsertionOrder.clear();
    mExports.clear();
}

CompiledEffectGraph EffectGraph::Compile() const {
    enum class Mark : uint8_t { None, Visiting, Visited };

    CompiledEffectGraph result;
    for (const auto& name : mInsertionOrder) {
        const auto& pass = mPasses.at(name);
        if (pass.Enabled && !ValidateContract(pass, result.Error)) {
            return result;
        }
        if (!pass.Enabled || !pass.FallbackStage) continue;
        // Imports remain externally owned and exports remain live through graph
        // completion. A delayed producer must not invalidate a same-frame read
        // or a transient resource lifetime compiled at its primary stage.
        for (const auto& use : pass.Resources) {
            if ((ReadsEffectResource(use.Access) && !IsExternalResource(use.Resource)) ||
                (WritesEffectResource(use.Access) &&
                 (!IsAuxiliarySceneData(use.Resource) ||
                  std::find(mExports.begin(), mExports.end(), use.Resource) == mExports.end()))) {
                result.Error = "fallback pass requires imported inputs and auxiliary exports: " + name;
                return result;
            }
            if (!WritesEffectResource(use.Access)) continue;
            for (const auto& [otherName, other] : mPasses) {
                if (!other.Enabled || otherName == name) continue;
                if (std::any_of(other.Resources.begin(), other.Resources.end(),
                                [&](const EffectResourceUse& read) {
                                    return read.Resource == use.Resource && ReadsEffectResource(read.Access);
                                })) {
                    result.Error = "fallback export has a same-frame consumer: " + name;
                    return result;
                }
            }
        }
    }

    std::vector<std::string> schedule;
    schedule.reserve(mInsertionOrder.size());
    for (const auto& name : mInsertionOrder) {
        if (mPasses.at(name).Enabled)
            schedule.push_back(name);
    }
    std::stable_sort(schedule.begin(), schedule.end(), [this](const std::string& left, const std::string& right) {
        return StageOrder(mPasses.at(left).Stage) < StageOrder(mPasses.at(right).Stage);
    });

    std::unordered_map<std::string, Mark> marks;
    std::function<bool(const std::string&)> visit = [&](const std::string& name) {
        const auto found = mPasses.find(name);
        if (found == mPasses.end() || !found->second.Enabled)
            return true;
        auto& mark = marks[name];
        if (mark == Mark::Visited)
            return true;
        if (mark == Mark::Visiting) {
            result.Error = "effect dependency cycle at " + name;
            return false;
        }
        mark = Mark::Visiting;
        for (const auto& dependency : found->second.DependsOn) {
            const auto dependencyPass = mPasses.find(dependency);
            if (dependencyPass == mPasses.end()) {
                result.Error = "missing effect dependency " + dependency;
                return false;
            }
            if (dependencyPass->second.Enabled &&
                StageOrder(dependencyPass->second.FallbackStage.value_or(dependencyPass->second.Stage)) >
                    StageOrder(found->second.Stage)) {
                result.Error = "effect dependency crosses stages " + dependency + " -> " + name;
                return false;
            }
            if (!visit(dependency))
                return false;
        }
        mark = Mark::Visited;
        result.Order.push_back(name);
        result.Passes.push_back({ name, found->second.Stage, found->second.Contract, found->second.Resources,
                                  found->second.FallbackStage });
        return true;
    };

    for (const auto& name : schedule) {
        if (!visit(name))
            return result;
    }

    ::Fast::Renderer::ExtensionResourceGraphInput resourceInput;
    resourceInput.Resources.reserve(
        static_cast<size_t>(EffectResource::Count));
    for (uint8_t value = 0U;
         value < static_cast<uint8_t>(EffectResource::Count); ++value) {
        resourceInput.Resources.push_back(BuildEffectResourceDeclaration(
            static_cast<EffectResource>(value)));
    }

    std::unordered_map<std::string, uint64_t> passIds;
    passIds.reserve(result.Passes.size());
    for (size_t passIndex = 0U; passIndex < result.Passes.size(); ++passIndex) {
        passIds.emplace(result.Passes[passIndex].Name, passIndex + 1U);
    }
    resourceInput.Passes.reserve(result.Passes.size());
    for (size_t passIndex = 0U; passIndex < result.Passes.size(); ++passIndex) {
        const auto& pass = result.Passes[passIndex];
        ::Fast::Renderer::ExtensionResourcePassDeclaration declaration;
        declaration.PassId = passIndex + 1U;
        declaration.DebugName = pass.Name;
        for (const auto& dependency : mPasses.at(pass.Name).DependsOn) {
            const auto dependencyId = passIds.find(dependency);
            if (dependencyId != passIds.end()) {
                declaration.DependsOn.push_back(dependencyId->second);
            }
        }
        declaration.Resources.reserve(pass.Resources.size());
        for (const auto& use : pass.Resources) {
            result.Attachments.AuxiliaryOutputs |=
                AuxiliaryOutputFor(use.Resource);
            declaration.Resources.push_back(
                {BuildEffectResourceIdentity(use.Resource), use.Access});
        }
        resourceInput.Passes.push_back(std::move(declaration));
    }
    resourceInput.Exports.reserve(mExports.size());
    for (const EffectResource resource : mExports) {
        resourceInput.Exports.push_back(
            BuildEffectResourceIdentity(resource));
    }

    auto resourceGraph =
        ::Fast::Renderer::CompileExtensionResourceGraph(resourceInput);
    if (!resourceGraph.Valid()) {
        result.Error = resourceGraph.Error;
        return result;
    }
    result.ResourceLifetimes.reserve(resourceGraph.Lifetimes.size());
    for (const auto& lifetime : resourceGraph.Lifetimes) {
        const auto resource = ResolveEffectResource(lifetime.Resource);
        if (!resource.has_value()) {
            result.Error = "portable resource graph returned an unknown OOT3D resource";
            return result;
        }
        result.ResourceLifetimes.push_back({
            *resource,
            lifetime.FirstUse,
            lifetime.LastUse,
            lifetime.ReadCount,
            lifetime.WriteCount,
        });
    }
    result.Barriers.reserve(resourceGraph.Barriers.size());
    for (const auto& barrier : resourceGraph.Barriers) {
        const auto resource = ResolveEffectResource(barrier.Resource);
        if (!resource.has_value()) {
            result.Error = "portable resource graph returned an unknown OOT3D barrier";
            return result;
        }
        result.Barriers.push_back({
            *resource,
            barrier.ProducerPass,
            barrier.ConsumerPass,
            barrier.Before,
            barrier.After,
        });
    }
    result.Exports.reserve(resourceGraph.Exports.size());
    for (const auto& exported : resourceGraph.Exports) {
        const auto resource = ResolveEffectResource(exported);
        if (!resource.has_value()) {
            result.Error = "portable resource graph returned an unknown OOT3D export";
            return result;
        }
        result.Exports.push_back(*resource);
    }
    result.PortableResources = std::move(resourceGraph);
    return result;
}

std::string_view PicaExtensionPassName(PicaExtensionPass pass) noexcept {
    switch (pass) {
        case PicaExtensionPass::Guides: return "PicaAuxiliaryOutputs";
        case PicaExtensionPass::DirectionalShadowLighting: return "DirectionalShadowLighting";
        case PicaExtensionPass::DirectionalShadowMap: return "DirectionalShadowMap";
        case PicaExtensionPass::PointShadowLighting: return "PointShadowLighting";
        case PicaExtensionPass::PointShadowMap: return "PointShadowMap";
        case PicaExtensionPass::AmbientOcclusion: return "AmbientOcclusion";
        case PicaExtensionPass::Reflections: return "Reflections";
        case PicaExtensionPass::TemporalReconstruction: return "TemporalReconstruction";
        case PicaExtensionPass::InteractiveGrass: return "InteractiveGrass";
        case PicaExtensionPass::ToonOutline: return "ToonOutline";
        case PicaExtensionPass::SceneDistanceField: return "SceneDistanceField";
        case PicaExtensionPass::Count: return {};
    }
    return {};
}

bool PicaExtensionPassEnabled(const CompiledEffectGraph& graph,
                              PicaExtensionPass pass) noexcept {
    return graph.FindPass(PicaExtensionPassName(pass)) != nullptr;
}

CompiledEffectGraph BuildPicaExtensionGraph(const PicaAttachmentFeatureRequests& requests) {
    EffectGraph graph;
    std::vector<EffectResourceUse> guideOutputs;
    if (requests.AmbientOcclusion || requests.ToonOutline || requests.Reflections ||
        requests.GlobalIllumination) {
        guideOutputs.push_back(Write(EffectResource::NormalGuide));
    }
    if (requests.Reflections || requests.TemporalReconstruction || requests.GlobalIllumination) {
        guideOutputs.push_back(Write(EffectResource::MaterialGuide));
    }
    if (requests.TemporalReconstruction || requests.ToonOutline) {
        guideOutputs.push_back(Write(EffectResource::RigidMotionGuide));
    }
    if (requests.AmbientOcclusion) {
        guideOutputs.push_back(Write(EffectResource::AmbientGuide));
    }
    if (requests.ToonOutline) {
        guideOutputs.push_back(Write(EffectResource::FogGuide));
    }
    // The geometry guide carries the outline's native normal and depth and
    // the face normal of the visible surface.
    if (requests.ToonOutline || requests.FaceNormal) {
        guideOutputs.push_back(Write(EffectResource::OutlineGeometryGuide));
    }
    if (requests.GlobalIllumination) {
        guideOutputs.push_back(Write(EffectResource::DiffuseAlbedoGuide));
    }
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::Guides)),
                .Enabled = !guideOutputs.empty(),
                .Stage = EffectStage::NativeLighting,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = std::move(guideOutputs) });
    // Contact shadows: the shadow map producer also writes the screen-space
    // visibility of its light, and the receivers read it.
    const bool contactShadows = requests.DirectionalShadows && requests.ContactShadows;
    std::vector<EffectResourceUse> shadowLightingResources{ Read(EffectResource::NativeSceneView),
                                                            Read(EffectResource::DirectionalShadowHistory) };
    if (contactShadows)
        shadowLightingResources.push_back(Read(EffectResource::ContactShadowHistory));
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::DirectionalShadowLighting)),
                .Enabled = requests.DirectionalShadows,
                .Stage = EffectStage::NativeLighting,
                .Contract = EffectContractKind::LightingContributor,
                .Resources = std::move(shadowLightingResources) });
    // This sample's casters are prepared before the sequence executes, so the
    // map is complete before the first opaque receiver samples it. The late
    // anchor remains for targets that publish no opaque boundary.
    std::vector<EffectResourceUse> shadowMapResources{ Read(EffectResource::PreparedShadowCasters),
                                                       Read(EffectResource::DirectionalShadowHistory),
                                                       Read(EffectResource::DirectionalShadowStaticHistory),
                                                       Write(EffectResource::DirectionalShadowStaticCache),
                                                       Write(EffectResource::DirectionalShadowMap) };
    if (contactShadows) {
        shadowMapResources.push_back(Read(EffectResource::ContactShadowHistory));
        shadowMapResources.push_back(Write(EffectResource::ContactShadowMap));
    }
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::DirectionalShadowMap)),
                .Enabled = requests.DirectionalShadows,
                .Stage = EffectStage::BeforeOpaque,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = std::move(shadowMapResources),
                .FallbackStage = EffectStage::BeforeUi });
    if (requests.DirectionalShadows) {
        graph.Export(EffectResource::DirectionalShadowMap);
        graph.Export(EffectResource::DirectionalShadowStaticCache);
        if (contactShadows)
            graph.Export(EffectResource::ContactShadowMap);
    }
    // Point lights: receivers read the published cube atlas; the producer
    // renders it from the same prepared casters at the opaque boundary.
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::PointShadowLighting)),
                .Enabled = requests.PointShadows,
                .Stage = EffectStage::NativeLighting,
                .Contract = EffectContractKind::LightingContributor,
                .Resources = { Read(EffectResource::NativeSceneView),
                               Read(EffectResource::PointShadowHistory) } });
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::PointShadowMap)),
                .Enabled = requests.PointShadows,
                .Stage = EffectStage::BeforeOpaque,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = { Read(EffectResource::PreparedShadowCasters),
                               Read(EffectResource::PointShadowHistory),
                               Write(EffectResource::PointShadowMap) },
                .FallbackStage = EffectStage::BeforeUi });
    if (requests.PointShadows) {
        graph.Export(EffectResource::PointShadowMap);
    }
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::AmbientOcclusion)),
                .DependsOn = { std::string(PicaExtensionPassName(PicaExtensionPass::Guides)) },
                .Enabled = requests.AmbientOcclusion,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeSceneView), Read(EffectResource::NativeDepth),
                               Read(EffectResource::NormalGuide), Write(EffectResource::AmbientOcclusion) } });
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::Reflections)),
                .DependsOn = { std::string(PicaExtensionPassName(PicaExtensionPass::Guides)) },
                .Enabled = requests.Reflections,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::PicaSceneFrame), Read(EffectResource::NativeSceneView),
                               Read(EffectResource::SceneColor), Read(EffectResource::NativeDepth),
                               Read(EffectResource::NormalGuide), Read(EffectResource::MaterialGuide),
                               Write(EffectResource::ReflectionColor) } });
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::TemporalReconstruction)),
                .DependsOn = { std::string(PicaExtensionPassName(PicaExtensionPass::Guides)) },
                .Enabled = requests.TemporalReconstruction,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeSceneView), Read(EffectResource::NativeDepth),
                               Read(EffectResource::MaterialGuide), Read(EffectResource::RigidMotionGuide),
                               Write(EffectResource::MotionVectors),
                               Write(EffectResource::ReactiveMask) } });
    std::vector<EffectResourceUse> grassResources{
        Read(EffectResource::PicaSceneFrame), Read(EffectResource::NativeSceneView),
        Write(EffectResource::ExtensionGeometry)};
    // Its coarse grid reads the static casters' depth (no actor shadows) and
    // its refinement near the actors the composed map.
    if (requests.DirectionalShadows) {
        grassResources.push_back(Read(EffectResource::DirectionalShadowHistory));
        grassResources.push_back(Read(EffectResource::DirectionalShadowStaticHistory));
    }
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::InteractiveGrass)),
                .Enabled = requests.InteractiveGrass,
                .Stage = EffectStage::BeforeTransparent,
                .Contract = EffectContractKind::GeometryProvider,
                .Resources = std::move(grassResources) });
    if (requests.InteractiveGrass) {
        graph.Export(EffectResource::ExtensionGeometry);
    }
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::ToonOutline)),
                .DependsOn = { std::string(PicaExtensionPassName(PicaExtensionPass::Guides)) },
                .Enabled = requests.ToonOutline,
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeDepth), Read(EffectResource::OutlineGeometryGuide),
                               Read(EffectResource::NormalGuide), Read(EffectResource::RigidMotionGuide) } });
    // The distance field voxelizes the native opaque geometry of the frame; it
    // changes no native target and publishes a persistent field (a
    // screen-space GI gathers from the guides instead).
    graph.Add({ .Name = std::string(PicaExtensionPassName(PicaExtensionPass::SceneDistanceField)),
                .Enabled = requests.GlobalIllumination && !requests.ScreenSpaceGlobalIllumination,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::Observer,
                .Resources = { Read(EffectResource::PicaSceneFrame), Read(EffectResource::NativeSceneView) } });
    return graph.Compile();
}

} // namespace Fast::Oot3d
