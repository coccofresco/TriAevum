#include "triaevum_oot3d_pica_batch.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "triaevum_oot3d_pica_batch_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

class RecordingSink final : public Oot3dNativeGame::TriAevumOot3dPicaBatchSink {
  public:
    bool SubmitDraw(Oot3dNativeGame::Oot3dPicaDrawSubmission& draw, std::string*) override {
        Events.push_back("draw:" + std::to_string(draw.Id));
        return true;
    }

    bool SubmitDisplayTransfer(const Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission& transfer,
                               std::string*) override {
        Events.push_back("transfer:" + std::to_string(transfer.CompletionId));
        return true;
    }

    bool SubmitMemoryFill(const Oot3dNativeGame::Oot3dPicaMemoryFillSubmission& fill, std::string*) override {
        Events.push_back("fill:" + std::to_string(fill.CompletionId));
        return true;
    }

    bool QueueCompletion(std::uint64_t completionId, Oot3dNativeGame::Oot3dPicaInterruptId, std::string*) override {
        Events.push_back("complete:" + std::to_string(completionId));
        return true;
    }

    std::vector<std::string> Events;
};

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    Oot3dPicaSubmissionBatch batch;
    batch.Draws = { { 1U, {} }, { 2U, {} } };
    batch.Completions = {
        { 12U, 1U, Oot3dPicaInterruptId::P3d },
        { 15U, 2U, Oot3dPicaInterruptId::P3d },
    };
    batch.DisplayTransfers = {
        { 10U, 0U, 0U, 0U, {}, true },
        { 13U, 1U, 0U, 0U, {}, true },
    };
    batch.MemoryFills = {
        { 11U, 1U, 0U, 0U, 0U, 0U, Oot3dPicaInterruptId::Psc0 },
        { 0U, 2U, 0U, 0U, 0U, 0U, std::nullopt },
    };

    RecordingSink sink;
    TriAevumOot3dPicaBatchDispatcher dispatcher;
    std::string error;
    Require(dispatcher.Dispatch(std::move(batch), sink, &error), error);
    const std::vector<std::string> expected = {
        "transfer:10", "complete:10", "fill:11", "complete:11", "draw:1",      "complete:12",
        "transfer:13", "complete:13", "fill:0",  "draw:2",      "complete:15",
    };
    Require(sink.Events == expected, "CTR fill/draw/transfer/completion ordering changed");
    Require(dispatcher.LastSubmittedDrawId() == 2U && dispatcher.Stats().Batches == 1U &&
                dispatcher.Stats().Draws == 2U && dispatcher.Stats().DisplayTransfers == 2U &&
                dispatcher.Stats().MemoryFills == 2U && dispatcher.Stats().Completions == 5U,
            "batch accounting is inconsistent");

    Oot3dPicaSubmissionBatch continuation;
    continuation.DisplayTransfers = {
        { 16U, 2U, 0U, 0U, {}, true },
    };
    Require(dispatcher.Dispatch(std::move(continuation), sink, &error) &&
                sink.Events[sink.Events.size() - 2U] == "transfer:16" && sink.Events.back() == "complete:16",
            "cross-batch draw dependency was not retained");

    TriAevumOot3dPicaBatchDispatcher invalidDispatcher;
    Oot3dPicaSubmissionBatch invalid;
    invalid.Completions = {
        { 1U, 99U, Oot3dPicaInterruptId::P3d },
    };
    Require(!invalidDispatcher.Dispatch(std::move(invalid), sink, &error) && invalidDispatcher.IsFaulted() &&
                !error.empty(),
            "orphan completion did not fault before submission");
    return 0;
}
