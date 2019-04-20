#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "cpmdate.h"
#include "console-posix.h"

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

Context context;
uint8_t mainMem [1<<16];
uint8_t bankMem [480*1024]; // additional memory banks

FILE* disk_fp;

void disk_init () {
    const char* name = "hd.img";
    disk_fp = fopen(name, "r+");
    if (disk_fp == 0) {
        perror(name);
        exit(1);
    }
    setbuf(disk_fp, 0);
}

void disk_read (int pos, void* buf, int len) {
    fseek(disk_fp, pos * len, 0);
    fread(buf, len, 1, disk_fp);
}

void disk_write (int pos, void const* buf, int len) {
    fseek(disk_fp, pos * len, 0);
    fwrite(buf, len, 1, disk_fp);
}

static void setBankSplit (uint8_t page) {
    context.split = mainMem + (page << 8);
    memset(context.offset, 0, sizeof context.offset);
#if NBANKS > 1
    uint8_t* base = bankMem;
    for (int i = 1; i < NBANKS; ++i) {
        uint8_t* limit = base + (page << 8);
        if (limit > bankMem + sizeof bankMem)
            break; // no more complete banks left
        context.offset[i] = base - mainMem;
        base = limit;
    }
#endif
}

void systemCall (Context* z, int req, int pc) {
    Z80_STATE* state = &(z->state);
#if 0
    if (req > 3)
        printf("\treq %d AF %04X BC %04X DE %04X HL %04X SP %04X @ %d:%04X\r\n",
                req, AF, BC, DE, HL, SP, context.bank, pc);
#endif
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
            for (uint16_t i = DE; *mapMem(&context, i) != 0; i++)
                consoleOut(*mapMem(&context, i));
            break;
        case 4: // read/write
            //  ld a,(sekdrv)
            //  ld b,1 ; +128 for write
            //  ld de,(seksat)
            //  ld hl,(dmaadr)
            //  in a,(4)
            //  ret
            //printf("AF %04X BC %04X DE %04X HL %04X\r\n", AF, BC, DE, HL);
            {
                bool out = (B & 0x80) != 0;
#if 0
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 2048*dsk + 26*trk + sec;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    void* mem = mapMem(&context, HL + 128*i);
                    if (out)
                        disk_write(pos + i, mem, 128);
                    else
                        disk_read(pos + i, mem, 128);
                }
#else
                uint8_t cnt = B & 0x7F;
                uint32_t pos = 65536*A + DE;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    void* mem = mapMem(&context, HL + 512*i);
#if 0
                    printf("HD%d wr %d mem %d:0x%X pos %d\r\n",
                            A, out, context.bank, HL + 512*i, pos + i);
#endif
                    if (out)
                        disk_write(pos + i, mem, 512);
                    else
                        disk_read(pos + i, mem, 512);
                }
#endif
            }
            A = 0;
            break;
        case 5: // time get/set
            if (C == 0) { // XXX
                time_t now = time(0);
                struct tm* p = localtime(&now);
                //printf("y %d m %d d %d hh %d mm %d ss %d\r\n",
                //    p->tm_year+1900, p->tm_mon+1, p->tm_mday,
                //    p->tm_hour, p->tm_min, p->tm_sec);
                uint8_t* ptr = mapMem(&context, HL);
                int t = date2dr(p->tm_year+1900, p->tm_mon+1, p->tm_mday);
                ptr[0] = t;
                ptr[1] = t>>8;
                ptr[2] = p->tm_hour + 6*(p->tm_hour/10); // hours, to BCD
                ptr[3] = p->tm_min + 6*(p->tm_min/10);   // minutes, to BCD
                ptr[4] = p->tm_sec + 6*(p->tm_sec/10);   // seconcds, to BCD
            }
            break;
        case 6: // set banked memory limit
            setBankSplit(A);
            break;
        case 7: { // select bank and return previous setting
            uint8_t prevBank = context.bank;
            context.bank = A;
            A = prevBank;
            break;
        }
        case 8: { // for use in xmove, inter-bank copying
            uint8_t *src = mainMem + DE, *dst = mainMem + HL;
            // never map above the split, i.e. in the common area
            if (dst < context.split)
                dst += context.offset[(A>>4) % NBANKS];
            if (src < context.split)
                src += context.offset[A % NBANKS];
            // TODO careful, this won't work across the split!
            memcpy(dst, src, BC);
            DE += BC;
            HL += BC;
            break;
        }
        default:
            printf("syscall %d @ %04x ?\r\n", req, pc);
            exit(2);
    }
}

int main() {
    disk_init();

    const char* kernel = "fuzix.bin";
    const uint16_t origin = 0x0100;

    FILE* fp = fopen(kernel, "r");
    if (fp == 0 || fread(mapMem(&context, origin), 1, 0xFF00, fp) <= 1000) {
        perror(kernel);
        exit(1);
    }
    fclose(fp);

    if (isatty(0)) {
        tcgetattr(0, &tiosSaved);
        atexit(cleanup);

        struct termios tios = tiosSaved;
        cfmakeraw(&tios);
        tcsetattr(0, TCSANOW, &tios);
    } else
        batchMode = 1;

    // start emulating
    Z80Reset(&context.state);
    context.state.pc = origin;
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
    } while (!context.done);

    printf("\r\ndone @ %04x\r\n", context.state.pc);
    return 0;
}
