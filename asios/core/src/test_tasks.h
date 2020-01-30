// This test code is included in the body of system task #0, see main.cpp

#define DEFINE_TASK(num, stacksize, body) \
alignas(8) static uint8_t stack_##num [stacksize]; \
Task::index(num).init(stack_##num + stacksize, []() { body });

DEFINE_TASK(1, 256,
    PinA<7> led3;
    led3.mode(Pinmode::out);
    while (true) {
        led3 = 0; // inverted logic
        msWait(140);
        led3 = 1;
        msWait(140);
    }
)

DEFINE_TASK(2, 256,
    msWait(1000);
    printf("%d 2: start listening\n", ticks);
    while (true) {
        Message msg;
        int src = ipcRecv(&msg);
        printf("%d 2: received #%d from %d\n", ticks, msg.req, src);
        if (src == 4) {
            msWait(50);
            msg.req = -msg.req;
            printf("%d 2: about to reply #%d\n", ticks, msg.req);
            int e = ipcSend(src, &msg);
            if (e != 0)
                printf("%d 2: reply? %d\n", ticks, e);
        }
    }
)

DEFINE_TASK(3, 256,
    Message msg;
    msg.req = 99;
    while (true) {
        msWait(500);
        printf("%d 3: sending #%d\n", ticks, ++msg.req);
#if 1
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
        msWait(1000);
    }
)

DEFINE_TASK(4, 256,
    Message msg;
    msg.req = 250;
    while (true) {
        msWait(2000);
        printf("%d 4: calling #%d\n", ticks, ++msg.req);
        int e = ipcCall(2, &msg);
        printf("%d 4: result #%d status %d\n", ticks, msg.req, e);
        msWait(2000);
    }
)

DEFINE_TASK(5, 256,
    msWait(300);
    for (int i = 0; i < 3; ++i) {
        Task::dump();
        msWait(3210);
    }
    printf("%d 5: about to exit\n", ticks);
)

DEFINE_TASK(6, 256,
    PinA<6> led2;
    led2.mode(Pinmode::out);
    while (true) {
        printf("%d\n", ticks);
        led2 = 0; // inverted logic
        msWait(50);
        led2 = 1;
        msWait(950);
        int n = demo(11, 22, 33, 44);
        if (n != 11 + 22 + 33 + 44)
            printf("%d n? %d\n", ticks, n);
    }
)
