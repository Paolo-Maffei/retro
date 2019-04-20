#include "z80emu.h"
#include <stdint.h>

#define MAINMEM ((uint8_t*) 0x10000000) // CCM: special 64K area in F407
#define NBANKS  16  // not necessarily all usable, depends on current split

typedef struct {
    Z80_STATE	state;
    uint8_t	done;
    int         bank;
    uint8_t*    split;
    uint32_t    offset [NBANKS];
} Context;

// defined as static in this header, so that it will be inlined where possible
static uint8_t* mapMem (void* cp, uint16_t addr) {
    uint8_t* ptr = MAINMEM + addr;
#if NBANKS > 1
    Context* ctx = (Context*) cp;
    if (ptr < ctx->split)
        ptr += ctx->offset[ctx->bank];
#endif
    return ptr;
}

extern void systemCall (Context *ctx, int request, uint16_t pc);
