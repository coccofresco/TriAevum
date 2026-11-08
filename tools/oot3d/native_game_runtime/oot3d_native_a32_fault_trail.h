#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Oot3dNativeGame {

// The support-report context of a guest fault (developer and product builds
// alike): the faulting thread's registers and the last dispatch transitions
// (dispatch entry pc -> exit pc, exit kind). Guest addresses and register
// values only: the text never names a host path, user or machine.
class NativeA32FaultTrail {
  public:
    static constexpr std::size_t kCapacity = 32;
    struct Transition {
        uint32_t From = 0;
        uint32_t To = 0;
        uint8_t Kind = 0;
    };

    void Record(uint32_t from, uint32_t to, uint8_t kind) noexcept {
        mEntries[static_cast<std::size_t>(mNext % kCapacity)] = {from, to, kind};
        ++mNext;
    }
    std::size_t Size() const noexcept {
        return mNext < kCapacity ? static_cast<std::size_t>(mNext) : kCapacity;
    }
    // Oldest first.
    Transition At(std::size_t index) const noexcept {
        const uint64_t first = mNext - Size();
        return mEntries[static_cast<std::size_t>((first + index) % kCapacity)];
    }

    // " lr=.. r0=.. .. r12=.. sp=.. cpsr=.. trail=N:from>to/kind,.." on one line.
    template <class Registers>
    std::string Describe(const Registers& r, uint32_t cpsr) const {
        std::string text;
        char word[40];
        std::snprintf(word, sizeof(word), " lr=0x%08X", static_cast<unsigned>(r[14]));
        text += word;
        for (unsigned i = 0; i <= 12; ++i) {
            std::snprintf(word, sizeof(word), " r%u=0x%08X", i, static_cast<unsigned>(r[i]));
            text += word;
        }
        std::snprintf(word, sizeof(word), " sp=0x%08X cpsr=0x%08X", static_cast<unsigned>(r[13]),
                      static_cast<unsigned>(cpsr));
        text += word;
        std::snprintf(word, sizeof(word), " trail=%u:", static_cast<unsigned>(Size()));
        text += word;
        for (std::size_t i = 0; i < Size(); ++i) {
            const auto t = At(i);
            std::snprintf(word, sizeof(word), "%s%08X>%08X/%u", i == 0 ? "" : ",",
                          static_cast<unsigned>(t.From), static_cast<unsigned>(t.To),
                          static_cast<unsigned>(t.Kind));
            text += word;
        }
        return text;
    }

  private:
    std::array<Transition, kCapacity> mEntries{};
    uint64_t mNext = 0;
};

} // namespace Oot3dNativeGame
