#include "triaevum/pica_scanout_state.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
  std::cerr << "pica_scanout_state_tests: " << message << '\n';
  std::exit(1);
}

void Require(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

} // namespace

int main() {
  using namespace triaevum::module;
  PicaScanoutStateV1 state;
  PicaFramebufferV1 framebuffer;
  framebuffer.screen = 0U;
  framebuffer.activeBuffer = 1U;
  framebuffer.addressLeft = 0x14002000U;
  framebuffer.addressRight = 0x14003000U;
  framebuffer.stride = 960U;
  framebuffer.format = 0U;
  framebuffer.shownBuffer = 0U;

  Require(state.SetFramebuffer(framebuffer) == TRIAEVUM_MODULE_OK_V1 &&
              !state.Framebuffer(0U).has_value() && state.Revision() == 1U,
          "hidden framebuffer slot was selected before configuration");

  framebuffer.activeBuffer = 0U;
  framebuffer.addressLeft = 0x14001000U;
  framebuffer.addressRight = 0x14001000U;
  framebuffer.shownBuffer = 1U;
  Require(state.SetFramebuffer(framebuffer) == TRIAEVUM_MODULE_OK_V1 &&
              state.Framebuffer(0U).has_value() &&
              state.Framebuffer(0U)->addressLeft == 0x14002000U &&
              state.Framebuffer(0U)->bufferIndex == 1U &&
              state.ShouldPresent(0U, 0x14002000U) &&
              state.ShouldPresent(0U, 0x14003000U) &&
              !state.ShouldPresent(0U, 0x14001000U),
          "shown CTR framebuffer did not follow its selected slot");

  Require(state.SetLcdForceBlack(true) == TRIAEVUM_MODULE_OK_V1 &&
              state.LcdForceBlack() && !state.ShouldPresent(0U, 0x14002000U),
          "LCD force-black did not suppress scanout");
  Require(state.SetLcdForceBlack(false) == TRIAEVUM_MODULE_OK_V1 &&
              !state.LcdForceBlack() && state.ShouldPresent(0U, 0x14002000U),
          "clearing LCD force-black did not restore scanout");

  const std::uint64_t revision = state.Revision();
  framebuffer.screen = 2U;
  Require(state.SetFramebuffer(framebuffer) ==
                  TRIAEVUM_MODULE_INVALID_ARGUMENT_V1 &&
              state.Revision() == revision &&
              !state.Framebuffer(2U).has_value(),
          "invalid CTR framebuffer mutated scanout state");
  return 0;
}
