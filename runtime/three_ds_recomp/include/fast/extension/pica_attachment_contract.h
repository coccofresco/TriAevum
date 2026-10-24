#pragma once

#include "fast/renderer3ds/pica_attachment_contract.h"

namespace Fast::Extension {

using Renderer3ds::PicaAttachmentFeatureRequests;
using Renderer3ds::PicaAttachmentIndex;
using Renderer3ds::PicaAttachmentRequirements;
using Renderer3ds::PicaAuxiliaryOutput;
using Renderer3ds::PicaAuxiliaryOutputFor;
using Renderer3ds::PicaColorAttachment;
using Renderer3ds::kAllPicaAuxiliaryOutputs;
using Renderer3ds::kExtendedPicaAuxiliaryOutputs;
using Renderer3ds::kPicaBaseInstrumentedColorAttachmentCount;
using Renderer3ds::kPicaColorAttachmentCount;
using Renderer3ds::operator|;
using Renderer3ds::operator|=;

} // namespace Fast::Extension
