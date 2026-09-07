#pragma once

#include <cstdint>
#include <string_view>

namespace Fast::Renderer3ds {

struct PicaShaderSourceIdentity {
    uint64_t Id = 0;
    uint64_t SecondaryHash = 0;
    uint64_t Size = 0;

    [[nodiscard]] bool Available() const noexcept {
        return Id != 0U && SecondaryHash != 0U && Size != 0U;
    }

    bool operator==(const PicaShaderSourceIdentity&) const = default;
};

[[nodiscard]] inline PicaShaderSourceIdentity
IdentifyPicaShaderSource(std::string_view source) noexcept {
    constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    const auto hash = [&](uint64_t seed) {
        uint64_t value = seed;
        for (const char byte : source) {
            value = (value ^ static_cast<uint8_t>(byte)) * kFnvPrime;
        }
        return value == 0U ? 1U : value;
    };
    return {
        hash(kFnvOffset),
        hash(kFnvPrime ^ source.size()),
        source.size(),
    };
}

} // namespace Fast::Renderer3ds
