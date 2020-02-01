#include "syscalls.h"

int main () {
    gpio(0xA7, 0); // PinA<7>::mode(Pinmode::out)

    while (1) {
        gpio(0xA7, 1); // PinA<7>::clear()
        for (int i = 0; i < 10000000; ++i) asm ("");
        gpio(0xA7, 2); // PinA<7>::set()
        for (int i = 0; i < 10000000; ++i) asm ("");
    }
}

void SystemInit (void) {}
