#include <jee.h>
#include <string.h>

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinB<9> led;

Z80_STATE z80state;

void systemCall (void* context, int req) {
    Z80_STATE* state = &z80state;
    //printf("req %d A %d\n", req, A);
    switch (req) {
        case 0: // coninst
            A = console.readable() ? 0xFF : 0x00;
            break;
        case 1: // conin
            A = console.getc();
            break;
        case 2: // conout
            console.putc(C);
            break;
        case 3: // constr
            for (uint16_t i = DE; *mapMem(context, i) != 0; i++)
                console.putc(*mapMem(context, i));
            break;
#if ZEXALL
        case 10:
            // emulate CP/M bdos calls from 0x0005, reg C functions 2 and 9
            // TODO implement as Z80 asm in low mem, using "real" system calls
            switch (C) {
                case 2: // output the character in E
                    console.putc(DE);
                    break;
                case 9: // output the string in DE until '$' terminator
                    for (int i = DE; *mapMem(context, i) != '$'; ++i)
                        console.putc(*mapMem(context, i));
                    break;
            }
            break;
#endif
        default:
            printf("syscall %d ?\n", req);
            while (1) {}
    }
}

void initMemory () {
#if ZEXALL
    static const uint8_t rom [] = {
    #include "zexall.h"
    };

    printf("load ZEXALL: %d bytes @ 0x0100\n", sizeof rom);

    uint8_t *mem = mapMem(0, 0);
    memcpy(mem + 0x100, rom, sizeof rom);

    // Patch the memory of the program. Reset at 0x0000 is trapped by an
    // OUT which will stop emulation. CP/M bdos call 5 is trapped by an IN.
    // See Z80_INPUT_BYTE() and Z80_OUTPUT_BYTE() definitions in z80user.h.

    mem[0] = 0xD3;       // OUT N,A
    mem[1] = 0x00;

    mem[5] = 0xDB;       // IN A,N (N = 10, see systemCall)
    mem[6] = 0x0A;
    mem[7] = 0xC9;       // RET

    z80state.pc = 0x100;
#endif
}

int main() {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);

    printf("%d\n", ticks);

    // start emulating
    Z80Reset(&z80state);
    initMemory();

    while (true) {
        Z80Emulate(&z80state, 5000000, 0);
        led.toggle();
    }
}
