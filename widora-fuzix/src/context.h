#include "z80emu.h"
#include <stdint.h>

#define NBANKS      4   // not necessarily all usable, depends on current split
#define CHUNK_BITS  12  // there's a pool of chunks for use in memory banks
#define CHUNK_SIZE  (1<<CHUNK_BITS)         // 4096 bytes per chunk
#define CHUNK_COUNT (1<<(16-CHUNK_BITS))    // 16 chunks needed for 64K
#define CHUNK_TOTAL (NBANKS * CHUNK_COUNT)  // 64 chunks can be mapped

typedef struct {
    Z80_STATE state;
    uint8_t   done;
    int       bank;
    uint8_t*  split;
    uint32_t  offset [CHUNK_TOTAL];
} Context;

extern uint8_t mainMem [];

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    uint8_t* ptr = mainMem + addr;
#if NBANKS > 1
    Context* ctx = (Context*) cp;
    if (ptr < ctx->split)
        ptr += ctx->offset[CHUNK_COUNT * ctx->bank + (addr >> CHUNK_BITS)];
#endif
    return ptr;
}

extern void systemCall (Context *ctx, int request, int pc);
