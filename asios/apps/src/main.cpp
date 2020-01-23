#include <jee.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

//PinA<6> led;
PinA<7> led;

extern char _etext;
extern char _edata;
extern char _ebss;
extern char _estack;
extern "C" void Reset_Handler ();

#if 1 // flash
extern __attribute__ ((section(".isr_vec")))
void (* const g_pfnVec[])() = {
    (void (*)()) &_estack, // stack pointer
    Reset_Handler,         // reset handler
};
#endif

struct ExeHeader {
    uint32_t magic;
    uint32_t reset;
    uint32_t etext, edata, ebss, estack;
    uint32_t filler [2];    // header is 32 bytes
};

__attribute__ ((section(".exe_header")))
ExeHeader g_exeHeader = {
    0x12345678,
    (uint32_t) Reset_Handler,
    (uint32_t) &_etext,
    (uint32_t) &_edata,
    (uint32_t) &_ebss,
    (uint32_t) &_estack,
    0, 0,
};

int main() {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);

    while (true) {
        printf("%d\n", ticks);
        led = 0;
        wait_ms(100);
        led = 1;
        wait_ms(900);
    }
}
