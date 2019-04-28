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

#if LOLIN32
constexpr int LED = 22; // not 5!
#elif WROVER
constexpr int LED = 5; // no built-in, use the LCD backlight
#elif TTGOT8
constexpr int LED = 21; // reusing wrover board def
#else
constexpr int LED = BUILTIN_LED;
#endif

#if TTGOT8
#define MYFS SD
#else
#define MYFS SPIFFS
#endif

#define BLKSZ 512

uint8_t mainMem [1<<16];

#define NCHUNKS 45  // 180K available, e.g. three banks of 60K
uint8_t* chunkMem [NCHUNKS]; // lots of memory on ESP32, but it's fragmented!

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
} mappedRoot, mappedSwap;

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

SpiFlashWear<EspFlash,512> flassDisk;

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
    if (z->nbanks > NBANKS)
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
    bool hasFlashDisk = EspFlash::base != 0;

    // use intermediate buffer in case I/O spans different chunks
    uint8_t buf [BLKSZ];

    if (out) {
        for (int j = 0; j < sizeof buf; ++j)
            buf[j] = *mapMem(z, addr + j);

        if (disk == 0)
            mappedRoot.writeBlock(pos, buf);
        else if (hasFlashDisk)
            flassDisk.writeBlock(pos, buf);
        else
            mappedSwap.writeBlock(pos, buf);
    } else {
        if (disk == 0)
            mappedRoot.readBlock(pos, buf);
        else if (hasFlashDisk)
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
            uint8_t cnt = B & 0x7F;
            for (int i = 0; i < cnt; ++i)
                diskReq(z, out, A, DE + i, HL + BLKSZ * i);
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
        if (!file.isDirectory())
            printf("    %-15s %8d\n", file.name(), file.size());
        //file.close();
    }

    //root.close();
}

void setup () {
    Serial.begin(115200);
    pinMode(LED, OUTPUT);

#if TTGOT8
    SPI.begin(14, 2, 15, 13);

    if(!SD.begin(13)){
        printf("Card Mount Failed\n");
        return;
    }
    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        printf("No SD card attached\n");
        return;
    }

    const char* s = "UNKNOWN";
    switch (cardType) {
        case CARD_MMC:  s = "MMC";  break;
        case CARD_SD:   s = "SDSC"; break;
        case CARD_SDHC: s = "SDHC"; break;
    }
    printf("SD Card Type: %s\n", s);

    uint32_t cardSize = SD.cardSize() >> 20;
    printf("SD Card Size: %lu MB\n", cardSize);

    listDir(SD, "/");
#else
    if (SPIFFS.begin(true))
        printf("- SPIFFS mounted:\n");

    listDir(SPIFFS, "/");
#endif

    // ESP32 heap can be very fragmented, must allocate lots of small chunks
    //heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    //printf("- free %d max %d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    for (int i = 0; i < NCHUNKS; ++i) {
#if TTGOT8
        chunkMem[i] = (uint8_t*) ps_malloc(CHUNK_SIZE);
#else
        chunkMem[i] = (uint8_t*) malloc(CHUNK_SIZE);
#endif
        if (chunkMem[i] == 0)
            printf("- can't allocate memory chunk %d\n", i);
    }
    printf("- free mem %d\n", ESP.getFreeHeap());

    File root = MYFS.open("/root.img", "r+");
    if (!root) {
        printf("- can't open root\n");
        return;
    }
    mappedRoot.init(&root);

    File swap = MYFS.open("/swap.img", "w+");
    if (!swap) {
        printf("- can't open swap\n");
        return;
    }
    mappedSwap.init(&swap);

    const uint16_t origin = 0x0100;

    // embeded the fuzix binary using a special platformio trick, see
    // http://docs.platformio.org/en/latest/platforms/espressif32.html
    // in platformio.ini: build_flags = -DCOMPONENT_EMBED_TXTFILES=fuzix.bin
    extern const uint8_t fuzix_start[] asm("_binary_fuzix_bin_start");
    extern const uint8_t fuzix_end[]   asm("_binary_fuzix_bin_end");
    unsigned size = fuzix_end - fuzix_start;
    printf("  fuzix.bin @ 0x%08x, %u b\n", (int32_t) fuzix_start, size);
    memcpy(mainMem + origin, fuzix_start, size);

    //spi_flash_mmap_dump();
    const esp_partition_t* ep = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) 0x01, 0
    );
    if (ep != 0) {
        printf("- esp disk partition %08x\n", ep->address);
        EspFlash::init(ep);
    }

    printf("- start z80emu\n");

    static Context context; // just static so it starts out cleared
    Z80Reset(&context.state);
    context.state.pc = origin;

    do {
        Z80Emulate(&context.state, 5000000, &context);
        digitalWrite(LED, !digitalRead(LED));
    } while (!context.done);

    printf("\n- done @ %04x\n", context.state.pc);
}

void loop () {}
