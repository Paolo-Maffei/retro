// Very basic test of the SD-card driver in the JeeH library.
// wARNING: This will destroy the contents on the SD card.

#include <jee.h>
#include <jee/spi-sdcard.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinC<13> led;

#if SPI_1
SpiHw< PinA<7>, PinA<6>, PinA<5>, PinA<4> > spi;
#elif SPI_2
SpiHw< PinB<15>, PinB<14>, PinB<13>, PinB<12> > spi;
#elif CIRCLE103 || CIRCLE407
// all pins connected, i.e. could also use SDIO
// see schematic: STM32F103/407VET6mini (circle)
SpiGpio< PinD<2>, PinC<8>, PinC<12>, PinC<11> > spi;
#endif

SdCard< decltype(spi) > sd;

int main() {
    console.init();
    enableSysTick();
    led.mode(Pinmode::out);

    wait_ms(500);
    printf("\nsd card: ");
    spi.init();
    if (sd.init())
        printf("detected, hd=%d\n", sd.sdhc);

    // switch to full speed, now that the SD card has been inited
    wait_ms(10); // let serial output drain
#if STM32F1
    console.baud(115200, fullSpeedClock());
#elif STM32F4
    console.baud(115200, fullSpeedClock()/2);
#endif

#if SPI1 || SPI_2
    // switch to div-2, i.e. 36 MHz for SPI1, or 18 MHz for SPI2 (on F103)
    MMIO32(spi.cr1) = (1<<6) | (0<<3) | (1<<2) | (0<<1);  // [1] p.742
#endif

    uint8_t buf [512];

    for (int block = 0; ; block += 256) {
        printf("\nblock = %d: ", block);

        uint32_t t1 = ticks;
        memset(buf, 0, sizeof buf);
        for (int i = 0; i < 256; ++i) {
            buf[i] = i;
            buf[i+256] = block;
            sd.write512(block + i, buf);
            console.putc('.');
            led.toggle();
        }

        uint32_t t2 = ticks;
        memset(buf, 0, sizeof buf);
        for (int i = 0; i < 256; ++i) {
            sd.read512(block + i, buf);
            bool ok = buf[i] == i && buf[i+256] == (uint8_t) block;
            console.putc(ok ? '+' : '?');
            led.toggle();
        }

        uint32_t t3 = ticks;
        printf("\nwr %d µs/blk, rd %d µs/blk",
                ((t2-t1)*1000)/256, ((t3-t2)*1000)/256);
    }
}
