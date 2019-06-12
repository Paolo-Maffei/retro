#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

uint8_t mem [1<<16];

// xxd -i <hexsave.com >../common-z80/hexsave.h
const uint8_t ram [] = {
#include "hexsave.h"
};

Context context;

struct Disk {
    FILE* fp;

    void init () {
        const char* name = "fd.img";
        fp = fopen(name, "r+");
        if (fp == 0) {
            perror(name);
            exit(1);
        }
        setbuf(fp, 0);
    }

    void readSector (int pos, void* buf) {
        fseek(fp, pos * 128, 0);
        fread(buf, 128, 1, fp);
    }

    void writeSector (int pos, void const* buf) {
        fseek(fp, pos * 128, 0);
        fwrite(buf, 128, 1, fp);
    }
} disk;

static bool readable () {
    return false; // XXX
}

void systemCall (Context* z, int req, uint16_t pc) {
    Z80_STATE* state = &(z->state);
    //printf("req %d A %d\n", req, A);
    switch (req) {
        case 0: // coninst
            A = readable() ? 0xFF : 0x00;
            break;
        case 1: // conin
            A = getchar();
            break;
        case 2: // conout
            putchar(C);
            break;
        case 3: // constr
            for (uint16_t i = DE; *mapMem(&context, i) != 0; i++)
                putchar(*mapMem(&context, i));
            break;
        case 4: // read/write
            //  ld a,(sekdrv)
            //  ld b,1 ; +128 for write
            //  ld de,(seksat)
            //  ld hl,(dmaadr)
            //  in a,(4)
            //  ret
            {
                bool out = (B & 0x80) != 0;
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 2048*dsk + 26*trk + sec;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    void* mem = mapMem(&context, HL + 128*i);
                    if (out)
                        disk.writeSector(pos + i, mem);
                    else
                        disk.readSector(pos + i, mem);
                }
            }
            A = 0;
            break;
        default:
            printf("syscall %d @ %04x ?\n", req, pc);
            while (1) {}
    }
}

int main() {
    disk.init();

    // emulated rom bootstrap, loads first disk sector to 0x0000
    disk.readSector(0, mapMem(&context, 0x0000));

    // leave a copy of HEXSAVE.COM at 0x0100
    memcpy(mapMem(&context, 0x0100), ram, sizeof ram);

    // start emulating
    Z80Reset(&context.state);
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
    } while (!context.done);

    return 0;
}
