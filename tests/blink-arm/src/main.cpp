#include <jee.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

#if BLUEPILL
PinC<13> led;
#elif WIDEPILL
PinA<1> led;
#elif CIRCLE407
PinB<9> led;
#elif BLACK407
PinA<6> led;
#elif DIYMORE
PinE<0> led;
#endif

int main() {
    console.init();
    enableSysTick();
    led.mode(Pinmode::out);

    while (true) {
        printf("%d\n", ticks);
        led = 0;
        wait_ms(100);
        led = 1;
        wait_ms(400);
    }
}
