#include "syscalls.h"

int main () {
    gpio(0, 0xA7); // FIXME first syscall gets lost?
    gpio(0, 0xA7); // PinA<7>::mode(Pinmode::out)

    while (1) {
        gpio(1, 0xA7); // PinA<7>::clear()
        for (int i = 0; i < 3000000; ++i) asm ("");
        gpio(2, 0xA7); // PinA<7>::set()
        for (int i = 0; i < 3000000; ++i) asm ("");
    }
}
