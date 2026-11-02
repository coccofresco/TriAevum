// The title sessions this build contains (CMake TRIAEVUM_TITLES).
#include "triaevum_title_session.h"

#include <stdexcept>

namespace Oot3dNativeGame {

#if defined(TRIAEVUM_TITLE_MM3D)
std::unique_ptr<TitleSession> CreateMm3dTitleSession();
#endif

std::unique_ptr<TitleSession> CreateTitleSession(TitleAdapter adapter) {
  switch (adapter) {
  case TitleAdapter::Mm3d:
#if defined(TRIAEVUM_TITLE_MM3D)
    return CreateMm3dTitleSession();
#elif defined(__APPLE__)
    throw std::runtime_error("this build does not contain the requested title adapter (TRIAEVUM_TITLES)");
#else
    throw std::runtime_error("this build does not contain the Majora's Mask 3D adapter (TRIAEVUM_TITLES)");
#endif
  case TitleAdapter::Oot3dEur:
  case TitleAdapter::Generic:
    break;
  }
  return nullptr;
}

} // namespace Oot3dNativeGame
