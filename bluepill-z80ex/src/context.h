#include "z80emu.h"
#include <stdint.h>

typedef struct {
    Z80_STATE state;
    uint8_t   mem [1<<14];  // size should be a power of two
    uint8_t   done;
} Context;

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    Context* ctx = (Context*) cp;
    return ctx->mem + (addr % sizeof ctx->mem);
}

extern void SystemCall (Context *ctx, int request);
