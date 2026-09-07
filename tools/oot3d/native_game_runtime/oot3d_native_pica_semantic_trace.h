#pragma once

#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_vulkan_plan.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_set>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

inline constexpr std::string_view kOot3dPicaSemanticTraceFormat =
    "oot3d_pica_semantic_trace_v1";

class Oot3dPicaSemanticTraceWriter {
  public:
    explicit Oot3dPicaSemanticTraceWriter(
        const std::filesystem::path& outputPath);
    ~Oot3dPicaSemanticTraceWriter();

    Oot3dPicaSemanticTraceWriter(const Oot3dPicaSemanticTraceWriter&) = delete;
    Oot3dPicaSemanticTraceWriter& operator=(
        const Oot3dPicaSemanticTraceWriter&) = delete;

    bool Enabled() const noexcept;
    const std::filesystem::path& OutputPath() const noexcept;

    void BeginSession(std::string_view renderer,
                      std::string_view gameplayTiming,
                      std::string_view uiProfile);
    void RecordMemoryFill(
        uint64_t presentationFrame, uint32_t guestFrame,
        const Oot3dPicaMemoryFillSubmission& fill);
    void RecordDisplayTransfer(
        uint64_t presentationFrame, uint32_t guestFrame,
        const Oot3dPicaDisplayTransferSubmission& transfer,
        bool selectedForTopAtSubmission,
        bool topScreenBackdropSuppressionActive);
    void RecordDraw(uint64_t presentationFrame, uint32_t guestFrame,
                    const Oot3dPicaDrawSubmission& submission,
                    const Oot3dPicaVulkanDrawPlan& plan,
                    bool suppressed, bool usesCommonBackground,
                    bool opaqueTargetInitialization);
    void RecordPresentationSelection(
        uint64_t presentationFrame, uint32_t guestFrame,
        bool lcdForceBlack, std::optional<uint32_t> topAddressLeft,
        std::optional<uint32_t> topAddressRight,
        const Oot3dPicaDisplayTransferSubmission* selectedTransfer);
    void RecordFrameBoundary(uint64_t presentationFrame,
                             uint32_t guestFrame,
                             bool guestAdvanced);
    void Finish();

  private:
    void WriteEvent(nlohmann::json event);

    std::filesystem::path mOutputPath;
    std::ofstream mStream;
    uint64_t mNextEventId = 1;
    bool mSessionStarted = false;
    bool mFinished = false;
    std::unordered_set<uint64_t> mWrittenVertexSources;
    std::unordered_set<uint64_t> mWrittenFragmentSources;
};

} // namespace Oot3dNativeGame
