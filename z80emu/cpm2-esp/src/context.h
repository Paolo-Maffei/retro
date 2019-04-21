#include "z80emu.h"
#include <stdint.h>

typedef struct {
    Z80_STATE state;
    uint8_t   done;
} Context;

extern uint8_t mainMem [];

inline uint8_t* mapMem (void* cp, uint16_t addr) {
    return mainMem + addr;
}

extern void systemCall (Context *ctx, int request, uint16_t pc);
