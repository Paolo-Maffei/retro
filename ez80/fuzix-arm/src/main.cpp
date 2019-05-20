// Bootstrap FUZIX onto eZ80 via ZDI from Wide Pill.
//
// Connections:
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

SpiGpio< PinA<7>, PinA<6>, PinA<5>, PinA<4> > spi;
SdCard< decltype(spi) > sd;
FatFS< decltype(sd) > fat;

PinA<1> led;
Timer<3> timer;

PinB<0> XIN;
PinB<2> ZDA;
PinB<4> ZCL;
PinB<8> RST;

#include <zdi-util.h>

const uint8_t fuzix [] = {
#include "fuzix.h"
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

    ezReset(); // seems to be required for robust startup in all situations?
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

void diskSetup () {
    printf("<%d>", sd.sdhc);
    fat.init();
}

void ramDisk () {
    FileMap< decltype(fat), 49 > file (fat);
    int len = file.open("ROOTFS  IMG");
    printf("<%d>", len);

    zCmd(0x08); // set ADL
    uint8_t buf [512];
#if 0
    for (int pos = 0; pos < len; pos += 512) {
        if (!file.ioSect(false, pos/512, buf))
            printf("? fat map error at %d\n", pos);
        writeMem(0x080000 + pos, buf, sizeof buf);
    }
#endif
}

void romBoot () {
    // 2) enter ADL mode to switch to 24-bit addressing
    zCmd(0x08); // set ADL

    // 3) set MBASE now that we're in ADL mode
    setMbase(0x00);

    // 4) disable ERAM and move SRAM to same bank as MBASE
    zIns(0x3E, 0x80);       // ld a,80h
    zIns(0xED, 0x39, 0xB4); // out0 (RAM_BANK),a ; disable ERAM
    zIns(0x3E, 0x00);       // ld a,00h
    zIns(0xED, 0x39, 0xB5); // out0 (RAM_BANK),a ; SRAM to 0x00E000

    // 4A) move flash to high memory
    zIns(0x3E, 0xF0);       // ld a,0F0h
    zIns(0xED, 0x39, 0xF7); // out0 (FLASH_BANK),a ; FLASH to 0xF00000

    // 8) load FUZIX to {0x00,0x0100}
    writeMem(0x000100, fuzix, sizeof fuzix);

    // 9) switch from ADL mode to Z80 mode and jump to SLOAD address
    setPC(0x000100);
    zCmd(0x09); // reset ADL
    zdiOut(0x10, 0x00); // continue
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

    printf("I"); zdiConfig();     // initialise the clock, ZDI pins, etc
    printf("Z"); zdiCheck();      // check basic ZDI access
    printf("C"); controlCheck();  // check status control

    if (sdOk) {
        printf("D"); diskSetup(); // prepare SD card access
        printf("A"); ramDisk();   // load ram disk from SD card
        printf("B"); romBoot();   // simulate rom bootstrap
    } else
        ezReset(true);

    printf("\n");

    while (true) {
        if (serial.readable())
            console.putc(serial.getc());
        if (console.readable())
            serial.putc(console.getc());
    }
}
