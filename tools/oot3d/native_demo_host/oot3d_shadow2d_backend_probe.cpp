#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "fast/interpreter.h"
#include "ship/Context.h"

#include "oot3d_demo_host_context.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_types.h"

namespace {

struct ProbeArgs {
  std::filesystem::path ResourceRoot;
  std::filesystem::path OutputPath;
  std::filesystem::path ScreenshotPath;
  std::string Renderer;
  uint32_t Width = 320;
  uint32_t Height = 240;
  uint32_t ShadowMapSize = 64;
};

struct ContextGuard {
  bool Active = false;

  ~ContextGuard() {
    if (Active) {
      Ship::Context::DestroyInstance();
    }
  }
};

uint32_t ParseU32(const char *value, const char *name) {
  const unsigned long parsed = std::stoul(value);
  if (parsed == 0 || parsed > UINT32_MAX) {
    throw std::runtime_error(std::string(name) + " must be in [1, UINT32_MAX]");
  }
  return static_cast<uint32_t>(parsed);
}

ProbeArgs ParseArgs(int argc, char **argv) {
  ProbeArgs args;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--resource-root" && index + 1 < argc) {
      args.ResourceRoot = argv[++index];
    } else if (arg == "--output" && index + 1 < argc) {
      args.OutputPath = argv[++index];
    } else if (arg == "--screenshot" && index + 1 < argc) {
      args.ScreenshotPath = argv[++index];
    } else if (arg == "--renderer" && index + 1 < argc) {
      args.Renderer = argv[++index];
    } else if (arg == "--width" && index + 1 < argc) {
      args.Width = ParseU32(argv[++index], "width");
    } else if (arg == "--height" && index + 1 < argc) {
      args.Height = ParseU32(argv[++index], "height");
    } else if (arg == "--shadow-map-size" && index + 1 < argc) {
      args.ShadowMapSize = ParseU32(argv[++index], "shadow map size");
    } else {
      throw std::runtime_error("unknown or incomplete argument: " + arg);
    }
  }
  if (args.ResourceRoot.empty() || args.OutputPath.empty() ||
      (args.Renderer != "opengl" && args.Renderer != "vulkan")) {
    throw std::runtime_error(
        "usage: oot3d_shadow2d_backend_probe --renderer <opengl|vulkan> "
        "--resource-root <three_ds_recomp/legacy/src/fast> --output <probe.json> "
        "[--screenshot <probe.bmp>] [--width <pixels>] [--height <pixels>] "
        "[--shadow-map-size <pixels>]");
  }
  return args;
}

uint64_t PackShaderInput(uint32_t value, uint32_t cycle, uint32_t component,
                         uint32_t term) {
  return static_cast<uint64_t>(value)
         << (cycle * 32 + component * 16 + term * 4);
}

uint64_t BuildVertexColorShaderId0(bool alpha) {
  return PackShaderInput(SHADER_INPUT_1, 0, 0, 3) |
         (alpha ? PackShaderInput(SHADER_INPUT_1, 0, 1, 3) : 0);
}

void AppendReceiverVertex(std::vector<float> &vertices, float x, float y,
                          float u, float v, bool alpha) {
  const std::array<float, 7> positionAndColor = {
      x, y, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  vertices.insert(vertices.end(), positionAndColor.begin(),
                  positionAndColor.end());
  if (alpha) {
    vertices.push_back(1.0f);
  }
  const std::array<float, 3> shadowCoordinate = {u, v, 0.75f};
  vertices.insert(vertices.end(), shadowCoordinate.begin(),
                  shadowCoordinate.end());
}

std::vector<float> BuildReceiverVertices(bool alpha) {
  std::vector<float> vertices;
  vertices.reserve(6 * (alpha ? 11 : 10));
  AppendReceiverVertex(vertices, -1.0f, -1.0f, 0.0f, 0.0f, alpha);
  AppendReceiverVertex(vertices, 1.0f, -1.0f, 1.0f, 0.0f, alpha);
  AppendReceiverVertex(vertices, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
  AppendReceiverVertex(vertices, -1.0f, -1.0f, 0.0f, 0.0f, alpha);
  AppendReceiverVertex(vertices, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
  AppendReceiverVertex(vertices, -1.0f, 1.0f, 0.0f, 1.0f, alpha);
  return vertices;
}

double Rgba5551Luminance(uint16_t pixel) {
  const double red = static_cast<double>((pixel >> 11) & 0x1f);
  const double green = static_cast<double>((pixel >> 6) & 0x1f);
  const double blue = static_cast<double>((pixel >> 1) & 0x1f);
  return (red + green + blue) / (3.0 * 31.0);
}

double MeanRegionLuminance(const std::vector<uint16_t> &framebuffer,
                           uint32_t width, uint32_t height, uint32_t x0,
                           uint32_t y0, uint32_t x1, uint32_t y1) {
  x0 = std::min(x0, width);
  x1 = std::min(x1, width);
  y0 = std::min(y0, height);
  y1 = std::min(y1, height);
  if (x0 >= x1 || y0 >= y1) {
    return 0.0;
  }
  double sum = 0.0;
  uint64_t count = 0;
  for (uint32_t y = y0; y < y1; ++y) {
    for (uint32_t x = x0; x < x1; ++x) {
      sum += Rgba5551Luminance(framebuffer[static_cast<size_t>(y) * width + x]);
      ++count;
    }
  }
  return sum / static_cast<double>(count);
}

double MeanCornerLuminance(const std::vector<uint16_t> &framebuffer,
                           uint32_t width, uint32_t height) {
  const uint32_t regionWidth = std::max<uint32_t>(1, width / 8);
  const uint32_t regionHeight = std::max<uint32_t>(1, height / 8);
  return (MeanRegionLuminance(framebuffer, width, height, 0, 0, regionWidth,
                              regionHeight) +
          MeanRegionLuminance(framebuffer, width, height, width - regionWidth,
                              0, width, regionHeight) +
          MeanRegionLuminance(framebuffer, width, height, 0,
                              height - regionHeight, regionWidth, height) +
          MeanRegionLuminance(framebuffer, width, height, width - regionWidth,
                              height - regionHeight, width, height)) /
         4.0;
}

nlohmann::json RunProbe(const ProbeArgs &probeArgs) {
  Args hostArgs;
  hostArgs.ApplicationName = "OOT3D Shadow2D Backend Probe";
  hostArgs.ApplicationId = "oot3d_shadow2d_backend_probe_" + probeArgs.Renderer;
  hostArgs.ConfigurationPath =
      (probeArgs.OutputPath.parent_path() /
       ("oot3d_shadow2d_backend_probe_" + probeArgs.Renderer + ".config.json"))
          .string();
  hostArgs.ResourceRoot = probeArgs.ResourceRoot;
  hostArgs.Width = probeArgs.Width;
  hostArgs.Height = probeArgs.Height;
  hostArgs.Renderer = probeArgs.Renderer;
  hostArgs.BackendId = probeArgs.Renderer == "vulkan"
                           ? Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN
                           : Fast::WindowBackend::FAST3D_SDL_OPENGL;

  ContextGuard contextGuard;
  InitContextForDemo(hostArgs);
  contextGuard.Active = true;
  auto &window = GetActiveFast3dWindowForDemo();
  auto &api = GetActiveRenderingApiForDemo(window);
  const uint32_t width = std::max<uint32_t>(1, window.GetWidth());
  const uint32_t height = std::max<uint32_t>(1, window.GetHeight());

  const bool supportsR32ui = api.SupportsOot3dShadow2dR32uiPipeline();
  if (!supportsR32ui) {
    throw std::runtime_error(
        "renderer does not support the OOT3D Shadow2D R32UI pipeline");
  }

  const int shadowFramebuffer = api.CreateFramebuffer();
  const bool targetCreated = api.UpdateFramebufferParametersWithColorFormat(
      shadowFramebuffer, probeArgs.ShadowMapSize, probeArgs.ShadowMapSize, 1,
      false, true, true, false, Fast::GfxFramebufferColorFormat::R32ui);
  if (!targetCreated) {
    throw std::runtime_error(
        "failed to create the OOT3D Shadow2D R32UI target");
  }

  const uint64_t shadowShaderMask =
      Fast::ShaderIdMask(Fast::SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D);
  const std::array<Fast::ShaderProgram *, 2> shaders = {
      api.CreateAndLoadNewShader(BuildVertexColorShaderId0(false),
                                 shadowShaderMask),
      api.CreateAndLoadNewShader(BuildVertexColorShaderId0(true),
                                 shadowShaderMask | SHADER_OPT(ALPHA)),
  };
  if (shaders[0] == nullptr || shaders[1] == nullptr) {
    throw std::runtime_error(
        "failed to create the OOT3D Shadow2D sample shader");
  }

  constexpr uint32_t kShadowClearValue = UINT32_MAX;
  std::array<float, 24> casterVertices = {
      -0.5f, -0.5f, 0.25f, 1.0f, 0.5f,  -0.5f, 0.25f, 1.0f,
      0.5f,  0.5f,  0.25f, 1.0f, -0.5f, -0.5f, 0.25f, 1.0f,
      0.5f,  0.5f,  0.25f, 1.0f, -0.5f, 0.5f,  0.25f, 1.0f,
  };
  constexpr std::array<float, 16> kIdentity = {
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
  };
  std::array<std::vector<float>, 2> receiverVertices = {
      BuildReceiverVertices(false),
      BuildReceiverVertices(true),
  };
  constexpr uint32_t kProbeFrameCount = 2;
  bool depthPassStarted = true;
  bool casterSubmitted = true;
  bool shadowBound = true;
  bool parametersApplied = true;
  bool transformApplied = true;
  std::vector<uint16_t> firstFramebuffer;
  std::vector<uint16_t> framebuffer(static_cast<size_t>(width) * height);
  for (uint32_t frameIndex = 0; frameIndex < kProbeFrameCount; ++frameIndex) {
    window.StartFrame();
    api.UpdateFramebufferParameters(0, width, height, 1, false, true, true,
                                    true);
    api.StartFrame();

    const bool frameDepthPassStarted = api.StartOot3dShadow2dDepthEncodePass(
        shadowFramebuffer, kShadowClearValue);
    depthPassStarted = depthPassStarted && frameDepthPassStarted;
    if (!frameDepthPassStarted) {
      throw std::runtime_error(
          "failed to start the OOT3D Shadow2D depth encode pass");
    }
    const bool frameCasterSubmitted =
        api.DrawOot3dShadow2dDepthEncodedTriangles(casterVertices.data(),
                                                   casterVertices.size(), 2);
    casterSubmitted = casterSubmitted && frameCasterSubmitted;
    api.EndOot3dShadow2dDepthEncodePass();
    if (!frameCasterSubmitted) {
      throw std::runtime_error("failed to submit the OOT3D Shadow2D caster");
    }

    api.StartDrawToFramebuffer(0, 1.0f);
    api.SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    api.ClearFramebuffer(true, true);
    api.SetViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    api.SetScissor(0, 0, static_cast<int>(width), static_cast<int>(height));
    api.SetDepthTestAndMask(false, false);
    api.SetZmodeDecal(false);
    api.SetUseAlpha(false);
    api.SetNativeCullMode(Fast::GfxNativeCullMode::KeepAll);
    api.LoadShader(shaders[frameIndex]);
    const bool frameShadowBound =
        api.BindOot3dShadow2dTexture(shadowFramebuffer, 6);
    const bool frameParametersApplied =
        api.SetOot3dShadow2dShaderParameters(0, true, false);
    const bool frameTransformApplied =
        api.SetOot3dNativeTransform(kIdentity.data());
    shadowBound = shadowBound && frameShadowBound;
    parametersApplied = parametersApplied && frameParametersApplied;
    transformApplied = transformApplied && frameTransformApplied;
    if (!frameShadowBound || !frameParametersApplied ||
        !frameTransformApplied) {
      throw std::runtime_error(
          "failed to configure the OOT3D Shadow2D sample pass");
    }

    api.DrawTriangles(receiverVertices[frameIndex].data(),
                      receiverVertices[frameIndex].size(), 2);
    api.ReadFramebufferToCPU(0, width, height, framebuffer.data());
    window.EndFrame();
    if (frameIndex == 0) {
      firstFramebuffer = framebuffer;
    }
  }

  const bool consecutiveFramesStable = firstFramebuffer == framebuffer;

  const uint32_t centerHalfWidth = std::max<uint32_t>(1, width / 20);
  const uint32_t centerHalfHeight = std::max<uint32_t>(1, height / 20);
  const double centerLuminance = MeanRegionLuminance(
      framebuffer, width, height, width / 2 - centerHalfWidth,
      height / 2 - centerHalfHeight, width / 2 + centerHalfWidth,
      height / 2 + centerHalfHeight);
  const double cornerLuminance =
      MeanCornerLuminance(framebuffer, width, height);
  const bool visualComparePassed = centerLuminance < 0.15 &&
                                   cornerLuminance > 0.85 &&
                                   consecutiveFramesStable;

  if (!probeArgs.ScreenshotPath.empty()) {
    WriteBmpFromRgba5551Framebuffer(probeArgs.ScreenshotPath, width, height,
                                    framebuffer);
  }

  return {
      {"format", "oot3d_shadow2d_backend_probe_v1"},
      {"renderer", probeArgs.Renderer},
      {"rendering_api", api.GetName()},
      {"width", width},
      {"height", height},
      {"frame_count", kProbeFrameCount},
      {"shader_variants", nlohmann::json::array({"rgb", "rgba"})},
      {"shadow_map_width", probeArgs.ShadowMapSize},
      {"shadow_map_height", probeArgs.ShadowMapSize},
      {"r32ui_pipeline_supported", supportsR32ui},
      {"r32ui_target_created", targetCreated},
      {"depth_encode_pass_started", depthPassStarted},
      {"depth_encoded_caster_submitted", casterSubmitted},
      {"shadow_texture_bound", shadowBound},
      {"shadow_shader_parameters_applied", parametersApplied},
      {"native_transform_applied", transformApplied},
      {"center_shadow_luminance", centerLuminance},
      {"corner_lit_luminance", cornerLuminance},
      {"consecutive_framebuffers_stable", consecutiveFramesStable},
      {"visual_sample_compare_passed", visualComparePassed},
      {"capture",
       {
           {"source", "fast3d_rendering_api_read_framebuffer_to_cpu"},
           {"pixel_format", "rgba5551"},
           {"depends_on_windows_foreground", false},
       }},
  };
}

} // namespace

int main(int argc, char **argv) {
  try {
    const ProbeArgs args = ParseArgs(argc, argv);
    auto result = RunProbe(args);
    const bool passed = result.at("visual_sample_compare_passed").get<bool>();
    WriteJsonFile(args.OutputPath, result);
    return passed ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "oot3d_shadow2d_backend_probe: " << error.what() << '\n';
    return 1;
  }
}
