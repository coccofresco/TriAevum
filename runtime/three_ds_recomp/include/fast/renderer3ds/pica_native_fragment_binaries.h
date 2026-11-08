#pragma once
// Generated from maintained PICA equations, not captured game shaders.
// Vulkan 1.1, shaderc performance optimization; canonical and NRI interfaces.
// Regenerate pica_native_fragment.spvpack with
// tools/renderer/tev_program/build_fragment_artifacts.
#include "pica_fragment_artifact.h"

namespace Fast::Renderer3ds {
std::span<const PicaFragmentArtifact> NativeFragmentArtifacts();
// A title's own built-in fragment-family pack (MM3D's
// mm3d_native_fragment.spvpack), installed by the host before the renderer
// starts. Appended after the built-in families; nothing installed (OoT3D)
// keeps NativeFragmentArtifacts exactly as before. Returns false (and
// installs nothing) once the artifacts have been read.
bool InstallTitleFragmentPack(std::span<const unsigned char> pack);
} // namespace Fast::Renderer3ds
