#pragma once

#include "triaevum/module_abi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Oot3dNativeGame {

inline constexpr std::array<uint8_t, 32> kOot3dGameModuleIdentity{
    0x16U, 0xA6U, 0xB0U, 0xAAU, 0x4CU, 0x47U, 0x84U, 0x68U, 0x02U, 0x20U, 0xA6U,
    0xF7U, 0x80U, 0xF7U, 0xF8U, 0xA7U, 0x3CU, 0xFBU, 0x20U, 0x55U, 0x57U, 0xAAU,
    0x9FU, 0x9FU, 0x0EU, 0x70U, 0x51U, 0x79U, 0xE0U, 0x61U, 0x32U, 0x20U,
};

class Oot3dGameModuleRuntime final {
public:
  static std::unique_ptr<Oot3dGameModuleRuntime>
  Create(const TriAevumHostApiV1 &host, std::string_view privateContentIndex,
         std::string *error = nullptr);

  ~Oot3dGameModuleRuntime();

  Oot3dGameModuleRuntime(const Oot3dGameModuleRuntime &) = delete;
  Oot3dGameModuleRuntime &operator=(const Oot3dGameModuleRuntime &) = delete;

  TriAevumModuleStatusV1 Start(std::string *error = nullptr);
  TriAevumModuleStatusV1 RunFrame(const TriAevumFrameInputV1 &input,
                                  std::string *error = nullptr);
  size_t StateSize(std::string *error = nullptr);
  TriAevumModuleStatusV1 SaveState(TriAevumMutableBytesV1 destination,
                                   size_t *writtenSize,
                                   std::string *error = nullptr);
  TriAevumModuleStatusV1 LoadState(TriAevumReadOnlyBytesV1 source,
                                   std::string *error = nullptr);
  TriAevumModuleStatusV1
  MapGuestMemory(const TriAevumGuestMemoryMapRequestV1 &request,
                 TriAevumGuestMemoryViewV1 *view);
  TriAevumModuleStatusV1 UnmapGuestMemory(uint64_t token, uint32_t flags);

private:
  struct Impl;

  explicit Oot3dGameModuleRuntime(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> mImpl;
};

} // namespace Oot3dNativeGame
