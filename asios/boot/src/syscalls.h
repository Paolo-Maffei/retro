//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector. These always run in SVC context.

enum {
    SYSCALL_noop = 3,
    SYSCALL_demo,
    SYSCALL_exit_, // name clash
    SYSCALL_gpio,
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
