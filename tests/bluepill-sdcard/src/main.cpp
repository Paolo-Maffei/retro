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

SpiGpio< PinB<15>, PinB<14>, PinB<13>, PinB<12> > spi;
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

    wait_ms(10);
    console.baud(115200, fullSpeedClock());

    uint8_t buf [512];

    for (int block = 0; ; block += 256) {
        printf("\nblock = %d: ", block);

        memset(buf, 0, sizeof buf);
        for (int i = 0; i < 256; ++i) {
            buf[i] = i;
            buf[i+256] = block;
            sd.write512(block + i, buf);
            console.putc('.');
            led.toggle();
        }

        memset(buf, 0, sizeof buf);
        for (int i = 0; i < 256; ++i) {
            sd.read512(block + i, buf);
            bool ok = buf[i] == i && buf[i+256] == (uint8_t) block;
            console.putc(ok ? '+' : '?');
            led.toggle();
        }
    }
}
