#if TEENSY
#include <Arduino.h>
#define ticks millis()
#define printf Serial.printf
#define PUTC Serial.write
#else
// FIXME comment this out for TEENSY, even though it's in the #else branch (!)
#include <jee.h>
#include <string.h>
#define PUTC console.putc
#endif

extern "C" {
#include "context.h"
#include "z80emu.h"
}

const uint8_t rom [] = {
#include "zexall.h"
};

#if TEENSY
#define led 13
#else
UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinC<13> led;
#endif
Context context;

void systemCall (Context *ctx, int, uint16_t) {
    auto& regs = ctx->state.registers;

    // emulate CP/M bdos calls from 0x0005, register C functions 2 and 9
    switch (regs.byte[Z80_C]) {

        case 2: // output the character in E
            PUTC(regs.byte[Z80_E]);
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = regs.word[Z80_DE]; *mapMem(ctx, i) != '$'; ++i)
                PUTC(*mapMem(ctx, i));
            break;
    }
}

void setup () {
#if TEENSY
    Serial.begin(115200);
    pinMode(led, OUTPUT);
#else
    console.init();
#if BLUEPILL
    int usartBusHz = fullSpeedClock();
#else
    int usartBusHz = fullSpeedClock() / 2; // usart bus runs at 84 iso 168 MHz
#endif
    console.baud(115200, usartBusHz);
    led.mode(Pinmode::out);
#endif

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

    uint64_t cycles = 0;
    uint32_t start = ticks;

    do {
        cycles += Z80Emulate(&context.state, 100000000, &context);
#if TEENSY
        digitalWrite(led, !digitalRead(led));
#else
        led.toggle();
#endif
    } while (!context.done);

    uint32_t t = ticks - start;
    printf("\nEmulating zexall took %.1f seconds: %llu cycles @ %.1f MHz\n",
            t/1000.0, cycles, cycles/(1000.0*t));

    // Teensy 4.0 @ 600 MHz:
    //  Emulating zexall took 294.6 seconds: 46734978649 cycles @ 158.6 MHz
}

void loop () {}

int main () {
    setup();
    while (true)
        loop();
}
