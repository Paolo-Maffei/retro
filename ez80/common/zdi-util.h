// ZDI access and utility code

void ezReset () {
    RST = 1;
    ZCL = 1; // p.257
    ZDA = 0; // p.243
    wait_ms(2);
    RST = 0;
    wait_ms(2);
    RST = 1;
}

static void delay () {
#if SLOW
    for (int i = 0; i < SLOW; ++i) __asm("nop"); // prevents optimisation
#endif
}

static void zcl (int f) {
    delay(); ZCL = f; delay();
}

static void zdiSet (bool f) {
    zcl(0); ZDA = f; zcl(1);
}

static uint8_t zdiInBits (bool last =0) {
    uint8_t b = 0;
    for (int i = 0; i < 8; ++i) {
        zcl(0); zcl(1);
        b <<= 1;
        b |= ZDA & 1;
    }
    zdiSet(last);
    return b;
}

static void zdiOutBits (uint8_t b, bool last =0) {
    for (int i = 0; i < 8; ++i) {
        zdiSet((b & 0x80) != 0);
        b <<= 1;
    }
    zdiSet(last);
}

static void zdiStart (uint8_t b, int rw) {
    ZDA = 0;
    zdiOutBits((b<<1) | rw);
}

static uint8_t zdiIn (uint8_t addr) {
    zdiStart(addr, 1);
    ZDA.mode(Pinmode::in_pullup);
    uint8_t b = zdiInBits(1);
    ZDA.mode(Pinmode::out);
    return b;
}

static void zdiOut (uint8_t addr, uint8_t val) {
    zdiStart(addr, 0);
    zdiOutBits(val, 1);
}

void zIns (uint8_t v0) {
    zdiOut(0x25, v0);
}

void zIns (uint8_t v0, uint8_t v1) {
    zdiStart(0x24, 0);
    zdiOutBits(v1);
    zdiOutBits(v0, 1);
}

void zIns (uint8_t v0, uint8_t v1, uint8_t v2) {
    zdiStart(0x23, 0);
    zdiOutBits(v2);
    zdiOutBits(v1);
    zdiOutBits(v0, 1);
}

void zIns (uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) {
    zdiStart(0x22, 0);
    zdiOutBits(v3);
    zdiOutBits(v2);
    zdiOutBits(v1);
    zdiOutBits(v0, 1);
}

void zIns (uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3, uint8_t v4) {
    zdiStart(0x21, 0);
    zdiOutBits(v4);
    zdiOutBits(v3);
    zdiOutBits(v2);
    zdiOutBits(v1);
    zdiOutBits(v0, 1);
}

void zCmd (uint8_t cmd) {
    zdiOut(0x16, cmd);
}

uint8_t getMbase () {
    zCmd(0x00); // read MBASE
    uint8_t b = zdiIn(0x12); // get U
    return b;
}

void setMbase (uint8_t b) {
#if 0
    // FIXME can't get this to work, why is MBASE not changing?
    zCmd(0x00); // read MBASE
    zdiOut(0x13, zdiIn(0x10)); // keep L
    zdiOut(0x14, zdiIn(0x11)); // keep H
    zdiOut(0x15, b); // set U
    zCmd(0x80); // write MBASE
#else
    zIns(0x3E,b); // ld a,<b>
    zIns(0xED, 0x6D); // ld mb,a
#endif
}

void setPC (uint32_t addr) {
    zdiOut(0x13, addr);
    zdiOut(0x14, addr >> 8);
    zdiOut(0x15, addr >> 16); // must be in ADL mode
    zCmd(0x87); // write PC
}

uint32_t getPC () {
    zCmd(0x07); // read PC
    uint8_t l = zdiIn(0x10);
    uint8_t h = zdiIn(0x11);
    uint8_t u = zdiIn(0x12); // only useful in ADL mode
    return (u<<16) | (h<<8) | l;
}

void readMem (uint32_t addr, void *ptr, unsigned len) {
    if (len > 0) {
        setPC(--addr); // p.255 start reading one byte early
        zdiStart(0x20, 1);
        ZDA.mode(Pinmode::in_pullup);
        zdiInBits(0); // ignore first read
        for (unsigned i = 0; i < len; ++i)
            ((uint8_t*) ptr)[i] = zdiInBits(i >= len-1);
        ZDA.mode(Pinmode::out);
    }
}

void writeMem (uint32_t addr, const void *ptr, unsigned len) {
    if (len > 0) {
        setPC(addr);
        zdiStart(0x30, 0);
        for (unsigned i = 0; i < len; ++i)
            zdiOutBits(((const uint8_t*) ptr)[i], i >= len-1);
    }
}

void dumpReg () {
    static const char* regs [] = {
        "AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC"
    };

    for (int i = 0; i < 8; ++i) {
        zCmd(i);
        uint8_t l = zdiIn(0x10);
        uint8_t h = zdiIn(0x11);
        uint8_t u = zdiIn(0x12);
        printf("  %s = %02x:%02x%02x", regs[i], u, h, l);
        if (i % 4 == 3)
            printf("\n");
    }
}

uint32_t seedBuf (uint32_t seed, uint8_t mask, uint8_t *ptr, unsigned len) {
    while (len--) {
        *ptr++ = (seed>>8) & mask;
        // see https://en.wikipedia.org/wiki/Linear_congruential_generator
        seed = (22695477U * seed) + 1U;
    }
    return seed;
}

bool memoryTest (uint32_t base, uint32_t size, uint8_t mask =0xFF) {
    // needs to be in ADL mode!
    uint8_t wrBuf [1<<8], rdBuf [1<<8];
    for (unsigned bank = 0; bank < 32; ++bank) {
        unsigned addr = base + (bank<<16);
        if (addr >= base + size)
            break;

        uint32_t seed = (bank+1) * mask;
        printf("%02xxxxx: %d ", addr >> 16, seed);

        for (unsigned offset = 0; offset <= 1<<16; offset += 1<<8) {
            if (addr + offset >= base + size)
                break;
            seed = seedBuf(seed, mask, wrBuf, sizeof wrBuf);
            writeMem(addr+offset, wrBuf, sizeof wrBuf);
        }
        printf(" => ");

        seed = (bank+1) * mask;
        for (unsigned offset = 0; offset <= 1<<16; offset += 1<<8) {
            if (addr + offset >= base + size)
                break;
            seed = seedBuf(seed, mask, wrBuf, sizeof wrBuf);
            readMem(addr+offset, rdBuf, sizeof rdBuf);
            if (memcmp(wrBuf, rdBuf, sizeof wrBuf) != 0) {
                printf(" *FAILED* in %04xxx", (addr+offset) >> 8);
                // show differences
                for (unsigned i = 0; i < sizeof wrBuf; ++i) {
                    if (i % 64 == 0)
                        printf("\n    %06x: ", addr+offset+i);
                    printf("%c", wrBuf[i] != rdBuf[i] ? '?' : '.');
                }
                if (mask != 0xFF) {
                    // show the bits which have been read as '1'
                    for (unsigned i = 0; i < sizeof wrBuf; ++i) {
                        if (i % 64 == 0)
                            printf("\n    %06x: ", addr+offset+i);
                        printf("%c", rdBuf[i] & mask ? '+' : ' ');
                    }
                } else {
                    // show bytes as written and as read back
                    for (unsigned i = 0; i < sizeof rdBuf; ++i) {
                        if (i % 8 == 0)
                            printf("\n\t%06x:", addr+offset+i);
                        printf(" %02x:%02x", wrBuf[i], rdBuf[i]);
                    }
                }
                printf("\n");
                return false;
            }
        }
        printf(" %08x  OK\n", seed);
    }
    return true;
}

uint8_t memRepeatMap (uint8_t val) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t tst;
        readMem(0x800000 + (1<<(22-i)), &tst, 1);
        if (tst == val)
            r |= 1<<i;
    }
    return r;
}

int memSizer () {
    uint8_t val;
    // get the current byte at the start of memory
    readMem(0x800000, &val, 1);
    // look for repetitions at a+4M, a+2M, a+1M, a+512K .. a+32K
    uint8_t v = memRepeatMap(val);
    // increment the value stored in memory
    ++val; writeMem(0x800000, &val, 1);    // v+1
    // look for repetitions again, ignore any not seen before
    v &= memRepeatMap(val);
    // restore the original value so this test is non-destructive
    --val; writeMem(0x800000, &val, 1);    // restore
    // now find the smallest repetition, this will be the memory size
    if (v & 1)
        for (int i = 0; i < 8; ++i)
            if ((v & (1<<i)) == 0) {
                return 1 << (13-i);
                printf("%d KB\n", 1<<(13-i));
            }
    return 0; // no sensible memory size detected
}
