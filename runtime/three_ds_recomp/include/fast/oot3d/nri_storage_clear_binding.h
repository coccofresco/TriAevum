#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

// Owns the descriptor contract required by NRI CmdClearStorage. The resource
// transition remains the caller's responsibility so storage clears can be
// composed into different passes without duplicating barrier policy.
class NriStorageClearBinding final {
  public:
    NriStorageClearBinding();
    ~NriStorageClearBinding();
    NriStorageClearBinding(const NriStorageClearBinding&) = delete;
    NriStorageClearBinding& operator=(
        const NriStorageClearBinding&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool ClearUint32(uint32_t frameIndex, VkImage image,
                     uint32_t value);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
