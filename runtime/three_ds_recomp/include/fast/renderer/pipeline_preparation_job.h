#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace Fast::Renderer {
struct PipelinePreparationProgress {
    size_t Total = 0;
    size_t Attempted = 0;
    size_t Prepared = 0;
    size_t Failed = 0;
    bool Cancelled = false;
    std::string LastError;
    bool Finished() const { return Cancelled || Attempted == Total; }
    bool Complete() const { return Total != 0 && !Cancelled && Attempted == Total && Failed == 0; }
};

// No window, guest or platform dependency. Hosts call Step between UI/service
// events; the renderer callback performs one actual device pipeline creation.
class PipelinePreparationJob {
  public:
    using PrepareOne = std::function<bool(size_t, std::string&)>;
    PipelinePreparationJob(size_t count, PrepareOne prepare);
    const PipelinePreparationProgress& Step(size_t budget, const std::function<bool()>& cancel = {});
    const PipelinePreparationProgress& Progress() const { return mProgress; }
  private:
    PipelinePreparationProgress mProgress;
    PrepareOne mPrepare;
};
} // namespace Fast::Renderer
