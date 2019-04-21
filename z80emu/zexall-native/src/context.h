#include "z80emu.h"
#include <stdint.h>

typedef struct {
    Z80_STATE state;
    uint8_t   done;
} Context;

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    static uint8_t mem [1<<16];
    return mem + addr;
}

extern void systemCall (Context *ctx, int request, uint16_t pc);
