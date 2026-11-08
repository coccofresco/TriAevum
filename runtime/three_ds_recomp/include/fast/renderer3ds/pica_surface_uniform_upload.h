#pragma once

#include "fast/renderer3ds/pica_fragment_balance.h"

#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace Fast::Renderer3ds {

// Reservation and descriptor range must describe the same payload. In
// particular, a balanced draw appends native fragment-light gains to Surface.
[[nodiscard]] constexpr size_t PicaSurfaceUniformUploadSize(bool enabled, bool balanced) noexcept {
    return !enabled ? 0U : balanced ? sizeof(PicaBalancedSurfaceUniforms) : sizeof(PicaSurfaceLightingUniforms);
}

// The same byte-exact last-upload reuse as ordinary surface lighting, but a
// separate slot: plain and balanced descriptor ranges cannot alias. Compare
// host bytes, never write-combined GPU upload memory. The caller resets this
// when recycling a frame slot OR replacing its arena, after retiring readers.
class PicaBalancedSurfaceUploadCache {
public:
    template <class Upload>
    uint64_t Resolve(const PicaBalancedSurfaceUniforms& uniforms, Upload&& upload) {
        static_assert(std::is_trivially_copyable_v<PicaBalancedSurfaceUniforms>);
        const auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&uniforms), sizeof(uniforms));
        if (!mOffset || std::memcmp(mBytes.data(), bytes.data(), bytes.size()) != 0) {
            const uint64_t offset = upload(bytes); // failure must not publish an offset
            std::memcpy(mBytes.data(), bytes.data(), bytes.size());
            mOffset = offset;
        }
        return *mOffset;
    }

    void Reset() noexcept { mOffset.reset(); }

private:
    std::optional<uint64_t> mOffset;
    std::array<uint8_t, sizeof(PicaBalancedSurfaceUniforms)> mBytes{};
};

} // namespace Fast::Renderer3ds
