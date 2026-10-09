#include "native_a32_read_probe.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

using namespace Oot3dNativeGame;

namespace {
int gChecks = 0;
void Check(bool condition, const char *what) {
  ++gChecks;
  if (!condition) {
    std::fprintf(stderr, "read probe tests FAILED: %s\n", what);
    std::exit(1);
  }
}

std::map<uint32_t, uint8_t> gMemory;
void Poke32(uint32_t address, uint32_t value) {
  for (uint32_t b = 0; b < 4U; ++b)
    gMemory[address + b] = static_cast<uint8_t>(value >> (8U * b));
}
bool Read32(uint32_t address, uint32_t *value) {
  *value = 0;
  for (uint32_t b = 0; b < 4U; ++b) {
    const auto found = gMemory.find(address + b);
    if (found == gMemory.end())
      return false;
    *value |= uint32_t{found->second} << (8U * b);
  }
  return true;
}
bool ReadBytes(uint32_t address, uint8_t *bytes, uint32_t size) {
  for (uint32_t i = 0; i < size; ++i) {
    const auto found = gMemory.find(address + i);
    if (found == gMemory.end())
      return false;
    bytes[i] = found->second;
  }
  return true;
}
} // namespace

int main() {
  uint32_t registers[16]{};
  registers[0] = 0x1000;
  registers[1] = 0x2000;
  registers[13] = 0x3000;
  registers[14] = 0x4000;
  Poke32(0x20A0, 0x5000);
  Poke32(0x5148, 0x0012006F);
  Poke32(0x656B5C, 0x08045D38);

  Check(EvaluateA32ProbeAddress("r1+0xA0*+0x148", registers, Read32) == 0x5148U, "register deref chain");
  Check(EvaluateA32ProbeAddress("0x656B5C*-0x1F8", registers, Read32) == 0x08045D38U - 0x1F8U, "absolute deref");
  Check(EvaluateA32ProbeAddress("sp+8", registers, Read32) == 0x3008U, "sp");
  Check(EvaluateA32ProbeAddress("lr", registers, Read32) == 0x4000U, "lr");
  std::string error;
  Check(!EvaluateA32ProbeAddress("r1+0x10*", registers, Read32, &error) && error == "unreadable dereference",
        "unreadable deref reported");
  Check(!EvaluateA32ProbeAddress("r16", registers, Read32) && !EvaluateA32ProbeAddress("x1", registers, Read32) &&
            !EvaluateA32ProbeAddress("r1+", registers, Read32),
        "malformed expressions rejected");

  auto set = A32ReadProbeSet::Parse(R"({"first":10,"last":20,"probes":[
    {"pc":"0x180B68","label":"cam","max":2,"reads":[
      {"label":"scene","addr":"r1+0xA0*+0x148","w":2},
      {"label":"pair","addr":"r1+0xA0*+0x148","w":2,"n":2},
      {"label":"missing","addr":"r0+4"}]}]})");
  Check(set.Pcs().size() == 1U && set.Watches(0x180B68U) && !set.Watches(0x180B6CU), "pcs");
  Check(!set.Observe(0x180B68U, 9, registers, Read32, ReadBytes), "before window");
  const auto line = set.Observe(0x180B68U, 10, registers, Read32, ReadBytes);
  Check(line.has_value(), "hit in window");
  const auto json = nlohmann::json::parse(*line);
  Check(json["reads"]["scene"]["v"] == 0x6FU && json["reads"]["pair"]["v"][1] == 0x12U, "values little endian");
  Check(json["reads"]["missing"]["ok"] == false && json["r"][1] == 0x2000U && json["f"] == 10, "failed read kept");
  Check(set.Observe(0x180B68U, 11, registers, Read32, ReadBytes).has_value() &&
            !set.Observe(0x180B68U, 12, registers, Read32, ReadBytes),
        "hit limit");
  bool threw = false;
  try {
    (void)A32ReadProbeSet::Parse(R"({"probes":[{"pc":1,"reads":[{"addr":"r0","w":3}]}]})");
  } catch (const std::exception &) {
    threw = true;
  }
  Check(threw, "bad width rejected");
  std::printf("read probe tests passed: %d checks\n", gChecks);
  return 0;
}
