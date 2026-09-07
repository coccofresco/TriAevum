#include "triaevum/service_registry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace triaevum::module {

HostServiceRegistry::HostServiceRegistry(HostRuntimeCallbacksV1 callbacks)
    : mCallbacks(callbacks) {}

ServiceRegistrationResult
HostServiceRegistry::Register(std::uint32_t serviceId,
                              HostServiceHandlerV1 handler,
                              void *serviceContext) {
  if (mSealed) {
    return ServiceRegistrationResult::RegistrySealed;
  }
  if (serviceId == 0U || handler == nullptr) {
    return ServiceRegistrationResult::InvalidArgument;
  }
  const auto location =
      std::lower_bound(mBindings.begin(), mBindings.end(), serviceId,
                       [](const Binding &binding, std::uint32_t candidate) {
                         return binding.serviceId < candidate;
                       });
  if (location != mBindings.end() && location->serviceId == serviceId) {
    return ServiceRegistrationResult::DuplicateService;
  }
  mBindings.insert(location, {serviceId, handler, serviceContext});
  return ServiceRegistrationResult::Registered;
}

bool HostServiceRegistry::Has(std::uint32_t serviceId) const {
  const auto location =
      std::lower_bound(mBindings.begin(), mBindings.end(), serviceId,
                       [](const Binding &binding, std::uint32_t candidate) {
                         return binding.serviceId < candidate;
                       });
  return location != mBindings.end() && location->serviceId == serviceId;
}

bool HostServiceRegistry::IsSealed() const { return mSealed; }

TriAevumHostApiV1 HostServiceRegistry::SealAndCreateHostApi() {
  mSealed = true;
  return {
      sizeof(TriAevumHostApiV1), TRIAEVUM_RUNTIME_ABI_V1, this,
      LogThunk,                     MonotonicTimeThunk,       InvokeThunk,
  };
}

void TRIAEVUM_ABI_CALL
HostServiceRegistry::LogThunk(void *hostContext, TriAevumLogLevelV1 level,
                              const char *message, std::size_t messageSize) {
  auto *registry = static_cast<HostServiceRegistry *>(hostContext);
  if (registry != nullptr && registry->mCallbacks.log != nullptr) {
    registry->mCallbacks.log(registry->mCallbacks.context, level, message,
                             messageSize);
  }
}

std::uint64_t TRIAEVUM_ABI_CALL
HostServiceRegistry::MonotonicTimeThunk(void *hostContext) {
  auto *registry = static_cast<HostServiceRegistry *>(hostContext);
  if (registry == nullptr || registry->mCallbacks.monotonicTimeNs == nullptr) {
    return 0U;
  }
  return registry->mCallbacks.monotonicTimeNs(registry->mCallbacks.context);
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL HostServiceRegistry::InvokeThunk(
    void *hostContext, std::uint32_t service, std::uint32_t operation,
    TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
    std::size_t *responseSize) {
  auto *registry = static_cast<HostServiceRegistry *>(hostContext);
  if (registry == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return registry->Invoke(service, operation, request, response, responseSize);
}

TriAevumModuleStatusV1 HostServiceRegistry::Invoke(
    std::uint32_t service, std::uint32_t operation,
    TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
    std::size_t *responseSize) {
  if (!mSealed || service == 0U || operation == 0U || responseSize == nullptr ||
      (request.size != 0U && request.data == nullptr) ||
      (response.size != 0U && response.data == nullptr)) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  *responseSize = 0U;
  const auto location =
      std::lower_bound(mBindings.begin(), mBindings.end(), service,
                       [](const Binding &binding, std::uint32_t candidate) {
                         return binding.serviceId < candidate;
                       });
  if (location == mBindings.end() || location->serviceId != service) {
    return TRIAEVUM_MODULE_SERVICE_NOT_FOUND_V1;
  }
  const TriAevumModuleStatusV1 status = location->handler(
      location->context, operation, request, response, responseSize);
  if (status == TRIAEVUM_MODULE_OK_V1 && *responseSize > response.size) {
    *responseSize = 0U;
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  return status;
}

} // namespace triaevum::module
