#include "fast/oot3d/grass_render_telemetry.h"

namespace Fast::Oot3d {

const char* GrassRenderStatusName(GrassRenderStatus status) noexcept {
    switch (status) {
        case GrassRenderStatus::Disabled:
            return "disabled";
        case GrassRenderStatus::ContractUnavailable:
            return "geometry-provider contract unavailable";
        case GrassRenderStatus::PipelineUnavailable:
            return "pipeline unavailable";
        case GrassRenderStatus::CameraUnavailable:
            return "camera unavailable";
        case GrassRenderStatus::NoRules:
            return "no texture rules";
        case GrassRenderStatus::NoBudget:
            return "zero blade budget";
        case GrassRenderStatus::NoMasks:
            return "texture masks unavailable";
        case GrassRenderStatus::NoMatchingTexture:
            return "assigned texture is not on an eligible surface";
        case GrassRenderStatus::NoScopedSurfaces:
            return "matching surfaces belong to another render target";
        case GrassRenderStatus::PlacementPending:
            return "placement is building asynchronously";
        case GrassRenderStatus::NoAnchors:
            return "placement rules rejected every anchor";
        case GrassRenderStatus::Culled:
            return "all anchors are outside visibility limits";
        case GrassRenderStatus::Drawn:
            return "drawing";
        case GrassRenderStatus::Error:
            return "renderer error";
    }
    return "unknown";
}

GrassRenderTelemetry& GrassRenderTelemetry::Instance() {
    static GrassRenderTelemetry telemetry;
    return telemetry;
}

void GrassRenderTelemetry::Publish(
    const GrassRenderTelemetrySnapshot& snapshot) {
    std::scoped_lock lock(mMutex);
    mSnapshot = snapshot;
}

GrassRenderTelemetrySnapshot GrassRenderTelemetry::Snapshot() const {
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}

void GrassRenderTelemetry::Reset() {
    std::scoped_lock lock(mMutex);
    mSnapshot = {};
}

} // namespace Fast::Oot3d
