// Terminal pass-through for eZ80 via Wide Pill.
//
// Connections:
//  PB0 = eZ80 XIN, pin 86
//  PB2 = eZ80 ZDA, pin 69 (w/ 10 kΩ pull-up)
//  PB4 = eZ80 ZCL, pin 67 (w/ 10 kΩ pull-up)
//  PB8 = eZ80 RESET, pin 55
//  PA2 = eZ80 RX0, pin 74
//  PA3 = eZ80 TX0, pin 73

#include <jee.h>
#include <string.h>

UartBufDev< PinA<9>, PinA<10> > console;
UartBufDev< PinA<2>, PinA<3> > serial;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

Timer<3> timer;

PinB<0> XIN;
PinB<2> ZDA;
PinB<4> ZCL;
PinB<8> RST;

#include <zdi-util.h>

void zdiConfig () {
    XIN.mode(Pinmode::alt_out); // XXX alt_out_50mhz
    // generate a 36 MHz signal with 50% duty cycle on PB0, using TIM3
    timer.init(2);
    timer.pwm(1);

    // initialise all the main control pins
    RST = 1; ZCL = 1; ZDA = 0;
    RST.mode(Pinmode::out_od);
    ZCL.mode(Pinmode::out); // XXX out_50mhz
    ZDA.mode(Pinmode::out);
}

int main() {
    console.init();
    serial.init();
    const int hz = fullSpeedClock();
    console.baud(115200, hz);
    serial.baud(9600 * 36/4, hz/2);
    wait_ms(100);

    // disable JTAG in AFIO-MAPR to release PB3, PB4, and PA15
    // (looks like this has to be done *after* some GPIO mode inits)
    constexpr uint32_t afio = 0x40010000;
    MMIO32(afio+0x04) |= (2<<24); // disable JTAG, keep SWD enabled

    zdiConfig();
    ezReset();

    // no wait states for external ram (default is 7)
    zIns(0x3E,0x08);        // ld a,08h
    zIns(0xED, 0x39, 0xAA); // out0 (0AAh),a

    // start running from flash
    setPC(0x000000);        // PC was messed up by zIns
    zdiOut(0x10, 0x00);     // continue

    while (true) {
        if (serial.readable())
            console.putc(serial.getc());
        if (console.readable())
            serial.putc(console.getc());
    }
}
