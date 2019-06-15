#include "z80emu.h"
#include <stdint.h>

#define CCMEM ((uint8_t*) 0x10000000)  // available: 0x10000000..0x1000FFFF

inline uint8_t* mapMem (void* /*context*/, uint16_t addr) {
    return CCMEM + addr;
}

extern void systemCall (void *context, int request);
