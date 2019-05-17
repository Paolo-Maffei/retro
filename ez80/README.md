In order of development:

| Project | Description |
|---:|---|
| `common/` | C++ header files shared by multiple projects |
| `zdi-arm/` | Interactive test environment to test and debug ZDI |
| `zdi-esp/` | Same as `zdi-arm`, but using an ESP32 WROVER board |
| `cpm2-arm/` | Copy disk image from SD card to RAM and launch CP/M 2.2 |
| `run-arm/` | Set up ZDI, boot eZ80 from flash, then pass console I/O |
