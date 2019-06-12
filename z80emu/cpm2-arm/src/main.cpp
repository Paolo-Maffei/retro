#include <jee.h>
#include <string.h>
#include "flashwear.h"

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

// cat boot.com bdos22.com bios.com | xxd -i >../common-z80/rom-cpm.h
const uint8_t rom [] = {
#include "rom-cpm.h"
};

// xxd -i <hexsave.com >../common-z80/hexsave.h
const uint8_t ram [] = {
#include "hexsave.h"
};

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinA<6> led;
Context context;
FlashWear disk;

void systemCall (Context* z, int req, uint16_t pc) {
    Z80_STATE* state = &(z->state);
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
            for (uint16_t i = DE; *mapMem(&context, i) != 0; i++)
                console.putc(*mapMem(&context, i));
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
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);

    if (disk.valid())
        disk.init();
    else {
        disk.init(true);
        
        // write boot loader and system to tracks 0..1
        int pos = 0;
        for (uint32_t off = 0; off < sizeof rom; off += 128)
            disk.writeSector(pos++, rom + off);

        // write 16 empty directory sectors to track 2
        uint8_t buf [128];
        memset(buf, 0xE5, sizeof buf);
        for (int i = 0; i < 16; ++i)
            disk.writeSector(26*2 + i, buf);
    }

    // emulated rom bootstrap, loads first disk sector to 0x0000
    disk.readSector(0, mapMem(&context, 0x0000));

    // leave a copy of HEXSAVE.COM at 0x0100
    memcpy(mapMem(&context, 0x0100), ram, sizeof ram);

    // start emulating
    Z80Reset(&context.state);
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
        led.toggle();
    } while (!context.done);

    led = 1;  // turn LED off (inverted logic)
    while (true) {}
}
