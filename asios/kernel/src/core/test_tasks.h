// This test code is included in the body of system task #0, see main.cpp

#define DEFINE_TASK(num, stacksize, body) \
alignas(8) static uint8_t stack_##num [stacksize]; \
Task::vec[num].init(stack_##num + stacksize, [](void*) { body }, 0);

DEFINE_TASK(2, 256,
    yield(1000);
    printf("%d 2: start listening\n", ticks);
    while (true) {
        Message msg;
        int src = ipcRecv(&msg);
        printf("%d 2: received #%d from %d\n", ticks, msg[0], src);
        if (src == 4) {
            yield(50);
            msg[0] = -msg[0];
            printf("%d 2: about to reply #%d to %d\n", ticks, msg[0], src);
            int e = ipcSend(src, &msg);
            if (e != 0)
                printf("%d 2: reply? %d\n", ticks, e);
        }
    }
)

DEFINE_TASK(3, 256,
    Message msg;
    msg[0] = 99;
    while (true) {
        yield(500);
        printf("%d 3: sending #%d\n", ticks, ++msg[0]);
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
        yield(1000);
    }
)

DEFINE_TASK(4, 256,
    Message msg;
    msg[0] = 250;
    while (true) {
        yield(2000);
        printf("%d 4: calling #%d\n", ticks, ++msg[0]);
        int e = ipcCall(2, &msg);
        printf("%d 4: result #%d status %d\n", ticks, msg[0], e);
        yield(2000);
    }
)

DEFINE_TASK(5, 256,
    yield(300);
    for (int i = 0; i < 3; ++i) {
        Task::dumpAll();
        yield(3210);
    }
    printf("%d 5: about to exit\n", ticks);
)

#if 0
DEFINE_TASK(6, 256,
    PinA<6> led2;
    led2.mode(Pinmode::out);
    while (true) {
        printf("%d\n", ticks);
        led2 = 0; // inverted logic
        yield(50);
        led2 = 1;
        yield(950);
        int n = demo(11, 22, 33, 44);
        if (n != 11 + 22 + 33 + 44)
            printf("%d n? %d\n", ticks, n);
    }
)
#endif

// gpio driver
DEFINE_TASK(7, 256,
    while (true) {
        Message msg;
        int src = ipcRecv(&msg);

        int cmd = msg[0]; //gpioPin = msg[1];
        //char port = 'A' + (gpioPin >> 4) - 0xA;
        //Pin<port,gpioPin&0xF> pin;
        //printf("%d 7: cmd %d from %d\n", ticks, cmd, src);
        //PinA<7> pin;
        PinE<0> pin;
        switch (cmd) {
            case 0: pin.mode(Pinmode::out); break;
            case 1: pin = 0; break;
            case 2: pin = 1; break;
        }

        //msg[0] = 0; // reply
        int e = ipcSend(src, &msg);
        if (e != 0)
            printf("%d 7: reply send got %d\n", ticks, e);
    }
)

#if 0
DEFINE_TASK(7, 256,
    PinA<7> led3;
    led3.mode(Pinmode::out);
    while (true) {
        led3 = 0; // inverted logic
        yield(140);
        led3 = 1;
        yield(140);
    }
)
#endif
