#include <string.h>

#include "syscalls.h"
#include "context.h"
#include "z80emu.h"

const uint8_t rom [] = {
#include "zexall.h"
};

Context context;

void putc (char c) { write(1, &c, 1); }

void systemCall (Context *ctx, int request, uint16_t pc) {
    Z80_STATE* s = &ctx->state;

    // emulate CP/M bdos calls from 0x0005, register C functions 2 and 9
    switch (s->registers.byte[Z80_C]) {

        case 2: // output the character in E
            putc(s->registers.byte[Z80_E]);
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = s->registers.word[Z80_DE]; *mapMem(ctx, i) != '$'; ++i)
                putc(*mapMem(ctx, i));
            break;
    }
}

int main() {
    uint8_t* mem = CCMEM;
    memcpy(mem + 0x100, rom, sizeof rom);

    // Patch the memory of the program. Reset at 0x0000 is trapped by an
    // OUT which will stop emulation. CP/M bdos call 5 is trapped by an IN.
    // See Z80_INPUT_BYTE() and Z80_OUTPUT_BYTE() definitions in z80user.h.

    mem[0] = 0xd3;       // OUT N, A
    mem[1] = 0x00;

    mem[5] = 0xdb;       // IN A, N
    mem[6] = 0x00;
    mem[7] = 0xc9;       // RET

    // start emulating

    Z80Reset(&context.state);
    context.state.pc = 0x100;
    context.done = 0;

    do
        Z80Emulate(&context.state, 2000000, &context);
    while (!context.done);

    while (1) {}
}
