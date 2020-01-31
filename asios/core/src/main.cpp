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
    asm volatile ("cpsid if"); // disable interrupts and faults
    kputs("\n*** panic: "); kputs(msg); kputs(" ***\n");
    while (true) {} // hang
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

// XXX asm below assumes nextTask is placed just after currTask in memory
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

#if 1
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
    uint8_t req;
    uint32_t* args;
    int filler [14];
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tasks and task management.

class Task {
public:
    uint32_t* pspSaved; // MUST be first in task objects, see PendSV_Handler
    uint32_t* mpuMaps;  // MUST be second in task objects, see PendSV_Handler
    Task* blocking;     // set while waiting, to task we're queued on, or self
    Task* pendingQueue; // tasks waiting for their call to be accepted
    Task* finishQueue;  // tasks waiting for their call to be completed
    Message* msgBuf;    // set while recv or reply can take a new message
    Task* next;         // used in waiting tasks, i.e. when on some linked list

    enum { Early, App, Server, Driver }; // type of task
    uint8_t type :2;    // set once the first SVC call is made

    uint8_t spare[3];   // pad the total task size to 32 bytes

    static constexpr int MAX_TASKS = 25;
    static Task vec [MAX_TASKS];

    void init (void* stackTop, void (*func)()) {
        // use the C++11 compiler to verify some design choices
        static_assert(sizeof (Message) == 64); // fixed and known message size
        static_assert((sizeof *this & (sizeof *this - 1)) == 0); // power of 2

        extern int exit (int); // forward declaration XXX yuck

        HardwareStackFrame* psp = (HardwareStackFrame*) stackTop - 1;
        psp->lr = (uint32_t) exit;
        psp->pc = (uint32_t) func;
        psp->psr = 0x01000000;
        pspSaved = (uint32_t*) psp - PSP_EXTRA;
        static const uint32_t dummyMaps [4] = {}; // two disabled regions
        mpuMaps = (uint32_t*) dummyMaps;
    }

    static void run () {
        startTasks(vec); // never returns
    }

    static Task& current () { return *(Task*) currTask; }

    static Task* nextRunnable () {
        Task* tp = &current();
        do
            if (++tp - vec >= MAX_TASKS)
                tp = vec;
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
    uint32_t index () const { return this - vec; }

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
};

Task Task::vec [];

// a crude task dump for basic debugging, using console & printf
void Task::dump () {
    for (int i = 0; i < MAX_TASKS; ++i) {
        Task& t = Task::vec[i];
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
// Main entry point: set up the system task (task #0) and start multi-tasking.
// Note that main knows nothing about system calls, MPU, SVC, or IRQ handlers.

extern void systemTask ();
static VTable* irqVec; // without static it crashes FIXME stray mem corruption?

int main () {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    setupFaultHandlers();
    wait_ms(200); // give platformio's console time to connect

    irqVec = &VTableRam(); // this call can't be used in thread mode XXX yuck

    // set up the stack and initialize the very first task
    alignas(8) static uint8_t stack_0 [256];
    Task::vec[0].init(stack_0 + sizeof stack_0, systemTask);

    Task::run(); // the big leap into unprivileged thread mode, never returns
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector. These always run in SVC context.

enum {
    SYSCALL_ipcSend,
    SYSCALL_ipcCall,
    SYSCALL_ipcRecv,
    SYSCALL_noop,
    SYSCALL_demo,
    SYSCALL_exit,
    SYSCALL_MAX
};

// helper to define system call stubs (up to 4 typed args, returning int)
#define SYSCALL_STUB(name, args) \
    __attribute__((naked)) int name args \
    { asm volatile ("svc %0; bx lr" :: "i" (SYSCALL_ ## name)); }

// these are all the system call stubs
SYSCALL_STUB(ipcSend, (int dst, Message* msg))
SYSCALL_STUB(ipcCall, (int dst, Message* msg))
SYSCALL_STUB(ipcRecv, (Message* msg))
SYSCALL_STUB(noop, ())
SYSCALL_STUB(demo, (int a, int b, int c, int d))
SYSCALL_STUB(exit, (int e))

// TODO move everything up to the above enum to a C header for use in tasks

// non-blocking message send, behaves as atomic test-and-set
static void do_ipcSend (HardwareStackFrame* sfp) {
    int dst = sfp->r[0];
    Message* msg = (Message*) sfp->r[1];
    // ... validate dst and msg
    sfp->r[0] = Task::vec[dst].deliver(msg);
}

// blocking send + receive, used for request/reply sequences
static void do_ipcCall (HardwareStackFrame* sfp) {
    int dst = sfp->r[0];
    Message* msg = (Message*) sfp->r[1];
    // ... validate dst and msg
    sfp->r[0] = Task::vec[dst].replyTo(msg);
}

// blocking receive, used by drivers and servers
static void do_ipcRecv (HardwareStackFrame* sfp) {
    Message* msg = (Message*) sfp->r[0];
    // ... validate msg
    sfp->r[0] = Task::current().listen(msg);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SVC system call interface, required to switch from thread to handler state.
// Only an IPC call is processed right away, the rest is sent to task #0.
//
// During a system call, only *part* of the context is saved (r0..psr), the
// remaining context (i.e. R4-R11 and optional FP regs) need to be preserved.
// This is ok, as context switches only happen in PendSV, i.e. outside SVCs.

void SVC_Handler () {
    HardwareStackFrame* psp;
    asm volatile ("mrs %0, psp" : "=r" (psp));
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

    // fully-automated task categorisation: the first SVC request made by a
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
    // done in the ipc calls and in task #0, which handles all other calls

    switch (req) {
        case SYSCALL_ipcSend: do_ipcSend(psp); break;
        case SYSCALL_ipcCall: do_ipcCall(psp); break;
        case SYSCALL_ipcRecv: do_ipcRecv(psp); break;

        default: { // wrap into an ipcCall to task #0
            // msg can be on the stack, because task #0 always accepts 'em now
            Message sysMsg;
            sysMsg.req = req;
            sysMsg.args = psp->r;
            (void) Task::vec[0].replyTo(&sysMsg); // XXX explain void
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call routing table. All 256 entries can be adjusted dynamically.
// Defines how incoming messages are handled, most are checked and forwarded.

struct SysRoute {
    uint8_t task, num, info, format;

    void set (int t, int n, int i =0, int f =0) {
        task = t; num = n; info = i; format = f;
    }
};

SysRoute routes [256]; // can be indexed by any uint8_t value, i.e. SVC number

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Task zero, the special system task. It's the only one started by main().

#if 1
// just a quick hack to force a context switch on the next 1000 Hz clock tick
// it causes a continous race of task switches, since each waiting task yields
// ... but it does have the desired effect of wait_ms letting other tasks run
// still some delays, as each running task in the cycle consumes a clock tick
// note: changeTask can't be called from unprivileged code, i.e. thread mode
//
// TODO solution is to suspend delaying tasks so they don't eat up cpu cycles
// but this needs some new code, to manage multiple timers and queues for them
bool yield;

// use a different name to avoid clashing with JeeH's "wait_ms" version
void msWait (uint32_t ms) {
    uint32_t start = ticks;
    while ((uint32_t) (ticks - start) < ms) {
        yield = true;
        __asm("wfi");  // reduce power consumption
    }
}
#endif

// call the given function while briefly switched into privileged handler mode
// note that for this to be robust, the system task must never be preempted
void runPrivileged (void (*fun)()) {
    irqVec->sv_call = fun;
    asm volatile ("svc #0");
    irqVec->sv_call = SVC_Handler;
}

void systemTask () {
    // This is task #0, running in thread mode. Since the MPU has not yet been
    // enabled and we have full R/W access to the interrupt vector in RAM, we
    // can still get to privileged mode by installing an exception handler and
    // then triggering it (i.e. through a replaced SVC call). This is used here
    // to fix up a few details which can't be done in unprivileged mode.

    runPrivileged([] {
        // lower the priority level of SVCs: this allows handling SVC requests
        // from thread state, but not from exception handlers (an SVC call which
        // can't be serviced right away will generate a hard fault), so now
        // kernel code can be interrupted by most handlers other than PendSV
        MMIO8(0xE000ED1F) = 0xFF; // SHPR2->PRI_11 = 0xFF

        // TODO set up all MPU regions and enable the MPU's memory protection
    });

    irqVec->systick = []() {
        ++ticks;
        if (yield || ticks % 20 == 0) // switch tasks every 20 ms
            if (&Task::current() != Task::vec) // unless in system task
                changeTask(Task::nextRunnable());
        yield = false;
    };

#include "test_tasks.h"

    // these requests are handled by this system task
    routes[SYSCALL_noop].set(0, 0); // same as all the default entries
    routes[SYSCALL_demo].set(0, 1);
    routes[SYSCALL_exit].set(0, 2);

    while (true) {
        static Message sysMsg; // must be static, else it usage-faults TODO ?
        int src = ipcRecv(&sysMsg);

        int req = sysMsg.req;
        uint32_t* args = sysMsg.args;
        bool wantsReply = Task::vec[src].blocking == &Task::current();
        printf("%d 0: ipc %s req %d from %d\n",
                ticks, wantsReply ? "CALL" : "SEND", req, src);

        SysRoute& sr = routes[(uint8_t) req]; // index is never out of range
        if (sr.task != 0)
            ; // TODO needs to be forwarded
        else
            switch (sr.num) {
                case 1: { // demo
                    printf("<demo %d %d %d %d>\n",
                            args[0], args[1], args[2], args[3]);
                    int result = args[0] + args[1] + args[2] + args[3];
                    /*int e =*/ ipcSend(src, &sysMsg);
                    //printf("%d 0: replied to #%d with %d status %d\n",
                    //        ticks, src, result, e);
                    args[0] = result; // replace resume's result with actual one
                    break;
                }

                case 2: // exit
                    break; // TODO task is kept waiting forever, must clean up

                default:
                    printf("%d 0: sysroute (0,%d) ?\n", ticks, sr.num);
            }
    }
}
