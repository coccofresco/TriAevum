#ifndef TRIAEVUM_PICA_SCANOUT_STATE_H
#define TRIAEVUM_PICA_SCANOUT_STATE_H

#include "triaevum/pica_service_adapter.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace triaevum::module {

struct PicaScanoutFramebufferV1 {
  std::uint32_t addressLeft = 0U;
  std::uint32_t addressRight = 0U;
  std::uint32_t stride = 0U;
  std::uint32_t format = 0U;
  std::uint32_t bufferIndex = 0U;
};

// Title-neutral state for the two CTR displays and their double-buffered
// scanout surfaces. Renderer resources deliberately remain outside it.
class PicaScanoutStateV1 final {
public:
  static TriAevumModuleStatusV1
  ValidateFramebuffer(const PicaFramebufferV1 &framebuffer);

  TriAevumModuleStatusV1 SetFramebuffer(const PicaFramebufferV1 &framebuffer);
  TriAevumModuleStatusV1 SetLcdForceBlack(bool forceBlack);

  [[nodiscard]] std::optional<PicaScanoutFramebufferV1>
  Framebuffer(std::uint32_t screen) const;
  [[nodiscard]] bool ShouldPresent(std::uint32_t screen,
                                   std::uint32_t outputAddress) const;
  [[nodiscard]] bool LcdForceBlack() const;
  [[nodiscard]] std::uint64_t Revision() const;

private:
  struct Slot {
    std::uint32_t addressLeft = 0U;
    std::uint32_t addressRight = 0U;
    std::uint32_t stride = 0U;
    std::uint32_t format = 0U;
    bool configured = false;
  };

  struct Screen {
    std::array<Slot, 2> slots{};
    std::uint32_t shownBuffer = 0U;
  };

  [[nodiscard]] std::optional<PicaScanoutFramebufferV1>
  FramebufferLocked(std::uint32_t screen) const;

  mutable std::mutex mMutex;
  std::array<Screen, 2> mScreens{};
  bool mLcdForceBlack = false;
  std::uint64_t mRevision = 0U;
};

} // namespace triaevum::module

#endif
