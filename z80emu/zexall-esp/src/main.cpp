#include <Arduino.h>

#define printf Serial.printf

#undef F
#undef DEC

extern "C" {
#include "context.h"
#include "z80emu.h"
}

const uint8_t rom [] = {
#include "zexall.h"
};

#if LOLIN32
constexpr int LED = 5;
#else
constexpr int LED = BUILTIN_LED;
#endif

Context context;

void systemCall (Context *ctx, int, uint16_t) {
    auto& regs = ctx->state.registers;

    // emulate CP/M bdos calls from 0x0005, register C functions 2 and 9
    switch (regs.byte[Z80_C]) {

        case 2: // output the character in E
            Serial.write(regs.byte[Z80_E]);
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = regs.word[Z80_DE]; *mapMem(ctx, i) != '$'; ++i)
                Serial.write(*mapMem(ctx, i));
            break;
    }
}

void setup () {
    Serial.begin(115200);
    pinMode(LED, OUTPUT);

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

    uint32_t start = millis();
    do {
        Z80Emulate(&context.state, 2000000, &context);
        digitalWrite(LED, !digitalRead(LED));
    } while (!context.done);
    printf("\nEmulating zexall took %d ms.\n", millis() - start);

    digitalWrite(LED, 1);  // turn LED off (inverted logic)
}

void loop () {}
