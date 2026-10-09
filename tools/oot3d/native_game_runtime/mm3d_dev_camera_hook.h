#pragma once
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "mm3d_dev_tools.h"
#include "mm3d_guest_patches.h"
namespace Oot3dNativeGame {
inline void RegisterMm3dDevCamera(Mm3dGuestPatches &patches, std::shared_ptr<DevWarpTitleAbi> abi,
                                  std::shared_ptr<DevFreeCamera> camera) {
  Mm3dGuestPatchSite site;
  site.Pc = Mm3dDevAbi::CameraUpdate;
  site.Name = "MM3D developer main camera";
  site.Guard.assign(Mm3dDevAbi::CameraGuard.begin(), Mm3dDevAbi::CameraGuard.end());
  site.Handler = [abi = std::move(abi), camera = std::move(camera),
                  reported = false](auto &state, auto &memory) mutable -> std::optional<uint32_t> {
    class Guest final : public DevWarpGuest {
      oot3d::recomp::a32::MemoryBus &m;

    public:
      explicit Guest(oot3d::recomp::a32::MemoryBus &value) : m(value) {}
      bool Read(uint32_t a, std::span<uint8_t> b) override {
        for (size_t i = 0; i < b.size(); ++i)
          if (!m.Read8(a + uint32_t(i), &b[i]))
            return false;
        return true;
      }
      bool Write(uint32_t a, std::span<const uint8_t> b) override {
        for (size_t i = 0; i < b.size(); ++i)
          if (!m.Write8(a + uint32_t(i), b[i]))
            return false;
        return true;
      }
      bool RequestTransition(uint32_t, uint16_t, bool *, std::string *) override { return false; }
    } guest(memory);
    if (!abi->ApplyCamera(guest, *camera, state.r[0], state.r[1]))
      return std::nullopt;
    if (!reported) {
      reported = true;
      const auto view = camera->LastView();
      if (view)
        std::fprintf(stderr, "TRIAEVUM_DEV_FREECAM applied camera=0x%08X eye=%.1f,%.1f,%.1f\n", state.r[1],
                     view->Eye[0], view->Eye[1], view->Eye[2]);
    }
    return state.r[14]; // function entry; prologue has not run
  };
  patches.Add(std::move(site));
}
} // namespace Oot3dNativeGame
#endif
