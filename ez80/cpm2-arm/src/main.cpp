// Bootstrap CP/M onto eZ80 via ZDI from Wide Pill.
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
#include <jee/spi-sdcard.h>
#include <string.h>

UartBufDev< PinA<9>, PinA<10> > console;
UartBufDev< PinA<2>, PinA<3> > serial;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

SpiGpio< PinD<2>, PinC<8>, PinC<12>, PinC<11> > spi;
SdCard< decltype(spi) > sd;
FatFS< decltype(sd) > fat;
//FileMap< decltype(fat), 9 > DiskMap;

PinA<1> led;
Timer<3> timer;

PinB<0> XIN;
PinB<2> ZDA;
PinB<4> ZCL;
PinB<8> RST;

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

void zdiCheck () {
    uint32_t version = (zdiIn(1) << 16) | (zdiIn(0) << 8) | zdiIn(2);
    if (version != 0x000802)
        printf("? %06x\n", version);
}

void controlCheck () {
    zdiOut(0x10, 0x80); // break
    zCmd(0x08); // set ADL
    uint8_t stat = zdiIn(3);
    if (stat != 0x90)
        printf("? %02x\n", stat);
}

void memoryCheck () {
    writeMem(0xFFE000, hello, sizeof hello);
    uint8_t buf [sizeof hello];
    readMem(0xFFE000, buf, sizeof buf);
    if (memcmp(hello, buf, sizeof hello) != 0)
        printf("? memory read mismatch\n");
}

void sendCheck () {
    zCmd(0x08); // set ADL
    setMbase(0xFF);
    zCmd(0x09); // reset ADL
    setPC(0xFFE000); // jump

    while (serial.readable())
        serial.getc();

    zdiOut(0x10, 0x00); // continue
    wait_ms(100);
    zdiOut(0x10, 0x80); // break

    char buf [30];
    for (unsigned i = 0; i < sizeof buf; ++i)
        buf[i] = serial.readable() ? serial.getc() : 0;
    if (strcmp(buf, "Hello world!\n\r") != 0) // TODO oops, LFCR iso CRLF
        printf("? <%s>\n", buf);
}

void listSdFiles () {
    for (int i = 0; i < fat.rmax; ++i) {
        int off = (i*32) % 512;
        if (off == 0)
            sd.read512(fat.rdir + i/16, fat.buf);
        int length = *(int32_t*) (fat.buf+off+28);
        if (length >= 0 &&
                '!' < fat.buf[off] && fat.buf[off] < '~' &&
                fat.buf[off+5] != '~' && fat.buf[off+6] != '~') {
            uint8_t attr = fat.buf[off+11];
            printf("   %s\t", attr & 8 ? "vol:" : attr & 16 ? "dir:" : "");
            for (int j = 0; j < 11; ++j) {
                int c = fat.buf[off+j];
                if (j == 8)
                    printf(".");
                printf("%c", c);
            }
            printf(" %7d b\n", length);
        }
    }
}

int main() {
    // basic console and LED setup
    console.init();
    enableSysTick();
    led.mode(Pinmode::out);
    wait_ms(100);
    printf("\n");

    // init SD card while the system clock is still at 8 MHz
    spi.init();
    bool sdOk = sd.init();

    // now switch to 72 MHz
    wait_ms(10);
    const int hz = fullSpeedClock();
    console.baud(115200, hz);
    serial.init();
    serial.baud(9600 * 36/4, hz/2);

    // disable JTAG in AFIO-MAPR to release PB3, PB4, and PA15
    // (looks like this has to be done *after* some GPIO mode inits)
    constexpr uint32_t afio = 0x40010000;
    MMIO32(afio+0x04) |= (2<<24); // disable JTAG, keep SWD enabled

    printf("I"); zdiConfig();    // initialise the clock, ZDI pins, etc
    printf("Z"); zdiCheck();     // check basic ZDI access
    printf("C"); controlCheck(); // check status control
    printf("M"); memoryCheck();  // check internal memory
    printf("S"); sendCheck();    // check serial send

    printf("D"); // SD card
    if (sdOk) {
        printf("detected, sdhc=%d\n", sd.sdhc);
        fat.init();
        listSdFiles();
    }

    printf("\n");
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

            case '\r': // console.getc();
            case '\n': printf("\r"); break;

            default: printf("?\n");
        }
    }
}
