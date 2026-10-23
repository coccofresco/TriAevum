#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Fast::Oot3d {

// Image handles may be recycled after destruction. The interop owner supplies
// a new generation whenever it registers a texture (owned or borrowed).
struct UpscalerDescriptorBinding {
    uint64_t Image = 0;
    uint64_t Generation = 0;
    bool Storage = false;
};

[[nodiscard]] inline std::optional<std::vector<uint64_t>> BuildUpscalerDescriptorKey(
    bool input, std::span<const UpscalerDescriptorBinding> bindings) {
    if (bindings.empty()) return std::nullopt;
    std::vector<uint64_t> key{input ? 1U : 0U};
    key.reserve(1U + 3U * bindings.size());
    for (const auto& binding : bindings) {
        // An unregistered image must never hit a cached descriptor set.
        if (binding.Image == 0U || binding.Generation == 0U) return std::nullopt;
        key.push_back(binding.Image);
        key.push_back(binding.Generation);
        key.push_back(binding.Storage ? 1U : 0U);
    }
    return key;
}

} // namespace Fast::Oot3d
