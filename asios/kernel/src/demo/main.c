#include <string.h>
#include <syslib.h>

#include "context.h"
#include "z80emu.h"
#include "macros.h"

// cat boot.com bdos22.com bios.com | xxd -i >../common-z80/rom-cpm.h
const uint8_t rom [] = {
#include "rom-cpm.h"
};

// xxd -i <hexsave.com >../common-z80/hexsave.h
const uint8_t ram [] = {
#include "hexsave.h"
};

Context context;

static int getch (void) { int v = -1; read(0, &v, 1); return v; }
static void putch (char c) { write(1, &c, 1); }

void systemCall (Context *ctx, int req, uint16_t pc) {
    Z80_STATE* state = &ctx->state;

    switch (state->registers.byte[Z80_C]) {

        case 0: { // return true if there's input
            int n = 0;
            ioctl(0, /*FIONREAD*/ 0, &n);
            A = n ? 0xFF : 0x00;
            break;
        }

        case 1: // wait for input, return in A
            A = getch();
            break;

        case 2: // output the character in E
            putch((uint8_t) DE);
            break;

        case 3: // output the string in DE until null byte
            for (int i = DE; CCMEM[i] != 0; ++i)
                putch(CCMEM[i]);
            break;

        case 4: { // r/w diskio
                int out = (B & 0x80) != 0;
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 26*trk + sec;  // no skewing

                diskio(dsk, pos | (out ? 1<<31 : 0), CCMEM + HL, cnt);
            }
            A = 0;
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = DE; CCMEM[i] != '$'; ++i)
                putch(CCMEM[i]);
            break;

        default:
            //printf("Z: sysreq %d @ %04x ?\n", req, pc);
            write(2, "\n*** sysreq? ***\n", 17);
            while (1) {}
    }
}

int main() {
    for (int i = 0; i < 10000000; ++i) asm ("");
    putch('?'); // FIXME first call lost?

    // emulated rom bootstrap, loads first disk sector to 0x0000
    diskio(0, 0, CCMEM, 1);

    // if boot sector is not as expected, init disk from scratch
    if (memcmp(CCMEM, rom, 128) != 0) {
        diskio(0, 0 | (1<<31), (void*) rom, (sizeof rom + 127) / 128);
        diskio(0, 0, CCMEM, 1);
    }

    // leave a copy of HEXSAVE.COM at 0x0100
    memcpy(CCMEM + 0x0100, ram, sizeof ram);

    // start emulating
    Z80Reset(&context.state);
    context.done = 0;

    do
        Z80Emulate(&context.state, 2000000, &context);
    while (!context.done);

    while (1) {}
}
