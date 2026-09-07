#pragma once

#include "fast/oot3d/pica_toon_shader.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace Fast::Oot3d {

struct PicaToonTelemetrySnapshot {
    std::array<uint64_t, 7> ByEligibility{};

    [[nodiscard]] uint64_t Count(PicaToonEligibility value) const {
        return ByEligibility[static_cast<size_t>(value)];
    }
    [[nodiscard]] uint64_t Total() const;
};

class PicaToonTelemetry final {
  public:
    static PicaToonTelemetry& Instance();
    [[nodiscard]] uint64_t Record(PicaToonEligibility eligibility);
    [[nodiscard]] PicaToonTelemetrySnapshot Snapshot() const;

  private:
    std::array<std::atomic<uint64_t>, 7> mCounts{};
    std::atomic<uint64_t> mTotal{ 0 };
};

} // namespace Fast::Oot3d
