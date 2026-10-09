#include "native_a32_read_probe.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

std::optional<uint32_t> ParseNumber(const std::string &text, size_t *position) {
  size_t used = 0;
  try {
    const unsigned long long value = std::stoull(text.substr(*position), &used, 0);
    if (value > UINT32_MAX)
      return std::nullopt;
    *position += used;
    return static_cast<uint32_t>(value);
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

uint32_t ParseAddressValue(const nlohmann::json &value, const char *field) {
  if (value.is_number_unsigned())
    return value.get<uint32_t>();
  if (value.is_string()) {
    size_t position = 0;
    const auto text = value.get<std::string>();
    if (const auto number = ParseNumber(text, &position); number && position == text.size())
      return *number;
  }
  throw std::runtime_error(std::string("TRIAEVUM_A32_PROBE: ") + field + " must be a number or numeric string");
}

} // namespace

std::optional<uint32_t> EvaluateA32ProbeAddress(const std::string &expression, const uint32_t (&registers)[16],
                                                const A32ProbeRead32 &read32, std::string *error) {
  const auto fail = [&](const std::string &why) -> std::optional<uint32_t> {
    if (error != nullptr)
      *error = why;
    return std::nullopt;
  };
  size_t position = 0;
  uint32_t value = 0;
  if (expression.rfind("sp", 0) == 0) {
    value = registers[13];
    position = 2;
  } else if (expression.rfind("lr", 0) == 0) {
    value = registers[14];
    position = 2;
  } else if (!expression.empty() && expression[0] == 'r') {
    position = 1;
    size_t digits = position;
    while (digits < expression.size() && std::isdigit(static_cast<unsigned char>(expression[digits])))
      ++digits;
    if (digits == position)
      return fail("register without a number");
    const unsigned index = static_cast<unsigned>(std::stoul(expression.substr(position, digits - position)));
    if (index > 15U)
      return fail("register out of range");
    value = registers[index];
    position = digits;
  } else if (const auto number = ParseNumber(expression, &position)) {
    value = *number;
  } else {
    return fail("expected a register or a number");
  }
  while (position < expression.size()) {
    const char op = expression[position++];
    if (op == '*') {
      uint32_t loaded = 0;
      if (!read32(value, &loaded))
        return fail("unreadable dereference");
      value = loaded;
    } else if (op == '+' || op == '-') {
      const auto number = ParseNumber(expression, &position);
      if (!number)
        return fail("expected a number after +/-");
      value = op == '+' ? value + *number : value - *number;
    } else {
      return fail(std::string("unexpected '") + op + "'");
    }
  }
  return value;
}

A32ReadProbeSet A32ReadProbeSet::Parse(const std::string &text) {
  const auto document = nlohmann::json::parse(text, nullptr, false);
  if (!document.is_object() || !document.contains("probes") || !document["probes"].is_array())
    throw std::runtime_error("TRIAEVUM_A32_PROBE: expected an object with a probes array");
  A32ReadProbeSet set;
  if (document.contains("first"))
    set.mFirst = ParseAddressValue(document["first"], "first");
  if (document.contains("last"))
    set.mLast = ParseAddressValue(document["last"], "last");
  const uint32_t none[16]{};
  const A32ProbeRead32 noRead = [](uint32_t, uint32_t *) { return false; };
  for (const auto &entry : document["probes"]) {
    A32ReadProbe probe;
    probe.Pc = ParseAddressValue(entry.at("pc"), "pc");
    probe.Label = entry.value("label", std::string());
    probe.MaxHits = entry.value("max", 0U);
    for (const auto &read : entry.value("reads", nlohmann::json::array())) {
      A32ReadProbeRead r;
      r.Address = read.at("addr").get<std::string>();
      r.Label = read.value("label", r.Address);
      r.Width = read.value("w", 4U);
      r.Count = read.value("n", 1U);
      if ((r.Width != 1U && r.Width != 2U && r.Width != 4U) || r.Count == 0U || r.Count > 4096U)
        throw std::runtime_error("TRIAEVUM_A32_PROBE: " + r.Label + ": w must be 1/2/4 and n 1..4096");
      // Syntax only: dereferences fail here, which is fine.
      std::string error;
      if (!EvaluateA32ProbeAddress(r.Address, none, noRead, &error) && error != "unreadable dereference")
        throw std::runtime_error("TRIAEVUM_A32_PROBE: " + r.Label + ": " + error);
      probe.Reads.push_back(std::move(r));
    }
    if (!set.mProbes.emplace(probe.Pc, std::move(probe)).second)
      throw std::runtime_error("TRIAEVUM_A32_PROBE: a pc is listed twice");
  }
  return set;
}

std::vector<uint32_t> A32ReadProbeSet::Pcs() const {
  std::vector<uint32_t> pcs;
  for (const auto &[pc, probe] : mProbes)
    pcs.push_back(pc);
  return pcs;
}

std::optional<std::string> A32ReadProbeSet::Observe(uint32_t pc, uint32_t hostFrame, const uint32_t (&registers)[16],
                                                    const A32ProbeRead32 &read32, const A32ProbeReadBytes &readBytes) {
  const auto found = mProbes.find(pc);
  if (found == mProbes.end() || hostFrame < mFirst || hostFrame > mLast)
    return std::nullopt;
  auto &probe = found->second;
  if (probe.MaxHits != 0U && probe.Hits >= probe.MaxHits)
    return std::nullopt;
  ++probe.Hits;
  nlohmann::json line = {{"f", hostFrame}, {"pc", pc}, {"label", probe.Label}, {"hit", probe.Hits}};
  line["r"] = nlohmann::json::array();
  for (uint32_t index = 0; index < 16U; ++index)
    line["r"].push_back(registers[index]);
  nlohmann::json reads = nlohmann::json::object();
  for (const auto &read : probe.Reads) {
    nlohmann::json value = {{"expr", read.Address}};
    std::string error;
    const auto address = EvaluateA32ProbeAddress(read.Address, registers, read32, &error);
    if (!address) {
      value["ok"] = false;
      value["error"] = error;
    } else {
      value["a"] = *address;
      value["w"] = read.Width;
      std::vector<uint8_t> bytes(static_cast<size_t>(read.Width) * read.Count);
      const bool ok = readBytes(*address, bytes.data(), static_cast<uint32_t>(bytes.size()));
      value["ok"] = ok;
      if (ok) {
        auto values = nlohmann::json::array();
        for (uint32_t element = 0; element < read.Count; ++element) {
          uint32_t v = 0;
          for (uint32_t b = 0; b < read.Width; ++b)
            v |= uint32_t{bytes[element * read.Width + b]} << (8U * b);
          values.push_back(v);
        }
        value["v"] = read.Count == 1U ? values[0] : values;
      }
    }
    reads[read.Label] = std::move(value);
  }
  line["reads"] = std::move(reads);
  return line.dump();
}

} // namespace Oot3dNativeGame
