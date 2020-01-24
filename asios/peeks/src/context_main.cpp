#include <jee.h>

UartDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

//------------------------------------------------------------------------------
// see https://blog.stratifylabs.co/device/2013-10-09-Context-Switching-on-the-Cortex-M3/

#define MAX_TASKS 5

#define IN_USE_FLAG 0x1
#define EXEC_FLAG   0x2

void context_switcher(void);
void del_process(void);

typedef struct {
     void* sp; //The task's current stack pointer
     int flags; //Status flags includes activity status, parent task, etc
} task_table_t;

volatile task_table_t task_table[MAX_TASKS];
volatile int current_task;
volatile uint32_t* stack; //This is stored on the heap rather than the stack

#define MAIN_RETURN 0xFFFFFFF9  //Tells the handler to return using the MSP
#define THREAD_RETURN 0xFFFFFFFD //Tells the handler to return using the PSP

//Reads the main stack pointer
static inline void* rd_stack_ptr(void){
  void* result=0;
  asm volatile ("MRS %0, msp\n\t"
      //"MOV r0, %0 \n\t"
      : "=r" (result) );
  return result;
}

//This saves the context on the PSP, the Cortex-M3 pushes the other registers using hardware
static inline void save_context(void){
  uint32_t scratch;
  asm volatile ("MRS %0, psp\n\t"
      "STMDB %0!, {r4-r11}\n\t"
      "MSR psp, %0\n\t"  : "=r" (scratch) );
}

//This loads the context from the PSP, the Cortex-M3 loads the other registers using hardware
static inline void load_context(void){
  uint32_t scratch;
  asm volatile ("MRS %0, psp\n\t"
      "LDMFD %0!, {r4-r11}\n\t"
      "MSR psp, %0\n\t"  : "=r" (scratch) );
}

//The SysTick interrupt handler -- this grabs the main stack value then calls the context switcher
void systick_handler(void){
    save_context();  //The context is immediately saved
console.putc('.');
    stack = (uint32_t*)rd_stack_ptr();
    ///if ( SysTick->CTRL & (1<16) ){ //Indicates timer counted to zero
    if (*(uint32_t*)0xE000E010 & (1<16) ){ //Indicates timer counted to zero
        context_switcher();
    }
    load_context(); //Since the PSP has been updated, this loads the last state of the new task
}

//This does the same thing as the SysTick handler -- it is just triggered in a different way
void pendsv_handler(void){
    save_context();  //The context is immediately saved
console.putc('p');
    stack = (uint32_t*)rd_stack_ptr();
    context_switcher();
    load_context(); //Since the PSP has been updated, this loads the last state of the new task
}

//This reads the PSP so that it can be stored in the task table
static inline void* rd_thread_stack_ptr(void){
    void* result=0;
    asm volatile ("MRS %0, psp\n\t" : "=r" (result) );
    return(result);
}

//This writes the PSP so that the task table stack pointer can be used again
static inline void wr_thread_stack_ptr(void* ptr){
    asm volatile ("MSR psp, %0\n\t" : : "r" (ptr) );
}

//This is the context switcher
void context_switcher(void){
//console.putc('c');
   task_table[current_task].sp = rd_thread_stack_ptr(); //Save the current task's stack pointer
   do {
      current_task++;
      if ( current_task == MAX_TASKS ){
         current_task = 0;
         //*((uint32_t*)stack) = MAIN_RETURN; //Return to main process using main stack
         *((uint32_t*)stack) = THREAD_RETURN; //Use the thread stack upon handler return
         break;
      } else if ( task_table[current_task].flags & EXEC_FLAG ){ //Check exec flag
         //change to unprivileged mode
         *((uint32_t*)stack) = THREAD_RETURN; //Use the thread stack upon handler return
         break;
      }
   } while(1);
//console.putc('0'+current_task);
   wr_thread_stack_ptr( task_table[current_task].sp ); //write the value of the PSP to the new task
}

//This defines the stack frame that is saved  by the hardware
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t psr;
} hw_stack_frame_t;

//This defines the stack frame that must be saved by the software
typedef struct {
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
} sw_stack_frame_t;

static char m_stack [2000] __attribute__((aligned(8)));

void task_init(void){
    ///...
    task_table[0].sp = (uint8_t*) m_stack + sizeof m_stack;
    printf("main sp 0x%08x\n", task_table[0].sp);
    ///....
    //The systick needs to be configured to the desired round-robin time
    //..when the systick interrupt fires, context switching will begin
}

int new_task(void* (*p)(void*), void* arg, void* stackaddr, int stack_size){
    int i;
    uint8_t* mem = (uint8_t*) stackaddr - stack_size; // start of app mem
    hw_stack_frame_t* process_frame;
    //Disable context switching to support multi-threaded calls to this function
    ///systick_disable_irq();
    __asm("cpsid if");
    for(i=1; i < MAX_TASKS; i++){
        if( task_table[i].flags == 0 ){
            process_frame = ((hw_stack_frame_t*) stackaddr) - 1;
            process_frame->r0 = (uint32_t)arg;
            process_frame->r1 = 0;
            process_frame->r2 = 0;
            process_frame->r3 = 0;
            process_frame->r12 = 0;
            process_frame->pc = ((uint32_t)p);
            process_frame->lr = (uint32_t)del_process;
            process_frame->psr = 0x21000000; //default PSR value
            task_table[i].flags = IN_USE_FLAG | EXEC_FLAG;
            task_table[i].sp = ((sw_stack_frame_t*) process_frame) - 1;
            printf("mem 0x%08x pf 0x%08x sp 0x%08x pc 0x%08x\n",
                    mem, process_frame, task_table[i].sp, process_frame->pc);
            break;
        }
    }
    ///systick_enable_irq();  //Enable context switching
    __asm("cpsie if");
    if ( i == MAX_TASKS ){
        //New task could not be created
        return 0;
    } else {
        //New task ID is i
        return i;
    }
}

//This is called when the task returns
void del_process(void){
console.putc('D');
    task_table[current_task].flags = 0; //clear the in use and exec flags
    ///SCB->ICSR |= (1<<28); //switch the context
    *(uint32_t*)0xE000ED04 |= (1<<28); //switch the context
    while(1); //once the context changes, the program will no longer return to this thread
}

//------------------------------------------------------------------------------

int main () {
    console.init();
    //console.baud(115200, fullSpeedClock()/2);
    enableSysTick();
    wait_ms(100);
    enableSysTick(16000000/25);

    task_init();

    auto myTask = [](void*) -> void* {
        console.putc('(');
        for (int i = 0; i < 1000000; ++i) __asm("");
        console.putc(')');
        return 0;
    };

    constexpr int N = 1000;
    static uint8_t myStack [N] __attribute__((aligned(8)));
    int pid = new_task(myTask, (void*) "abc", myStack + N, N);
    printf("pid %d\n", pid);

    VTableRam().systick = systick_handler;
    VTableRam().pend_sv = pendsv_handler;

    while (1) {
        console.putc('M');
        for (int i = 0; i < 1000000; ++i) __asm("");
    }
}
