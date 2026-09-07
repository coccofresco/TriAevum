#include <stdint.h>

/* Target 0x00303B14 uses the game ABI: destination, size, byte value. */
void* oot3d_boot_contract_memset_00303b14(
    void* destination, uint32_t size, uint8_t value) {
    volatile uint8_t* bytes = (volatile uint8_t*)destination;
    uint32_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
    return (void*)((uintptr_t)destination + size);
}

