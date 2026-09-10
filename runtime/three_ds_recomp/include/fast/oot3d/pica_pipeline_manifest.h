#pragma once

#include "fast/oot3d/pica_aot_shader_pack.h"
#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/oot3d/pica_shader_instrumentation.h"
#include "oot3d/renderer/pica_render_backend.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint32_t kPicaGraphicsPipelineManifestSchemaVersion = 2U;

enum class PicaGraphicsPipelineDomain : uint8_t {
    Canonical = 0U,
    Instrumented = 1U,
};

struct PicaGraphicsPipelineVertexBinding {
    uint8_t Binding = 0U;
    uint16_t ByteStride = 0U;
    bool PerInstance = false;

    auto operator<=>(const PicaGraphicsPipelineVertexBinding&) const = default;
};

struct PicaGraphicsPipelineVertexAttribute {
    uint8_t Location = 0U;
    uint8_t Binding = 0U;
    ::Oot3d::Renderer::PicaVertexFormat Format =
        ::Oot3d::Renderer::PicaVertexFormat::Float;
    uint8_t ComponentCount = 4U;
    uint16_t ByteOffset = 0U;

    auto operator<=>(const PicaGraphicsPipelineVertexAttribute&) const =
        default;
};

struct PicaGraphicsPipelineBlendState {
    bool Enabled = false;
    ::Oot3d::Renderer::NativeBlendEquation EquationRgb =
        ::Oot3d::Renderer::NativeBlendEquation::Add;
    ::Oot3d::Renderer::NativeBlendEquation EquationAlpha =
        ::Oot3d::Renderer::NativeBlendEquation::Add;
    ::Oot3d::Renderer::NativeBlendFactor SourceRgb =
        ::Oot3d::Renderer::NativeBlendFactor::One;
    ::Oot3d::Renderer::NativeBlendFactor DestRgb =
        ::Oot3d::Renderer::NativeBlendFactor::Zero;
    ::Oot3d::Renderer::NativeBlendFactor SourceAlpha =
        ::Oot3d::Renderer::NativeBlendFactor::One;
    ::Oot3d::Renderer::NativeBlendFactor DestAlpha =
        ::Oot3d::Renderer::NativeBlendFactor::Zero;

    auto operator<=>(const PicaGraphicsPipelineBlendState&) const = default;
};

struct PicaGraphicsPipelineStencilState {
    bool Enabled = false;
    ::Oot3d::Renderer::PicaCompareFunction Compare =
        ::Oot3d::Renderer::PicaCompareFunction::Always;
    uint8_t Reference = 0U;
    uint8_t CompareMask = 0U;
    uint8_t WriteMask = 0U;
    ::Oot3d::Renderer::PicaStencilAction Fail =
        ::Oot3d::Renderer::PicaStencilAction::Keep;
    ::Oot3d::Renderer::PicaStencilAction DepthFail =
        ::Oot3d::Renderer::PicaStencilAction::Keep;
    ::Oot3d::Renderer::PicaStencilAction Pass =
        ::Oot3d::Renderer::PicaStencilAction::Keep;

    auto operator<=>(const PicaGraphicsPipelineStencilState&) const = default;
};

using PicaGraphicsPipelineShaderOutputs =
    PicaFragmentInstrumentationOutputLayout;

struct PicaGraphicsPipelineManifestEntry {
    uint32_t SchemaVersion =
        kPicaGraphicsPipelineManifestSchemaVersion;
    uint32_t DescriptorSchemaVersion = 0U;
    PicaGraphicsPipelineDomain Domain =
        PicaGraphicsPipelineDomain::Canonical;
    uint64_t VertexShaderKey = 0U;
    uint64_t FragmentShaderKey = 0U;
    PicaAotShaderSourceIdentity VertexSource;
    PicaAotShaderSourceIdentity FragmentSource;
    PicaAotShaderSourceIdentity NriFragmentSource;
    bool NriFragmentAvailable = false;
    PicaShaderInstrumentationFeature RequestedFeatures =
        PicaShaderInstrumentationFeature::None;
    PicaShaderInstrumentationFeature AppliedFeatures =
        PicaShaderInstrumentationFeature::None;
    uint8_t AttachmentRequirementsKey = 0U;
    uint8_t SampleCount = 1U;
    bool WritesReactiveMask = false;
    // A distinct live pipeline; absent in older manifests means false.
    bool OutlineOcclusionOnly = false;
    ::Oot3d::Renderer::PicaTopology Topology =
        ::Oot3d::Renderer::PicaTopology::TriangleList;
    ::Oot3d::Renderer::NativeCullMode CullMode =
        ::Oot3d::Renderer::NativeCullMode::KeepAll;
    bool FramebufferFlipped = false;
    std::vector<PicaGraphicsPipelineVertexBinding> VertexBindings;
    std::vector<PicaGraphicsPipelineVertexAttribute> VertexAttributes;
    uint8_t ColorWriteMask = 0U;
    uint8_t FragmentOperationMode = 0U;
    ::Oot3d::Renderer::PicaLogicOperation LogicOperation =
        ::Oot3d::Renderer::PicaLogicOperation::Copy;
    PicaGraphicsPipelineBlendState Blend;
    bool AlphaTestEnabled = false;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    ::Oot3d::Renderer::PicaCompareFunction DepthCompare =
        ::Oot3d::Renderer::PicaCompareFunction::Always;
    PicaGraphicsPipelineStencilState Stencil;
    PicaGraphicsPipelineShaderOutputs ShaderOutputs;

    // Evidence metadata does not participate in StructuralId().
    uint64_t ObservationCount = 0U;
    std::set<uint64_t> CanonicalPipelineIds;
    std::set<uint64_t> SettingsRevisions;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] uint64_t StructuralId() const noexcept;
    [[nodiscard]] bool StructurallyEquivalent(
        const PicaGraphicsPipelineManifestEntry& other) const noexcept;
    [[nodiscard]] bool MatchesPrewarmProfile(
        PicaShaderInstrumentationFeature activeFeatures,
        bool nativeFidelity) const noexcept;
};

PicaGraphicsPipelineManifestEntry DescribePicaGraphicsPipelineDraw(
    const Renderer3ds::PicaDrawView& draw);

bool WritePicaGraphicsPipelineManifest(
    const std::filesystem::path& path,
    uint32_t descriptorSchemaVersion,
    std::span<const PicaGraphicsPipelineManifestEntry> entries,
    std::string* error = nullptr);

class PicaGraphicsPipelineManifest final {
  public:
    bool Load(const std::filesystem::path& path,
              std::string* error = nullptr);
    void Clear();

    [[nodiscard]] bool Loaded() const noexcept;
    [[nodiscard]] uint32_t DescriptorSchemaVersion() const noexcept;
    [[nodiscard]] std::span<const PicaGraphicsPipelineManifestEntry>
    Entries() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

  private:
    std::filesystem::path mPath;
    uint32_t mDescriptorSchemaVersion = 0U;
    std::vector<PicaGraphicsPipelineManifestEntry> mEntries;
};

// Diagnostic collector. It observes immutable pipeline descriptors and emits
// a deterministic manifest that can be merged offline and prewarmed later.
class PicaGraphicsPipelineInventory final {
  public:
    bool Configure(const std::filesystem::path& path,
                   std::string* error = nullptr);
    void Observe(PicaGraphicsPipelineManifestEntry entry,
                 uint64_t canonicalPipelineId,
                 uint64_t settingsRevision);
    bool Finish(std::string* error = nullptr);
    void Clear();

    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] size_t EntryCount() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

  private:
    std::filesystem::path mPath;
    std::map<uint64_t, PicaGraphicsPipelineManifestEntry> mEntries;
    uint32_t mDescriptorSchemaVersion = 0U;
    bool mDescriptorSchemaMismatch = false;
    bool mStructuralCollision = false;
    bool mFinished = false;
};

} // namespace Fast::Oot3d
