#include "triaevum_oot3d_pica_batch.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace Oot3dNativeGame {
namespace {

bool Fail(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool ValidateBatch(const Oot3dPicaSubmissionBatch& batch, std::uint64_t lastSubmittedDrawId, std::string* error) {
    std::uint64_t previousDrawId = lastSubmittedDrawId;
    std::unordered_set<std::uint64_t> drawIds;
    for (const auto& draw : batch.Draws) {
        if (draw.Id == 0U || draw.Id <= previousDrawId) {
            return Fail(error, "PICA draw IDs are not strictly increasing");
        }
        previousDrawId = draw.Id;
        drawIds.insert(draw.Id);
    }

    if (!std::is_sorted(batch.Completions.begin(), batch.Completions.end(), [](const auto& left, const auto& right) {
            return left.AfterDrawSubmissionId < right.AfterDrawSubmissionId;
        })) {
        return Fail(error, "PICA draw completions are not ordered");
    }
    if (!std::is_sorted(batch.DisplayTransfers.begin(), batch.DisplayTransfers.end(),
                        [](const auto& left, const auto& right) {
                            return left.AfterDrawSubmissionId < right.AfterDrawSubmissionId;
                        })) {
        return Fail(error, "PICA display transfers are not ordered");
    }
    if (!std::is_sorted(batch.MemoryFills.begin(), batch.MemoryFills.end(), [](const auto& left, const auto& right) {
            return left.BeforeDrawSubmissionId < right.BeforeDrawSubmissionId;
        })) {
        return Fail(error, "PICA memory fills are not ordered");
    }

    std::unordered_set<std::uint64_t> completionIds;
    const auto rememberCompletion = [&](std::uint64_t id) { return id != 0U && completionIds.insert(id).second; };
    for (const auto& completion : batch.Completions) {
        if (!drawIds.contains(completion.AfterDrawSubmissionId)) {
            return Fail(error, "PICA completion does not reference this draw batch");
        }
        if (!rememberCompletion(completion.Id)) {
            return Fail(error, "PICA completion ID is zero or duplicated");
        }
    }
    for (const auto& transfer : batch.DisplayTransfers) {
        if (transfer.AfterDrawSubmissionId > previousDrawId) {
            return Fail(error, "PICA display transfer references a future draw");
        }
        if (transfer.SignalInterrupt && !rememberCompletion(transfer.CompletionId)) {
            return Fail(error, "PICA transfer completion ID is zero or duplicated");
        }
    }
    for (const auto& fill : batch.MemoryFills) {
        if ((fill.CompletionId == 0U) != !fill.Interrupt.has_value()) {
            return Fail(error, "PICA memory-fill completion contract is inconsistent");
        }
        if (fill.CompletionId != 0U && !rememberCompletion(fill.CompletionId)) {
            return Fail(error, "PICA fill completion ID is duplicated");
        }
    }
    return true;
}

} // namespace

bool TriAevumOot3dPicaBatchDispatcher::Dispatch(Oot3dPicaSubmissionBatch batch, TriAevumOot3dPicaBatchSink& sink,
                                                std::string* error) {
    if (mFaulted) {
        return Fail(error, "PICA batch dispatcher is faulted");
    }
    if (!ValidateBatch(batch, mLastSubmittedDrawId, error)) {
        mFaulted = true;
        return false;
    }
    if (batch.Empty()) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    const auto queueCompletion = [&](std::uint64_t id, Oot3dPicaInterruptId interrupt) {
        if (!sink.QueueCompletion(id, interrupt, error)) {
            return false;
        }
        ++mStats.Completions;
        return true;
    };
    const auto submitTransfer = [&](const auto& transfer) {
        if (!sink.SubmitDisplayTransfer(transfer, error)) {
            return false;
        }
        ++mStats.DisplayTransfers;
        return !transfer.SignalInterrupt || queueCompletion(transfer.CompletionId, Oot3dPicaInterruptId::Ppf);
    };
    const auto submitFill = [&](const auto& fill) {
        if (!sink.SubmitMemoryFill(fill, error)) {
            return false;
        }
        ++mStats.MemoryFills;
        return fill.CompletionId == 0U || queueCompletion(fill.CompletionId, *fill.Interrupt);
    };

    std::size_t completionIndex = 0U;
    std::size_t transferIndex = 0U;
    std::size_t fillIndex = 0U;
    while (transferIndex < batch.DisplayTransfers.size() &&
           batch.DisplayTransfers[transferIndex].AfterDrawSubmissionId <= mLastSubmittedDrawId) {
        if (!submitTransfer(batch.DisplayTransfers[transferIndex++])) {
            mFaulted = true;
            return false;
        }
    }

    for (auto& draw : batch.Draws) {
        while (fillIndex < batch.MemoryFills.size() && batch.MemoryFills[fillIndex].BeforeDrawSubmissionId <= draw.Id) {
            if (!submitFill(batch.MemoryFills[fillIndex++])) {
                mFaulted = true;
                return false;
            }
        }
        if (!sink.SubmitDraw(draw, error)) {
            mFaulted = true;
            return false;
        }
        ++mStats.Draws;
        while (completionIndex < batch.Completions.size() &&
               batch.Completions[completionIndex].AfterDrawSubmissionId == draw.Id) {
            const auto& completion = batch.Completions[completionIndex++];
            if (!queueCompletion(completion.Id, completion.Interrupt)) {
                mFaulted = true;
                return false;
            }
        }
        mLastSubmittedDrawId = draw.Id;
        while (transferIndex < batch.DisplayTransfers.size() &&
               batch.DisplayTransfers[transferIndex].AfterDrawSubmissionId == draw.Id) {
            if (!submitTransfer(batch.DisplayTransfers[transferIndex++])) {
                mFaulted = true;
                return false;
            }
        }
    }

    while (fillIndex < batch.MemoryFills.size()) {
        if (!submitFill(batch.MemoryFills[fillIndex++])) {
            mFaulted = true;
            return false;
        }
    }
    if (completionIndex != batch.Completions.size() || transferIndex != batch.DisplayTransfers.size()) {
        mFaulted = true;
        return Fail(error, "PICA batch contains unconsumed ordered work");
    }
    ++mStats.Batches;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void TriAevumOot3dPicaBatchDispatcher::Reset() noexcept {
    mFaulted = false;
    mLastSubmittedDrawId = 0U;
    mStats = {};
}

bool TriAevumOot3dPicaBatchDispatcher::IsFaulted() const noexcept {
    return mFaulted;
}

std::uint64_t TriAevumOot3dPicaBatchDispatcher::LastSubmittedDrawId() const noexcept {
    return mLastSubmittedDrawId;
}

const TriAevumOot3dPicaBatchStats& TriAevumOot3dPicaBatchDispatcher::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
