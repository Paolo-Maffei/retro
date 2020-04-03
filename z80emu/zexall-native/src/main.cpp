#include <string.h>
#include <stdio.h>
#include <sys/time.h>

extern "C" {
#include "context.h"
#include "z80emu.h"
}

const uint8_t rom [] = {
#include "zexall.h"
};

uint8_t mem [1<<16];
Context context;

void systemCall (Context *ctx, int, uint16_t) {
    auto& regs = ctx->state.registers;

    // emulate CP/M bdos calls from 0x0005, register C functions 2 and 9
    switch (regs.byte[Z80_C]) {

        case 2: // output the character in E
            putchar(regs.byte[Z80_E]);
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = regs.word[Z80_DE]; *mapMem(ctx, i) != '$'; ++i)
                putchar(*mapMem(ctx, i));
            break;
    }
}

static uint32_t millis () {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main() {
    memcpy(mapMem(&context, 0x100), rom, sizeof rom);

    // Patch the memory of the program. Reset at 0x0000 is trapped by an
    // OUT which will stop emulation. CP/M bdos call 5 is trapped by an IN.
    // See Z80_INPUT_BYTE() and Z80_OUTPUT_BYTE() definitions in z80user.h.

    *mapMem(&context, 0) = 0xd3;       // OUT N, A
    *mapMem(&context, 1) = 0x00;

    *mapMem(&context, 5) = 0xdb;       // IN A, N
    *mapMem(&context, 6) = 0x00;
    *mapMem(&context, 7) = 0xc9;       // RET

    // start emulating

    Z80Reset(&context.state);
    context.state.pc = 0x100;
    context.done = 0;

    uint64_t cycles = 0;
    uint32_t start = millis();

    do
        cycles += Z80Emulate(&context.state, 1000000000, &context);
    while (!context.done);

    uint32_t t = millis() - start;
    printf("\nEmulating zexall took %.1f seconds: %llu cycles @ %.1f MHz\n",
            t/1000.0, cycles, cycles/(1000.0*t));

    // Mac Mini 2018 i7 @ 3.2 GHz:
    //  Emulating zexall took 48.8 seconds: 46734978649 cycles @ 957.2 MHz

    return 0;
}
