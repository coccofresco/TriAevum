#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dSourceRuntime {

using GuestAddress = std::uint32_t;

enum class GuestMemoryAccess : std::uint8_t {
    Read = 1,
    Write = 2,
    Execute = 4,
};

constexpr GuestMemoryAccess operator|(GuestMemoryAccess left, GuestMemoryAccess right) {
    return static_cast<GuestMemoryAccess>(static_cast<std::uint8_t>(left) |
                                          static_cast<std::uint8_t>(right));
}

constexpr bool HasAccess(GuestMemoryAccess value, GuestMemoryAccess required) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(required)) ==
           static_cast<std::uint8_t>(required);
}

class GuestAddressSpace {
  public:
    virtual ~GuestAddressSpace() = default;

    virtual std::span<const std::byte> ResolveRead(GuestAddress address,
                                                   std::size_t size) const = 0;
    virtual std::span<std::byte> ResolveWrite(GuestAddress address,
                                             std::size_t size) = 0;
};

class MappedGuestAddressSpace final : public GuestAddressSpace {
  public:
    bool Map(GuestAddress base, std::vector<std::byte> bytes,
             GuestMemoryAccess access, std::string name);
    bool MapZeroed(GuestAddress base, std::size_t size,
                   GuestMemoryAccess access, std::string name);
    bool Unmap(GuestAddress base, std::size_t size);

    std::span<const std::byte> ResolveRead(GuestAddress address,
                                           std::size_t size) const override;
    std::span<std::byte> ResolveWrite(GuestAddress address,
                                     std::size_t size) override;

    std::size_t RegionCount() const;
    std::optional<std::string_view> RegionName(GuestAddress address) const;

  private:
    struct Region {
        GuestAddress Base = 0;
        std::vector<std::byte> Bytes;
        GuestMemoryAccess Access = GuestMemoryAccess::Read;
        std::string Name;
    };

    const Region* FindRegion(GuestAddress address, std::size_t size,
                             GuestMemoryAccess access) const;
    Region* FindRegion(GuestAddress address, std::size_t size,
                       GuestMemoryAccess access);

    std::vector<Region> mRegions;
};

class DirectMappedGuestAddressSpace final : public GuestAddressSpace {
  public:
    DirectMappedGuestAddressSpace() = default;
    ~DirectMappedGuestAddressSpace();

    DirectMappedGuestAddressSpace(const DirectMappedGuestAddressSpace&) = delete;
    DirectMappedGuestAddressSpace& operator=(const DirectMappedGuestAddressSpace&) = delete;
    DirectMappedGuestAddressSpace(DirectMappedGuestAddressSpace&& other) noexcept;
    DirectMappedGuestAddressSpace& operator=(DirectMappedGuestAddressSpace&& other) noexcept;

    bool Map(GuestAddress base, std::vector<std::byte> bytes,
             GuestMemoryAccess access, std::string name);
    bool MapZeroed(GuestAddress base, std::size_t size,
                   GuestMemoryAccess access, std::string name);
    bool Unmap(GuestAddress base, std::size_t size);

    std::span<const std::byte> ResolveRead(GuestAddress address,
                                           std::size_t size) const override;
    std::span<std::byte> ResolveWrite(GuestAddress address,
                                     std::size_t size) override;

    std::size_t RegionCount() const;

  private:
    bool MapStorage(GuestAddress base, std::size_t size,
                    std::span<const std::byte> initialBytes,
                    GuestMemoryAccess access, std::string name);

    struct Region {
        GuestAddress Base = 0;
        std::size_t Size = 0;
        GuestMemoryAccess Access = GuestMemoryAccess::Read;
        std::string Name;
    };

    struct Allocation {
        void* Base = nullptr;
        std::size_t Size = 0;
    };

    const Region* FindRegion(GuestAddress address, std::size_t size,
                             GuestMemoryAccess access) const;
    void Release();

    std::vector<Region> mRegions;
    std::vector<Allocation> mAllocations;
};

} // namespace Oot3dSourceRuntime
