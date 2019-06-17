#include "z80emu.h"
#include <stdint.h>

#if NATIVE
extern uint8_t mainMem [];
#else
#define mainMem ((uint8_t*) 0x10000000)  // available: 0x10000000..0x1000FFFF
#endif

static uint8_t* mapMem (void* context, uint16_t addr) {
    return mainMem + addr;
}

extern void systemCall (void *context, int request);
