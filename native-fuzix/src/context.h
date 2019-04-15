#include "z80emu.h"
#include <stdint.h>

#define NBANKS  16  // not necessarily all usable, depends on current split

typedef struct {
    Z80_STATE state;
    uint8_t   done;
    int       bank;
    uint8_t*  split;
    uint32_t  offset [NBANKS];
} Context;

extern uint8_t mainMem [1<<16];

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    uint8_t* ptr = mainMem + addr;
#if NBANKS > 1
    Context* ctx = (Context*) cp;
    if (ptr < ctx->split)
        ptr += ctx->offset[ctx->bank];
#endif
    return ptr;
}

extern void systemCall (Context *ctx, int request, int pc);
