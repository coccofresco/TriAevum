#pragma once

// Developer read probes (TRIAEVUM_A32_PROBE=SPEC.json,OUT.jsonl): at chosen
// guest code addresses, within a host-frame window, record registers and
// memory reads described by address expressions, one JSON line per hit.
// Read-only and title-neutral: the spec names the title's addresses.
//
// An address expression is a base (a register r0..r15/sp/lr, or a number)
// followed by terms: "+N"/"-N" adds, "*" replaces the value with the u32 at
// it. "r1+0xA0*+0x148" is u32[r1+0xA0]+0x148.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

struct A32ReadProbeRead {
  std::string Label;
  std::string Address; // address expression
  uint32_t Width = 4;  // 1, 2 or 4 bytes per element
  uint32_t Count = 1;
};

struct A32ReadProbe {
  uint32_t Pc = 0;
  std::string Label;
  uint32_t MaxHits = 0; // 0: unlimited
  std::vector<A32ReadProbeRead> Reads;
  uint32_t Hits = 0;
};

using A32ProbeRead32 = std::function<bool(uint32_t address, uint32_t *value)>;
using A32ProbeReadBytes = std::function<bool(uint32_t address, uint8_t *bytes, uint32_t size)>;

// Evaluates an address expression; nullopt when malformed or a dereference
// cannot be read. `registers` holds r0..r15.
std::optional<uint32_t> EvaluateA32ProbeAddress(const std::string &expression, const uint32_t (&registers)[16],
                                                const A32ProbeRead32 &read32, std::string *error = nullptr);

class A32ReadProbeSet {
public:
  // Parses the spec ({"first","last","probes":[{"pc","label","max","reads":[
  // {"label","addr","w","n"}]}]}); throws std::runtime_error on errors.
  static A32ReadProbeSet Parse(const std::string &json);

  [[nodiscard]] std::vector<uint32_t> Pcs() const;
  [[nodiscard]] bool Watches(uint32_t pc) const { return mProbes.count(pc) != 0U; }
  // One JSON line (without newline) for a hit, or nothing outside the window
  // or past the probe's hit limit.
  std::optional<std::string> Observe(uint32_t pc, uint32_t hostFrame, const uint32_t (&registers)[16],
                                     const A32ProbeRead32 &read32, const A32ProbeReadBytes &readBytes);

private:
  uint32_t mFirst = 0;
  uint32_t mLast = UINT32_MAX;
  std::unordered_map<uint32_t, A32ReadProbe> mProbes;
};

} // namespace Oot3dNativeGame
