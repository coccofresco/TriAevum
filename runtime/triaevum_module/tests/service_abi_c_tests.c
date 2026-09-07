#include "triaevum/service_abi.h"

int main(void) {
  TriAevumPicaWriteRegistersRequestV1 request = {0};
  TriAevumInputReadStateResponseV1 input = {0};
  request.header.struct_size = (uint32_t)sizeof(request);
  request.header.schema_version = TRIAEVUM_SERVICE_SCHEMA_V1;
  input.header.struct_size = (uint32_t)sizeof(input);
  return sizeof(TriAevumServiceRequestHeaderV1) == 8U &&
                 sizeof(TriAevumPayloadRangeV1) == 8U &&
                 request.header.struct_size != 0U &&
                 input.header.struct_size != 0U
             ? 0
             : 1;
}
