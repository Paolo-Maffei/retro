#include <jee.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinC<13> led;

int main() {
    console.init();
    console.baud(115200, fullSpeedClock());
    led.mode(Pinmode::out);

    while (true) {
        printf("%d\n", ticks);
        led = 0;
        wait_ms(100);
        led = 1;
        wait_ms(400);
    }
}
