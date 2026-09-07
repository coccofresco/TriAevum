#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

struct TitlePresentationSettingsView {
    // 0 windowed, 1 borderless desktop, 2 exclusive fullscreen.
    uint8_t WindowMode = 0;
    uint32_t Width = 1280;
    uint32_t Height = 720;
    bool VSync = true;
    // 0 baseline, 1 candidate awaiting confirmation, 2 rollback.
    uint8_t TransactionKind = 0;
};

struct TitleSceneViewSubmission {
    uint32_t GuestFunction = 0;
    uint32_t GuestReturnAddress = 0;
    float Left = 0.0F;
    float Right = 0.0F;
    float Bottom = 0.0F;
    float Top = 0.0F;
    float NearPlane = 0.0F;
    float FarPlane = 0.0F;
    std::array<float, 3> Eye{};
    std::array<float, 3> At{};
    bool CameraAvailable = true;
};

// Optional renderer hooks owned by the OOT3D title adapter. Other Nintendo
// 3DS games provide their own adapter while sharing the PICA backend contract.
class TitleRenderBackend {
  public:
    virtual ~TitleRenderBackend() = default;

    virtual bool PrepareOverlay(std::string* = nullptr) {
        return true;
    }
    virtual bool ApplyPresentationSettings(
        const TitlePresentationSettingsView&,
        std::string* error = nullptr) {
        if (error != nullptr) {
            *error = "renderer cannot apply OOT3D presentation settings";
        }
        return false;
    }
    virtual bool PublishSceneView(const TitleSceneViewSubmission&) {
        return false;
    }
    virtual bool ResetTitleState(std::string* = nullptr) {
        return true;
    }
};

} // namespace Fast::Oot3d
