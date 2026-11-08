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
} // namespace Oot3dNativeGame
