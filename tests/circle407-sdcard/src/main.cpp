// Very basic test of the SD-card driver in the JeeH library.

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

    while (1) {
        printf("%d\n", ticks);
        led = 0;
        wait_ms(100);
        led = 1;
        wait_ms(400);
    }
}
