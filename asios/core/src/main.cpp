#include <jee.h>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// This stuff should probably be in JeeH, perhaps arch/stm32f4.h ?

// cycle counts, see https://stackoverflow.com/questions/11530593/

namespace Periph {
    constexpr uint32_t dwt = 0xE0001000;
}

struct DWT {
    constexpr static uint32_t ctrl   = Periph::dwt + 0x0;
    constexpr static uint32_t cyccnt = Periph::dwt + 0x4;

    static void start () { MMIO32(cyccnt) = 0; MMIO32(ctrl) |= 1<<0; }
    static void stop () { MMIO32(ctrl) &= ~(1<<0); }
    static uint32_t count () { return MMIO32(cyccnt); }
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Console device and exception handler debugging.

UartBufDev< PinA<9>, PinA<10>, 150 > console;

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

// Note: currently, R4 to R11 are saved on the stack, but they could also be
// stored in the task object, reducing PSP stack usage by 32 bytes. Not sure
// which is better, and there's still the FPU regs to deal with (128 bytes!).
constexpr int PSP_EXTRA = 8; // R4 to R11

struct HardwareStackFrame {
    uint32_t r[4], r12, lr, pc, psr;
};

// FIXME asm below assumes nextTask is placed just after currTask in memory
uint32_t **currTask, **nextTask; // ptrs for saved PSPs of current & next tasks

// context switcher, includes updating MPU maps, no floating point support
// this does not need to know about tasks, just pointers to its first 2 fields
void PendSV_Handler () {
    //DWT::start();
    asm volatile ("\
        // save current context \n\
        mrs    r0, psp      // get current process stack pointer value \n\
        stmdb  r0!,{r4-r11} // push R4 to R11 to task stack (8 regs) \n\
        ldr    r1,=currTask \n\
        ldr    r2,[r1]      // get current task ptr \n\
        str    r0,[r2]      // save PSP value into current task \n\
        // load next context \n\
        ldr    r4,[r1,#4]   // get next task ptr \n\
        str    r4,[r1]      // set currTask = nextTask \n\
        ldr    r0,[r4]      // load PSP value from next task \n\
        ldr    r1,[r4,#4]   // load pointer to MPU regions \n\
        ldm    r1,{r2-r5}   // load R2 to R5 for 2 MPU regions (4 regs) \n\
        ldr    r1,=0xE000ED9C // load address of first MPU RBAR reg \n\
        stm    r1,{r2-r5}   // store 2 new maps in MPU regs (4 regs) \n\
        ldmia  r0!,{r4-r11} // pop R4 to R11 from task stack (8 regs) \n\
        msr    psp, r0      // set PSP to next task \n\
    ");
    //DWT::stop();
}

void startTasks (void* firstTask) {
    currTask = nextTask = (uint32_t**) firstTask;

    // prepare to run all tasks in unprivileged thread mode, with one PSP
    // stack per task, keeping the original main stack for MSP/handler use
    HardwareStackFrame* psp = (HardwareStackFrame*) (*currTask + PSP_EXTRA);
    asm volatile ("msr psp, %0\n" :: "r" (psp + 1));

    // PendSV will be used to switch stacks, at the lowest interrupt priority
    MMIO8(0xE000ED22) = 0xFF; // SHPR3->PRI_14 = 0xFF
    VTableRam().pend_sv = PendSV_Handler;

#if 0
    asm volatile ("msr control, %0; isb" :: "r" (3)); // to unprivileged mode
#else
    asm volatile ("msr control, %0; isb" :: "r" (2)); // keep privileged mode
#endif

    // launch main task, running in unprivileged thread mode from now on
    ((void (*)()) psp->pc)();
    panic("main task exit");
}

void changeTask (void* next) {
    // trigger a PendSV when back in thread mode to switch tasks
    nextTask = (uint32_t**) next;
    if (nextTask != currTask)
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
void listAppend (T*& list, T& item) {
    item.next = 0; // just to be safe
    while (list)
        list = list->next;
    list = &item;
}

// remove an item from a list
template< typename T >
bool listRemove (T*& list, T& item) {
    while (list != &item)
        if (list)
            list = list->next;
        else
            return false;
    list = grab(item.next);
    return true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Message-based IPC.

struct Message {
    int request;
    int filler [15];
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tasks and task management.

class Task {
    uint32_t* pspSaved; // MUST be first in task objects, see PendSV_Handler
    uint32_t* mpuMaps;  // MUST be second in task objects, see PendSV_Handler
    Task* blocking;     // set while waiting, to task we're queued on, or self
    Task* pendingQueue; // tasks waiting for their call to be accepted
    Task* finishQueue;  // tasks waiting for their call to be completed
    Message* msgBuf;    // set while recv or reply can take a new message
    uint8_t spare[3];   // pad the total task size to 32 bytes
public:
    enum { Early, App, Server, Driver }; // type of task
    uint8_t type :2;    // set once the first SVC call is made

    Task* next;         // used in waiting tasks, i.e. when on some linked list

    static constexpr int MAX_TASKS = 25;

    void init (void* stackTop, void (*func)()) {
        HardwareStackFrame* psp = (HardwareStackFrame*) stackTop - 1;
        psp->pc = (uint32_t) func;
        psp->psr = 0x01000000;
        pspSaved = (uint32_t*) psp - PSP_EXTRA;
        static const uint32_t dummyMaps [4] = {}; // two disabled regions
        mpuMaps = (uint32_t*) dummyMaps;
    }

    static void run () {
        startTasks(taskVec); // never returns
    }

    static Task& index (int num) { return taskVec[num]; }
    static Task& current () { return *(Task*) currTask; }

    static Task* nextRunnable () {
        Task* tp = &current();
        do
            if (++tp - taskVec >= MAX_TASKS)
                tp = taskVec;
        while (tp->state() < Runnable);
        return tp;
    }

    // try to deliver a message to this task
    int deliver (Message* msg) {
        bool waitingForMe = false;
        if (blocking && blocking != this) { // is it waiting for completion?
            waitingForMe = listRemove(blocking->finishQueue, *this);
            if (!waitingForMe)
                return -1; // waiting on something else, reject this delivery
        }

        if (msgBuf == 0) // is this task ready to receive a message?
            return -1; // nope, can't deliver this message

        *grab(msgBuf) = *msg; // copy message to destination
        resume(waitingForMe ? 0 : current().index()); // if a reply, return 0
        return 0; // successful delivery
    }

    // deal with an incoming message which expects a reply
    int replyTo (Message* msg) {
        Task& sender = current();
        int e = deliver(msg);
        // either try delivery again later, or wait for reply
        listAppend(e < 0 ? pendingQueue : finishQueue, sender);
        sender.msgBuf = msg;
        return sender.suspend(this);
    }

    // listen for incoming messages, block each sender while handling calls
    int listen (Message* msg) {
        if (pendingQueue != 0) {
            Task& sender = *pendingQueue;
            pendingQueue = grab(pendingQueue->next);
            listAppend(finishQueue, sender);
            *msg = *sender.msgBuf; // copy message to this receiver
            return sender.index();
        }
        msgBuf = msg;
        return suspend(this);
    }

    static void dump ();

private:
    uint32_t index () const { return this - taskVec; }

    enum State { Unused, Suspended, Waiting, Runnable, Active };

    State state () const {
        return pspSaved == 0 ? Unused :     // task is not in use
            blocking == this ? Suspended :  // has to be resumed explicitly
                    blocking ? Waiting :    // waiting on another task
          this != &current() ? Runnable :   // will run when scheduled
                               Active;      // currently running
    }

    HardwareStackFrame& context () const {
        return *(HardwareStackFrame*) (pspSaved + PSP_EXTRA);
    }

    int suspend (Task* reason) {
        if (currTask != &pspSaved) // could be supported, but no need so far
            printf(">>> SUSPEND? %08x != curr %08x\n", this, currTask);

        void* nextTask = nextRunnable();
        if (nextTask == currTask)
            panic("no runnable tasks left");
        changeTask(nextTask); // will trigger PendSV tail-chaining

        blocking = reason;
        return -1; // suspended, this result will be adjusted in resume()
    }

    void resume (int result) {
        context().r[0] = result; // save result inside calling context
        blocking = 0; // then allow it to run again
    }

    static Task taskVec [];
};

Task Task::taskVec [MAX_TASKS];

// a crude task dump for basic debugging, using console & printf
void Task::dump () {
    for (int i = 0; i < MAX_TASKS; ++i) {
        Task& t = Task::index(i);
        if (t.pspSaved) {
            printf("[%03x] %2d: %c%c sp %08x", (uint32_t) &t & 0xFFF,
                    i, " *<~"[t.type], "USWRA"[t.state()], t.pspSaved);
            printf(" blkg %2d pend %08x fini %08x mbuf %08x\n",
                    t.blocking == 0 ? -1 : t.blocking->index(),
                    t.pendingQueue, t.finishQueue, t.msgBuf);
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector. These always run in SVC context.

enum {
    SYSCALL_ipcSend,
    SYSCALL_ipcCall,
    SYSCALL_ipcRecv,
    SYSCALL_noop,
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
SYSCALL_STUB(ipcRecv, (Message* msg))
SYSCALL_STUB(noop, ())
SYSCALL_STUB(demo, (int a, int b, int c, int d))

// TODO move everything up to the above enum to a C header for use in tasks

// non-blocking message send, behaves as atomic test-and-set
int syscall_ipcSend (HardwareStackFrame* fp) {
    int dst = fp->r[0];
    Message* msg = (Message*) fp->r[1];
    // ... validate dst and msg
    return Task::index(dst).deliver(msg);
}

// blocking send + receive, used for request/reply sequences
int syscall_ipcCall (HardwareStackFrame* fp) {
    int dst = fp->r[0];
    Message* msg = (Message*) fp->r[1];
    // ... validate dst and msg
    return Task::index(dst).replyTo(msg);
}

// blocking receive, used by drivers and servers
int syscall_ipcRecv (HardwareStackFrame* fp) {
    Message* msg = (Message*) fp->r[0];
    // ... validate msg
    return Task::current().listen(msg);
}

// returns immediately, only used for timing tests
int syscall_noop (HardwareStackFrame* fp) {
    return 0;
}

// test syscall to check that args + return values are properly transferred
int syscall_demo (HardwareStackFrame* fp) {
    //printf("%d cycles\n", DWT::count());
    printf("< demo %d %d %d %d >\n", fp->r[0], fp->r[1], fp->r[2], fp->r[3]);
    return fp->r[0] + fp->r[1] + fp->r[2] + fp->r[3];
}

// these stubs must match the exact order and number of SYSCALL_* enums
int (*const syscallVec[])(HardwareStackFrame*) = {
    syscall_ipcSend,
    syscall_ipcCall,
    syscall_ipcRecv,
    syscall_noop,
    syscall_demo,
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SVC system call interface, required to switch from thread to handler state.

void setupSystemCalls () {
    // use the C++11 compiler to verify a few design invariants
    static_assert(SYSCALL_MAX == sizeof syscallVec / sizeof *syscallVec);
    static_assert((sizeof (Task) & (sizeof (Task) - 1)) == 0); // power of 2

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
        // first of all, make *currTask "resemble" a stack with full context
        // this is fiction, since R4-R11 (and FP regs) have *not* been saved
        // note that *currTask is not authoritative, h/w uses the real psp
        // now all valid task entries have similar stack ptrs for kernel use
        *currTask = (uint32_t*) psp - PSP_EXTRA;

        // fully-automated task categorisation: the first SVC call made by a
        // task will configure its type, and therefore its system permissions
        Task& t = Task::current();
        if (t.type == Task::Early)
            switch (req) {
                case SYSCALL_ipcSend: break; // no change
                case SYSCALL_ipcCall: t.type = Task::Driver; break;
                case SYSCALL_ipcRecv: t.type = Task::Server; break;
                default: t.type = Task::App; break;
            }

        // there's no need to enforce these permissions right now, that can be
        // done in the ipc calls and in task #0, which handles all system calls

        psp->r[0] = req < SYSCALL_MAX ? syscallVec[req](psp) : -1;
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

// just a quick hack to force a context switch on the next 1000 Hz clock tick
// it causes a continous race of task switches, since each waiting task yields
// ... but it does have the desired effect of wait_ms letting other tasks run
// still some delays, as each running task in the cycle consumes a clock tick
// note: changeTask can't be called from unprivileged code, i.e. thread mode
//
// TODO solution is to suspend delaying tasks so they don't eat up cpu cycles
// but this needs some new code, to manage multiple timers and queues for them

bool yield;

#define wait_ms myWait // rename to avoid clashing with JeeH's version
void wait_ms (uint32_t ms) {
    uint32_t start = ticks;
    while ((uint32_t) (ticks - start) < ms) {
        yield = true;
        __asm("wfi");  // reduce power consumption
    }
}

int main () {
    const auto hz = fullSpeedClock();
    console.init();
    console.baud(115200, hz/2);
    wait_ms(200); // give platformio's console time to connect

    setupFaultHandlers();
    setupSystemCalls();

    VTableRam().systick = []() {
        ++ticks;
        if (yield || ticks % 20 == 0) // switch tasks every 20 ms
            changeTask(Task::nextRunnable());
        yield = false;
    };

#define DEFINE_TASK(num, stacksize, body) \
    alignas(8) static uint8_t stack_##num [stacksize]; \
    Task::index(num).init(stack_##num + stacksize, []() { body });

    DEFINE_TASK(0, 256,
        PinA<6> led2;
        led2.mode(Pinmode::out);
        while (true) {
            printf("%d\n", ticks);
            led2 = 0; // inverted logic
            wait_ms(50);
            led2 = 1;
            wait_ms(950);
            int n = demo(1, 2, 3, 4);
            if (n != 1 + 2 + 3 + 4)
                printf("%d n? %d\n", ticks, n);
        }
    )

    DEFINE_TASK(1, 256,
        PinA<7> led3;
        led3.mode(Pinmode::out);
        while (true) {
            led3 = 0; // inverted logic
            wait_ms(140);
            led3 = 1;
            wait_ms(140);
        }
    )

    DEFINE_TASK(2, 256,
        wait_ms(1700);
        printf("%d 2: start listening\n", ticks);
        while (true) {
            Message msg;
            int src = ipcRecv(&msg);
            printf("%d 2: received #%d from %d\n", ticks, msg.request, src);
            if (src == 4) {
                wait_ms(50);
                msg.request = -msg.request;
                printf("%d 2: about to reply #%d\n", ticks, msg.request);
                int e = ipcSend(src, &msg);
                if (e != 0)
                    printf("%d 2: reply? %d\n", ticks, e);
            }
        }
    )

    DEFINE_TASK(3, 256,
        Message msg;
        msg.request = 99;
        while (true) {
            wait_ms(1500);
            printf("%d 3: sending #%d\n", ticks, ++msg.request);
#if 0
            int e = ipcSend(2, &msg);
#else
            DWT::start(); // bus faults unless in priviliged mode
            int e = ipcSend(2, &msg);
            DWT::stop();
            printf("%d 3: send %d cycles\n", ticks, DWT::count());

            DWT::start(); // bus faults unless in priviliged mode
            noop();
            DWT::stop();
            printf("%d 3: noop %d cycles\n", ticks, DWT::count());
#endif
            if (e != 0)
                printf("%d 3: send? %d\n", ticks, e);
        }
    )

    DEFINE_TASK(4, 256,
        Message msg;
        msg.request = 999;
        while (true) {
            wait_ms(4000);
            printf("%d 4: calling #%d\n", ticks, ++msg.request);
            int e = ipcCall(2, &msg);
            printf("%d 4: result #%d status %d\n", ticks, msg.request, e);
        }
    )

    DEFINE_TASK(5, 256,
        while (true) {
            wait_ms(4321);
            Task::dump();
        }
    )

    Task::run(); // never returns
}
