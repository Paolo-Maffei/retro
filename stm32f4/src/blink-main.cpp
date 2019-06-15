// Simple blink test to verify that the toochain and board are working properly.
// The LED blinks (briefly on), and the serial port prints elapsed milliseconds.
// This also verifies that interrupts work and that the vector is placed in RAM.
// Furthermore, it generates periodic SysTicks and makes the CPU run at 168 MHz.
// Not bad for a couple lines of C++ code, eh? For details see the JeeH library.

#include <jee.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinB<9> led;

int main() {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);

    while (true) {
        printf("%d\n", ticks);
        led = 0;
        wait_ms(100);
        led = 1;
        wait_ms(400);
    }
}
