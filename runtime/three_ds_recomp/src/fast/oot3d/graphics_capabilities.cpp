#include "fast/oot3d/graphics_capabilities.h"

namespace Fast::Oot3d {
namespace {
const GraphicsCapabilityState kUnavailable{ false, "capability not reported" };
}

void GraphicsCapabilities::Set(GraphicsCapability capability, bool available,
                               std::string reason) {
    mStates[capability] = { available, std::move(reason) };
}

bool GraphicsCapabilities::Has(GraphicsCapability capability) const {
    return Get(capability).Available;
}

const GraphicsCapabilityState&
GraphicsCapabilities::Get(GraphicsCapability capability) const {
    const auto found = mStates.find(capability);
    return found == mStates.end() ? kUnavailable : found->second;
}

} // namespace Fast::Oot3d
