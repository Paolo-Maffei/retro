# A new microkernel

The code in this area implement a very small kernel. It currently runs on the
STM32F407 µC, which is an ARM Cortex M4. Some properties of the current kernel:

* preemptive multi-tasking (fixed time slices)
* one dedicated stack for all system calls and interrupts
* the kernel code is 100% interruptable, no locked critical regions
* tasks (i.e. threads) run in unprivileged mode and can't block interrupts
* task stacks must have space for 64 extra bytes, above what the task needs
* task zero is linked into the kernel and has r/w access to all its data

This implementation was massively inspired by Minix 3, taking its minimalism
and modularity even further. It's also nowhere near being complete, let alone
able to act as a POSIX-like operating system. Let's face it: 500 lines of C++
code can't possibly implement anything of that extent and complexity.

Here are some of the more interesing features:

* the kernel implements three message-based IPC primitives:
    1. `ipcSend` sends a message, and returns immediately with a success flag
    2. `ipcCall` does a blocking send + receive, like a normal function call
    3. `ipcRecv` does a blocking wait for new incoming messages
* the above are the three key functions, _everything_ else is built on top
* there are four types of tasks:
    1. `Early` - the initial state, when a task hasn't made any requests yet
    2. `App` - this task can only make std system calls, no IPC, no direct I/O
    3. `Server` - a task which listens to incoming messages, and can do IPC
    4. `Driver` - this task can be given access to the µC's I/O peripherals
* a task's type is determined by its first request and _can't_ be changed later
* this type is checked by the kernel to decide what level of acccess it grants

All non-IPC calls to the kernel are treated as "system calls". They are turned
into individual IPC messages, and sent to task #0. Task #0 is never preempted,
therefore it's either doing some real work, or waiting for new requests. If
task #0 were to ever fault or hang, all bets are off.

Task #0 can either process and respond to an IPC message, or forward it to
another task.  A "routing table" determines how each request is treated. One
idea would be to allow adjusting this table, so that servers and drivers can be
replaced at run time.

The kernel only handles message passing and context switching. System calls,
IRQ handlers, I/O devices, and memory management are not its responsability.
Everything else needed for security and robustness has to be done in task #0.

All the other tasks run in unprivileged mode, and cannot escape from it. The
only thing an App task can do, is perform system calls. Servers and Drivers
can also perform IPC between them, exchanging fixed-size (32-byte) messages.
When granted access to (all or part of) I/O space, drivers can perform I/O,
but even then they can't disable interrupts, or grant themselves more rights.
For actual data transfers in or out of other tasks, they need to ask task #0.

Lastly, this kernel includes logic for the Cortex M4's Memory Protection Unit,
which can be enabled (by task #0) to completely tie down which memory areas
each task has access to. This work is not ready, but should allow for full
encapsulation of each task, so that they can't mess with any other tasks. Just
as in a "big" operating system, access violations will then shut down the task.

_Why?_

Because I can? No, seriously ...

* Because I really like the idea of a modular system, even on embedded µCs.
* Because Minix 3 (and L4) show that a microkernel design really works.
* And because I think it can be brought to even a "lowly" ARM Cortex M4 ...

This implementation only relies on the "JeeH" library, which handles a lot of
the low-level hardware access on STM32s. It's only a week old right now, and
here's some sample output from running the demo code in `src/test_tasks.h`:

``` text
% pio run -t upload -t monitor
Processing black407ve (platform: ststm32; framework: stm32cube; board: genericSTM32F407VET6)
[...]
DATA:    [          ]   4.3% (used 5632 bytes from 131072 bytes)
PROGRAM: [          ]   1.1% (used 5556 bytes from 514288 bytes)
Configuring upload protocol...
AVAILABLE: blackmagic, dfu, jlink, stlink
CURRENT: upload_protocol = blackmagic
Looking for BlackMagic port...
Use manually specified: /dev/cu.usbmodemE4BFAFA21
[...]
=========================== [SUCCESS] Took 2.02 seconds ===========================
--- Miniterm on /dev/cu.usbmodemE4BFAFA23  115200,8,N,1 ---
--- Quit: Ctrl+C | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---
205
[75C]  0: <S sp 2001D150 blkg  0 pend 00000000 fini 00000000 mbuf 2001CA7C
[77C]  1:  R sp 2001CB68 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[79C]  2:  R sp 2001CC28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7BC]  3:  R sp 2001CD28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7DC]  4:  R sp 2001CE28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7FC]  5:  A sp 2001CEF8 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[81C]  6:  R sp 2001D068 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
702 3: sending #100
702 3: send? -1
1205 2: start listening
1213 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
1217
2204 3: sending #101
2205 4: calling #251
2208 2: received #101 from 3
2208 2: received #251 from 4
2221 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
2225
2262 2: about to reply #5
2263 4: result #5 status 0
3225 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
3229
3711 3: sending #102
3716 2: received #102 from 3
[75C]  0: <S sp 2001D150 blkg  0 pend 00000000 fini 00000000 mbuf 2001CA7C
[77C]  1:  R sp 2001CB68 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[79C]  2: <S sp 2001CC38 blkg  2 pend 00000000 fini 00000000 mbuf 2001CC78
[7BC]  3:  R sp 2001CD28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7DC]  4: ~R sp 2001CE28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7FC]  5:  A sp 2001CEF8 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[81C]  6: *R sp 2001D068 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
4230 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
4234
5211 3: sending #103
5216 2: received #103 from 3
5234 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
5238
6238 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
6242
6270 4: calling #6
6273 2: received #6 from 4
6323 2: about to reply #250
6324 4: result #250 status 0
6718 3: sending #104
6723 2: received #104 from 3
[75C]  0: <S sp 2001D150 blkg  0 pend 00000000 fini 00000000 mbuf 2001CA7C
[77C]  1:  R sp 2001CB68 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[79C]  2: <S sp 2001CC38 blkg  2 pend 00000000 fini 00000000 mbuf 2001CC78
[7BC]  3:  R sp 2001CD28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7DC]  4: ~R sp 2001CE28 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[7FC]  5:  A sp 2001CEA8 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
[81C]  6: *R sp 2001D068 blkg -1 pend 00000000 fini 00000000 mbuf 00000000
7248 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
7252
8219 3: sending #105
8224 2: received #105 from 3
8252 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
8256
9256 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
9260
9727 3: sending #106
9732 2: received #106 from 3
10239 5: about to exit
10240 0: ipc CALL req 5 from 5
10263 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
10266
10325 4: calling #251
10327 2: received #251 from 4
10379 2: about to reply #5
10380 4: result #5 status 0
11227 3: sending #107
11231 2: received #107 from 3
11269 0: ipc CALL req 4 from 6
<demo 11 22 33 44>
11272
[...etc...]
```

Some performance measurements at 168 MHz, using the Cortex M4 cycle counter:

* context switching takes ≈ 1.1 µs
* system call overhead is ≈ 0.7 µs
* successful async IPC send ≈ 1.6 µs

These are _very early_ results, I'd still be ok if that would double later on.

-jcw, 2020-01-30
