#include "z80emu.h"
#include <stdint.h>

typedef struct {
    Z80_STATE state;
    uint8_t   done;
} Context;

extern uint8_t mem [];

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    return mem + addr;
}

extern void systemCall (Context *ctx, int request, uint16_t pc);
