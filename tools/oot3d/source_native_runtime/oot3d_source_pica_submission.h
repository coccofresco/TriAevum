#pragma once

#include "oot3d_native_pica_submission.h"
#include "oot3d_source_process_image.h"

namespace Oot3dSourceRuntime {

class SourcePicaSubmission {
  public:
    SourcePicaSubmission(GuestAddressSpace& memory,
                         const SourceProcessImageDescriptor& descriptor,
                         bool deferGpuBackedDisplayTransfers = true);

    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue& Queue() noexcept;
    const Oot3dNativeGame::Oot3dNativePicaSubmissionQueue& Queue() const noexcept;

  private:
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue mQueue;
};

} // namespace Oot3dSourceRuntime
