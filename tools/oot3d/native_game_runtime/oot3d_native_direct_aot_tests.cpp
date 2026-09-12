// Exercises the product title-plugin loader (ConfigureTitlePlugin + the ABI
// query/validation in oot3d_native_direct_aot.cpp) against a real plugin
// built by validate_whole_aot_toolchain.py. The synthetic plugin exposes one
// entry point at 0x1000; a stub would report no program at all.
//
// usage: oot3d_native_direct_aot_tests <triaevum_title_aot.(dll|so)>

#include "oot3d_native_direct_aot.h"
#include "triaevum_title_plugin_loader.h"

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace {

int Fail(const char* message) {
  std::fprintf(stderr, "direct-aot loader test failed: %s\n", message);
  return 1;
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2 && (argc != 3 || std::string_view(argv[2]) != "--expect-empty")) return 2;
  using namespace Oot3dNativeGame;

  try {
    ConfigureTitlePlugin(std::filesystem::path(argv[1]) / "does-not-exist");
    return Fail("missing plugin path was accepted");
  } catch (const std::runtime_error&) {
  }

  ConfigureTitlePlugin(argv[1]);
  if (argc == 3) {
    if (Oot3dWholeAotPluginV2Available() || !Oot3dWholeAotEntryPoints().empty())
      return Fail("empty stub was accepted as a game title");
    std::puts("{\"empty_stub_rejected\":true}");
    return 0;
  }
  if (!Oot3dWholeAotPluginV2Available()) return Fail("plugin V2 table was not accepted");
  const auto entries = Oot3dWholeAotEntryPoints();
  if (entries.size() != 1 || entries[0] != 0x1000) return Fail("unexpected entry points");
  if (Oot3dWholeAotPluginExecutionActive()) return Fail("execution active before any call");

  try {
    ConfigureTitlePlugin(argv[1]);
    return Fail("re-selection after query was accepted");
  } catch (const std::runtime_error&) {
  }
  std::puts("{\"selected\":true,\"query\":true,\"entry_points\":true,\"sealed\":true}");
  return 0;
}
