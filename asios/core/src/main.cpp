#include <jee.h>
#include <string.h>

const uint8_t appCode [] = {
#include "appcode.h"
};

UartDev< PinA<9>, PinA<10> > console;

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
    return 0;
}

PinA<6> led;
//PinA<7> led2;

//------------------------------------------------------------------------------
// see https://blog.stratifylabs.co/device/2013-10-09-Context-Switching-on-the-Cortex-M3/

#define MAX_TASKS 10

#define IN_USE_FLAG 0x1
#define EXEC_FLAG   0x2

void context_switcher(void);
void del_process(void);

typedef struct {
     void * sp; //The task's current stack pointer
     int flags; //Status flags includes activity status, parent task, etc
} task_table_t;
int current_task;
task_table_t task_table[MAX_TASKS];

static uint32_t * stack; //This is stored on the heap rather than the stack

#define MAIN_RETURN 0xFFFFFFF9  //Tells the handler to return using the MSP
#define THREAD_RETURN 0xFFFFFFFD //Tells the handler to return using the PSP

//Reads the main stack pointer
static inline void * rd_stack_ptr(void){
  void * result=NULL;
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
    stack = (uint32_t *)rd_stack_ptr();
    ///if ( SysTick->CTRL & (1<16) ){ //Indicates timer counted to zero
    if ( *(uint32_t*)0xE000E010 & (1<16) ){ //Indicates timer counted to zero
        context_switcher();
    }
    load_context(); //Since the PSP has been updated, this loads the last state of the new task
}

//This does the same thing as the SysTick handler -- it is just triggered in a different way
void pendsv_handler(void){
    save_context();  //The context is immediately saved
    stack = (uint32_t *)rd_stack_ptr();
    context_switcher();
    load_context(); //Since the PSP has been updated, this loads the last state of the new task
}

//This reads the PSP so that it can be stored in the task table
static inline void * rd_thread_stack_ptr(void){
    void * result=NULL;
    asm volatile ("MRS %0, psp\n\t" : "=r" (result) );
    return(result);
}

//This writes the PSP so that the task table stack pointer can be used again
static inline void wr_thread_stack_ptr(void * ptr){
    asm volatile ("MSR psp, %0\n\t" : : "r" (ptr) );
}

//This is the context switcher
void context_switcher(void){
   task_table[current_task].sp = rd_thread_stack_ptr(); //Save the current task's stack pointer
   do {
      current_task++;
      if ( current_task == MAX_TASKS ){
         current_task = 0;
         *((uint32_t*)stack) = MAIN_RETURN; //Return to main process using main stack
         break;
      } else if ( task_table[current_task].flags & EXEC_FLAG ){ //Check exec flag
         //change to unprivileged mode
         *((uint32_t*)stack) = THREAD_RETURN; //Use the thread stack upon handler return
         break;
      }
   } while(1);
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

static char m_stack[sizeof(sw_stack_frame_t)];

void task_init(void){
    ///...
    task_table[0].sp = m_stack + sizeof(sw_stack_frame_t);
    ///....
    //The systick needs to be configured to the desired round-robin time
    //..when the systick interrupt fires, context switching will begin
}

int new_task(void *(*p)(void*), void * arg, void * stackaddr, int stack_size){
    int i;
    uint8_t* mem = (uint8_t*) stackaddr - stack_size; // start of app mem
    hw_stack_frame_t* process_frame;
    //Disable context switching to support multi-threaded calls to this function
    ///systick_disable_irq();
    __asm("cpsid i");
    for(i=1; i < MAX_TASKS; i++){
        if( task_table[i].flags == 0 ){
            process_frame = (hw_stack_frame_t*) stackaddr - 1;
            process_frame->r0 = (uint32_t)arg;
            process_frame->r1 = 0;
            process_frame->r2 = 0;
            process_frame->r3 = 0;
            process_frame->r12 = 0;
            process_frame->pc = ((uint32_t)p);
            process_frame->lr = (uint32_t)del_process;
            process_frame->psr = 0x21000000; //default PSR value
            task_table[i].flags = IN_USE_FLAG | EXEC_FLAG;
            task_table[i].sp = mem +
                stack_size -
                sizeof(hw_stack_frame_t) -
                sizeof(sw_stack_frame_t);
            break;
        }
    }
    ///systick_enable_irq();  //Enable context switching
    __asm("cpsie i");
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
  task_table[current_task].flags = 0; //clear the in use and exec flags
  ///SCB->ICSR |= (1<<28); //switch the context
  *(uint32_t*)0xE000ED04 |= (1<<28); //switch the context
  while(1); //once the context changes, the program will no longer return to this thread
}

//------------------------------------------------------------------------------

int main () {
    console.init();
    //console.baud(115200, fullSpeedClock()/2);
    enableSysTick(16000000/100);
    wait_ms(100);
    //led.mode(Pinmode::out);

    // e.g. https://github.com/dwelch67/stm32_samples/blob/master/NUCLEO-F767ZI/cpuid/README
    for (int i = 0; i < 16; ++i)
        printf("scb+0x%02x: 0x%08x = 0x%08x\n",
            4*i, 0xE000ED00 + 4*i, ((const uint32_t*) 0xE000ED00)[i]);

    VTableRam().sv_call = []() {
        // console must be polled inside svc, i.e. UartDev iso UartBufDev
        //printf("X %d\n", ticks);
        console.putc('X');
        // wait_ms() can't be used inside svc
        //led = 0;
        //for (int i = 0; i < 2000000; ++i) __asm("");
        //led = 1;
        //for (int i = 0; i < 20000000; ++i) __asm("");
    };

    uint32_t* appMem = (uint32_t*) 0x20000000;
    memcpy(appMem, appCode, sizeof appCode);

    printf("magic 0x%08x start 0x%08x staxk 0x%08x size %d b\n",
            appMem[0], appMem[1], appMem[5], sizeof appCode);

    task_init();

    VTableRam().systick = []() {
        console.putc('.');
        ++ticks;
        systick_handler();
    };

    //static uint32_t am = appMem[1];
    auto taskFun = [](void*) -> void* {
        //console.putc('<');
        //((int (*)()) am)();
        //console.putc('>');
        while (1)
            console.putc('!');
    };

    static uint32_t tempStack [1000];
    //int pid = new_task(taskFun, (void*) "abc", (void*) appMem[5], appMem[5] & 0xFFFFF);
    int pid = new_task(taskFun, (void*) "abc", stack + sizeof stack, 4 * sizeof stack);
    printf("pid %d\n", pid);

    while (1) {
        //printf("main %d\n", ticks);
        console.putc('M');
        for (int i = 0; i < 2000000; ++i) __asm("");
    }
}
