#include <jee.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinA<6> led2;
PinA<7> led3;
#define LED led3

extern char _etext, _edata, _ebss, _estack;
extern "C" void Reset_Handler ();

struct ExeHeader {
    uint32_t magic, reset;
    char *etext, *edata, *ebss, *estack;
    uint32_t aux1, aux2;
}; // header is 32 bytes

__attribute__ ((section(".exe_header")))
ExeHeader g_exeHeader = {
    0x12345678, (uint32_t) Reset_Handler,
    &_etext, &_edata, &_ebss, &_estack,
    0, 0,
};

int main() {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    LED.mode(Pinmode::out);

    while (true) {
        printf("%d\n", ticks);
        LED = 0;
        wait_ms(100);
        LED = 1;
        wait_ms(900);
    }
}
