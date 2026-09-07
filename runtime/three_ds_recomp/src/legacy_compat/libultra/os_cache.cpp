#include "three_ds_recomp/legacy/runtime_compat.h"

extern "C" {

void osWritebackDCacheAll() {
}

void osInvalICache(void* p, int32_t x) {
}

void osWritebackDCache(void* p, int32_t x) {
}

void osInvalDCache(void* p, int32_t l) {
}
}