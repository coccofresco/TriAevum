#pragma once

#include <cstdint>
#include <unordered_map>

namespace Fast::Renderer3ds {

// Occurrences distinguish repeated resources only within a presentation
// namespace. Work in another namespace must not change a draw's history slot.
// This is not an object identity: callers still require stable within-namespace
// ordering for repeated geometry.
class PicaTemporalOccurrences {
  public:
    [[nodiscard]] uint32_t Next(uint64_t geometryIdentity, uint64_t renderTargetNamespace) {
        return mNamespaces[renderTargetNamespace][geometryIdentity]++;
    }

    void clear() { mNamespaces.clear(); }

  private:
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> mNamespaces;
};

} // namespace Fast::Renderer3ds
