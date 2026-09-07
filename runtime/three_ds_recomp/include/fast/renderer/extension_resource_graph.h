#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Fast::Renderer {

struct ExtensionResourceIdentity {
    uint64_t Namespace = 0U;
    uint64_t Resource = 0U;
    uint32_t SchemaVersion = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Namespace != 0U && Resource != 0U && SchemaVersion != 0U;
    }

    bool operator==(const ExtensionResourceIdentity&) const = default;
};

struct ExtensionResourceIdentityHash {
    [[nodiscard]] size_t operator()(
        const ExtensionResourceIdentity& identity) const noexcept;
};

enum class ExtensionResourceAccess : uint8_t {
    Read,
    Write,
    ReadWrite,
};

[[nodiscard]] constexpr bool ReadsExtensionResource(
    ExtensionResourceAccess access) noexcept {
    return access == ExtensionResourceAccess::Read ||
           access == ExtensionResourceAccess::ReadWrite;
}

[[nodiscard]] constexpr bool WritesExtensionResource(
    ExtensionResourceAccess access) noexcept {
    return access == ExtensionResourceAccess::Write ||
           access == ExtensionResourceAccess::ReadWrite;
}

enum class ExtensionResourceOrigin : uint8_t {
    External,
    GraphProduced,
};

enum class ExtensionInitialBarrierPolicy : uint8_t {
    None,
    Managed,
};

struct ExtensionResourceDeclaration {
    ExtensionResourceIdentity Identity;
    ExtensionResourceOrigin Origin = ExtensionResourceOrigin::GraphProduced;
    ExtensionInitialBarrierPolicy InitialBarrier =
        ExtensionInitialBarrierPolicy::None;
};

struct ExtensionResourceUse {
    ExtensionResourceIdentity Resource;
    ExtensionResourceAccess Access = ExtensionResourceAccess::Read;
};

struct ExtensionResourcePassDeclaration {
    uint64_t PassId = 0U;
    std::string DebugName;
    std::vector<uint64_t> DependsOn;
    std::vector<ExtensionResourceUse> Resources;
};

struct ExtensionResourceGraphInput {
    std::vector<ExtensionResourceDeclaration> Resources;
    std::vector<ExtensionResourcePassDeclaration> Passes;
    std::vector<ExtensionResourceIdentity> Exports;
};

struct ExtensionResourceLifetime {
    ExtensionResourceIdentity Resource;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t ReadCount = 0U;
    uint32_t WriteCount = 0U;
};

struct ExtensionResourceBarrier {
    ExtensionResourceIdentity Resource;
    std::optional<size_t> ProducerPass;
    size_t ConsumerPass = 0U;
    ExtensionResourceAccess Before = ExtensionResourceAccess::Write;
    ExtensionResourceAccess After = ExtensionResourceAccess::Read;
};

struct CompiledExtensionResourceGraph {
    size_t PassCount = 0U;
    std::vector<ExtensionResourceLifetime> Lifetimes;
    std::vector<ExtensionResourceBarrier> Barriers;
    std::vector<ExtensionResourceIdentity> Exports;
    std::string Error;

    [[nodiscard]] bool Valid() const noexcept {
        return Error.empty();
    }
};

// Compiles title-neutral resource ownership and synchronization from an
// already ordered pass list. Title adapters remain responsible for native
// stage discovery and for classifying each resource's origin/barrier policy.
[[nodiscard]] CompiledExtensionResourceGraph CompileExtensionResourceGraph(
    const ExtensionResourceGraphInput& input);

} // namespace Fast::Renderer
