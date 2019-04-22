// Very basic test of the SD-card driver in the JeeH library.
// wARNING: This will destroy the contents on the SD card.

#include <jee.h>
#include <jee/spi-sdcard.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinB<9> led;

SpiGpio< PinD<2>, PinC<8>, PinC<12>, PinC<11> > spi;
SdCard< decltype(spi) > sd;

int main() {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);

    printf("\nsd card: ");
    spi.init();
    if (sd.init())
        printf("detected, hd=%d\n", sd.sdhc);

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
