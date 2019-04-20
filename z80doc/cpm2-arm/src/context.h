#include "z80emu.h"
#include <stdint.h>

#define CCMEM ((uint8_t*) 0x10000000)  // available: 0x10000000..0x1000FFFF

typedef struct {
    Z80_STATE state;
    uint8_t   done;
} Context;

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    return CCMEM + addr;
}

extern void systemCall (Context *ctx, int request);
