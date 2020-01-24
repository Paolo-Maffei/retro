#include <stdint.h>

int main (void) {
    for (int i = 0; i < 5; ++i)
        __asm("svc #0");
    return 123;
}

extern char _etext, _edata, _ebss, _estack;

struct ExeHeader {
    uint32_t magic, reset;
    char *etext, *edata, *ebss, *estack;
    uint32_t aux1, aux2;
}; // header is 32 bytes

__attribute__ ((section(".exe_header")))
struct ExeHeader g_exeHeader = {
    0x12345678, (uint32_t) main,
    &_etext, &_edata, &_ebss, &_estack,
    0, 0,
};
