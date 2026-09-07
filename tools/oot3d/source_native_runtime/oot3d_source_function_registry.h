#pragma once

#include "oot3d_guest_address_space.h"

#include <any>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Oot3dSourceRuntime {

class SourceFunctionRegistry {
  public:
    template <typename Function>
    bool Register(GuestAddress address, Function* function, std::string name) {
        static_assert(std::is_function_v<Function>);
        if (address == 0 || function == nullptr || Contains(address)) {
            return false;
        }
        mEntries.push_back({address, std::move(name), function});
        return true;
    }

    bool RegisterRaw(GuestAddress address, void* function, std::string name);
    void* ResolveRaw(GuestAddress address) const;

    template <typename Function>
    Function* Resolve(GuestAddress address) const {
        static_assert(std::is_function_v<Function>);
        const auto found = Find(address);
        if (found == nullptr) {
            return nullptr;
        }
        Function* const* function = std::any_cast<Function*>(&found->FunctionPointer);
        return function == nullptr ? nullptr : *function;
    }

    bool Contains(GuestAddress address) const;
    std::string_view Name(GuestAddress address) const;
    std::size_t Size() const;

  private:
    struct Entry {
        GuestAddress Address;
        std::string Name;
        std::any FunctionPointer;
    };

    Entry* Find(GuestAddress address);
    const Entry* Find(GuestAddress address) const;
    std::vector<Entry> mEntries;
};

} // namespace Oot3dSourceRuntime

extern "C" {
void oot3d_host_register_target_function(std::uint32_t address,
                                         void* function,
                                         const char* name);
void oot3d_host_register_target_function_overlay(std::uint32_t address,
                                                 void* function,
                                                 const char* name);
void oot3d_host_finalize_target_function_registry(std::uint32_t expectedCount);
void* oot3d_host_resolve_target_function(std::uintptr_t address);
}
