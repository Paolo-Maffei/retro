#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

Context context;

struct Disk {
    FILE* fp;

    void init () {
        const char* name = "hd.img";
        fp = fopen(name, "r+");
        if (fp == 0) {
            perror(name);
            exit(1);
        }
        setbuf(fp, 0);
    }

    void readSector (int pos, void* buf) {
        fseek(fp, pos * 512, 0);
        fread(buf, 512, 1, fp);
    }

    void writeSector (int pos, void const* buf) {
        fseek(fp, pos * 512, 0);
        fwrite(buf, 512, 1, fp);
    }
} disk;

static bool readable () {
    return false; // XXX
}

static void setBankSplit (uint8_t page) {
    context.split = 0;
    uint8_t* mainMem = mapMem(&context, 0);
    context.split = mainMem + (page << 8);
    memset(context.offset, 0, sizeof context.offset);
#if NBANKS > 1
    static uint8_t bankedMem [420*1024]; // lots of additional memory banks
    uint8_t* base = bankedMem;
    for (int i = 1; i < NBANKS; ++i) {
        uint8_t* limit = base + (page << 8);
        if (limit > bankedMem + sizeof bankedMem)
            break; // no more complete banks left
        context.offset[i] = base - mainMem;
        base = limit;
    }
#endif
}

void systemCall (Context* z, int req) {
    Z80_STATE* state = &(z->state);
    //printf(" req %d A %d @ %04x\n", req, A, state->pc);
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
                uint32_t pos = 65536*dsk + 256*trk + sec;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    void* mem = mapMem(&context, HL + 512*i);
                    if (out)
                        disk.writeSector(pos + i, mem);
                    else
                        disk.readSector(pos + i, mem);
                }
            }
            A = 0;
            break;
        case 5: // time get/set
            if (C == 0) { // XXX
#if 0
                RTC::DateTime dt = rtc.get();
                //printf("mdy %02d/%02d/20%02d %02d:%02d:%02d (%d ms)\n",
                //        dt.mo, dt.dy, dt.yr, dt.hh, dt.mm, dt.ss, ticks);
                uint8_t* ptr = mapMem(&context, HL);
                int t = date2dr(dt.yr, dt.mo, dt.dy);
                ptr[0] = t;
                ptr[1] = t>>8;
                ptr[2] = dt.hh + 6*(dt.hh/10); // hours, to BCD
                ptr[3] = dt.mm + 6*(dt.mm/10); // minutes, to BCD
                ptr[4] = dt.ss + 6*(dt.ss/10); // seconcds, to BCD
#endif
            }
            break;
        case 6: // banked memory config
            setBankSplit(A);
            break;
        case 7: // selmem (or return previous)
            if (A < NBANKS)
                context.bank = A;
            else
                A = context.bank;
            break;
        default:
            printf("syscall %d @ %04x ?\n", req, state->pc);
            exit(2);
    }
}

int main() {
    disk.init();

    const char* kernel = "fuzix.bin";
    const uint32_t origin = 0x0088;

    FILE* fp = fopen(kernel, "r");
    if (fp == 0 || fread(mapMem(&context, origin), 1, 0xFE00, fp) <= 10000) {
        perror(kernel);
        exit(1);
    }
    fclose(fp);

    // start emulating
    Z80Reset(&context.state);
    context.state.pc = origin;
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
    } while (!context.done);

    printf("\ndone @ %04x\n", context.state.pc);
    return 0;
}
