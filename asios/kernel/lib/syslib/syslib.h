//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector. These always run in SVC context.

enum {
    SYSCALL_ipcSend,
    SYSCALL_ipcCall,
    SYSCALL_ipcRecv,
    //SYSCALL_ipcPass,
    SYSCALL_noop,
    SYSCALL_demo,
    SYSCALL_exit_, // name clash
    SYSCALL_gpio,
    SYSCALL_write,
    SYSCALL_read,
    SYSCALL_ioctl,
    SYSCALL_diskio,
    SYSCALL_MAX
};

typedef int Message [8];

// helper to define system call stubs (up to 4 typed args, returning int)
#ifndef DEFINE_SYSCALLS
#define SYSCALL_STUB(name, args) extern int name args;
#else
// TODO find a way to inline asm code, "svc #n" is shorter & faster than a call
// the problem is that gcc fails to set up the arg regs when inlining is forced
#define SYSCALL_STUB(name, args) \
    __attribute__((naked)) int name args \
    { asm volatile ("svc %0; bx lr" :: "i" (SYSCALL_ ## name)); }
#endif

// these are all the system call stubs
SYSCALL_STUB(ipcSend, (int dst, Message* msg))
SYSCALL_STUB(ipcCall, (int dst, Message* msg))
SYSCALL_STUB(ipcRecv, (Message* msg))
//SYSCALL_STUB(ipcPass, (int dst, Message* msg))
SYSCALL_STUB(noop, (void))
SYSCALL_STUB(demo, (int a, int b, int c, int d))
SYSCALL_STUB(exit_, (int e))
SYSCALL_STUB(gpio, (int gpioPin, int gpioCmd))
SYSCALL_STUB(write, (int fd, void const* ptr, int len))
SYSCALL_STUB(read, (int fd, void* ptr, int len))
SYSCALL_STUB(ioctl, (int fd, int req, ...))
SYSCALL_STUB(diskio, (int rw, int pos, void* buf, int cnt))
