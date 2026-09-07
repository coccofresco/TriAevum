#pragma once

#include "fast/renderer3ds/pica_render_backend.h"
#include "oot3d/renderer/pica_shader_hooks.h"
#include "oot3d/renderer/pica_shader_source_identity.h"

namespace Oot3d::Renderer {

using ::Fast::Renderer3ds::NativeBlendEquation;
using ::Fast::Renderer3ds::NativeBlendFactor;
using ::Fast::Renderer3ds::NativeBlendState;
using ::Fast::Renderer3ds::NativeCullMode;
using ::Fast::Renderer3ds::NativeSamplerState;
using ::Fast::Renderer3ds::NativeTextureFilter;
using ::Fast::Renderer3ds::NativeTextureWrap;
using ::Fast::Renderer3ds::PicaCompareFunction;
using ::Fast::Renderer3ds::PicaLogicOperation;
using ::Fast::Renderer3ds::PicaStencilAction;
using ::Fast::Renderer3ds::PicaTopology;
using ::Fast::Renderer3ds::PicaVertexFormat;

using ::Fast::Renderer3ds::kPicaLightingLutPackedEntryCount;
using ::Fast::Renderer3ds::PicaLightingLutView;
using ::Fast::Renderer3ds::PicaStencilState;
using ::Fast::Renderer3ds::PicaTextureView;
using ::Fast::Renderer3ds::PicaVertexAttributeView;
using ::Fast::Renderer3ds::PicaVertexBindingView;

using ::Fast::Renderer3ds::kPicaFragmentFeatureLegacySchemaVersion;
using ::Fast::Renderer3ds::kPicaFragmentFeatureSchemaVersion;
using ::Fast::Renderer3ds::kPicaFragmentLightCount;
using ::Fast::Renderer3ds::kPicaFragmentLightingLayoutSchemaVersion;
using ::Fast::Renderer3ds::kPicaFragmentLightingLutSamplerCount;
using ::Fast::Renderer3ds::PicaFragmentFeatureView;
using ::Fast::Renderer3ds::PicaFragmentLightLayout;
using ::Fast::Renderer3ds::PicaFragmentLightingBumpMode;
using ::Fast::Renderer3ds::PicaFragmentLightingLayout;
using ::Fast::Renderer3ds::PicaFragmentLightingLutInput;
using ::Fast::Renderer3ds::PicaFragmentLightingLutSamplerLayout;

using ::Fast::Renderer3ds::kPicaCompositionSequenceSchemaVersion;
using ::Fast::Renderer3ds::PicaCompositionAttribution;
using ::Fast::Renderer3ds::PicaCompositionDomain;
using ::Fast::Renderer3ds::PicaCompositionDrawReference;
using ::Fast::Renderer3ds::PicaCompositionLayer;
using ::Fast::Renderer3ds::PicaCompositionProvenance;
using ::Fast::Renderer3ds::PicaCompositionSequenceView;
using ::Fast::Renderer3ds::PicaCompositionTargetReference;

using ::Fast::Renderer3ds::PicaDisplayImageColorSnapshot;
using ::Fast::Renderer3ds::PicaDisplayTransferView;
using ::Fast::Renderer3ds::PicaDrawView;
using ::Fast::Renderer3ds::PicaMemoryFillView;
using ::Fast::Renderer3ds::PicaPresentationMode;
using ::Fast::Renderer3ds::PicaPresentationStateSnapshot;
using ::Fast::Renderer3ds::PicaRenderBackend;
using ::Fast::Renderer3ds::PicaRenderTargetColorSnapshot;
using ::Fast::Renderer3ds::PicaTextureCacheEntrySnapshot;
using ::Fast::Renderer3ds::PicaTextureSnapshotFormat;

} // namespace Oot3d::Renderer
