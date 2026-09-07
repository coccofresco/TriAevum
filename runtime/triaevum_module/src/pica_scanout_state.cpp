#include "triaevum/pica_scanout_state.h"

namespace triaevum::module {

TriAevumModuleStatusV1
PicaScanoutStateV1::ValidateFramebuffer(const PicaFramebufferV1 &framebuffer) {
  if (framebuffer.screen >= 2U || framebuffer.activeBuffer >= 2U ||
      framebuffer.shownBuffer >= 2U || framebuffer.stride == 0U ||
      (framebuffer.format & 7U) > 4U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
PicaScanoutStateV1::SetFramebuffer(const PicaFramebufferV1 &framebuffer) {
  const TriAevumModuleStatusV1 status = ValidateFramebuffer(framebuffer);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }

  std::lock_guard lock(mMutex);
  Screen &screen = mScreens[framebuffer.screen];
  screen.slots[framebuffer.activeBuffer] = {
      framebuffer.addressLeft,
      framebuffer.addressRight,
      framebuffer.stride,
      framebuffer.format,
      true,
  };
  screen.shownBuffer = framebuffer.shownBuffer;
  ++mRevision;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 PicaScanoutStateV1::SetLcdForceBlack(bool forceBlack) {
  std::lock_guard lock(mMutex);
  if (mLcdForceBlack != forceBlack) {
    mLcdForceBlack = forceBlack;
    ++mRevision;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

std::optional<PicaScanoutFramebufferV1>
PicaScanoutStateV1::FramebufferLocked(std::uint32_t screen) const {
  if (screen >= mScreens.size()) {
    return std::nullopt;
  }
  const Screen &selectedScreen = mScreens[screen];
  const Slot &slot = selectedScreen.slots[selectedScreen.shownBuffer];
  if (!slot.configured) {
    return std::nullopt;
  }
  return PicaScanoutFramebufferV1{slot.addressLeft, slot.addressRight,
                                  slot.stride, slot.format,
                                  selectedScreen.shownBuffer};
}

std::optional<PicaScanoutFramebufferV1>
PicaScanoutStateV1::Framebuffer(std::uint32_t screen) const {
  std::lock_guard lock(mMutex);
  return FramebufferLocked(screen);
}

bool PicaScanoutStateV1::ShouldPresent(std::uint32_t screen,
                                       std::uint32_t outputAddress) const {
  std::lock_guard lock(mMutex);
  if (mLcdForceBlack) {
    return false;
  }
  const auto framebuffer = FramebufferLocked(screen);
  return framebuffer.has_value() &&
         (outputAddress == framebuffer->addressLeft ||
          outputAddress == framebuffer->addressRight);
}

bool PicaScanoutStateV1::LcdForceBlack() const {
  std::lock_guard lock(mMutex);
  return mLcdForceBlack;
}

std::uint64_t PicaScanoutStateV1::Revision() const {
  std::lock_guard lock(mMutex);
  return mRevision;
}

} // namespace triaevum::module
