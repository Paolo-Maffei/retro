#include <jee.h>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Console device and exception handler debugging.

UartBufDev< PinA<9>, PinA<10> > console;

int printf (char const* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

// print a string on the polled uart, can be used with interrupts disabled
// ... but the elapsed time waiting for the uart will slow things down a lot
void kputs (char const* msg) {
    auto& polled = (decltype(console)::base&) console;
    while (*msg)
        polled.putc(*msg++);
}

// give up, but not before trying to send a final message to the console port
void panic (char const* msg) {
    asm ("cpsid if"); // disable interrupts and faults
    kputs("\n*** panic: "); kputs(msg); kputs(" ***\n");
    while (1) {} // hang
}

// set up and enable the main fault handlers
void setupFaultHandlers () {
    VTableRam().hard_fault          = []() { panic("hard fault"); };
    VTableRam().memory_manage_fault = []() { panic("mem fault"); };
    VTableRam().bus_fault           = []() { panic("bus fault"); };
    VTableRam().usage_fault         = []() { panic("usage fault"); };
    MMIO32(0xE000ED24) |= 0b111<<16; // SCB->SHCSR |= (USG|BUS|MEM)FAULTENA
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Task switcher, adapted from a superb example in Joseph Yiu's book, ch. 10:
// "The Definitive Guide to Arm Cortex M3 and M4", 3rd edition, 2014.

constexpr int MAX_TASKS = 10;
uint32_t* pspVec[MAX_TASKS]; // Process Stack Pointers for each runnable task
uint32_t currTask, nextTask; // index of current and next tasks

struct HardwareStackFrame {
    uint32_t r[4], r12, lr, pc, psr;
};

void initTask (int index, void* stackTop, void (*func)()) {
    HardwareStackFrame* psp = (HardwareStackFrame*) stackTop - 1;
    psp->pc = (uint32_t) func;
    psp->psr = 0x01000000;
    pspVec[index] = (uint32_t*) psp - 8; // room for the extra registers
}

// context switcher, no floating point support
void PendSV_Handler () {
    asm volatile ("\
        // save current context \n\
        mrs    r0, psp      // get current process stack pointer value \n\
        stmdb  r0!,{r4-r11} // save R4 to R11 in task stack (8 regs) \n\
        ldr    r1,=currTask \n\
        ldr    r2,[r1]      // get current task ID \n\
        ldr    r3,=pspVec \n\
        str    r0,[r3, r2, lsl #2] // save PSP value into pspVec \n\
        // load next context \n\
        ldr    r4,=nextTask \n\
        ldr    r4,[r4]      // get next task ID \n\
        str    r4,[r1]      // set currTask = nextTask \n\
        ldr    r0,[r3, r4, lsl #2] // Load PSP value from pspVec \n\
        ldmia  r0!,{r4-r11} // load R4 to R11 from task stack (8 regs) \n\
        msr    psp, r0      // set PSP to next task \n\
    ");
}

void startTasks () {
    // prepare to run all tasks in unprivileged thread mode, with one PSP
    // stack per task, keeping the original main stack for MSP/handler use
    HardwareStackFrame* psp = (HardwareStackFrame*) (pspVec[currTask] + 8);
    asm volatile ("msr psp, %0\n" :: "r" (psp + 1));

    // PendSV will be used to switch stacks, at the lowest interrupt priority
    MMIO8(0xE000ED22) = 0xFF; // SHPR3->PRI_14 = 0xFF
    VTableRam().pend_sv = PendSV_Handler;

    asm volatile ("msr control, %0; isb" :: "r" (3)); // to unprivileged mode

    // launch main task, running in unprivileged thread mode from now on
    ((void (*)()) psp->pc)();
    panic("main task exit");
}

void changeTask (uint32_t index) {
    // trigger a PendSV when back in thread mode to switch tasks
    nextTask = index;
    if (index != currTask)
        MMIO32(0xE000ED04) |= 1<<28; // SCB->ICSR |= PENDSVSET
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Generic zero-terminated list handling, all it needs is a "next" field.

// copy a value (of any type), setting the original to zero
template< typename T >
T grab (T& x) {
    T r = x;
    x = 0;
    return r;
}

// append an item to the end of a list
template< typename T >
void listAppend (T*& list, T* item) {
    item->next = 0; // just to be safe
    while (list)
        list = list->next;
    list = item;
}

// remove an item from a list
template< typename T >
bool listRemove (T*& list, T* item) {
    while (list != item)
        if (list)
            list = list->next;
        else
            return false;
    list = grab(item->next);
    return true;
}

// take the first item off a list, returns null if none
template< typename T >
T* listTakeFirst (T*& list) {
    T* item = list;
    if (item)
        list = grab(item->next);
    return item;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Error numbers

enum {
    EOK,
    ENOSYS,
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Message-based IPC.

struct Message {
    int request;
    int filler [15];
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tasks (with same index as pspVec) and task management.

class Task {
public:
    static Task& index (int num) { return taskVec[num]; }
    static Task& current () { return taskVec[currTask]; }

    static int nextRunnable () {
        int tidx = currTask;
        do
            tidx = (tidx + 1) % MAX_TASKS;
        while (index(tidx).state() < Runnable);
        return tidx;
    }

    // try to deliver a message to this task
    int deliver (int reply, Message* msg) {
        if (blocking && blocking != this) // is it waiting on another task?
            if (!listRemove(blocking->finishQueue, this)) // try to get removed
                return -1; // waiting on something else, reject this delivery

        if (recvBuf == 0) // is everything ready to receive a message?
            return -1; // sorry, can't accept this delivery

        *grab(recvBuf) = *msg;
        resume(reply);
        return 0;
    }

    // deal with an incoming message which expects a reply
    int replyTo (int src, Message* msg) {
        Task& sender = index(src);
        int e = deliver(src, msg);
        if (e < 0) // not in receive mode, will receive later
            listAppend(pendingQueue, &sender);
        else { // it has been received, queue up to wait for reply
            listAppend(finishQueue, &sender);
            sender.recvBuf = msg;
        }
        sender.suspend(this);
        return -1; // suspended, this reply will be adjusted in resume()
    }

    int listen (int flags, Message* msg) {
        Task* sender = listTakeFirst(pendingQueue);
        if (sender != 0) {
            printf("R from %d ok %d\n", sender->index(), index());
            listAppend(finishQueue, sender);
            return sender->index();
        }
        recvBuf = msg;
        suspend(this);
        return -1; // suspended, this reply will be adjusted in resume()
    }

    Task* next;     // used in suspended tasks, i.e. when on some linked list
private:
    Task* blocking; // set while suspended, to the task where we're queued
    Task* pendingQueue; // tasks waiting for their call to be accepted
    Task* finishQueue;  // tasks waiting for their call to be completed
    Message* recvBuf; // set while recv is waiting for a new message

    uint32_t index () const { return this - taskVec; }

    enum State { Unused, Suspended, Waiting, Runnable, Active };

    State state () const {
        uint32_t tidx = index();
        return pspVec[tidx] == 0 ? Unused :     // task is not in use
                blocking == this ? Suspended :  // has to be resumed explicitly
                   blocking != 0 ? Waiting :    // waiting on another task
                tidx != currTask ? Runnable :   // will run when scheduled
                                   Active;      // currently running
    }

    HardwareStackFrame& context () const {
        return *(HardwareStackFrame*) (pspVec[index()] + 8);
    }

    void suspend (Task* reason) {
        if (index() != currTask)
            printf(">>> SUSPEND? index %d != curr %d\n", index(), currTask);
        if (index() == currTask) {
            nextTask = nextRunnable();
            if (nextTask == currTask)
                panic("no runnable tasks left");
            changeTask(nextTask); // will trigger PendSV tail-chaining
        }
        printf("susp %d curr %d next %d\n", index(), currTask, nextTask);
        blocking = reason;
    }

    void resume (int result) {
        context().r[0] = result;
        blocking = 0;
    }

    static Task taskVec [];
};

Task Task::taskVec [MAX_TASKS];

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector.

enum {
    SYSCALL_ipcSend,
    SYSCALL_ipcCall,
    SYSCALL_ipcRecv,
    SYSCALL_demo,
    SYSCALL_MAX
};

// helper to define system call stubs with up to 4 typed arguments
#define SYSCALL_STUB(name, args) \
    __attribute__((naked)) int name args \
    { asm ("svc %0; bx lr" :: "i" (SYSCALL_ ## name)); }

// these are all the system call stubs
SYSCALL_STUB(ipcSend, (int dst, Message* msg))
SYSCALL_STUB(ipcCall, (int dst, Message* msg))
SYSCALL_STUB(ipcRecv, (int flags, Message* msg))
SYSCALL_STUB(demo, (int a, int b, int c, int d))

// TODO move everything up to the above enum to a C header for use in tasks

// non-blocking message send, behaves as atomic test-and-set
int syscall_ipcSend (HardwareStackFrame* fp) {
    int dst = fp->r[0];
    Message* msg = (Message*) fp->r[1];
    // ... validate dst and msg
    return Task::index(dst).deliver(currTask, msg);
}

// blocking send + receive, used for request/reply sequences
int syscall_ipcCall (HardwareStackFrame* fp) {
    int dst = fp->r[0];
    Message* msg = (Message*) fp->r[1];
    // ... validate dst and msg
    return Task::index(dst).replyTo(currTask, msg);
}

// blocking receive, used by drivers and servers
int syscall_ipcRecv (HardwareStackFrame* fp) {
    int flags = fp->r[0];
    Message* msg = (Message*) fp->r[1];
    // ... validate flags and msg
    return Task::current().listen(flags, msg);
}

int syscall_demo (HardwareStackFrame* fp) {
    printf("< demo %d %d %d %d >\n", fp->r[0], fp->r[1], fp->r[2], fp->r[3]);
    return fp->r[0] + fp->r[1] + fp->r[2] + fp->r[3];
}

// these stubs must match the exact order and number of SYSCALL_* enums
int (*const syscallVec[])(HardwareStackFrame*) = {
    syscall_ipcSend,
    syscall_ipcCall,
    syscall_ipcRecv,
    syscall_demo,
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SVC system call interface, required to switch from thread to handler state.

void setupSystemCalls () {
    // use the compiler to verify that the syscall enum and vector size match
    static_assert(SYSCALL_MAX == sizeof syscallVec / sizeof *syscallVec);

    // allow SVC requests from thread state, but not from exception handlers
    // (a SVC which can't be serviced right away will generate a hard fault)
    // kernel code can now be interrupted by most handlers other than PendSV
    MMIO8(0xE000ED1F) = 0xFF; // SHPR2->PRI_11 = 0xFF

    VTableRam().sv_call = []() {
        HardwareStackFrame* psp;
        asm ("mrs %0, psp" : "=r" (psp));
        uint8_t req = ((uint8_t*) (psp->pc))[-2];
#if 0
        printf("< svc.%d psp %08x r0.%d lr %08x pc %08x psr %08x >\n",
                req, psp, psp->r[0], psp->lr, psp->pc, psp->psr);
#endif
        // make pspVec[currTask] "resemble" a stack with full context XXX why?
        // this is fiction, since r4..r11 (and fp regs) have *not* been saved
        // note that pspVec[currTask] is redundant, the real psp is what counts
        // now all valid pspVec entries have similar stack ptrs for kernel use
        pspVec[currTask] = (uint32_t*) psp - 8;

        psp->r[0] = req < SYSCALL_MAX ? syscallVec[req](psp) : -ENOSYS;
    };
}

/* During a system call, only *part* of the context is saved (r0..psr), the
   remaining context (i.e. r4..r11 and optional fp regs) need to be preserved.
   This is ok, as context switches only happen in PendSV, i.e. outside SVCs. */

__attribute__((naked)) // avoid warning about missing return value
int syscall (...) {
    asm volatile ("svc #0; bx lr");
}

// system calls using a compile-time configurable "SVC #N" (only works in C++)
template< int N >
__attribute__((naked)) // avoid warning about missing return value
int syscall (...) {
    asm volatile ("svc %0; bx lr" :: "i" (N));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main () {
    const auto hz = fullSpeedClock();
    console.init();
    console.baud(115200, hz/2);
    wait_ms(200); // give platformio's console time to connect

    setupFaultHandlers();
    setupSystemCalls();

    VTableRam().systick = []() {
        ++ticks;
        if (ticks % 20 == 0) // switch tasks every 20 ms
            changeTask(Task::nextRunnable());
    };

#define DEFINE_TASK(index, stacksize, body) \
    alignas(8) static uint8_t stack_##index [stacksize]; \
    initTask(index, stack_##index + stacksize, []() { body });

    DEFINE_TASK(0, 1000,
        PinA<6> led2;
        led2.mode(Pinmode::out);
        while (true) {
            printf("%d\n", ticks);
            led2 = 0; // inverted logic
            wait_ms(100);
            led2 = 1;
            wait_ms(900);
            int n = demo(1, 2, 3, 4);
            if (n != 1 + 2 + 3 + 4)
                printf("n? %d\n", n);
        }
    )

    DEFINE_TASK(1, 1000,
        PinA<7> led3;
        led3.mode(Pinmode::out);
        while (true) {
            led3 = 0; // inverted logic
            wait_ms(20);
            led3 = 1;
            wait_ms(260);
        }
    )

    DEFINE_TASK(2, 1000,
        wait_ms(1700);
        printf("2: start listening\n");
        while (1) {
            Message msg;
            int src = ipcRecv(0, &msg);
            printf("2: received #%d from %d\n", msg.request, src);
            if (src == 4) {
                wait_ms(1000);
                msg.request = -msg.request;
                int e = ipcSend(src, &msg);
                if (e != 0)
                    printf("2: reply? %d\n", e);
            }
        }
    )

    DEFINE_TASK(3, 1000,
        wait_ms(2000);
        Message msg;
        msg.request = 99;
        while (1) {
            printf("3: sending #%d\n", ++msg.request);
            int e = ipcSend(2, &msg);
            if (e != 0)
                printf("3: send? %d\n", e);
            wait_ms(1500);
        }
    )

    DEFINE_TASK(4, 1000,
        Message msg;
        msg.request = 9999;
        while (1) {
            wait_ms(5000);
            printf("4: calling #%d\n", ++msg.request);
            int r = ipcCall(2, &msg);
            printf("4: result %d\n", r);
        }
    )

    startTasks(); // never returns
}
