#ifndef OOT3D_MEMORY_H
#define OOT3D_MEMORY_H

#include "oot3d/types.h"

typedef struct Oot3dCounted8ByteBuffer {
    u32 count;
    u8 pad_04[0x18];
    u32 entries;
} Oot3dCounted8ByteBuffer;

void* oot3d_memclear(void* destination, size_t size);
void* oot3d_memclear_counted_8byte_entries(Oot3dCounted8ByteBuffer* buffer);
void Lib_MemSet(void* destination, size_t size, u8 value);
void Oot3d_ClearTenBytes(void* destination);
void* oot3d_memcpy_aligned_end(void* destination, const void* source,
                               size_t size);
void* oot3d_memcpy_end(void* destination, const void* source, size_t size);

#endif
