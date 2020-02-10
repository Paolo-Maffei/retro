#include "syslib.h"

int main () {
    //write(1, "boot:\n", 6);
    if (demo(22,33,44,55) != 22 + 33 + 44 + 55)
        write(1, "demo??\n", 7);

    // fork a new task, also in flash memory, for some additional experiments
    int* task = (int*) 0x08008000;
    
    return twait(tfork((void*) task[0], (void (*)(void*)) task[1], 0));
}
