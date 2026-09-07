#pragma once

#include "fast/renderer/extension_contract.h"
#include "fast/renderer3ds/pica_composition.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Fast::Renderer3ds {

enum class PicaCompositionScheduleIssue : uint8_t {
    None,
    NoSceneDomain,
    UnknownLayer,
    MissingOpaqueWorld,
    NonContiguousOpaqueWorld,
    NonCanonicalPostOpaqueOrder,
};

struct PicaCompositionRun {
    PicaCompositionTargetReference Target;
    size_t FirstDrawIndex = 0U;
    size_t DrawCount = 0U;
    PicaCompositionDomain Domain = PicaCompositionDomain::Unknown;
    PicaCompositionAttribution Composition;
};

struct PicaCompositionStageAnchor {
    ::Fast::Renderer::ExtensionStage Stage =
        ::Fast::Renderer::ExtensionStage::SceneResolved;
    PicaCompositionTargetReference Target;
    size_t BeforeDrawIndex = 0U;
    uint64_t BeforeSubmissionId = 0U;
    bool AtTargetEnd = false;
};

struct PicaCompositionTargetSchedule {
    PicaCompositionTargetReference Target;
    PicaCompositionScheduleIssue WorldStageIssue =
        PicaCompositionScheduleIssue::NoSceneDomain;
    uint32_t DrawCount = 0U;
    uint32_t OpaqueWorldDrawCount = 0U;
    uint32_t TransparentWorldDrawCount = 0U;
    uint32_t AtmosphereDrawCount = 0U;
    uint32_t UiDrawCount = 0U;
    uint32_t UnknownDrawCount = 0U;
};

struct PicaCompositionScheduleStats {
    uint32_t DrawCount = 0U;
    uint32_t ConsumedDrawCount = 0U;
    uint32_t RunCount = 0U;
    uint32_t TargetCount = 0U;
    uint32_t StageAnchorCount = 0U;
    uint32_t WorldStageTargetCount = 0U;
    uint32_t NonSceneTargetCount = 0U;
    uint32_t UnknownLayerTargetCount = 0U;
    uint32_t MissingOpaqueWorldTargetCount = 0U;
    uint32_t NonContiguousWorldTargetCount = 0U;
    uint32_t NonCanonicalTailTargetCount = 0U;
    uint32_t ExecutionMismatchCount = 0U;
};

// Compiles exact native draw ownership into conservative extension-stage
// anchors. Draw order is never changed. A world-stage anchor is withheld when
// the native sequence does not prove one unambiguous insertion point.
class PicaCompositionSchedule final {
  public:
    bool Compile(
        const PicaCompositionSequenceView& sequence,
        std::string* error = nullptr);
    void Reset() noexcept;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] uint64_t SequenceId() const noexcept;
    [[nodiscard]] std::span<const PicaCompositionRun> Runs() const noexcept;
    [[nodiscard]] std::span<const PicaCompositionStageAnchor> Anchors()
        const noexcept;
    [[nodiscard]] std::span<const PicaCompositionTargetSchedule>
    Targets() const noexcept;
    [[nodiscard]] const PicaCompositionTargetSchedule* FindTarget(
        const PicaCompositionTargetReference& target)
        const noexcept;
    [[nodiscard]] const PicaCompositionStageAnchor* FindAnchor(
        ::Fast::Renderer::ExtensionStage stage,
        const PicaCompositionTargetReference& target)
        const noexcept;
    [[nodiscard]] const PicaCompositionStageAnchor* FindAnchorBeforeDraw(
        ::Fast::Renderer::ExtensionStage stage,
        const PicaCompositionTargetReference& target,
        uint64_t submissionId) const noexcept;
    [[nodiscard]] const PicaCompositionStageAnchor* FindTargetEndAnchor(
        ::Fast::Renderer::ExtensionStage stage,
        const PicaCompositionTargetReference& target)
        const noexcept;
    // Geometry attached to the already published opaque prefix can be inserted
    // before its transparency even when later draw groups are noncanonical.
    // This does not authorize whole-scene lighting or post-processing.
    [[nodiscard]] const PicaCompositionStageAnchor* FindPublishedGeometryAnchor(
        const PicaCompositionTargetReference& target) const noexcept;
    [[nodiscard]] bool Reached(
        const PicaCompositionStageAnchor& anchor) const noexcept;

    bool ConsumeDraw(const PicaCompositionDrawReference& draw,
                     std::string* error = nullptr);
    [[nodiscard]] PicaCompositionScheduleStats Stats() const noexcept;

  private:
    std::vector<PicaCompositionDrawReference> mDraws;
    std::vector<PicaCompositionRun> mRuns;
    std::vector<PicaCompositionStageAnchor> mAnchors;
    std::vector<PicaCompositionStageAnchor> mPublishedGeometryAnchors;
    std::vector<PicaCompositionTargetSchedule> mTargets;
    uint64_t mSequenceId = 0U;
    size_t mConsumedDrawCount = 0U;
    uint32_t mExecutionMismatchCount = 0U;
    bool mValid = false;
};

} // namespace Fast::Renderer3ds
