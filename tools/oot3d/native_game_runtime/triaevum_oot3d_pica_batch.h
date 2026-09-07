#pragma once

#include "oot3d_native_pica_submission.h"

#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

class TriAevumOot3dPicaBatchSink {
  public:
    virtual ~TriAevumOot3dPicaBatchSink() = default;
    virtual bool SubmitDraw(Oot3dPicaDrawSubmission& draw, std::string* error) = 0;
    virtual bool SubmitDisplayTransfer(const Oot3dPicaDisplayTransferSubmission& transfer, std::string* error) = 0;
    virtual bool SubmitMemoryFill(const Oot3dPicaMemoryFillSubmission& fill, std::string* error) = 0;
    virtual bool QueueCompletion(std::uint64_t completionId, Oot3dPicaInterruptId interrupt, std::string* error) = 0;
};

struct TriAevumOot3dPicaBatchStats {
    std::uint64_t Batches = 0U;
    std::uint64_t Draws = 0U;
    std::uint64_t DisplayTransfers = 0U;
    std::uint64_t MemoryFills = 0U;
    std::uint64_t Completions = 0U;
};

// Reconstructs the ordering encoded by the native CTR/GSP submission IDs.
// Rendering and guest-interrupt delivery remain responsibilities of the sink.
class TriAevumOot3dPicaBatchDispatcher final {
  public:
    bool Dispatch(Oot3dPicaSubmissionBatch batch, TriAevumOot3dPicaBatchSink& sink, std::string* error = nullptr);
    void Reset() noexcept;

    bool IsFaulted() const noexcept;
    std::uint64_t LastSubmittedDrawId() const noexcept;
    const TriAevumOot3dPicaBatchStats& Stats() const noexcept;

  private:
    bool mFaulted = false;
    std::uint64_t mLastSubmittedDrawId = 0U;
    TriAevumOot3dPicaBatchStats mStats;
};

} // namespace Oot3dNativeGame
