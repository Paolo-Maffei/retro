//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector. These always run in SVC context.

enum {
    SYSCALL_noop = 3,
    SYSCALL_demo,
    SYSCALL_exit_, // name clash
    SYSCALL_gpio,
    SYSCALL_write,
    SYSCALL_MAX
};

// helper to define system call stubs (up to 4 typed args, returning int)
#define SYSCALL_STUB(name, args) \
    __attribute__((naked)) int name args \
    { asm volatile ("svc %0; bx lr" :: "i" (SYSCALL_ ## name)); }

// these are all the system call stubs
SYSCALL_STUB(noop, (void))
SYSCALL_STUB(demo, (int a, int b, int c, int d))
SYSCALL_STUB(exit_, (int e))
SYSCALL_STUB(gpio, (int gpioPin, int gpioCmd))
SYSCALL_STUB(write, (int fd, void const* ptr, int len))

// small recplacement for the boot vector, since dispatch is now in the kernel
extern char _estack[], Reset_Handler[];
__attribute__ ((section(".boot_vector")))
char* bootVector[] = { _estack, Reset_Handler };

// disabled all privileged configuration code
void SystemInit (void) {}
