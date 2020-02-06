#define DEFINE_SYSCALLS
#include "syslib.h"

// small replacement for the boot vector, since dispatch is now in the kernel
extern char _estack[], Reset_Handler[];
__attribute__ ((section(".boot_vector")))
char* bootVector[] = { _estack, Reset_Handler };

// need to disable all privileged configuration code
void SystemInit (void) {}
