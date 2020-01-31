#include <jee.h>

int main () {
    PinA<7> led3;
    led3.mode(Pinmode::out);

    while (true) {
        led3 = 0; // inverted logic
        for (int i = 0; i < 10000000; ++i) asm ("");
        led3 = 1;
        for (int i = 0; i < 10000000; ++i) asm ("");
    }
}

extern "C" void SystemInit () {}
