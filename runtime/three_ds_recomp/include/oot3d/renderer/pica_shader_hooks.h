#pragma once

#include "fast/renderer3ds/pica_shader_hooks.h"

namespace Oot3d::Renderer {

using ::Fast::Renderer3ds::kPicaFragmentOutputContractSchemaVersion;
using ::Fast::Renderer3ds::kPicaShaderHookSchemaVersion;
using ::Fast::Renderer3ds::PicaFragmentDepthOutput;
using ::Fast::Renderer3ds::PicaFragmentOutputContract;
using ::Fast::Renderer3ds::PicaShaderHook;
using ::Fast::Renderer3ds::PicaShaderHookLayout;
using ::Fast::Renderer3ds::PicaShaderSemantic;
using ::Fast::Renderer3ds::PicaTemporalVertexProgramView;
using ::Fast::Renderer3ds::PicaTextureCoordinateOperation;
using ::Fast::Renderer3ds::PicaTextureSampleLayout;
using ::Fast::Renderer3ds::PicaVertexShaderHook;
using ::Fast::Renderer3ds::PicaVertexShaderHookLayout;
using ::Fast::Renderer3ds::PicaVertexShaderSemantic;
using ::Fast::Renderer3ds::PicaVertexSkeletonLayout;
using ::Fast::Renderer3ds::PicaVertexSkeletonOperation;
using ::Fast::Renderer3ds::PicaVertexTextureCoordinateLayout;
using ::Fast::Renderer3ds::PicaVertexTextureCoordinateOperation;
using ::Fast::Renderer3ds::PicaVertexTextureCoordinateSourceLayout;
using ::Fast::Renderer3ds::PicaVertexTransformLayout;
using ::Fast::Renderer3ds::PicaVertexTransformOperation;
using ::Fast::Renderer3ds::operator|;
using ::Fast::Renderer3ds::operator|=;

} // namespace Oot3d::Renderer
