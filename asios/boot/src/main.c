#include "syscalls.h"

int main () {
    gpio(0, 0xA7); // FIXME first syscall gets lost?
    gpio(0, 0xA7); // PinA<7>::mode(Pinmode::out)

    while (1) {
        gpio(1, 0xA7); // PinA<7>::clear()
        for (int i = 0; i < 10000000; ++i) asm ("");
        gpio(2, 0xA7); // PinA<7>::set()
        for (int i = 0; i < 10000000; ++i) asm ("");
    }
}

// small recplacement for the boot vector, since we'll use a RAM copy anyway
extern char _estack[], Reset_Handler[];
__attribute__ ((section(".boot_vector")))
char* bootVector[] = { _estack, Reset_Handler };

void SystemInit (void) {}
