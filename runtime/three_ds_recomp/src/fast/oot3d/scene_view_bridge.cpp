#include "fast/oot3d/scene_view_bridge.h"

#include <functional>

namespace Fast::Oot3d {

size_t SceneViewBridge::KeyHash::operator()(const SceneViewKey& key) const {
    size_t value = std::hash<uint64_t>{}(key.RenderTargetNamespace);
    value ^= std::hash<uint32_t>{}(key.ColorPhysicalAddress) + 0x9e3779b9U +
             (value << 6U) + (value >> 2U);
    value ^= std::hash<uint32_t>{}(key.DepthPhysicalAddress) + 0x9e3779b9U +
             (value << 6U) + (value >> 2U);
    return value;
}

void SceneViewBridge::Publish(SceneViewInfo view) {
    const auto previous = mPreviousByView.find(view.ViewId);
    if (!view.CameraCut && previous != mPreviousByView.end()) {
        view.PreviousViewProjectionMatrix = previous->second;
    } else {
        view.PreviousViewProjectionMatrix = view.ViewProjectionMatrix;
    }
    mPreviousByView[view.ViewId] = view.ViewProjectionMatrix;
    mViews[view.Target] = std::move(view);
}

std::optional<SceneViewInfo> SceneViewBridge::Find(
    const SceneViewKey& target) const {
    const auto found = mViews.find(target);
    return found == mViews.end() ? std::nullopt
                                 : std::optional<SceneViewInfo>(found->second);
}

void SceneViewBridge::InvalidateTarget(const SceneViewKey& target) {
    mViews.erase(target);
}

void SceneViewBridge::ResetHistory() {
    mViews.clear();
    mPreviousByView.clear();
}

} // namespace Fast::Oot3d
