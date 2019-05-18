[EZ-Retro](https://docs.jeelabs.org/projects/ezr/) is a simple board based on
the eZ80 µC, a 512..2048 KB static RAM chip, and a low-end STM32F103C8 board
which I'm calling the "Wide Pill". This area has a number of projects coded in
C/C++ with the F103 providing a 36 MHz clock, initialising and controlling the
eZ80 through two "ZDI" I/O pins, plus some pins for reset, serial comms, and
SPI.

In this folder, in order of development:

| Project | Description |
|---:|---|
| `common/` | C++ header files shared by multiple projects |
| `zdi-arm` | Interactive test environment to test and debug the eZ80 board over ZDI |
| `zdi-esp` | Similar to `zdi-arm`, but using an ESP32 WROVER instead of the Wide Pill |
| `cpm2-arm` | Copy disk image from SD card to RAM, then launch CP/M 2.2 |
| `run-arm` | Set up ZDI, boot eZ80 from its flash, then switch to pass-through console I/O |
