#include "syslib.h"

int main () {
    //write(1, "boot:\n", 6);
    if (demo(22,33,44,55) != 22 + 33 + 44 + 55)
        write(1, "demo??\n", 7);
    exit_(0);
}
