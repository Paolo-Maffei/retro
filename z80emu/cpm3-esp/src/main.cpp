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

const uint8_t ram [] = {
#include "hexsave.h"
};

#if LOLIN32
constexpr int LED = 22; // not 5!
#elif WROVER
constexpr int LED = 5; // no built-in, use the LCD backlight
#elif TTGOT8
constexpr int LED = 21; // reusing wrover board def
#elif ESP32SD
constexpr int LED = -1; // doesn't appear to have an LED
#else
constexpr int LED = LED_BUILTIN;
#endif

#define BLKSZ 128

bool hasSdCard, hasSpiffs, hasRawFlash, hasPsRam;

uint8_t mainMem [1<<16];

#define NCHUNKS 32  // 128K available, e.g. at least two banks of any size
uint8_t* chunkMem [CHUNK_TOTAL]; // lots of memory on ESP32, but fragmented!

struct MappedDisk {
    File* fp;

    void init (File* fptr) {
        fp = fptr;
    }

    void readBlock (int pos, void* buf) {
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
    }

    void writeBlock (int pos, void const* buf) {
        fp->seek(pos * BLKSZ);
        int e = fp->write((const uint8_t*) buf, BLKSZ);
        if (e != BLKSZ)
            printf("W %d? fp %08x pos %d buf %08x\n",
                    e, (int32_t) fp, pos, (int32_t) buf);
    }
} mappedBoot, mappedRoot, mappedSwap;

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

SpiFlashWear<EspFlash,BLKSZ> flassDisk;

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

void diskReq (Context* z, bool out, uint8_t disk, uint16_t pos, uint16_t addr) {
#if 0
    //void* mem = mapMem(z, addr);
    printf("HD%d wr %d mem %d:0x%x pos %d\n",
            disk, out, z->bank, addr, pos);
#endif
    // use intermediate buffer in case I/O spans different chunks
    uint8_t buf [BLKSZ];

    if (out) {
        for (int j = 0; j < sizeof buf; ++j)
            buf[j] = *mapMem(z, addr + j);

        if (disk == 0)
            mappedBoot.writeBlock(pos, buf);
        else if (hasRawFlash)
            flassDisk.writeBlock(pos, buf);
        else
            mappedSwap.writeBlock(pos, buf);
    } else {
        if (disk == 0)
            mappedBoot.readBlock(pos, buf);
        else if (hasRawFlash)
            flassDisk.readBlock(pos, buf);
        else
            mappedSwap.readBlock(pos, buf);

        for (int j = 0; j < sizeof buf; ++j)
            *mapMem(z, addr + j) = buf[j];
    }
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
            bool out = (B & 0x80) != 0;
            uint8_t cnt = B & 0x7F, dsk = A;
#if 1
            uint8_t sec = DE, trk = DE >> 8;
            uint32_t pos = 2048*dsk + 26*trk + sec;  // no skewing
            dsk = 0; // FIXME 0x10; // fd0 i.e. (1,0)
#else
            uint32_t pos = DE;
#endif
            for (int i = 0; i < cnt; ++i)
                diskReq(z, out, dsk, pos + i, HL + BLKSZ * i);
            A = 0;
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
        if (file.isDirectory())
            printf("    %s/\n", file.name());
        else
            printf("    %-15s %8d b\n", file.name(), file.size());
        //file.close();
    }

    //root.close();
}

bool initPsRam () {
    if (ESP.getPsramSize() > 1000000)
        return true;
    printf("- no PSRAM available\n");
    return false;
}

bool initRawFlash () {
    const esp_partition_t* ep = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) 0x01, 0
    );
    if (ep != 0) {
        printf("- esp disk partition %08x\n", ep->address);
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
    if (SPIFFS.begin(true) != 0) {
        uint32_t size = SPIFFS.totalBytes() >> 10;
        uint32_t free = (SPIFFS.totalBytes() - SPIFFS.usedBytes()) >> 10;
        printf("- SPIFFS size %u KB, free %u KB:\n", size, free);
        listDir(SPIFFS, "/");
        return true;
    }
    printf("No SPIFFS partition found\n");
    return false;
}

File openSdOrSpiffs (const char* name, const char* mode) {
    File fd;
    if (hasSdCard)
        fd = SD.open(name, mode);
    if (!fd && hasSpiffs)
        fd = SPIFFS.open(name, mode);
    if (!fd)
        printf("Can't open %s\n", name);
    return fd;
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
    hasSdCard = initSdCard();
    hasSpiffs = initSpiffs();

    File boot = openSdOrSpiffs("/fd0.img", "r+");
    if (!boot)
        return;
    mappedBoot.init(&boot);

    File root = openSdOrSpiffs("/fd1.img", "r+");
    if (!root)
        return;
    mappedRoot.init(&root);

    File swap = openSdOrSpiffs("/fd2.img", "w+");
    if (!swap)
        return;
    mappedSwap.init(&swap);

    printf("- start z80emu\n");

    static Context context; // just static so it starts out cleared
    Z80Reset(&context.state);

#if 1
    // emulated room bootstrap, loads first disk sector to 0x0000
    mappedBoot.readBlock(0, mainMem);

    // leave a copy of HEXSAVE.COM at 0x0100
    memcpy(mainMem + 0x0100, ram, sizeof ram);
#else
    // embeded the fuzix binary using a special platformio trick, see
    // http://docs.platformio.org/en/latest/platforms/espressif32.html
    // in platformio.ini: build_flags = -DCOMPONENT_EMBED_TXTFILES=fuzix.bin
    extern const uint8_t fuzix_start[] asm("_binary_fuzix_bin_start");
    extern const uint8_t fuzix_end[]   asm("_binary_fuzix_bin_end");
    unsigned size = fuzix_end - fuzix_start;
    printf("  fuzix.bin @ 0x%08x, %u b\n", (int32_t) fuzix_start, size);
    
    const uint16_t origin = 0x0100;
    memcpy(mainMem + origin, fuzix_start, size);
    context.state.pc = origin;
#endif

    do {
        Z80Emulate(&context.state, 5000000, &context);
        digitalWrite(LED, !digitalRead(LED));
    } while (!context.done);

    printf("\n- done @ %04x\n", context.state.pc);
}

void loop () {}
