#pragma once

namespace Fast::Oot3d {

struct PicaNriDrawOwnership {
    bool Prepared = false;
    bool ScopeRequiresOwnedSubmission = false;
    bool SubmitOwned = false;
};

[[nodiscard]] constexpr PicaNriDrawOwnership PreparePicaNriDrawOwnership(
    bool ownedDrawsEnabled, bool ownedPipelineReady) {
    const bool prepared = ownedDrawsEnabled && ownedPipelineReady;
    return {prepared, false, false};
}

[[nodiscard]] constexpr PicaNriDrawOwnership FinalizePicaNriDrawOwnership(
    PicaNriDrawOwnership ownership, bool currentScopeOwnedByNri) {
    ownership.ScopeRequiresOwnedSubmission = currentScopeOwnedByNri;
    ownership.SubmitOwned =
        ownership.Prepared && currentScopeOwnedByNri;
    return ownership;
}

[[nodiscard]] constexpr bool PicaNriVulkanFallbackAllowed(
    const PicaNriDrawOwnership& ownership) {
    return !ownership.ScopeRequiresOwnedSubmission;
}

} // namespace Fast::Oot3d
