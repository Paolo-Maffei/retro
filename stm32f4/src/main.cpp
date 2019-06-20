#include <string.h>

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

// NATIVE is a build for MacOS/Linux, with all the embedded stuff stubbed out
// this relies on platformio's LDF "chain+" evaluation to avoid compiling JeeH
#ifdef NATIVE

#include "native.h"

uint8_t mainMem [64*1024];
Console console;
DummyGPIO led;

DiskImage<128> fdisk ("flashmem.img");
DiskImage<512> sdisk ("sdcard.img");

#else // NATIVE

#include <jee.h>
#include <jee/spi-sdcard.h>
#include "flashwear.h"

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinB<9> led;
FlashWear fdisk;

// all pins connected, i.e. could also use SDIO
// see schematic: STM32F103/407VET6mini (circle)
SpiGpio< PinD<2>, PinC<8>, PinC<12>, PinC<11> > spi;
SdCard< decltype(spi) > sdisk;
FatFS< decltype(sdisk) > fat;

#endif // NATIVE

Z80_STATE z80state;

void systemCall (void* context, int req) {
    Z80_STATE* state = &z80state;
    if (req > 3)
        printf("req %d A %d\r\n", req, A);
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
        case 4: // read/write flash disk, 128 b/s, 26 s/t (8" SSSD: 77x3.25 KB)
            //  ld a,(sekdrv)
            //  ld b,1 ; +128 for write
            //  ld de,(seksat)
            //  ld hl,(dmaadr)
            //  in a,(4)
            //  ret
            printf("AF %04X BC %04X DE %04X HL %04X\r\n", AF, BC, DE, HL);
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
#if NATIVE // TODO update sd's read512/write512 to readSector/writeSector
        case 5: // read/write sd card, 512 b/s, 18 s/t (1.44M: 160x9 KB)
            //  ld a,(sekdrv)
            //  ld b,1 ; +128 for write
            //  ld de,(seksat)
            //  ld hl,(dmaadr)
            //  in a,(5)
            //  ret
            printf("AF %04X BC %04X DE %04X HL %04X\r\n", AF, BC, DE, HL);
            {
                bool out = (B & 0x80) != 0;
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 4096*dsk + 18*trk + sec;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    //printf("read %d pos %d\r\n", i, pos);
                    uint8_t* mem = mapMem(&context, HL + 512*i);
                    if (out)
                        sdisk.writeSector(pos + i, mem);
                    else
                        sdisk.readSector(pos + i, mem);
#if 0
                    for (int j = 0; j < 512; ++j) {
                        printf("%02x", mem[j]);
                        if (j % 32 == 31)
                            printf("\r\n");
                    }
                    for (int j = 0; j < 512; ++j) {
                        uint8_t c = mem[j];
                        if (c < ' ' || c > '~')
                            c = '.';
                        printf("%c", c);
                        if (j % 32 == 31)
                            printf("\r\n");
                    }
#endif
                }
                //printf("read ok\r\n");
            }
            A = 0;
            break;
#endif
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

uint16_t initMemory (uint8_t* mem) {
#if ZEXALL
    static const uint8_t rom [] = {
    #include "zexall.h"
    };

    printf("\n[zexall] %d bytes @ 0x0100\n", sizeof rom);
    memcpy(mem + 0x100, rom, sizeof rom);

    // Patch the memory of the program. Reset at 0x0000 is trapped by an
    // OUT which will stop emulation. CP/M bdos call 5 is trapped by an IN.
    // See Z80_INPUT_BYTE() and Z80_OUTPUT_BYTE() definitions in z80user.h.

    mem[0] = 0xD3;       // OUT N,A
    mem[1] = 0x00;

    mem[5] = 0xDB;       // IN A,N (N = 10, see systemCall)
    mem[6] = 0x0A;
    mem[7] = 0xC9;       // RET

    return 0x100;
#else
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

        // write 26 empty directory sectors to track 2
        // only 16 are used, but this way it covers both skewed and unskewed
        uint8_t buf [128];
        memset(buf, 0xE5, sizeof buf);
        for (int i = 0; i < 26; ++i)
            fdisk.writeSector(26*2 + i, buf);
    }

    // emulated rom bootstrap, loads first disk sector to 0x0000
    fdisk.readSector(0, mem);

#if !NATIVE
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
#endif
}

int main() {
    console.init();

#if NATIVE
    sdisk.init();
#else
    enableSysTick();
    led.mode(Pinmode::out);

    wait_ms(500);
    printf("\n[sd card] ");
    spi.init();
    if (sdisk.init()) {
        printf("detected, hd=%d\n", sdisk.sdhc);
        fat.init();
    }

    // switch to full speed, now that the SD card has been inited
    wait_ms(10); // let serial output drain
    console.baud(115200, fullSpeedClock()/2);
#endif

    Z80Reset(&z80state);
    z80state.pc = initMemory(mapMem(0, 0));

    while (true) {
        Z80Emulate(&z80state, 10000000, 0);
        led.toggle();
    }
}
