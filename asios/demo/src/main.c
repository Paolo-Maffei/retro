#include "syscalls.h"

int main () {
    char const* s = "<@>";
    while (1) {
        write(1, s, 3);
        for (int i = 0; i < 30000000; ++i) asm ("");
    }
}
