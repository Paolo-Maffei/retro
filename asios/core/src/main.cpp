#include <jee.h>
#include <string.h>

const uint8_t appCode [] = {
#include "appcode.h"
};

UartDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinA<6> led;
//PinA<7> led2;

int main () {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    wait_ms(100);
    led.mode(Pinmode::out);

    uint32_t* appMem = (uint32_t*) 0x20000000;
    memcpy(appMem, appCode, sizeof appCode);

    printf("magic 0x%08x start 0x%08x size %d b\n",
            appMem[0], appMem[1], sizeof appCode);

    VTableRam().sv_call = []() {
        // console must be polled inside svc, i.e. UartDev iso UartBufDev
        printf("%d\n", ticks);
        // wait_ms() can't be used inside svc
        led = 0;
        for (int i = 0; i < 2000000; ++i) __asm("");
        led = 1;
        for (int i = 0; i < 20000000; ++i) __asm("");
    };

    int r = -1;
    if (appMem[0] == 0x12345678)
        r = ((int (*)()) (appMem[1]))();

    printf("done %d\n", r);
}
