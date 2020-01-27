#include <jee.h>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Console device and exception handler debugging.

UartBufDev< PinA<9>, PinA<10> > console;

int printf (const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

// print a string on the polled uart, can be used with interrupts disabled
// ... but the elapsed time waiting for the uart will slow things down a lot
void kputs (const char* msg) {
    auto& polled = (decltype(console)::base&) console;
    while (*msg)
        polled.putc(*msg++);
}

// give up, but not before trying to send a final message to the console port
void panic (const char* msg) {
    __asm("cpsid if"); // disable interrupts
    kputs("\n*** panic: "); kputs(msg); kputs(" ***\n");
    while (1) {} // hang
}

// set up and enable the main fault handlers
void initFaultHandlers () {
    VTableRam().hard_fault          = []() { panic("hard fault"); };
    VTableRam().memory_manage_fault = []() { panic("mem fault"); };
    VTableRam().bus_fault           = []() { panic("bus fault"); };
    VTableRam().usage_fault         = []() { panic("usage fault"); };
    *(uint32_t*) 0xE000ED24 |= 7<<16; // SCB->SHCSR |= (USG|BUS|MEM)FAULTENA
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Task switcher, adapted from a superb example in Joseph Yiu's book, ch. 10:
// "The Definitive Guide to Arm Cortex M3 and M4", 3rd edition, 2014.

constexpr int MAX_TASKS = 4;
uint32_t* PSP_array[MAX_TASKS]; // Process Stack Pointer for each task
uint32_t curr_task, next_task;  // index of current and next task

void initTask (int index, void* stackTop, void (*func)()) {
    uint32_t* psp = (uint32_t*) stackTop - 16;
    psp[14] = (uint32_t) func; // initial PC
    psp[15] = 0x01000000; // initial xPSR
    PSP_array[index] = psp;
}

// context switcher, no floating point support
void PendSV_Handler () {
    __asm volatile (" \n\
        // save current context \n\
        mrs    r0, psp      // get current process stack pointer value \n\
        stmdb  r0!,{r4-r11} // save R4 to R11 in task stack (8 regs) \n\
        ldr    r1,=(curr_task) \n\
        ldr    r2,[r1]      // get current task ID \n\
        ldr    r3,=(PSP_array) \n\
        str    r0,[r3, r2, lsl #2] // save PSP value into PSP_array \n\
        // load next context \n\
        ldr    r4,=(next_task) \n\
        ldr    r4,[r4]      // get next task ID \n\
        str    r4,[r1]      // set curr_task = next_task \n\
        ldr    r0,[r3, r4, lsl #2] // Load PSP value from PSP_array \n\
        ldmia  r0!,{r4-r11} // load R4 to R11 from task stack (8 regs) \n\
        msr    psp, r0      // set PSP to next task \n\
    ");
}

void startMultiTasker () {
    // prepare to run all tasks in unprivileged thread mode, with one PSP
    // stack per task, keeping the original main stack for MSP/handler use
    uint32_t* psp = PSP_array[curr_task];
    asm volatile ("msr psp, %0\n" :: "r" (psp + 16));

    // PendSV will be used to switch stacks, at the lowest interrupt priority
    *(uint8_t*) 0xE000ED22 = 0xFF; // SHPR3->PRI_14 = 0xFF
    VTableRam().pend_sv = PendSV_Handler;

    asm volatile ("msr control, %0\n" :: "r" (3)); // go to unprivileged mode
    asm volatile ("isb\n"); // memory barrier, not really needed on M3/M4

    // launch main task, running in unprivileged thread mode from now on
    ((void (*)()) psp[14])();
    panic("main task exit");
}

void changeTask (uint32_t index) {
    // trigger a PendSV exception from thread mode to switch tasks
    if (index != curr_task) {
        next_task = index;
        *(uint32_t*) 0xE000ED04 |= 1<<28; // SCB->ICSR |= PENDSVSET
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main () {
    const auto hz = fullSpeedClock();
    console.init();
    console.baud(115200, hz/2);
    initFaultHandlers();
    wait_ms(200); // give platformio's console time to connect

    VTableRam().systick = []() {
        ++ticks;
        if (ticks % 20 == 0)           // switch tasks every 20 ms
            changeTask(1 - curr_task); // FIXME: alternate between tasks 0 & 1
    };

    alignas(8) static uint8_t stack0 [1000];
    initTask(0, stack0 + sizeof stack0, []() {
        PinA<6> led2;
        led2.mode(Pinmode::out);
        while (true) {
            printf("%d\n", ticks);
            led2 = 0; // inverted logic
            wait_ms(100);
            led2 = 1;
            wait_ms(900);
        }
    });

    alignas(8) static uint8_t stack1 [1000];
    initTask(1, stack1 + sizeof stack1, []() {
        PinA<7> led3;
        led3.mode(Pinmode::out);
        while (true) {
            led3 = 0; // inverted logic
            wait_ms(20);
            led3 = 1;
            wait_ms(260);
        }
    });

    startMultiTasker(); // never returns
}
