// SingleScreen 3.0 zelda_link_ext.zar image: a synthetic ZAR v1 archive
// exercises 3.0's selection rules (0x005D8BA8).
#include "oot3d_top_screen_link_ext.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace Oot3dNativeGame;

namespace {

int gFailures = 0;

void Check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++gFailures;
  }
}

void Put32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
  if (bytes.size() < offset + 4U) bytes.resize(offset + 4U);
  std::memcpy(bytes.data() + offset, &value, 4U);
}

std::uint32_t Get32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, 4U);
  return value;
}

std::size_t PutText(std::vector<std::uint8_t> &bytes, const std::string &text) {
  const std::size_t offset = bytes.size();
  bytes.insert(bytes.end(), text.begin(), text.end());
  bytes.push_back(0U);
  while (bytes.size() % 4U != 0U) bytes.push_back(0U);
  return offset;
}

// files: name and CSAB payload size (payload starts with "csab").
std::vector<std::uint8_t> MakeArchive(const std::vector<std::pair<std::string, std::uint32_t>> &files,
                                      bool badMagicFor = false) {
  std::vector<std::uint8_t> bytes(0x40U, 0U);
  Put32(bytes, 0U, 0x0152415AU);
  bytes[8] = 2U; // two types: an empty one, then csab
  const std::size_t types = 0x20U;
  Put32(bytes, 0x0CU, static_cast<std::uint32_t>(types));
  std::vector<std::size_t> names;
  for (const auto &file : files) names.push_back(PutText(bytes, file.first));
  const std::size_t csabName = PutText(bytes, "csab");
  const std::size_t unknownName = PutText(bytes, "unknown");
  const std::size_t list = bytes.size();
  for (std::size_t i = 0; i < files.size(); ++i) Put32(bytes, list + i * 4U, static_cast<std::uint32_t>(i));
  const std::size_t meta = bytes.size();
  for (std::size_t i = 0; i < files.size(); ++i) {
    Put32(bytes, meta + i * 8U, files[i].second);
    Put32(bytes, meta + i * 8U + 4U, static_cast<std::uint32_t>(names[i]));
  }
  const std::size_t data = bytes.size();
  bytes.resize(data + files.size() * 4U);
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::size_t at = bytes.size();
    Put32(bytes, data + i * 4U, static_cast<std::uint32_t>(at));
    bytes.resize(at + ((files[i].second + 3U) & ~3U), static_cast<std::uint8_t>(0xA0U + i));
    std::memcpy(bytes.data() + at, badMagicFor && i == 0 ? "xxxx" : "csab", 4U);
  }
  Put32(bytes, 0x10U, static_cast<std::uint32_t>(meta));
  Put32(bytes, 0x14U, static_cast<std::uint32_t>(data));
  Put32(bytes, types + 0U, 0U);
  Put32(bytes, types + 8U, static_cast<std::uint32_t>(unknownName));
  Put32(bytes, types + 0x10U, static_cast<std::uint32_t>(files.size()));
  Put32(bytes, types + 0x14U, static_cast<std::uint32_t>(list));
  Put32(bytes, types + 0x18U, static_cast<std::uint32_t>(csabName));
  return bytes;
}

} // namespace

int main() {
  constexpr std::uint32_t kBase = 0x06000000U;
  {
    // nml_dash (1) adult and child; ft_dash (6) adult only; the unported
    // N64 landing roll and an unknown name are ignored.
    const auto archive = MakeArchive({{"adult/nml_dash.csab", 0x40U},
                                      {"child/nml_dash.csab", 0x22U},
                                      {"adult/ft_dash.csab", 0x30U},
                                      {"adult/nml_landing_roll.csab", 0x30U},
                                      {"adult/not_in_table.csab", 0x10U}});
    TopScreenLinkExtImage image;
    std::string error;
    Check(BuildTopScreenLinkExtImage(archive, kBase, &image, &error), "valid archive builds");
    Check(image.Bytes.size() % 0x1000U == 0U, "image is page sized");
    Check(Get32(image.Bytes, 0U) == TopScreenLinkExtImage::kMagic, "image magic");
    Check(image.Present[0][1] && image.Present[1][1], "nml_dash for both ages");
    Check(image.Present[0][6] && image.Present[1][6], "child reuses the adult ft_dash");
    Check(!image.Present[0][0], "absent animation stays empty");
    const auto slot = [&](std::uint32_t age, std::size_t i) {
      return Get32(image.Bytes, TopScreenLinkExtImage::kSlots + (age * 16U + i) * 4U);
    };
    Check(slot(1, 6) == slot(0, 6), "shared slot points at the adult bytes");
    Check(slot(1, 1) != slot(0, 1), "child keeps its own nml_dash");
    for (const auto [age, i] : {std::pair{0U, std::size_t{1}}, std::pair{1U, std::size_t{1}},
                                std::pair{0U, std::size_t{6}}}) {
      const std::uint32_t address = slot(age, i);
      Check(address >= kBase + TopScreenLinkExtImage::kData && address % 16U == 0U,
            "slot points into the data area");
      Check(std::memcmp(image.Bytes.data() + (address - kBase), "csab", 4U) == 0,
            "slot bytes start with csab");
    }
    Check(TopScreenLinkExtImage::SlotAddress(kBase, 1U, 6U) ==
              kBase + TopScreenLinkExtImage::kSlots + (16U + 6U) * 4U,
          "slot address layout");
  }
  {
    // A CSAB without its magic is skipped, as 3.0 skips it.
    const auto archive = MakeArchive({{"adult/nml_dash.csab", 0x40U}}, true);
    TopScreenLinkExtImage image;
    std::string error;
    Check(!BuildTopScreenLinkExtImage(archive, kBase, &image, &error) && !error.empty(),
          "archive without a usable animation is rejected");
  }
  {
    std::vector<std::uint8_t> notZar(64U, 0U);
    TopScreenLinkExtImage image;
    std::string error;
    Check(!BuildTopScreenLinkExtImage(notZar, kBase, &image, &error), "non-ZAR is rejected");
  }
  if (gFailures != 0) return EXIT_FAILURE;
  std::puts("SingleScreen 3.0 Link animation image tests passed");
  return EXIT_SUCCESS;
}
