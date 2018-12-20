#include <jee.h>
#include <jee/usb.h>

UsbDev console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

#if BLACK407
PinA<6> led;
#elif DIYMORE
PinE<0> led;
#endif

int main() {
    fullSpeedClock();
    console.init();
    led.mode(Pinmode::out);

    uint32_t last = 0;
    while (true) {
        if (last != ticks / 500) {
            last = ticks / 500;
            printf("%d\n", ticks);
            led.toggle();
        }
        console.poll();
    }
}
