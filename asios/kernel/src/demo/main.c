#include <string.h>
#include <syslib.h>

#include "context.h"
#include "z80emu.h"
#include "macros.h"

const uint8_t rom [] = {
#include "zexall.h"
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
            for (int i = DE; *mapMem(ctx, i) != 0; ++i)
                putch(*mapMem(ctx, i));
            break;

        case 4: { // r/w diskio
                int out = (B & 0x80) != 0;
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 26*trk + sec;  // no skewing

                diskio(dsk, pos | (out ? 1<<31 : 0), mapMem(&context, HL), cnt);
            }
            A = 0;
            break;

        case 9: // output the string in DE until '$' terminator
            for (int i = DE; *mapMem(ctx, i) != '$'; ++i)
                putch(*mapMem(ctx, i));
            break;

        default:
            //printf("Z: sysreq %d @ %04x ?\n", req, pc);
            write(2, "\n*** sysreq? ***\n", 17);
            while (1) {}
    }
}

int main() {
    putch("?"); // FIXME first call lost?
    diskio(0, 0 | (1<<31), (void*) rom, (sizeof rom + 127) / 128);

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
