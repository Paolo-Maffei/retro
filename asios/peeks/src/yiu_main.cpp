#include <jee.h>

UartBufDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinA<6> led;
//PinA<7> led2;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Task switcher, adapted from a superb example in Joseph Liu's book, ch. 10:
// "The Definitive Guide to Arm Cortex M3 and M4", 3rd edition, 2014.

constexpr int MAX_TASKS = 4;
uint32_t  PSP_array[MAX_TASKS]; // Process Stack Pointer for each task
uint32_t  curr_task;            // current task
uint32_t  next_task;            // next task

__attribute__((naked))
void PendSV_Handler(void)
{   // Context switching code - no floating point support
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
        bx     lr           // return \n\
    ");
}

inline void startThreading (void* stackTop) {
    // prepare to run the rest of this code in unprivileged thread mode, with a
    // separate PSP stack, keeping the original stack for MSP/handler use
    asm volatile ("msr psp, %0\n" :: "r" (stackTop));

    // PendSV will be used to switch stacks, at the lowest interrupt priority
    *(uint8_t*) 0xE000ED22 = 0xFF; // SHPR3->PRI_14 = 0xFF
    VTableRam().pend_sv = PendSV_Handler;

    // and now the big jump, turning this into the main application thread
    asm volatile ("msr control, %0\n" :: "r" (3)); // go to unprivileged mode
    asm volatile ("isb\n"); // memory barrier, probably not needed on M3/M4
    // running in unprivileged thread mode from now on, acting as task zero
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// print a string on the polled uart, can be used even with interrupts disabled,
// but the elapsed time waiting for the uart will slow things down ... a lot
void kputs (const char* msg) {
    auto& polled = (UartBufDev< PinA<9>, PinA<10> >::base&) console;
    while (*msg)
        polled.putc(*msg++);
}

// give up, but not before trying to send a final message to the console port
void panic (const char* msg) {
    __asm("cpsid if"); // disable interrupts
    kputs("\n*** "); kputs(msg); kputs(" ***\n");
    while (1) {} // die
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main () {
    console.init();
    console.baud(115200, fullSpeedClock()/2);
    led.mode(Pinmode::out);
    wait_ms(100); // so PIO console has time to init

    // divider must stay below 16,777,216 (24-bit counter)
    // at 50 Hz, 32-bit ticks will roll over in 6.8 years
    enableSysTick(168000000/50); // 50 Hz

    VTableRam().systick = []() {
        ++ticks;
        if (curr_task != next_task)
            *(uint32_t*) 0xE000ED04 |= 1<<28; // SCB->ICSR |= PENDSVSET
    };

    VTableRam().hard_fault          = []() { panic("HARD FAULT"); };
    VTableRam().bus_fault           = []() { panic("BUS FAULT"); };
    VTableRam().usage_fault         = []() { panic("USAGE FAULT"); };
    VTableRam().memory_manage_fault = []() { panic("MEM FAULT"); };

    alignas(8) uint8_t myStack [1000];
    startThreading(myStack + sizeof myStack); // this MUST be inline code!

    while (true) {
        printf("%d\n", ticks);
        led = 0;
        wait_ms(100/20);
        led = 1;
        wait_ms(900/20);
    }
}
