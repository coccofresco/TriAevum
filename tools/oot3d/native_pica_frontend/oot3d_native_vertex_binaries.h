#pragma once
// Generated translated OOT3D title vertex SPIR-V. Not title-neutral code.
// Source: original SHBIN interfaces + maintained GLSL wrapper and translated body.
// Citra/Azahar translator donor: GPL-2.0-or-later; preserve donor notices.
// Regenerate oot3d_native_vertex.spvpack with
// tools/renderer/tev_program/build_vertex_artifacts (see the provenance JSON).
#include "fast/renderer3ds/pica_vertex_artifact.h"
#include "fast/renderer/pass_shader_artifact.h"

namespace Oot3dNativeGame {
std::span<const Fast::Renderer3ds::PicaVertexArtifact> TitleVertexArtifacts();
// Only publisher-provided programs; never prewarm OoT's family for another title.
std::span<const Fast::Renderer3ds::PicaVertexArtifact> AdditionalTitleVertexArtifacts();
std::span<const Fast::Renderer::PassShaderArtifact> TitlePassArtifacts();
// A title session's own built-in vertex family (MM3D's mm3d_native_vertex.spvpack),
// installed by the window session before the renderer starts, i.e. before the
// first use of the functions above. Appended after the embedded OoT3D programs
// (exact source identities keep titles apart), exactly as a publisher-provided
// pack, and returned by AdditionalTitleVertexArtifacts. OoT3D installs nothing.
// Returns false (and installs nothing) once the family has been read.
bool InstallTitleBuiltInVertexPack(std::span<const unsigned char> pack);
} // namespace Oot3dNativeGame
