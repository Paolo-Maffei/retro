#include <jee.h>
#include <string.h>

#include "syslib.h"

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
    for (int i = 0; i < 10000000; ++i) asm (""); // give uart time to settle
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
// General-purpose code.

// copy a value (of any type), setting the original to zero
template< typename T >
T grab (T& x) {
    T r = x;
    x = 0;
    return r;
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
// Tasks and task management with message-based IPC.

typedef int Message [8];

class Task {
    uint32_t* pspSaved; // MUST be first in task objects, see PendSV_Handler
    uint32_t* mpuMaps;  // MUST be second in task objects, see PendSV_Handler
    Message* message;   // set while recv or reply can take a new message
    // these task ptrs could be made indices, freeing 12b of 16b if <256 tasks
    Task* blocking;     // set while waiting, to task we're queued on, or self
    Task* incoming;     // tasks waiting for their call to be accepted
    Task* inProgress;   // tasks waiting for their call to be completed
    Task* next;         // used in waiting tasks, i.e. when on some linked list
public:
    enum { Early, App, Server, Driver }; // type of task

    uint8_t type :2;    // set once the first SVC call is made
    uint8_t request;    // request number of current system call
    uint8_t spare[2];   // pad the total task size to 32 bytes

    static constexpr int MAX_TASKS = 25;
    static Task vec [MAX_TASKS];

    // find the next free task slot and initialise it
    static int create (void* top, void (*proc)(void*), void* arg) {
        for (int i = 0; i < MAX_TASKS; ++i)
            if (vec[i].state() == Unused) {
                vec[i].init(top, proc, arg);
                return i;
            }
        return -1; // no free slot
    }

    // return pointer to saved registers on this task's process stack
    uint32_t* regs () const {
        // careful: pspSaved is NOT valid for the active task
        return pspSaved + PSP_EXTRA;
    }

    // find the next runnable task to switch to, might be same as current one
    static Task* nextRunnable () {
        Task* tp = &current();
        do
            if (++tp - vec >= MAX_TASKS)
                tp = vec;
        while (tp->state() < Runnable);
        return tp;
    }

    // try to deliver a message to this task
    int deliver (Task& from, Message* msg) {
        //printf("S: deliver %08x %d => %d req %d\n",
        //    msg, from.index(), index(), from.request);
        if (blocking && blocking != this) // am I waiting for a reply?
            if (!removeFrom(blocking->inProgress))
                return -1; // waiting on something else, reject this delivery

        if (message == 0) // is this task ready to receive a message?
            return -1; // nope, can't deliver this message

        Message* buf = grab(message);
        if (buf != (Message*) regs()) { // if it was a receive, not syscall
            memcpy(buf, msg, sizeof *msg);  // copy message to destination
            *regs() = from.index();  // return sender's task id
        }
        resume();
        return 0; // successful delivery
    }

    // deal with an incoming message which expects a reply
    int replyTo (Message* msg) {
        Task& from = current();
        //printf("S:   reply %08x %d => %d req %d\n",
        //    msg, from.index(), index(), from.request);
        int e = deliver(from, msg);
        // either try delivery again later, or wait for reply
        from.appendTo(e < 0 ? incoming : inProgress);
        from.suspend(this, msg);
        return -1; // will be adjusted before resuming
    }

    // listen for incoming messages, block each sender while handling calls
    int listen (Message* msg) {
        //printf("S:  listen %08x   at %d\n", msg, index());
        if (incoming == 0) {
            suspend(this, msg);
            return -1; // will be adjusted before resuming
        }
        Task& from = *incoming;
        //printf("S:     got %08x %d => %d req %d\n",
        //    from.message, from.index(), index(), from.request);
        incoming = grab(incoming->next);
        from.appendTo(inProgress);
        memcpy(msg, from.message, sizeof *msg); // copy msg to this task
        return from.index();
    }

    // forward current call to this destination
    bool forward (Task& from, Message* msg) {
        bool found = from.removeFrom(current().inProgress);
        //printf("S: forward %08x %d => %d req %d found %d\n",
        //    msg, from.index(), index(), from.request, found);
        if (found) {
            from.blocking = 0; // TODO figure this out, also request = 0 ?
            memcpy(from.message, msg, sizeof *msg); // copy req back to sender
            int e = deliver(from, from.message); // re-deliver
            from.appendTo(e < 0 ? incoming : inProgress);
        }
        return found;
    }

    // change this task from waiting on another task to suspended
    Message* detach () {
        if (!removeFrom(blocking->inProgress))
            ;//panic("can't detach");
        blocking = this;
        return grab(message); // return the message buffer and clear it
    }

    // unblock all tasks which are in yield() and have expired
    static uint32_t resumeAllExpired (uint32_t now) {
        int wakeup = 1<<30; // a really big value: over 12 days in milliseconds
        for (int i = 0; i < Task::MAX_TASKS; ++i)
            Task::vec[i].checkTimeout(now, &wakeup);
        return now + wakeup; // new best next yield expiration time
    }

    // dump all tasks in use, using console & printf
    static void dumpAll () {
        for (int i = 0; i < MAX_TASKS; ++i)
            Task::vec[i].dump();
    }

private:
    static Task& current () { return *(Task*) pspSw.curr; }

    uint32_t index () const { return this - vec; }

    enum State { Unused, Suspended, Waiting, Runnable, Active };

    State state () const {
        return pspSaved == 0 ? Unused :     // task is not in use
            blocking == this ? Suspended :  // needs an explicit resume
                    blocking ? Waiting :    // waiting on another task
          this != &current() ? Runnable :   // will run when scheduled
                               Active;      // currently running
    }

    void init (void* top, void (*proc)(void*), void* arg) {
        // use the C++11 compiler to verify some design choices
        static_assert(sizeof (Message) == 32); // fixed/known msg buffer size
        static_assert((sizeof *this & (sizeof *this - 1)) == 0); // power of 2

        HardwareStackFrame* psp = (HardwareStackFrame*) top - 1;
        psp->r[0] = (uint32_t) arg;
        psp->lr = (uint32_t) texit;
        psp->pc = (uint32_t) proc;
        psp->psr = 0x01000000;
        pspSaved = (uint32_t*) psp - PSP_EXTRA;
        static const uint32_t dummyMaps [4] = {}; // two disabled regions
        mpuMaps = (uint32_t*) dummyMaps;
    }

    void suspend (Task* reason, Message* msg) {
        if (pspSw.curr != &pspSaved) // could be supported, but no need so far
            printf(">>> SUSPEND? %08x != curr %08x\n", this, pspSw.curr);

        void* next = nextRunnable();
        if (next == pspSw.curr)
            panic("no runnable tasks left");
        changeTask(next); // will trigger PendSV tail-chaining

        blocking = reason;
        message = msg;
    }

    // make a suspended or waiting task runnable again
    void resume () {
        request = 0;
        blocking = 0; // then allow it to run again
    }

    // append this task to the end of a list
    void appendTo (Task*& list) {
        if (next)
            panic("item already in list");
        while (list)
            list = list->next;
        list = this;
    }

    // remove this task from a list
    bool removeFrom (Task*& list) {
        while (list != this)
            if (list)
                list = list->next;
            else
                return false;
        list = grab(next);
        return true;
    }

    // unblock task if in yield() and it has expired
    void checkTimeout (uint32_t now, int* wakeup) {
        // true when this task is suspended while in a yield system call
        // this is one of the few places where the kernel knows about req codes
        if (request == SYSCALL_yield && blocking == this) {
            // never the current active task, for which blocking == 0
            uint32_t timeout = regs()[1]; // i.e. the unused arg trick
            int msToGo = timeout - now;
            if (msToGo <= 0)
                resume(); // this yield timer has expired
            else if (msToGo < *wakeup)
                *wakeup = msToGo; // this is the best next wakeup time so far
        }
    }

    // a crude task dump for basic debugging, using console & printf
    void dump () const {
        if (pspSaved) {
            printf("  [%03x] %2d: %c%c sp %08x", (uint32_t) this & 0xFFF,
                    this - vec, " *<~"[type], "USWRA"[state()], pspSaved);
            printf(" blk %2d pq %08x fq %08x buf %08x req %d\n",
                    blocking == 0 ? -1 : blocking->index(),
                    incoming, inProgress, message, request);
        }
    }
};

Task Task::vec [];

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// The kernel code above needs to know almost nothing about everything below.
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
    Task& t = *(Task*) pspSw.curr;

    // fully-automated task categorisation: the first SVC request made by a
    // task will configure its type, and therefore its system permissions
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
            sfp->r[0] = Task::vec[dst].deliver(t, msg);
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
            sfp->r[0] = t.listen(msg);
            break;
        }

        //case SYSCALL_ipcPass: do_ipcPass(sfp); break;

        // wrap everything else into an ipcCall to task #0
        default: {
            Message* msg = (Message*) t.regs(); // not a real msg buffer
            t.request = req; // save SVC number in task object
            (void) Task::vec[0].replyTo(msg); // XXX explain void
            break;
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

volatile uint32_t nextWakeup;

//#define msWait yield

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

        // also lower the priority of SysTicks so they won't interrupt SVCs and
        // so task switches + timeout wakeups don't happen during syscalls
        MMIO8(0xE000ED23) = 0xFF; // SHPR3->PRI_15 = 0xFF
    });

    // periodic system tick, this never runs while SVC or other IRQs are active
    // note: pspSw.curr & Task::current() are valid, but curr->regs() isn't
    irqVec->systick = []() {
        ++ticks;

        bool expire = (int) (nextWakeup - ticks) < 0; // careful with overflow
        if (expire)
            nextWakeup = Task::resumeAllExpired(ticks);
        else
            expire = ticks % 32 == 0; // time to preempt

        // TODO maybe the system task needs to raise its base priority instead?
        if (expire && (Task*) pspSw.curr != Task::vec)
            changeTask(Task::nextRunnable()); // don't preempt system task
    };

    disk.init(); // TODO flashwear disk shouldn't be here

    // set up task 1, using the stack and entry point found in flash memory
    uint32_t* task1 = (uint32_t*) arg;
    Task::create((void*) task1[0], (void (*)(void*)) task1[1], 0);
#if 0
#include "test_tasks.h"
#endif

    // these requests are forwarded to other tasks
    routes[SYSCALL_gpio].set(7, 0);

    while (true) {
        Message sysMsg;
        int src = ipcRecv(&sysMsg);

        // examine the incoming request, calls will need a reply
        Task& from = Task::vec[src];
        int req = from.request;
        uint32_t* args = from.regs();
        bool isCall = true; /// always? TODO from.blocking == Task::vec;

        // decide what to do with this request
        SysRoute& sr = routes[(uint8_t) req]; // index is never out of range

        if (sr.task != 0) {
            // The routing table reports that this message should be forwarded.
            // That's only possible for ipc calls, since re-sending could fail.
            // But sends should be addressed directly to the proper task anyway.
            if (isCall) {
                //printf("S: rerouting req #%d from %d to %d\n",
                //        req, src, sr.task);
                from.request = sr.num; // adjust request code before forward
                bool f = Task::vec[sr.task].forward(from, &sysMsg);
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
        // cases below use "continue" iso "break" to avoid replying & resuming
        int reply = -1; // the default reply is failure
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

            case SYSCALL_read: {
                int /*fd = args[0],*/ len = args[2];
                uint8_t* ptr = (uint8_t*) args[1];
                for (int i = 0; i < len; ++i)
                    ptr[i] = console.getc();
                reply = len;
                break;
            }

            case SYSCALL_write: {
                int /*fd = args[0],*/ len = args[2];
                uint8_t const* ptr = (uint8_t const*) args[1];
                for (int i = 0; i < len; ++i)
                    console.putc(ptr[i]);
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
                uint32_t rw = args[0], pos = args[1], cnt = args[3];
                uint8_t* ptr = (uint8_t*) args[2];
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (rw)
                        disk.writeSector(pos + i, ptr);
                    else
                        disk.readSector(pos + i, ptr);
                    ptr += 128;
                }
                reply = 0;
                break;
            }

            case SYSCALL_tfork: {
                void* top = (void*) args[0];
                void (*proc)(void*) = (void (*)(void*)) args[1];
                void* arg = (void*) args[2];
                reply = Task::create(top, proc, arg);
                printf("%d S: tfork by %d => %d sp %08x pc %08x arg %d\n",
                        ticks, src, reply, top, proc, arg);
                break;
            }

            case SYSCALL_twait: {
                int arg = args[0];
                printf("%d S: twait by %d arg %d\n", ticks, src, arg);
                // TODO append to finished queue of task it's waiting on?
                continue; // don't reply & resume until the wait is over
            }

            case SYSCALL_texit: {
                int arg = args[0];
                printf("%d S: texit by %d arg %d\n", ticks, src, arg);
                // TODO resume waiting tasks and release the Task for re-use
                continue; // don't reply & resume, this task has ended
            }

            case SYSCALL_yield: {
                uint32_t now = ticks; // read volatile "ticks" counter once
                int ms = args[0];
                // the trick: calculate and store wakeup time in unused 2nd arg
                args[1] = now + ms;
                if ((uint32_t*) from.detach() != args)
                    ;//panic("detach mixup");
                // if this timer expires before the next wakeup time, fix it
                if (ms < (int) (nextWakeup - now)) // careful with overflow
                    nextWakeup = now + ms;
                continue; // don't reply & resume now, timeout will do it
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
    Task::create(systemStack, systemTask, (void*) 0x08004000);

    startTasks(Task::vec); // leap into unprivileged thread mode, never returns
}
