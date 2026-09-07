#include "fast/oot3d/gpu_profile_plan.h"

namespace Fast::Oot3d {

GpuProfileQueryRange GpuProfileQueries(GpuProfileScope scope) {
    const uint32_t begin =
        static_cast<uint32_t>(scope) * 2U;
    return {begin, begin + 1U};
}

size_t GpuProfileFramePlan::Index(GpuProfileScope scope) {
    return static_cast<size_t>(scope);
}

bool GpuProfileFramePlan::Begin(GpuProfileScope scope) {
    const size_t index = Index(scope);
    if (index >= kGpuProfileScopeCount ||
        mOpen[index] || mWritten[index]) {
        return false;
    }
    mOpen[index] = true;
    return true;
}

bool GpuProfileFramePlan::End(GpuProfileScope scope) {
    const size_t index = Index(scope);
    if (index >= kGpuProfileScopeCount || !mOpen[index]) {
        return false;
    }
    mOpen[index] = false;
    mWritten[index] = true;
    return true;
}

void GpuProfileFramePlan::Reset() {
    mOpen.fill(false);
    mWritten.fill(false);
}

bool GpuProfileFramePlan::Open(GpuProfileScope scope) const {
    const size_t index = Index(scope);
    return index < kGpuProfileScopeCount && mOpen[index];
}

bool GpuProfileFramePlan::Written(GpuProfileScope scope) const {
    const size_t index = Index(scope);
    return index < kGpuProfileScopeCount && mWritten[index];
}

} // namespace Fast::Oot3d
