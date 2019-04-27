#include "SPIFFS.h"
#include "SD.h"

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

Context context;
uint8_t mainMem [1<<16];

#define NCHUNKS 30  // 120K available, e.g. two banks of 60K

uint8_t* chunkMem [NCHUNKS]; // lots of memory on ESP32, but it's fragmented!

File disk_fp;
File swap_fp;

void disk_init (fs::FS &fs) {
    disk_fp = fs.open("/rootfs.img", "r+");
    if (!disk_fp)
        printf("- can't open root\n");
    swap_fp = fs.open("/swap.img", "r+");
    if (!swap_fp)
        printf("- can't open swap\n");
}

void disk_read (File* fp, int pos, void* buf, int len) {
    fp->seek(pos * len);
    int e = fp->read((uint8_t*) buf, len);
    if (e != len)
        printf("r %d? fp %x pos %d len %d buf %x = %d\n", e, fp, pos, len, buf);
#if 0
    printf("\t\t\t      ");
    for (int i = 0; i < 16; ++i)
        printf(" %02x", ((uint8_t*) buf)[i]);
    printf("\n");
#endif
}

void disk_write (File* fp, int pos, void const* buf, int len) {
    fp->seek(pos * len);
    int e = fp->write((const uint8_t*) buf, len);
    if (e != len)
        printf("W %d? fp %x pos %d len %d buf %x = %d\n", e, fp, pos, len, buf);
}

static void setBankSplit (uint8_t page) {
    context.split = mainMem + (page << 8);
    memset(context.offset, 0, sizeof context.offset);

    int cpb = ((page << 8) + CHUNK_SIZE - 1) / CHUNK_SIZE; // chunks per bank
    // for each bank 1..up, assign as many chunks as needed for that bank
    for (int i = 1, n = 0; i < NBANKS && n < NCHUNKS; ++i)
        for (int j = 0; j < cpb; ++j) {
            // the offset adjusts the calculated address to point into the chunk
            // the first <cpb> entries remain zero, i.e. use unbanked mainMem
            int pos = i * CHUNK_COUNT + j;
            context.offset[pos] = chunkMem[n] - mainMem - j * CHUNK_SIZE;
            if (++n >= NCHUNKS)
                break;
        }

#if 0
    printf("setBank %02d=%04x, CHUNK_SIZE %d, NBANKS %d, NCHUNKS %d, cpb %d\n",
            page, page << 8, CHUNK_SIZE, NBANKS, NCHUNKS, cpb);
    printf("%d chunks:\n", NCHUNKS);
    for (int i = 0; i < NCHUNKS; ++i)
        printf("%4d: %08x\n", i, chunkMem[i]);
    printf("%d offsets:\n", CHUNK_TOTAL);
    for (int i = 0; i < CHUNK_TOTAL; ++i)
        printf("%4d: off %9d -> %x\n",
                i, context.offset[i], mainMem + context.offset[i]);
    for (int b = 0; b < NBANKS; ++b) {
        printf("bank %d test:", b);
        context.bank = b;
        for (int i = 0; i < 6; ++i)
            printf(" %08x", mapMem(&context, 1024 * i));
        printf("\n");
    }
    context.bank = 0;
#endif
}

void systemCall (Context* z, int req, int pc) {
    Z80_STATE* state = &(z->state);
#if 0
    if (req > 3)
        printf("\treq %d AF %04x BC %04x DE %04x HL %04x SP %04x @ %d:%04x\n",
                req, AF, BC, DE, HL, SP, context.bank, pc);
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
                uint8_t cnt = B & 0x7F;
                uint16_t pos = DE;  // no skewing
                File* fp = A == 0 ? &disk_fp : &swap_fp;

                // use intermediate buffer in case I/O spans different chunks
                uint8_t buf [512];
                for (int i = 0; i < cnt; ++i) {
#if 0
                    //void* mem = mapMem(&context, HL + 512*i);
                    printf("HD%d wr %d mem %d:0x%x pos %d\n",
                            A, out, context.bank, HL + 512*i, pos + i);
#endif
                    if (out) {
                        for (int j = 0; j < sizeof buf; ++j)
                            buf[j] = *mapMem(&context, HL + 512*i + j);
                        disk_write(fp, pos + i, buf, 512);
                    } else {
                        disk_read(fp, pos + i, buf, 512);
                        for (int j = 0; j < sizeof buf; ++j)
                            *mapMem(&context, HL + 512*i + j) = buf[j];
                    }
                }
            }
            A = 0;
            break;
        case 5: // time get/set
#if 0
            if (C == 0) {
                RTC::DateTime dt = rtc.get();
                //printf("mdy %02d/%02d/20%02d %02d:%02d:%02d (%d ms)\n",
                //        dt.mo, dt.dy, dt.yr, dt.hh, dt.mm, dt.ss, ticks);
                uint8_t* ptr = mapMem(&context, HL);
                int t = date2dr(dt.yr, dt.mo, dt.dy);
                ptr[0] = t;
                ptr[1] = t>>8;
                ptr[2] = dt.hh + 6*(dt.hh/10); // hours, to BCD
                ptr[3] = dt.mm + 6*(dt.mm/10); // minutes, to BCD
                ptr[4] = dt.ss + 6*(dt.ss/10); // seconcds, to BCD
            }
#endif
            break;
        case 6: // set banked memory limit
            setBankSplit(A);
            break;
        case 7: { // select bank and return previous setting
            uint8_t prevBank = context.bank;
            context.bank = A;
            A = prevBank;
            break;
        }
        case 8: { // for use in xmove, inter-bank copying
            uint8_t *src = mainMem + DE, *dst = mainMem + HL;
            // never map above the split, i.e. in the common area
            if (dst < context.split)
                dst += context.offset[(A>>4) % NBANKS];
            if (src < context.split)
                src += context.offset[A % NBANKS];
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
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    printf("SD Card Size: %lluMB\n", cardSize);

    listDir(SD, "/");
#else
    if (SPIFFS.begin(true))
        printf("- SPIFFS mounted:\n");

    listDir(SPIFFS, "/");
#endif

    // ESP32 heap memory is very fragmented, must allocate lots of small chunks
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

#if TTGOT8
    disk_init(SD);
    File fp = SD.open("/fuzix.bin", "r");
#else
    disk_init(SPIFFS);
    File fp = SPIFFS.open("/fuzix.bin", "r");
#endif

    const uint16_t origin = 0x0100;

    // embeded the fuzix binary using a special platformio trick, see
    // http://docs.platformio.org/en/latest/platforms/espressif32.html
    // in platformio.ini: build_flags = -DCOMPONENT_EMBED_TXTFILES=fuzix.bin
    extern const uint8_t fuzix_start[] asm("_binary_fuzix_bin_start");
    extern const uint8_t fuzix_end[]   asm("_binary_fuzix_bin_end");
    printf("fuzix.bin at 0x%08x, %u b\n", fuzix_start, fuzix_end-fuzix_start);
    memcpy(mainMem + origin, fuzix_start, fuzix_end - fuzix_start);

    printf("- start z80emu\n");

    Z80Reset(&context.state);
    context.state.pc = origin;
    context.done = 0;

    do {
        Z80Emulate(&context.state, 2000000, &context);
        digitalWrite(LED, !digitalRead(LED));
    } while (!context.done);

    printf("\n- done @ %04x\n", context.state.pc);
}

void loop () {}
