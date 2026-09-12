#include "fast/renderer/pipeline_preparation_job.h"
#include <stdexcept>
#include <utility>

namespace Fast::Renderer {
PipelinePreparationJob::PipelinePreparationJob(size_t count, PrepareOne prepare)
    : mPrepare(std::move(prepare)) {
    if (!count || count > 100000 || !mPrepare) throw std::invalid_argument("invalid pipeline preparation job");
    mProgress.Total = count;
}
const PipelinePreparationProgress& PipelinePreparationJob::Step(
    size_t budget, const std::function<bool()>& cancel) {
    if (!budget) throw std::invalid_argument("pipeline preparation budget is zero");
    while (budget-- && !mProgress.Finished()) {
        if (cancel && cancel()) { mProgress.Cancelled = true; break; }
        std::string error;
        bool prepared = false;
        try { prepared = mPrepare(mProgress.Attempted, error); }
        catch (const std::exception& exception) { error = exception.what(); }
        ++mProgress.Attempted;
        if (prepared) ++mProgress.Prepared;
        else { ++mProgress.Failed; mProgress.LastError = error.empty() ? "device pipeline creation failed" : error; }
    }
    return mProgress;
}
} // namespace Fast::Renderer
