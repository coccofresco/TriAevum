#include "oot3d/memory.h"

#include <stdint.h>

typedef struct Oot3dCopyBlock32 { u32 words[8]; } Oot3dCopyBlock32;
typedef struct Oot3dCopyBlock16 { u32 words[4]; } Oot3dCopyBlock16;
typedef struct Oot3dCopyBlock8 { u32 words[2]; } Oot3dCopyBlock8;

void Lib_MemSet(void* destination, size_t size, u8 value) {
    u8* cursor = destination;
    if ((size & 1) != 0) { *cursor++ = value; size--; }
    while (size != 0) {
        cursor[0] = value; cursor[1] = value; cursor += 2; size -= 2;
    }
}

void Oot3d_ClearTenBytes(void* destination) {
    u8* bytes = destination;
    *(u32*)(bytes + 0) = 0;
    *(u32*)(bytes + 4) = 0;
    *(u16*)(bytes + 8) = 0;
}

void* oot3d_memclear(void* destination, size_t size) {
    u8* cursor = destination;
    while (size >= 32) {
        u32* words = (u32*)cursor;
        words[0] = 0; words[1] = 0; words[2] = 0; words[3] = 0;
        words[4] = 0; words[5] = 0; words[6] = 0; words[7] = 0;
        cursor += 32; size -= 32;
    }
    if ((size & 16) != 0) {
        u32* words = (u32*)cursor;
        words[0] = 0; words[1] = 0; words[2] = 0; words[3] = 0;
        cursor += 16;
    }
    if ((size & 8) != 0) {
        u32* words = (u32*)cursor; words[0] = 0; words[1] = 0; cursor += 8;
    }
    if ((size & 4) != 0) { *(u32*)cursor = 0; cursor += 4; }
    if ((size & 2) != 0) { *(u16*)cursor = 0; cursor += 2; }
    if ((size & 1) != 0) { *cursor++ = 0; }
    return cursor;
}

void* oot3d_memclear_counted_8byte_entries(Oot3dCounted8ByteBuffer* buffer) {
    void* entries = (void*)(uintptr_t)buffer->entries;
    return oot3d_memclear(entries, (size_t)buffer->count * 8);
}

void* oot3d_memcpy_aligned_end(void* destination, const void* source,
                               size_t size) {
    u8* destinationBytes = destination;
    const u8* sourceBytes = source;
    while (size >= 32) {
        *(Oot3dCopyBlock32*)destinationBytes = *(const Oot3dCopyBlock32*)sourceBytes;
        destinationBytes += 32; sourceBytes += 32; size -= 32;
    }
    if ((size & 16) != 0) {
        *(Oot3dCopyBlock16*)destinationBytes = *(const Oot3dCopyBlock16*)sourceBytes;
        destinationBytes += 16; sourceBytes += 16;
    }
    if ((size & 8) != 0) {
        *(Oot3dCopyBlock8*)destinationBytes = *(const Oot3dCopyBlock8*)sourceBytes;
        destinationBytes += 8; sourceBytes += 8;
    }
    if ((size & 4) != 0) {
        *(u32*)destinationBytes = *(const u32*)sourceBytes;
        destinationBytes += 4; sourceBytes += 4;
    }
    if ((size & 2) != 0) {
        *(u16*)destinationBytes = *(const u16*)sourceBytes;
        destinationBytes += 2; sourceBytes += 2;
    }
    if ((size & 1) != 0) { *destinationBytes++ = *sourceBytes; }
    return destinationBytes;
}

void* oot3d_memcpy_end(void* destination, const void* source, size_t size) {
    u8* destinationBytes = destination;
    const u8* sourceBytes = source;
    while (size != 0 && ((uintptr_t)destinationBytes & 3u) != 0) {
        *destinationBytes++ = *sourceBytes++; size--;
    }
    if (((uintptr_t)sourceBytes & 3u) == 0) {
        return oot3d_memcpy_aligned_end(destinationBytes, sourceBytes, size);
    }
    while (size >= 8) {
        destinationBytes[0] = sourceBytes[0]; destinationBytes[1] = sourceBytes[1];
        destinationBytes[2] = sourceBytes[2]; destinationBytes[3] = sourceBytes[3];
        destinationBytes[4] = sourceBytes[4]; destinationBytes[5] = sourceBytes[5];
        destinationBytes[6] = sourceBytes[6]; destinationBytes[7] = sourceBytes[7];
        destinationBytes += 8; sourceBytes += 8; size -= 8;
    }
    if (size >= 4) {
        destinationBytes[0] = sourceBytes[0]; destinationBytes[1] = sourceBytes[1];
        destinationBytes[2] = sourceBytes[2]; destinationBytes[3] = sourceBytes[3];
        destinationBytes += 4; sourceBytes += 4; size -= 4;
    }
    while (size != 0) { *destinationBytes++ = *sourceBytes++; size--; }
    return destinationBytes;
}
