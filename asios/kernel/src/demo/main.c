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

void systemCall (Context *ctx, int req, uint16_t pc) {
    Z80_STATE* state = &ctx->state;

    switch (req) {

        case 0: { // return true if there's input
            int n = 0;
            ioctl(0, /*FIONREAD*/ 0, &n);
            A = n ? 0xFF : 0x00;
            break;
        }

        case 1: { // wait for input, return in A
            // TODO should be a read which suspends until there is data
            // this polls for now, and has a race condition with two readers
            int n = 0;
            do
                ioctl(0, /*FIONREAD*/ 0, &n);
            while (n == 0);
            read(0, &n, 1);
            A = n;
            break;
        }

        case 2: // output the character in E
            write(1, &DE, 1); // ignores D
            break;

        case 3: // output the string in DE until null byte
            write(1, CCMEM + DE, strlen((char*) CCMEM + DE));
            break;

        case 4: { // r/w diskio
                int out = (B & 0x80) != 0;
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 26*trk + sec;  // no skewing

                diskio(dsk, pos | (out ? 1<<31 : 0), CCMEM + HL, cnt);
            }
            A = 0;
            break;

        default:
            write(2, "\n*** sysreq? ***\n", 17);
            while (1) {}
    }
}

int main() {
    // emulated rom bootstrap, loads first disk sector to 0x0000
    diskio(0, 0, CCMEM, 1);

    // if boot sector is not as expected, init disk from scratch
    if (memcmp(CCMEM, rom, 128) != 0) {
        diskio(0, 0 | (1<<31), (void*) rom, (sizeof rom + 127) / 128);
        diskio(0, 0, CCMEM, 1);

        // write 16 empty directory sectors to track 2
        uint8_t buf [128];
        memset(buf, 0xE5, sizeof buf);
        for (int i = 0; i < 16; ++i)
            diskio(0, (26*2 + i) | (1<<31), buf, 1);
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
