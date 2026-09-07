#include "fast/oot3d/renderer_validation_telemetry.h"

namespace Fast::Oot3d {

void RendererValidationTelemetry::SetEnabled(
    RendererValidationSource source, bool enabled) {
    auto& target = source == RendererValidationSource::Vulkan
        ? mVulkanEnabled : mNriEnabled;
    target.store(enabled, std::memory_order_relaxed);
}

void RendererValidationTelemetry::Record(
    RendererValidationSource source,
    RendererValidationSeverity severity) {
    std::atomic_uint64_t* counter = nullptr;
    if (source == RendererValidationSource::Vulkan) {
        counter = severity == RendererValidationSeverity::Error
            ? &mVulkanErrorCount
            : severity == RendererValidationSeverity::Warning
                ? &mVulkanWarningCount : &mVulkanInfoCount;
    } else {
        counter = severity == RendererValidationSeverity::Error
            ? &mNriErrorCount
            : severity == RendererValidationSeverity::Warning
                ? &mNriWarningCount : &mNriInfoCount;
    }
    counter->fetch_add(1U, std::memory_order_relaxed);
}

void RendererValidationTelemetry::Reset() {
    mVulkanEnabled.store(false, std::memory_order_relaxed);
    mNriEnabled.store(false, std::memory_order_relaxed);
    mVulkanInfoCount.store(0U, std::memory_order_relaxed);
    mVulkanWarningCount.store(0U, std::memory_order_relaxed);
    mVulkanErrorCount.store(0U, std::memory_order_relaxed);
    mNriInfoCount.store(0U, std::memory_order_relaxed);
    mNriWarningCount.store(0U, std::memory_order_relaxed);
    mNriErrorCount.store(0U, std::memory_order_relaxed);
}

RendererValidationSnapshot RendererValidationTelemetry::Snapshot() const {
    return {
        mVulkanEnabled.load(std::memory_order_relaxed),
        mNriEnabled.load(std::memory_order_relaxed),
        mVulkanInfoCount.load(std::memory_order_relaxed),
        mVulkanWarningCount.load(std::memory_order_relaxed),
        mVulkanErrorCount.load(std::memory_order_relaxed),
        mNriInfoCount.load(std::memory_order_relaxed),
        mNriWarningCount.load(std::memory_order_relaxed),
        mNriErrorCount.load(std::memory_order_relaxed),
    };
}

} // namespace Fast::Oot3d
