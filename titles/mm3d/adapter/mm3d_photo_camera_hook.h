#pragma once
#include "mm3d_photo_camera.h"
#include "mm3d_guest_patches.h"
#include <memory>
namespace Oot3dNativeGame {
inline void RegisterMm3dPhotoCamera(Mm3dGuestPatches& patches, Mm3dPhotoCamera& abi,
                                    std::shared_ptr<DevFreeCamera> camera) {
  if (!camera) return;
  Mm3dGuestPatchSite site;
  site.Pc=kMm3dCameraUpdateSite.Pc;
  site.Name="MM3D product photo camera";
  for(uint32_t i=0;i<kMm3dCameraUpdateSite.Words.size();++i)
    site.Guard.emplace_back(site.Pc+4*i,kMm3dCameraUpdateSite.Words[i]);
  site.Handler=[&abi,camera=std::move(camera)](auto& state,auto& memory)->std::optional<uint32_t> {
    GuestByteAccess guest(memory);
    if(!abi.Apply(guest,*camera,state.r[0],state.r[1]))return std::nullopt;
    return state.r[14]; // entry, no prologue to unwind; output direction populated
  };
  patches.Add(std::move(site));
}
} // namespace Oot3dNativeGame
