#include <jee.h>
#include <string.h>

extern "C" {
#include "context.h"
#include "z80emu.h"
}

const uint8_t rom [] = {
#include "zexall.h"
};

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinC<13> led;
Context context;

void systemCall (Context *ctx, int) {
    auto& regs = ctx->state.registers;

    // emulate CP/M bdos calls from 0x0005, register C functions 2 and 9
    switch (regs.byte[Z80_C]) {

        case 2: // output the character in E
            console.putc(regs.byte[Z80_E]);
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = regs.word[Z80_DE]; *mapMem(ctx, i) != '$'; ++i)
                console.putc(*mapMem(ctx, i));
            break;
    }
}

int main() {
    console.init();
    console.baud(115200, fullSpeedClock());
    led.mode(Pinmode::out);

    memcpy(context.mem + 0x100, rom, sizeof rom);

    // Patch the memory of the program. Reset at 0x0000 is trapped by an
    // OUT which will stop emulation. CP/M bdos call 5 is trapped by an IN.
    // See Z80_INPUT_BYTE() and Z80_OUTPUT_BYTE() definitions in z80user.h.

    context.mem[0] = 0xd3;       // OUT N, A
    context.mem[1] = 0x00;

    context.mem[5] = 0xdb;       // IN A, N
    context.mem[6] = 0x00;
    context.mem[7] = 0xc9;       // RET

    // start emulating

    Z80Reset(&context.state);
    context.state.pc = 0x100;
    context.done = 0;

    uint32_t start = ticks;
    do {
        Z80Emulate(&context.state, 2000000, &context);
        led.toggle();
    } while (!context.done);
    printf("Emulating zexall took %d ms.\n", ticks - start);

    led = 1;  // turn LED off (inverted logic)
    while (true) {}
}
