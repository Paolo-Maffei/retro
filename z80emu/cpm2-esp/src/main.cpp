#include "SPIFFS.h"

#define printf Serial.printf

#undef F
#undef DEC

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

// cat boot.com bdos22.com bios.com | xxd -i >../common-z80/rom-cpm.h
const uint8_t rom [] = {
#include "rom-cpm.h"
};

// xxd -i <hexsave.com >../common-z80/hexsave.h
const uint8_t ram [] = {
#include "hexsave.h"
};

Context context;
uint8_t mainMem [1<<16];

File disk_fp;

void disk_init () {
    disk_fp = SPIFFS.open("/fd.img", "r+");
    if (disk_fp == 0)
        printf("- can't open root\n");
}

void disk_read (int pos, void* buf, int len) {
    disk_fp.seek(pos * len);
    int e = disk_fp.read((uint8_t*) buf, len);
    if (e != len)
        printf("r %d: pos %d len %d buf %x = %d\n", e, pos, len, buf);
#if 0
    printf("\t\t\t      ");
    for (int i = 0; i < 16; ++i)
        printf(" %02x", ((uint8_t*) buf)[i]);
    printf("\n");
#endif
}

void disk_write (int pos, void const* buf, int len) {
    disk_fp.seek(pos * len);
    int e = disk_fp.write((const uint8_t*) buf, len);
    if (e != len)
        printf("W %d: pos %d len %d buf %x = %d\n", e, pos, len, buf);
}

void systemCall (Context* z, int req, uint16_t pc) {
    Z80_STATE* state = &(z->state);
    //printf("req %d A %d\n", req, A);
    switch (req) {
        case 0: // coninst
            A = Serial.available() ? 0xFF : 0x00;
            break;
        case 1: // conin
            while (!Serial.available()) {}
            A = Serial.read();
            break;
        case 2: // conout
            Serial.write(C);
            break;
        case 3: // constr
            for (uint16_t i = DE; *mapMem(&context, i) != 0; i++)
                Serial.write(*mapMem(&context, i));
            break;
        case 4: // read/write
            //  ld a,(sekdrv)
            //  ld b,1 ; +128 for write
            //  ld de,(seksat)
            //  ld hl,(dmaadr)
            //  in a,(4)
            //  ret
            //printf("AF %04X BC %04X DE %04X HL %04X\n", AF, BC, DE, HL);
            {
                bool out = (B & 0x80) != 0;
                uint8_t sec = DE, trk = DE >> 8, dsk = A, cnt = B & 0x7F;
                uint32_t pos = 2048*dsk + 26*trk + sec;  // no skewing

                for (int i = 0; i < cnt; ++i) {
                    void* mem = mapMem(&context, HL + 128*i);
#if 0
                    printf("HD%d wr %d mem 0x%x pos %d\n",
                            A, out, HL + 128*i, pos + i);
#endif
                    if (out)
                        disk_write(pos + i, mem, 128);
                    else
                        disk_read(pos + i, mem, 128);
                }
            }
            A = 0;
            break;
        default:
            printf("syscall %d @ %04x ?\n", req, pc);
            while (1) {}
    }
}

void listDir (const char * dirname) {
    File root = SPIFFS.open(dirname);
    if (!root || !root.isDirectory())
        printf("- can't open root dir\n");

    File file;
    while ((file = root.openNextFile()) != 0) {
        if (!file.isDirectory())
            printf("    %-15s %8d\n", file.name(), file.size());
        //file.close();
    }

    //root.close();
}

void setup () {
    Serial.begin(115200);

    if (SPIFFS.begin(true))
        printf("- SPIFFS mounted:\n");
    listDir("/");

    disk_init();

    // emulated room bootstrap, loads first disk sector to 0x0000
    disk_read(0, mapMem(&context, 0x0000), 128);

    // leave a copy of HEXSAVE.COM at 0x0100
    memcpy(mapMem(&context, 0x0100), ram, sizeof ram);

    // start emulating
    Z80Reset(&context.state);
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
    } while (!context.done);
}

void loop () {
}
