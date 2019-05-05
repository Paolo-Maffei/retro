#include "SPIFFS.h"
#include "SD.h"
#include "spiflash-wear.h"
#include "esp_partition.h"

#define printf Serial.printf

#undef F
#undef DEC

extern "C" {
#include "context.h"
#include "z80emu.h"
#include "macros.h"
}

constexpr int BLKSZ = 512;

#ifndef LED
#define LED LED_BUILTIN
#endif

const uint8_t ram [] = {
#include "hexsave.h"
};

// embeded the system binary using a special platformio trick, see
// http://docs.platformio.org/en/latest/platforms/espressif32.html
// in config file: build_flags = -DCOMPONENT_EMBED_TXTFILES=system.bin
extern const uint8_t system_start[] asm("_binary_system_bin_start");
extern const uint8_t system_end[]   asm("_binary_system_bin_end");

bool hasSdCard, hasSpiffs, hasRawFlash, hasPsRam;
uint8_t mainMem [1<<16], *ramDisk;

#define NCHUNKS 32  // 128K available, e.g. at least two banks of any size
uint8_t* chunkMem [CHUNK_TOTAL]; // lots of memory on ESP32, but fragmented!

File boot, root, swap;

struct MappedDisk {
    File* fp = 0;

    void init (File* fptr) {
        fp = fptr;
    }

    int readBlock (unsigned pos, void* buf) {
        if (fp == 0) {
            printf("can't read block %d, file not open\n", pos);
            return 0;
        }
        fp->seek(pos * BLKSZ);
        int e = fp->read((uint8_t*) buf, BLKSZ);
        if (e != BLKSZ)
            printf("r %d? fp %08x pos %d buf %08x\n",
                    e, (int32_t) fp, pos, (int32_t) buf);
#if 0
        printf("\t\t\t      ");
        for (int i = 0; i < 16; ++i)
            printf(" %02x", ((uint8_t*) buf)[i]);
        printf("\n");
#endif
        return e;
    }

    int writeBlock (unsigned pos, void const* buf) {
        if (fp == 0) {
            printf("can't write block %d, file not open\n", pos);
            return 0;
        }
        fp->seek(pos * BLKSZ);
        int e = fp->write((const uint8_t*) buf, BLKSZ);
        if (e != BLKSZ)
            printf("W %d? fp %08x pos %d buf %08x\n",
                    e, (int32_t) fp, pos, (int32_t) buf);
        return e;
    }
} mappedDisk[9]; // fd0..fd3 => 0..3, hda..hdd => 4..7, rd0..rd1 => 8

struct EspFlash {
    constexpr static uint32_t pageSize = 4096;

    static const esp_partition_t* base;

    static void init (const esp_partition_t* ep) {
        base = ep;
    }

    static void read (int pos, void* buf, int len) {
        int e = base == 0 ? -1 : esp_partition_read(base, pos, buf, len);
        if (e != 0)
            printf("flash write %d?\n", e);
    }

    static void write (int pos, void const* buf, int len) {
        int e = base == 0 ? -1 : esp_partition_write(base, pos, buf, len);
        if (e != 0)
            printf("flash write %d?\n", e);
    }

    static void erase (int pos) {
        int e = base == 0 ? -1 : esp_partition_erase_range(base, pos, pageSize);
        if (e != 0)
            printf("flash erase %d?\n", e);
    }
};

const esp_partition_t* EspFlash::base = 0;

SpiFlashWear<EspFlash,BLKSZ> flashDisk;

static void setBankSplit (Context* z, uint8_t page) {
    z->split = mainMem + (page << 8);
    memset(z->offset, 0, sizeof z->offset);

    int cpb = ((page << 8) + CHUNK_SIZE - 1) / CHUNK_SIZE; // chunks per bank
    // for each bank 1..up, assign as many chunks as needed for that bank
    for (int i = 1, n = 0; i < NBANKS && n < NCHUNKS; ++i)
        for (int j = 0; j < cpb; ++j) {
            // the offset adjusts the calculated address to point into the chunk
            // the first <cpb> entries remain zero, i.e. use unbanked mainMem
            int pos = i * CHUNK_COUNT + j;
            z->offset[pos] = chunkMem[n] - mainMem - j * CHUNK_SIZE;
            if (++n >= NCHUNKS)
                break;
        }

    // number of fully populated banks, incl main mem
    z->nbanks = NCHUNKS / cpb + 1;
    if (hasPsRam || z->nbanks > NBANKS)
        z->nbanks = NBANKS;

#if 0
    printf("setBank %02d=%04x, CHUNK_SIZE %d, NBANKS %d, NCHUNKS %d, cpb %d\n",
            page, page << 8, CHUNK_SIZE, NBANKS, NCHUNKS, cpb);
    printf("%d chunks:\n", NCHUNKS);
    for (int i = 0; i < NCHUNKS; ++i)
        printf("%4d: %08x\n", i, chunkMem[i]);
    printf("%d offsets:\n", CHUNK_TOTAL);
    for (int i = 0; i < CHUNK_TOTAL; ++i)
        printf("%4d: off %9d -> %x\n",
                i, z->offset[i], mainMem + z->offset[i]);
    for (int b = 0; b < NBANKS; ++b) {
        printf("bank %d test:", b);
        z->bank = b;
        for (int i = 0; i < 6; ++i)
            printf(" %08x", mapMem(z, 1024 * i));
        printf("\n");
    }
    z->bank = 0;
#endif
}

int diskReq (Context* z, bool out, uint8_t dev, uint16_t pos, uint16_t addr) {
    // use intermediate buffer, but only when I/O spans different chunks
    uint8_t buf [BLKSZ];
    uint8_t *first = mapMem(z, addr), *last = mapMem(z, addr+BLKSZ-1);
    uint8_t *ptr = first+BLKSZ-1 == last ? first : buf;

    int type = dev >> 6;
    int unit = dev & 0x0F;
    // each hard disk can be split into up to 16 partitions of 8 MB each
    unsigned blk = pos; // switch to 32-bit to handle disks > 32 MB

    switch (type) {
        case 1: // HD
            blk += 16384 * unit;        // hda1, hda2, etc - i.e. N*8 MB higher
            unit = (dev >> 4) & 0x03;   // unit is taken from bits 4 & 5
            unit += 4;
            break;
        case 2: // RD
            blk += 2048 * unit;         // rd0, rd1, etc - i.e. N*1 MB higher
            unit = 8;                   // all mapped to a single device
            break;
    }
#if 0
    printf("dev %d type %d unit %d wr %d mem %d:0x%x pos %d blk %d\n",
            dev, type, unit, out, z->bank, addr, pos, blk);
#endif

    int n = 0;
    if (out) {
        if (ptr == buf)
            for (int i = 0; i < sizeof buf; ++i)
                buf[i] = *mapMem(z, addr+i);

        if (type == 2 && hasPsRam)
            memcpy(ramDisk + blk*BLKSZ, ptr, n = BLKSZ);
        else if (type == 2 && hasRawFlash)
            n = flashDisk.writeBlock(blk, ptr);
        else
            n = mappedDisk[unit].writeBlock(blk, ptr);
    } else {
        if (type == 2 && hasPsRam)
            memcpy(ptr, ramDisk + blk*BLKSZ, n = BLKSZ);
        else if (type == 2 && hasRawFlash)
            n = flashDisk.readBlock(blk, ptr);
        else
            n = mappedDisk[unit].readBlock(blk, ptr);

        if (ptr == buf)
            for (int i = 0; i < sizeof buf; ++i)
                *mapMem(z, addr+i) = buf[i];
    }
    //return n == BLKSZ ? 0 : 1; // TODO different error returns
    return 0;
}

void systemCall (Context* z, int req, int pc) {
    Z80_STATE* state = &(z->state);
#if 0
    if (req > 3)
        printf("\treq %d AF %04x BC %04x DE %04x HL %04x SP %04x @ %d:%04x\n",
                req, AF, BC, DE, HL, SP, z->bank, pc);
#endif
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
            for (uint16_t i = DE; *mapMem(z, i) != 0; i++)
                Serial.write(*mapMem(z, i));
            break;
        case 4: { // read/write
            //  ld a,(sekdrv)
            //  ld b,1 ; +128 for write
            //  ld de,(seksat)
            //  ld hl,(dmaadr)
            //  in a,(4)
            //  ld (result),a
            bool out = (B & 0x80) != 0;
            uint8_t cnt = B & 0x7F, dsk = A;
            A = 0;
            for (int i = 0; i < cnt; ++i) {
                A = diskReq(z, out, dsk, DE + i, HL + BLKSZ * i);
                if (A != 0)
                    break;
            }
            break;
        }
        case 5: // time get/set
#if 0
            if (C == 0) {
                RTC::DateTime dt = rtc.get();
                //printf("mdy %02d/%02d/20%02d %02d:%02d:%02d (%d ms)\n",
                //        dt.mo, dt.dy, dt.yr, dt.hh, dt.mm, dt.ss, ticks);
                uint8_t* ptr = mapMem(z, HL);
                int t = date2dr(dt.yr, dt.mo, dt.dy);
                ptr[0] = t;
                ptr[1] = t>>8;
                ptr[2] = dt.hh + 6*(dt.hh/10); // hours, to BCD
                ptr[3] = dt.mm + 6*(dt.mm/10); // minutes, to BCD
                ptr[4] = dt.ss + 6*(dt.ss/10); // seconcds, to BCD
            }
#endif
            break;
        case 6: // set banked memory limit, return number available
            setBankSplit(z, A);
            A = z->nbanks;
            break;
        case 7: { // select bank and return previous setting
            uint8_t prevBank = z->bank;
            z->bank = A;
            A = prevBank;
            break;
        }
        case 8: { // for use in xmove, inter-bank copying
            uint8_t *src = mainMem + DE, *dst = mainMem + HL;
            // never map above the split, i.e. in the common area
            if (dst < z->split)
                dst += z->offset[(A>>4) % NBANKS];
            if (src < z->split)
                src += z->offset[A % NBANKS];
            // TODO careful, this won't work across the split!
            memcpy(dst, src, BC);
            DE += BC;
            HL += BC;
            break;
        }
        default:
            printf("syscall %d @ %04x ?\n", req, pc);
            while (1) {}
    }
}

void listDir (fs::FS &fs, const char * dirname) {
    File root = fs.open(dirname);
    if (!root || !root.isDirectory())
        printf("- can't open root dir\n");

    File file;
    while ((file = root.openNextFile()) != 0) {
        if (file.name()[1] == '.')
            continue;
        if (file.isDirectory())
            printf("    %s/\n", file.name());
        else
            printf("    %-15s %8d b\n", file.name(), file.size());
        //file.close();
    }

    //root.close();
}

bool initPsRam () {
    return ESP.getPsramSize() > 1000000;
}

bool initRawFlash () {
    const esp_partition_t* ep = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) 0x01, 0
    );
    if (ep != 0) {
        printf("- %u KB raw flash partition at 0x%08x\n",
                ep->size >> 10, ep->address);
        EspFlash::init(ep);
        return true;
    }
    return false;
}

bool initSdCard () {
    SPI.begin(14, 2, 15, 13);
    if (SD.begin(13, SPI, 120000000) != 0) {
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            printf("No SD card inserted\n");
            return false;
        }
        const char* type = "UNKNOWN";
        switch (cardType) {
            case CARD_MMC:  type = "MMC";  break;
            case CARD_SD:   type = "SDSC"; break;
            case CARD_SDHC: type = "SDHC"; break;
        }
        uint32_t size = SD.cardSize() >> 20;
        uint32_t free = (SD.totalBytes() - SD.usedBytes()) >> 20;
        printf("- %s card, size %u MB, free %u MB:\n", type, size, free);
        listDir(SD, "/");
        return true;
    }
    printf("SD card mount failed\n");
    return false;
}

bool initSpiffs () {
    if (SPIFFS.begin() != 0) {
        uint32_t size = SPIFFS.totalBytes() >> 10;
        uint32_t free = (SPIFFS.totalBytes() - SPIFFS.usedBytes()) >> 10;
        printf("- SPIFFS size %u KB, free %u KB:\n", size, free);
        listDir(SPIFFS, "/");
        return true;
    }
    printf("No SPIFFS partition found\n");
    SPIFFS.format();
    return false;
}

File openSdOrSpiffs (const char* name, const char* mode) {
    File fd;
    if (hasSdCard) {
        fd = SD.open(name, mode);
        if (fd && SD.exists(name))
            printf("- SD: %s (%d b)\n", name, fd.size());
    }
    if (!fd && hasSpiffs) {
        fd = SPIFFS.open(name, mode);
        if (fd && SPIFFS.exists(name))
            printf("- SPIFFS: %s (%d b)\n", name, fd.size());
    }
    return fd;
}

bool createSystemDisk (File& fp, const char* name) {
    fp = openSdOrSpiffs(name, "w+");
    if (!fp)
        return false;

    unsigned size = system_end - system_start;

    fp.seek(0);
    if (fp.write(system_start, size) != size)
        return false;

    uint8_t buf [128];
    memset(buf, 0xE5, sizeof buf);

    fp.seek(2 * 26 * 128);
    for (int i = 0; i < 26; ++i)
        if (fp.write(buf, sizeof buf) != sizeof buf)
            return false;

    printf("- system.bin @ 0x%08x, %u b\n", (int32_t) system_start, size);
    fp.flush();
    return true;
}

void allocateChunks () {
    // ESP32 heap can be very fragmented, must allocate lots of small chunks

    //heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    //printf("- free %d max %d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    for (int i = 0; i < NCHUNKS; ++i) {
        chunkMem[i] = (uint8_t*) malloc(CHUNK_SIZE);
        if (chunkMem[i] == 0) {
            printf("- can't allocate RAM chunk %d\n", i);
            return;
        }
    }
    printf("- RAM heap size %u KB, free %u KB\n",
            ESP.getHeapSize() >> 10, ESP.getFreeHeap() >> 10);

    // with PSRAM, we can allocate additional chunks for the rest of the banks
    // TODO there's no need to use small chunks for this as well
    if (!hasPsRam)
        return;

    for (int i = NCHUNKS; i < CHUNK_TOTAL; ++i) {
        chunkMem[i] = (uint8_t*) ps_malloc(CHUNK_SIZE);
        if (chunkMem[i] == 0) {
            printf("- can't allocate PSRAM chunk %d\n", i);
            return;
        }
    }

    // TODO ramdisk size is fixed 3.5 MB for now, of which 1 MB swap
    ramDisk = (uint8_t*) ps_malloc(7<<19);

    printf("- PSRAM size %u KB, available %u KB\n",
            ESP.getPsramSize() >> 10, ESP.getFreePsram() >> 10);
}

void setup () {
    Serial.begin(115200);
    printf("\n");
    pinMode(LED, OUTPUT);

    hasPsRam = initPsRam();
    allocateChunks(); // allocate before SD and SPIFFS are initialised

    hasRawFlash = initRawFlash ();
    hasSpiffs = initSpiffs();
    hasSdCard = initSdCard();

    boot = openSdOrSpiffs("/fd0.img", "r+");
    if (boot.size() == 0) {
        printf("No boot disk, creating ...\n");
        if (!createSystemDisk(boot, "/fd0.img")) {
            printf("Cannot create boot disk\n");
            return;
        }
    }
    mappedDisk[0].init(&boot);

    root = openSdOrSpiffs("/hda.img", "r+");
    if (!root)
        printf("Can't find root disk\n");
    mappedDisk[4].init(&root);

    if (!hasPsRam && !hasRawFlash) {
        swap = openSdOrSpiffs("/swap.img", "w+");
        if (!swap)
            printf("Can't find swap disk\n");
        mappedDisk[8].init(&swap);
    }

    static Context context; // just static so it starts out cleared
    Z80Reset(&context.state);

    // load and launch fuzix.bin if it exists
    const char* kernel = "/fuzix.bin";
    const uint16_t origin = 0x0100;

    File fp = openSdOrSpiffs(kernel, "r");
    if (fp && fp.read(mainMem + origin, 60000) > 1000) {
        fp.close();
        printf("- launching fuzix.bin\n");
        context.state.pc = origin;
    } else {
        // emulated room bootstrap, loads first disk sector to 0x0000
        mappedDisk[0].readBlock(0, mainMem);

        // leave a copy of HEXSAVE.COM at 0x0100
        memcpy(mainMem + 0x0100, ram, sizeof ram);
    }

    printf("- start z80emu\n");

    do {
        Z80Emulate(&context.state, 5000000, &context);
        digitalWrite(LED, !digitalRead(LED));
    } while (!context.done);

    printf("\n- done @ %04x\n", context.state.pc);
}

void loop () {}
