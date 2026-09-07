#ifndef TRIAEVUM_SERVICE_REGISTRY_H
#define TRIAEVUM_SERVICE_REGISTRY_H

#include "triaevum/module_abi.h"

#include <cstdint>
#include <vector>

namespace triaevum::module {

using HostServiceHandlerV1 = TriAevumModuleStatusV1(TRIAEVUM_ABI_CALL *)(
    void *serviceContext, std::uint32_t operation,
    TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
    std::size_t *responseSize);

struct HostRuntimeCallbacksV1 {
  void *context = nullptr;
  TriAevumLogFnV1 log = nullptr;
  TriAevumMonotonicTimeFnV1 monotonicTimeNs = nullptr;
};

enum class ServiceRegistrationResult {
  Registered,
  InvalidArgument,
  DuplicateService,
  RegistrySealed,
};

class HostServiceRegistry final {
public:
  explicit HostServiceRegistry(HostRuntimeCallbacksV1 callbacks);

  HostServiceRegistry(const HostServiceRegistry &) = delete;
  HostServiceRegistry &operator=(const HostServiceRegistry &) = delete;

  ServiceRegistrationResult Register(std::uint32_t serviceId,
                                     HostServiceHandlerV1 handler,
                                     void *serviceContext);
  [[nodiscard]] bool Has(std::uint32_t serviceId) const;
  [[nodiscard]] bool IsSealed() const;

  TriAevumHostApiV1 SealAndCreateHostApi();

private:
  struct Binding {
    std::uint32_t serviceId = 0;
    HostServiceHandlerV1 handler = nullptr;
    void *context = nullptr;
  };

  static void TRIAEVUM_ABI_CALL LogThunk(void *hostContext,
                                         TriAevumLogLevelV1 level,
                                         const char *message,
                                         std::size_t messageSize);
  static std::uint64_t TRIAEVUM_ABI_CALL
  MonotonicTimeThunk(void *hostContext);
  static TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL InvokeThunk(
      void *hostContext, std::uint32_t service, std::uint32_t operation,
      TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
      std::size_t *responseSize);

  TriAevumModuleStatusV1 Invoke(std::uint32_t service,
                               std::uint32_t operation,
                               TriAevumReadOnlyBytesV1 request,
                               TriAevumMutableBytesV1 response,
                               std::size_t *responseSize);

  HostRuntimeCallbacksV1 mCallbacks;
  std::vector<Binding> mBindings;
  bool mSealed = false;
};

} // namespace triaevum::module

#endif
