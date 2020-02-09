#include <jee.h>

#include <string.h>
#include "flashwear.h" // TODO this probably shouldn't be in the kernel
FlashWear disk;

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

// use a struct to force these two variables next to each other in memory
struct {
    uint32_t** curr; // current task pointer, MUST be first
    uint32_t** next; // next task to schedule, MUST be second
} pspSw;

// context switcher, includes updating MPU maps, no floating point support
// this doesn't need to know about tasks, just a pointer to its first 2 fields
void PendSV_Handler () {
    //DWT::start();
    asm volatile ("\
        // save current context \n\
        mrs    r0, psp      // get current process stack pointer value \n\
        stmdb  r0!,{r4-r11} // push R4 to R11 to task stack (8 regs) \n\
        ldr    r1,=pspSw \n\
        ldr    r2,[r1]      // get current task ptr \n\
        str    r0,[r2]      // save PSP value into current task \n\
        // load next context \n\
        ldr    r4,[r1,#4]   // get next task ptr \n\
        str    r4,[r1]      // set pspSw.curr = pspSw.next \n\
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
    pspSw.curr = pspSw.next = (uint32_t**) firstTask;

    // prepare to run all tasks in unprivileged thread mode, with one PSP
    // stack per task, keeping the original main stack for MSP/handler use
    HardwareStackFrame* psp = (HardwareStackFrame*) (*pspSw.curr + PSP_EXTRA);
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
    ((void (*)(void*)) psp->pc)((void*) psp->r[0]);
    panic("main task exit");
}

void changeTask (void* next) {
    // trigger a PendSV when back in thread mode to switch tasks
    pspSw.next = (uint32_t**) next;
    if (pspSw.next != pspSw.curr)
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
    uint8_t extra [3];
    union {
        uint32_t* args;
        int payload [7];
    };
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tasks and task management.

extern "C" int exit_ (int); // forward declaration XXX yuck

class Task {
public:
    uint32_t* pspSaved; // MUST be first in task objects, see PendSV_Handler
    uint32_t* mpuMaps;  // MUST be second in task objects, see PendSV_Handler
    Task* blocking;     // set while waiting, to task we're queued on, or self
    Message* message;   // set while recv or reply can take a new message
    Task* pendingQueue; // tasks waiting for their call to be accepted
    Task* finishQueue;  // tasks waiting for their call to be completed
    Task* next;         // used in waiting tasks, i.e. when on some linked list

    enum { Early, App, Server, Driver }; // type of task
    uint8_t type :2;    // set once the first SVC call is made

    uint8_t spare[3];   // pad the total task size to 32 bytes

    static constexpr int MAX_TASKS = 25;
    static Task vec [MAX_TASKS];

    void init (void* stackTop, void (*func)(void*), void* arg) {
        // use the C++11 compiler to verify some design choices
        static_assert(sizeof (Message) == 32); // fixed and known message size
        static_assert((sizeof *this & (sizeof *this - 1)) == 0); // power of 2

        HardwareStackFrame* psp = (HardwareStackFrame*) stackTop - 1;
        psp->r[0] = (uint32_t) arg;
        psp->lr = (uint32_t) exit_;
        psp->pc = (uint32_t) func;
        psp->psr = 0x01000000;
        pspSaved = (uint32_t*) psp - PSP_EXTRA;
        static const uint32_t dummyMaps [4] = {}; // two disabled regions
        mpuMaps = (uint32_t*) dummyMaps;
    }

    static Task& current () { return *(Task*) pspSw.curr; }

    static Task* nextRunnable () {
        Task* tp = &current();
        do
            if (++tp - vec >= MAX_TASKS)
                tp = vec;
        while (tp->state() < Runnable);
        return tp;
    }

    // try to deliver a message to this task
    int deliver (Task& sender, Message* msg) {
        bool waitingForMe = false;
        if (blocking && blocking != this) { // is it waiting for completion?
            waitingForMe = listRemove(blocking->finishQueue, *this);
            if (!waitingForMe)
                return -1; // waiting on something else, reject this delivery
        }

        if (message == 0) // is this task ready to receive a message?
            return -1; // nope, can't deliver this message

        *grab(message) = *msg; // copy message to destination
        resume(waitingForMe ? 0 : sender.index()); // if a reply, return 0
        return 0; // successful delivery
    }

    // deal with an incoming message which expects a reply
    int replyTo (Message* msg) {
        Task& sender = current();
        int e = deliver(sender, msg);
        if (e < 0 && this == vec) // oops, the system task was not ready
            printf("S: not ready for req #%d from %d args %08x\n",
                    msg->req, sender.index(), msg->args);
        // either try delivery again later, or wait for reply
        listAppend(e < 0 ? pendingQueue : finishQueue, sender);
        sender.message = msg;
        return sender.suspend(this);
    }

    // listen for incoming messages, block each sender while handling calls
    int listen (Message* msg) {
        if (pendingQueue != 0) {
            Task& sender = *pendingQueue;
            pendingQueue = grab(pendingQueue->next);
            listAppend(finishQueue, sender);
            *msg = *sender.message; // copy message to this receiver
            return sender.index();
        }
        message = msg;
        return suspend(this);
    }

    // forward current call to this destination
    bool forward (Task& sender, Message* msg) {
        if (!listRemove(current().finishQueue, sender))
            return false;
        sender.blocking = 0;
        *sender.message = *msg; // copy (possibly modified) request to sender
        int e = deliver(sender, sender.message); // re-deliver
        listAppend(e < 0 ? pendingQueue : finishQueue, sender);
        return true;
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

public:
    HardwareStackFrame& context () const {
        return *(HardwareStackFrame*) (pspSaved + PSP_EXTRA);
    }

    int suspend (Task* reason) {
        if (pspSw.curr != &pspSaved) // could be supported, but no need so far
            printf(">>> SUSPEND? %08x != curr %08x\n", this, pspSw.curr);

        void* next = nextRunnable();
        if (next == pspSw.curr)
            panic("no runnable tasks left");
        changeTask(next); // will trigger PendSV tail-chaining

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
            printf("  [%03x] %2d: %c%c sp %08x", (uint32_t) &t & 0xFFF,
                    i, " *<~"[t.type], "USWRA"[t.state()], t.pspSaved);
            printf(" blkg %2d pend %08x fini %08x mbuf %08x\n",
                    t.blocking == 0 ? -1 : t.blocking->index(),
                    t.pendingQueue, t.finishQueue, t.message);
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// System call handlers and dispatch vector. These always run in SVC context.
// The kernel code above needs to know almost nothing about everything below.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

extern "C" {
#include <syslib.h>
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SVC system call interface, required to switch from thread to handler state.
// Only an IPC call is processed right away, the rest is sent to task #0.
//
// During a system call, only *part* of the context is saved (r0..psr), the
// remaining context (i.e. R4-R11 and optional FP regs) need to be preserved.
// This is ok, as context switches only happen in PendSV, i.e. outside SVCs.

void SVC_Handler () {
    HardwareStackFrame* sfp;
    asm volatile ("mrs %0, psp" : "=r" (sfp));
    uint8_t req = ((uint8_t*) (sfp->pc))[-2];
#if 0
    printf("< svc.%d psp %08x r0.%d lr %08x pc %08x psr %08x >\n",
            req, sfp, sfp->r[0], sfp->lr, sfp->pc, sfp->psr);
#endif
    // first of all, make *pspSw.curr "resemble" a stack with full context
    // this is fiction, since R4-R11 (and FP regs) have *not* been saved
    // note that *pspSw.curr is not authoritative, h/w uses the real psp
    // now all valid task entries have similar stack ptrs for kernel use
    *pspSw.curr = (uint32_t*) sfp - PSP_EXTRA;

    // fully-automated task categorisation: the first SVC request made by a
    // task will configure its type, and therefore its system permissions
    Task& t = Task::current();
    if (t.type == Task::Early)
        switch (req) {
            case SYSCALL_ipcSend:                        break; // no change
            case SYSCALL_ipcCall: t.type = Task::Driver; break;
            case SYSCALL_ipcRecv: t.type = Task::Server; break;
            default:              t.type = Task::App;    break;
        }

    // there's no need to enforce these permissions right now, that can be
    // done in the ipc calls and in task #0, which handles all other calls

    switch (req) {
        // non-blocking message send, behaves as atomic test-and-set
        case SYSCALL_ipcSend: {
            int dst = sfp->r[0];
            Message* msg = (Message*) sfp->r[1];
            // ... validate dst and msg
            sfp->r[0] = Task::vec[dst].deliver(Task::current(), msg);
            break;
        }

        // blocking send + receive, used for request/reply sequences
        case SYSCALL_ipcCall: {
            int dst = sfp->r[0];
            Message* msg = (Message*) sfp->r[1];
            // ... validate dst and msg
            sfp->r[0] = Task::vec[dst].replyTo(msg);
            break;
        }

        // blocking receive, used by drivers and servers
        case SYSCALL_ipcRecv: {
            Message* msg = (Message*) sfp->r[0];
            // ... validate msg
            sfp->r[0] = Task::current().listen(msg);
            break;
        }

        //case SYSCALL_ipcPass: do_ipcPass(sfp); break;

        // wrap everything else into an ipcCall to task #0
        default: {
            // msg can be on the stack, because task #0 always accepts 'em now
            Message sysMsg;
            sysMsg.req = req;
            sysMsg.args = sfp->r;
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

SysRoute routes [256]; // indexed by the SVC request code, i.e. 0..255

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

VTable* irqVec;     // see below TODO this can be fixed in JeeH

// call the given function while briefly switched into privileged handler mode
// note that for this to be robust, the system task must never be preempted
void runPrivileged (void (*fun)()) {
    irqVec->sv_call = fun;
    asm volatile ("svc #0");
    irqVec->sv_call = SVC_Handler;
}

void systemTask (void* arg) {
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

    disk.init(); // TODO flashwear disk shouldn't be here

    // set up task 1, using the stack and entry point found in flash memory
    uint32_t* task1 = (uint32_t*) arg;
    Task::vec[1].init((void*) task1[0], (void (*)(void*)) task1[1], 0);
#if 0
#include "test_tasks.h"
#else
    // set up task 8, also in flash memory, for some additional experiments
    Task::vec[8].init((void*) MMIO32(0x08008000),
                      (void (*)(void*)) MMIO32(0x08008004), 0);
#endif

    // these requests are forwarded to other tasks
    routes[SYSCALL_gpio].set(7, 0);

    while (true) {
        Message sysMsg;
        int src = ipcRecv(&sysMsg);

        // examine the incoming request, calls will need a reply
        int req = sysMsg.req;
        Task& sender = Task::vec[src];
        uint32_t* args = sender.context().r;
        bool isCall = sender.blocking == Task::vec;
        int reply = -1; // the default reply is failure

        // decide what to do with this request
        SysRoute& sr = routes[(uint8_t) req]; // index is never out of range

        if (sr.task != 0) {
            // The routing table reports that this message should be forwarded.
            // That's only possible for ipc calls, since re-sending could fail.
            // But sends should be addressed directly to the proper task anyway.
            if (isCall) {
                //printf("S: rerouting req #%d from %d to %d\n",
                //        req, src, sr.task);
                sysMsg.req = sr.num; // adjust request code before forwarding
                bool f = Task::vec[sr.task].forward(sender, &sysMsg);
                if (!f)
                    printf("S: forward failed, req #%d from %d to %d\n",
                            req, src, sr.task);
            } else
                printf("S: can't re-send, req #%d from %d to %d\n",
                        req, src, sr.task);
            continue;
        }
#if 0
        if (req == 4)
            printf("%d S: ipc %s req #%d from %d args %08x\n",
                    ticks, isCall ? "CALL" : "SEND", req, src, args);
#endif
        // non-forwarded requests are handled by this system task
        switch (req) {
            default:
                printf("%d S: %s (0,#%d) ?\n",
                        ticks, isCall ? "CALL" : "SEND", req);
                break;

            case SYSCALL_noop:
                break;

            case SYSCALL_demo: {
                printf("\t<demo %d %d %d %d>\n",
                        args[0], args[1], args[2], args[3]);
                reply = args[0] + args[1] + args[2] + args[3];
                break;
            }

            case SYSCALL_exit_:
                printf("%d S: exit requested by %d\n", ticks, src);
                isCall = false; // TODO waits forever, must clean up
                break;

            case SYSCALL_write: {
                int /*fd = args[0],*/ len = args[2];
                uint8_t const* ptr = (uint8_t const*) args[1];
                for (int i = 0; i < len; ++i)
                    console.putc(ptr[i]);
                reply = len;
                break;
            }

            case SYSCALL_read: {
                int /*fd = args[0],*/ len = args[2];
                uint8_t* ptr = (uint8_t*) args[1];
                for (int i = 0; i < len; ++i)
                    ptr[i] = console.getc();
                reply = len;
                break;
            }

            case SYSCALL_ioctl: { // assumes FIONREAD on stdin for now
                //int fd = args[0], req = args[2];
                int* ptr = (int*) args[2];
                *ptr = console.readable();
                reply = 0;
                break;
            }

            case SYSCALL_diskio: {
                uint32_t dev = args[0], pos = args[1], cnt = args[3];
                uint8_t* ptr = (uint8_t*) args[2];
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (dev & 0x80)
                        disk.writeSector(pos + i, ptr);
                    else
                        disk.readSector(pos + i, ptr);
                    ptr += 128;
                }
                reply = 0;
                break;
            }
        }

        // unblock the originating task if it's waiting
        if (isCall) {
            /*int e =*/ ipcSend(src, &sysMsg);
            //printf("%d 0: replied to %d with %d status %d\n",
            //        ticks, src, reply, e);
            args[0] = reply; // replace resume's result with actual one
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Main entry point: set up the system task (task #0) and start multi-tasking.
// Note that main knows nothing about system calls, MPU, SVC, or IRQ handlers.

int main () {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    setupFaultHandlers();
    wait_ms(200); // give platformio's console time to connect

    extern uint8_t _estack[];
    uint8_t* systemStack = _estack - 1024; // leave 1k for the MSP stack

    // display some memory usage info for the kernel + system task
    extern uint8_t _stext[], _sidata[], _sdata[], _edata[], _sbss[], _ebss[];
    uint32_t textSz = (_sidata - _stext) + (_edata - _sdata); // incl data init
    printf("\n"
           "text %08x,%db data %08x,%db bss %04x,%db sp %04x,%db msp %04x,%db"
           "\n",
        _stext, textSz, _sdata, _edata - _sdata,
        (uint16_t) (int) _sbss, _ebss - _sbss,
        (uint16_t) (int) _ebss, systemStack - _ebss,
        (uint16_t) (int) systemStack, _estack - systemStack);
/*
text 08000010,4744b data 2001E000,0b bss E000,2992b sp EBB0,4176b msp FC00,1024b
*/

    irqVec = &VTableRam(); // this call can't be used in thread mode

    // initialize the very first task, and give it the vector of the second one
    Task::vec[0].init(systemStack, systemTask, (void*) 0x08004000);

    startTasks(Task::vec); // leap into unprivileged thread mode, never returns
}
