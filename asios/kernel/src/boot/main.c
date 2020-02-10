#include "syslib.h"

int main () {
    //write(1, "boot:\n", 6);
    if (demo(22,33,44,55) != 22 + 33 + 44 + 55)
        write(1, "demo??\n", 7);

    // fork a new task, also in flash memory, for some additional experiments
    int* vec = (int*) 0x08008000;
    tfork((void*) vec[0], (void (*)(void*)) vec[1], 0);

    texit(0);
}
