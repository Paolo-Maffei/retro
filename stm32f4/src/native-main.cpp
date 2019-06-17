#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "console-posix.h"

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

uint8_t mainMem [64*1024];

Z80_STATE z80state;

class DummyFlash {
    uint8_t fmem [256*1024];
public:
    bool valid () { return false; }
    int init (bool =false) { return 0; }
    void readSector (int pos, void* buf) {
        memcpy(buf, fmem + 128*pos, 128);
    }
    void writeSector (int pos, void const* buf) {
        memcpy(fmem + 128*pos, buf, 128);
    }
};

DummyFlash fdisk;

void systemCall (void *context, int req) {
    Z80_STATE* state = &z80state;
    //printf("req %d A %d\n", req, A);
    switch (req) {
        case 0: // coninst
            A = consoleHit() ? 0xFF : 0x00;
            break;
        case 1: // conin
            A = consoleWait();
            break;
        case 2: // conout
            consoleOut(C);
            break;
        case 3: // constr
            for (uint16_t i = DE; *mapMem(context, i) != 0; i++)
                consoleOut(*mapMem(context, i));
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
                    void* mem = mapMem(context, HL + 128*i);
                    if (out)
                        fdisk.writeSector(pos + i, mem);
                    else
                        fdisk.readSector(pos + i, mem);
                }
            }
            A = 0;
            break;
        default:
            printf("syscall %d ?\n", req);
            while (1) {}
    }
}

uint16_t initMemory (uint8_t* mem) {
    if (fdisk.valid())
        fdisk.init(false);  // setup, keeping current data
    else {
        fdisk.init(true);    // initialise fresh disk map
        
        static const uint8_t rom_cpm [] = {
        #include "rom-cpm.h"
        };

        // write boot loader and system to tracks 0..1
        for (uint32_t off = 0; off < sizeof rom_cpm; off += 128)
            fdisk.writeSector(off/128, rom_cpm + off);

        // write 16 empty directory sectors to track 2
        uint8_t buf [128];
        memset(buf, 0xE5, sizeof buf);
        for (int i = 0; i < 16; ++i)
            fdisk.writeSector(26*2 + i, buf);
    }

    // emulated rom bootstrap, loads first disk sector to 0x0000
    fdisk.readSector(0, mem);

#if 0
    // if there's a boot command, load it instead of the default "hexsave"
    // TODO this api sucks, should not need to mention "fat" twice
    FileMap< decltype(fat), 9 > file (fat);
    int len = file.open("BOOT    COM");
    if (len > 0) {
        printf("[sd boot] save %d boot.com\n", (len+255)/256);
        for (int i = 0; i < len; i += 512)
            file.ioSect(false, i/512, mem + 0x0100 + i);
        return 0x0000;
    }
#endif

    static const uint8_t rom [] = {
    #include "hexsave.h"
    };

    printf("\n[hexsave] %d bytes @ 0x0100\n", (int) sizeof rom);
    memcpy(mem + 0x100, rom, sizeof rom);

    return 0x0000;
}

int main() {
    tcgetattr(0, &tiosSaved);
    atexit(cleanup);

    struct termios tios = tiosSaved;
    cfmakeraw(&tios);
    tcsetattr(0, TCSANOW, &tios);

    Z80Reset(&z80state);
    z80state.pc = initMemory(mapMem(0, 0));

    while (true)
        Z80Emulate(&z80state, 10000000, 0);
}
