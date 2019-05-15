// Bring-up test code for the ez-retro, i.e. eZ80 connected to a Wide Pill
//
// hello ram test: a2zwjcb
// hello flash test: a2zfjcba0zwjcb
// 
// Connections:
//
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

PinA<1> led;
Timer<3> timer;

PinB<0> XIN;
PinB<2> ZDA;
PinB<4> ZCL;
PinB<8> RST;

#define SLOW 0  // 0 or 40, for 4 or 36 MHz clocks (flash demo assumes 4 MHz)

#include <zdi-util.h>

// see embello/explore/1608-forth/ezr/asm/hello.asm
const uint8_t hello [] = {
    0x06, 0x00, 0x0E, 0xA5, 0x3E, 0x03, 0xED, 0x79, 0x0E, 0xC3, 0x3E, 0x80,
    0xED, 0x79, 0x0E, 0xC0, 0x3E, 0x1A, 0xED, 0x79, 0x0E, 0xC3, 0x3E, 0x03,
    0xED, 0x79, 0x0E, 0xC2, 0x3E, 0x06, 0xED, 0x79, 0x21, 0x39, 0xE0, 0x7E,
    0xA7, 0x28, 0x10, 0x0E, 0xC5, 0xED, 0x78, 0xE6, 0x20, 0x28, 0xF8, 0x0E,
    0xC0, 0x7E, 0xED, 0x79, 0x23, 0x18, 0xEC, 0x18, 0xFE, 0x48, 0x65, 0x6C,
    0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x0A, 0x0D, 0x00,
};

// see embello/explore/1608-forth/ezr/asm/flash.asm - adjusted for 4 MHz
const uint8_t flash [] = {
    0x01, 0xF5, 0x00, 0x3E, 0xB6, 0xED, 0x79, 0x3E, 0x49, 0xED, 0x79, 0x0E, 
    0xF9, 0x3E, 0x15, 0xED, 0x79, 0x0E, 0xF5, 0x3E, 0xB6, 0xED, 0x79, 0x3E, 
    0x49, 0xED, 0x79, 0x0E, 0xFA, 0x3E, 0x00, 0xED, 0x79, 0x0E, 0xFF, 0x3E, 
    0x01, 0xED, 0x79, 0x18, 0xFE, 
};

int main() {
    console.init();
    console.baud(115200, fullSpeedClock());
    led.mode(Pinmode::out);
    wait_ms(500);
    printf("\n---\n");

    serial.init();
#if SLOW
    serial.baud(9600, 72000000/2);
#else
    serial.baud(9600 * 36/4, 72000000/2);
#endif

    // disable JTAG in AFIO-MAPR to release PB3, PB4, and PA15
    // (looks like this has to be done *after* some GPIO mode inits)
    constexpr uint32_t afio = 0x40010000;
    MMIO32(afio+0x04) |= (2<<24); // disable JTAG, keep SWD enabled

    XIN.mode(Pinmode::alt_out); // XXX alt_out_50mhz
#if SLOW
    // generate a 4 MHz signal with 50% duty cycle on PB0, using TIM3
    timer.init(18);
    timer.pwm(9);
#else
    // generate a 36 MHz signal with 50% duty cycle on PB0, using TIM3
    timer.init(2);
    timer.pwm(1);
#endif

    // initialise all the main control pins
    RST = 1; ZCL = 1; ZDA = 0;
    RST.mode(Pinmode::out_od);
    ZCL.mode(Pinmode::out); // XXX out_50mhz
    ZDA.mode(Pinmode::out);

    printf("v%02x", zdiIn(1));
    printf(".%02x", zdiIn(0));
    printf(".%02x\n", zdiIn(2));

    while (true) {
        uint8_t stat = zdiIn(3);
        zCmd(0x08); // set ADL
        printf("s%02x %02x: ", stat, getMbase());
        if ((stat & 0x10) == 0)
            zCmd(0x09); // reset ADL

        while (!console.readable()) {
            if (serial.readable())
                console.putc(serial.getc());
        }
        led.toggle();

        int ch = console.getc();
        if (ch != '\n')
            printf("%c\n", ch);

        switch (ch) {

#include <zdi-cmds.h>

            case 'f': writeMem(0xFFE000, flash, sizeof flash); break;
            case 'w': writeMem(0xFFE000, hello, sizeof hello); break;

            case 'S': // disconnect the clock
                XIN.mode(Pinmode::in_float);
                break;
            case 'Z': // disconnect the ZDI and reset pins
                ZDA.mode(Pinmode::in_float);
                ZCL.mode(Pinmode::in_float);
                RST.mode(Pinmode::in_float);
                while (true) {} // don't let main loop get control again
                break;

            case '\r': // console.getc();
            case '\n': printf("\r"); break;

            default: printf("?\n");
        }
    }
}
