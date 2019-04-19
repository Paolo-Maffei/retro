#include <jee.h>
#include <jee/spi-sdcard.h>
#include <string.h>
#include "cpmdate.h"

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinB<9> led;
RTC rtc;

SpiGpio< PinD<2>, PinC<8>, PinC<12>, PinC<11> > spi;
SdCard< decltype(spi) > sd;

Context context;
// max: 3x60K+4K, 3x48K+16K, 4x32K+32K, 8x16K+48K, 16x8K+56K
static uint8_t bankMem [120*1024]; // additional memory banks on F407

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
        printf("\treq %d AF %04x BC %04x DE %04x HL %04x SP %04x @ %d:%04x\n",
                req, AF, BC, DE, HL, SP, context.bank, pc);
#endif
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
            //printf("AF %04X BC %04X DE %04X HL %04X\n", AF, BC, DE, HL);
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
                uint32_t pos = 65536*A + DE + 2048;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    void* mem = mapMem(&context, HL + 512*i);
#if 1
                    printf("HD%d wr %d mem %d:0x%x pos %d\n",
                            A, out, context.bank, HL + 512*i, pos + i);
#endif
                    if (out)
                        sd.write512(pos + i, mem);
                    else
                        sd.read512(pos + i, mem);
                }
#endif
            }
            A = 0;
            break;
        case 5: { // time get/set
            if (C == 0) {
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
            } else {
#if 0
                RTC::DateTime dt;
                uint8_t* ptr = mapMem(&context, HL);
                // TODO set clock date & time
                dr2date(*(uint16_t*) ptr, &dt);
                dt.hh = ptr[2] - 6*(ptr[2]>>4); // hours, from BCD
                dt.mm = ptr[3] - 6*(ptr[3]>>4); // minutes, from BCD
                dt.ss = ptr[4] - 6*(ptr[4]>>4); // seconds, from BCD
                rtc.set(dt);
#endif
            }
            break;
        }
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
            printf("syscall %d @ %04x ?\n", req, state->pc);
            while (1) {}
    }
}

int main() {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);
    rtc.init();

    printf("\nsd init: ");
    spi.init();
    if (sd.init())
        printf("detected, hd=%d\n", sd.sdhc);

    const uint16_t origin = 0x0100;

#if 0
    // emulated room bootstrap, loads first disk sector to 0x0000
    disk.readSector(0, mapMem(&context, 0x0000));
#else
    printf("booting: ");
    for (int i = 0; i < 127; ++i) { // read 63.5K into RAM
        console.putc('.');
        sd.read512(1 + i, mapMem(&context, origin + 512*i));
    }
    printf("\n");
#endif

    // start emulating
    Z80Reset(&context.state);
    context.state.pc = origin;
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
        led.toggle();
    } while (!context.done);

    led = 1;  // turn LED off (inverted logic)
    while (true) {}
}
