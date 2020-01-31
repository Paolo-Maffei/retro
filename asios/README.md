# A new operating system

This area contains a kernel, boot loader, and applications for a small operating
system, running on STM32F407 µCs, which is an ARM Cortex M4. And although it's
for embedded processor use, it's a bit different from most Real-Time OS's:

* the core is based on a microkernel, very much like (a reduced) Minix 3 O/S
* the application(s) and the O/S (kernel, etc) can be compiled separately
* applications run in unprivileged mode and have limited access to the hardware
* communication between applications and the O/S happens via system & IPC calls
* some tasks can have extra privileges and can be used as drivers and servers
* when the MPU is enabled, each task can only access its own memory space(s)
* applications, including drivers and servers, can be installed without a reboot
* application faults lead to task termination, not to system hangs or failures

Given enough effort, this O/S might well be turned into a POSIX-compliant
operating system, with all the Unix'y features that come with it. Then again,
without virtual memory (i.e. no MMU), it'd probably be more like ucLinux.

But that's not the point of all this. The reason for creating this little
creature, is to have a very clean and robust platform to develop embedded
software in a more *modular* fashion. Instead of linking a growing application
with a small piece of code I'm currently working on (say an LCD driver), I'd
like to keep everything running, and just replace that piece with new versions.

It will take a while to reach this point, but even now, with just a bare kernel
in place, I can already see how drivers and servers could gradually be built and
added to this system, without turning everything into a monolithic "build".
_As I see it, incremental development is the name of the game._

I'm convinced that if an 8-bit Z80 with 64k RAM can run CP/M with tons of
applications, and if MS/DOS was able to do the same with a little more silicon,
then something as powerful as an ARM Cortex, running at 168 MHz, with over 192k
of RAM (and lots more with proper FSMC memory and SPI flash chips), should
_easily_ be able to run circles around these old systems. With just a few chips.

The layout of this project area is as follows:

* `core/` - This contains the microkernel, is built and installed with
  PlatformIO, and ends up at the boot location in flash (0x08000000..3FFF),
  using only the top 8k of RAM (0x2001E000..FFFF). Also linked in, is the
  "system task", which acts as interface to privileged mode access. This task is
  responsible for loading and launching the "boot" task.

* `boot/` - This is a self-contained image, built in the same way as the core,
  and saved in flash just above the kernel (0x08004000 and up). It can be
  anything, from a single large application which takes up all remaining flash
  and RAM, launching built-in tasks as it sees fit, to a small "system boot"
  which starts up one or more small processes, turning this into an elaborate
  collection of little drivers, servers, and user applications.

* `apps/` - This area is for building small self-contained applications, which
  could be saved in internal flash, or some external flash chip or disk. Apps
  can be loaded entirely in RAM or run from flash with only r/w data in RAM.
  The entire interface with the system is meant to go through system calls, but
  what they mean and which part of the system processes them is determined only
  by how the remaining tasks have been configured.

Right now, only the core has been implemented, with a proof-of-concept boot
image which does nothing but blink an LED. It's launched as task #1 by the
system task (always task #0) and will be time-sliced, illustrating that all
tasks run concurrently, and cannot get around the preemptive scheduling.

One other note: the kernel is written in C++11, because I get a lot of mileage
out of this language. But the system call "barrier" between all tasks and the
O/S means that there's no dependency on C++. I might decide to write new tasks
(drivers, servers, etc) in plain ANSI C, so that *if* one day there will be a C
compiler running *on* this system, it'll allow self-hosted recompilation.

-jcw, 2020-01-31
