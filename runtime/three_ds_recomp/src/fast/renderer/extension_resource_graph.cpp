#include "fast/renderer/extension_resource_graph.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Fast::Renderer {
namespace {

using ResourceMap = std::unordered_map<
    ExtensionResourceIdentity, ExtensionResourceDeclaration,
    ExtensionResourceIdentityHash>;

[[nodiscard]] std::string PassLabel(
    const ExtensionResourcePassDeclaration& pass) {
    return pass.DebugName.empty() ? std::to_string(pass.PassId)
                                  : pass.DebugName;
}

} // namespace

size_t ExtensionResourceIdentityHash::operator()(
    const ExtensionResourceIdentity& identity) const noexcept {
    uint64_t value = identity.Namespace;
    value ^= identity.Resource + 0x9e3779b97f4a7c15ULL +
             (value << 6U) + (value >> 2U);
    value ^= static_cast<uint64_t>(identity.SchemaVersion) +
             0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    return static_cast<size_t>(value);
}

CompiledExtensionResourceGraph CompileExtensionResourceGraph(
    const ExtensionResourceGraphInput& input) {
    CompiledExtensionResourceGraph result;
    result.PassCount = input.Passes.size();

    ResourceMap declarations;
    declarations.reserve(input.Resources.size());
    for (const auto& declaration : input.Resources) {
        if (!declaration.Identity.Valid()) {
            result.Error = "extension resource declaration has an invalid identity";
            return result;
        }
        if (!declarations.emplace(declaration.Identity, declaration).second) {
            result.Error = "duplicate extension resource declaration";
            return result;
        }
        if (declaration.Origin != ExtensionResourceOrigin::External &&
            declaration.InitialBarrier ==
                ExtensionInitialBarrierPolicy::Managed) {
            result.Error =
                "graph-produced resource requests an external initial barrier";
            return result;
        }
    }

    std::unordered_map<uint64_t, size_t> passIndices;
    passIndices.reserve(input.Passes.size());
    for (size_t passIndex = 0U; passIndex < input.Passes.size(); ++passIndex) {
        const auto& pass = input.Passes[passIndex];
        if (pass.PassId == 0U ||
            !passIndices.emplace(pass.PassId, passIndex).second) {
            result.Error = "extension resource graph has an invalid or duplicate pass id";
            return result;
        }
        std::unordered_set<ExtensionResourceIdentity,
                           ExtensionResourceIdentityHash> usedResources;
        for (const auto& use : pass.Resources) {
            if (!use.Resource.Valid() ||
                declarations.find(use.Resource) == declarations.end()) {
                result.Error = "extension pass uses an undeclared resource: " +
                               PassLabel(pass);
                return result;
            }
            if (!usedResources.insert(use.Resource).second) {
                result.Error = "duplicate resource use in extension pass: " +
                               PassLabel(pass);
                return result;
            }
        }
    }

    std::vector<std::unordered_set<uint64_t>> dependencyClosures(
        input.Passes.size());
    for (size_t passIndex = 0U; passIndex < input.Passes.size(); ++passIndex) {
        const auto& pass = input.Passes[passIndex];
        auto& closure = dependencyClosures[passIndex];
        std::unordered_set<uint64_t> directDependencies;
        for (const uint64_t dependency : pass.DependsOn) {
            const auto found = passIndices.find(dependency);
            if (dependency == 0U || found == passIndices.end() ||
                found->second >= passIndex ||
                !directDependencies.insert(dependency).second) {
                result.Error =
                    "extension pass has a missing, repeated or unordered dependency: " +
                    PassLabel(pass);
                return result;
            }
            closure.insert(dependency);
            closure.insert(dependencyClosures[found->second].begin(),
                           dependencyClosures[found->second].end());
        }
    }

    using IndexMap = std::unordered_map<
        ExtensionResourceIdentity, size_t, ExtensionResourceIdentityHash>;
    using WriterMap = std::unordered_map<
        ExtensionResourceIdentity, std::vector<size_t>,
        ExtensionResourceIdentityHash>;
    struct LastUse {
        size_t Pass = 0U;
        ExtensionResourceAccess Access = ExtensionResourceAccess::Read;
    };
    using LastUseMap = std::unordered_map<
        ExtensionResourceIdentity, LastUse, ExtensionResourceIdentityHash>;

    IndexMap lifetimeIndices;
    WriterMap writers;
    LastUseMap lastUses;
    for (size_t passIndex = 0U; passIndex < input.Passes.size(); ++passIndex) {
        const auto& pass = input.Passes[passIndex];
        for (const auto& use : pass.Resources) {
            const auto& declaration = declarations.at(use.Resource);
            const auto [lifetime, inserted] = lifetimeIndices.emplace(
                use.Resource, result.Lifetimes.size());
            if (inserted) {
                result.Lifetimes.push_back(
                    {use.Resource, passIndex, passIndex, 0U, 0U});
            }
            auto& resourceLifetime = result.Lifetimes[lifetime->second];
            resourceLifetime.LastUse = passIndex;
            resourceLifetime.ReadCount +=
                ReadsExtensionResource(use.Access) ? 1U : 0U;
            resourceLifetime.WriteCount +=
                WritesExtensionResource(use.Access) ? 1U : 0U;

            const auto lastUse = lastUses.find(use.Resource);
            if (lastUse != lastUses.end()) {
                if (WritesExtensionResource(lastUse->second.Access) ||
                    WritesExtensionResource(use.Access)) {
                    result.Barriers.push_back({
                        use.Resource,
                        lastUse->second.Pass,
                        passIndex,
                        lastUse->second.Access,
                        use.Access,
                    });
                }
            } else if (declaration.Origin ==
                           ExtensionResourceOrigin::External &&
                       declaration.InitialBarrier ==
                           ExtensionInitialBarrierPolicy::Managed) {
                result.Barriers.push_back({
                    use.Resource,
                    std::nullopt,
                    passIndex,
                    ExtensionResourceAccess::Write,
                    use.Access,
                });
            }
            lastUses[use.Resource] = {passIndex, use.Access};

            auto& resourceWriters = writers[use.Resource];
            if (ReadsExtensionResource(use.Access) &&
                declaration.Origin ==
                    ExtensionResourceOrigin::GraphProduced) {
                const bool produced = std::any_of(
                    resourceWriters.begin(), resourceWriters.end(),
                    [&](size_t writerIndex) {
                        return writerIndex < passIndex &&
                               dependencyClosures[passIndex].contains(
                                   input.Passes[writerIndex].PassId);
                    });
                if (!produced) {
                    result.Error =
                        "extension pass reads a graph resource without a producer dependency: " +
                        PassLabel(pass);
                    return result;
                }
            }
            if (WritesExtensionResource(use.Access)) {
                const bool ordered = std::all_of(
                    resourceWriters.begin(), resourceWriters.end(),
                    [&](size_t writerIndex) {
                        return dependencyClosures[passIndex].contains(
                            input.Passes[writerIndex].PassId);
                    });
                if (!ordered) {
                    result.Error =
                        "extension resource has unordered writers: " +
                        PassLabel(pass);
                    return result;
                }
                resourceWriters.push_back(passIndex);
            }
        }
    }

    std::unordered_set<ExtensionResourceIdentity,
                       ExtensionResourceIdentityHash> exported;
    for (const auto& resource : input.Exports) {
        if (!resource.Valid() || declarations.find(resource) == declarations.end() ||
            !exported.insert(resource).second) {
            result.Error = "extension graph has an invalid or duplicate export";
            return result;
        }
        const auto writer = writers.find(resource);
        if (writer == writers.end() || writer->second.empty()) {
            result.Error =
                "extension graph exports a resource without a producer";
            return result;
        }
        const auto lifetime = lifetimeIndices.find(resource);
        if (lifetime == lifetimeIndices.end()) {
            result.Error = "extension graph export has no declared lifetime";
            return result;
        }
        auto& exportedLifetime = result.Lifetimes[lifetime->second];
        exportedLifetime.LastUse = input.Passes.size();
        ++exportedLifetime.ReadCount;
        result.Exports.push_back(resource);
    }
    return result;
}

} // namespace Fast::Renderer
