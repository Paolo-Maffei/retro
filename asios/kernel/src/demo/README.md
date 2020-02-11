# Using CP/M as a command shell

To get the new microkernel usable fast, it needs some kind of filesystem and
some kind of "shell", i.e. a way to type commands and interactively launch
programs.

So here's a crazy idea: why not use an existing system for managing files on a
(virtual) disk, and with a built-in "Console Command Processor" to type at?

That's what this code is about: it's a Z80 emulator, with all the functionality
needed to run CP/M 2.2, an ancient operating system from the 1970's which not
only handles these requirements, but also offers an environment for which a huge
range of software is still available.

## The basic idea

The kernel starts off running its built-in special _system task_ (task #0),
which in turn launches a _boot task_ (as task #1). Right now, the boot task does
very little: launch yet another task, which is this "demo" emulator, and exit.

All these tasks run from flash memory, but have been independently compiled.
Through a careful choice of flash and RAM areas, they can all run side-by-side
without any issues (the MPU has not yet been enabled, i.e. all tasks run in a
common full-address region, in unprivileged mode).

The Z80 emulator uses system calls to perform console- and disk I/O. It also has
one very special _"escape out of The Matrix"_ option, which will launch a new
task, running either from emulated Z80 program space or from flash / RAM located
outside of the emulator. As long as this task runs, the Z80 emulator will be
blocked. Once the task exits, Z80 emulation resumes where it left off.

So there are in essence two completely separate processor views of the world in
this curious little setup:

* the ARM Cortex-M4, running a microkernel with transparent multi-tasking
* a task with emulates a single Z80 CPU with 64k of RAM and a virtual disk

Apart from the special escape mode just described, code running inside the Z80
emulator has no awareness of its larger context, i.e. the multi-tasking
microkernel which orchestrates everything and runs on the "real" ARM Cortex-M4.

## Memory layout

```text
Kernel + system task:    FLASH 0x080000000 (16k)   RAM 0x2001E000 (8k)
Boot task (#1):          FLASH 0x080004000 (16k)   RAM 0x2001D000 (4k)
Demo/z80emu task (#2):   FLASH 0x080008000 (16k)   RAM 0x2001CC00 (1k)
Unused:                  FLASH 0x08000C000 (16k)   RAM 0x2001C000 (3k)
```

The remaining flash memory (64k plus 3x or 7x 128k, for F407xE resp. F407xG µCs)
is used for emulating one or more virtual disk drives (at least one is needed
for CP/M). The code includes wear leveling logic which turns this into 256k
resp. 768k of r/w "disk" space with 128-byte "sectors").

There are two additional RAM areas in STM32F407xx µCs:

* 64k CCM RAM is used by z80emu as emulated Z80 program memory
* 112k RAM (SRAM1) is unused so far, and intended for other RAM-based tasks

## What next?

The end effect of all this is that on startup, there's now a little _Z80
retro-computing_ environment, sufficient to upload "programs" to the virtual
disk drive, and to launch them interactively.

Some programs will consist 100% of Z80 code and run happily in this emulated
context.  Others can now start off with a little Z80-code "preamble", followed
by arbitrary stand-alone ARM Cortex-M4 code (e.g. C or C++). The preamble needs
to set things up properly, and can then jump through the escape hatch to launch
this code as "native ARM tasks", for (and managed by) the microkernel.

There is no protection at this point, so crashes, freezes, faults, and panics
are most likely. But this CP/M based setup creates a framework to work out all
the kinks, and to grow the microkernel further. At some point, a more
sophisticated shell could be added, making this CP/M setup obsolete - other than
for running some old "8-bit retro" code from the previous century, that is!

-jcw, 2020-02-10
