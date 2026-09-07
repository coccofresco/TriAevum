#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Oot3dNativeGame {

constexpr uint32_t kSourceObjHanaInitCallbackEntry = 0x001E1734U;

struct SourceActorCallbackInvocation {
    uint32_t EntryAddress = 0U;
    uint32_t ActorAddress = 0U;
    uint32_t PlayAddress = 0U;
    uint32_t ParentFrameSize = 0U;
};

class SourceActorCallbackHost {
  public:
    virtual ~SourceActorCallbackHost() = default;

    virtual const uint8_t* ResolveGuestRead(
        uint32_t address, size_t size, std::string* error) = 0;
    virtual uint8_t* ResolveGuestWrite(
        uint32_t address, size_t size, std::string* error) = 0;
    virtual bool InvokeGuest(
        uint32_t entryAddress, std::span<const uint32_t> core,
        std::span<const uint32_t> vfp,
        std::span<const uint32_t> stack, uint32_t frameSize,
        uint32_t* coreResult, std::string* error) = 0;
};

enum class SourceActorCallbackDispatchResult : uint8_t {
    NotHandled,
    Completed,
    Failed,
};

struct SourceActorCallbackStats {
    uint64_t Dispatches = 0U;
    uint64_t ObjHanaInitCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t Failures = 0U;
};

// Address-driven source callback registry used inside promoted actor owners.
// It has no process or gameplay policy; guest memory and calls are supplied by
// the active owner bridge.
class SourceActorCallbackRuntime {
  public:
    bool CanDispatch(uint32_t entryAddress) const noexcept;
    SourceActorCallbackDispatchResult Dispatch(
        const SourceActorCallbackInvocation& invocation,
        SourceActorCallbackHost& host);

    SourceActorCallbackStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    SourceActorCallbackStats mStats;
    std::string mLastError;
};

} // namespace Oot3dNativeGame
