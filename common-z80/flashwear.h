// Wear-leveling wrapper around the internal Flash driver.

class FlashWear {
    static constexpr bool DEBUG = false;
    static constexpr int SECLEN = 128;
    static constexpr int NUM_MODS = 500;
    static constexpr int SEC_PER_SEG = 1024;
    static constexpr int SEGSUSED = 6; // plus one spare

    typedef struct {
        uint16_t map [NUM_MODS];
        uint8_t flags [NUM_MODS];
        uint8_t phys [SEGSUSED];
        uint8_t spare [36-SEGSUSED];
        uint8_t sectors [NUM_MODS][SECLEN];
    } ModPage;

    typedef uint8_t Segment [SEC_PER_SEG][SECLEN];

    static ModPage const& mods;  // used to collect all changes
    static Segment const* segs;  // actual data storage segments
    static uint16_t fill;        // next unused entry in map

    static void reFlashChanges () {
        if (DEBUG)
            printf("reFlashChanges\n");

        uint16_t counts [SEGSUSED];
        memset(counts, 0, sizeof counts);

        // count how many changes there are in each segment, after merging
        for (int seg = 0; seg < SEGSUSED; ++seg) {

            // keep track of all sectors, only count each sector once
            uint8_t useBits [SEC_PER_SEG/8];
            memset(useBits, 0, sizeof useBits);

            for (int i = 0; i < fill; ++i)
                if (mods.map[i] / SEC_PER_SEG == seg) {
                    int n = mods.map[i] % SEC_PER_SEG;
                    if ((useBits[n/8] & (1<<(n%8))) == 0) {
                        useBits[n/8] |= 1<<(n%8);
                        ++counts[seg];
                    }
                }
        }

        // start re-flashing segments, until only a few changes remain
        uint8_t newPhys [SEGSUSED];
        memcpy(newPhys, mods.phys, sizeof newPhys);

        for (;;) {
            if (DEBUG) {
                printf("counts:");
                for (int i = 0; i < SEGSUSED; ++i)
                    printf(" %d", counts[i]);
                printf("\n");
                printf("newPhys:");
                for (int i = 0; i < SEGSUSED; ++i)
                    printf(" %d", newPhys[i]);
                printf("\n");
            }

            uint32_t remain = 0, nextSeg = 0;
            for (int i = 0; i < SEGSUSED; ++i) {
                remain += counts[i];
                if (counts[i] > counts[nextSeg])
                    nextSeg = i;
            }

            if (DEBUG)
                printf("remain %d nextSeg %d\n", remain, nextSeg);

            //if (remain <= 30) // TODO needs more logic to migrate remaining
            if (remain == 0) {
                // changes have been merged back, now clean up the mods page
                Flash::erasePage(&mods);
                for (int i = 0; i < SEGSUSED; ++i)
                    Flash::write8(mods.phys + i, newPhys[i]);
                fill = remain; // TODO will always be zero for now
                return;
            }

            // dumb way to find *the* segment which is currently not being used
            // careful, maps.phys/newPhys/freePhys are PHYSICAL segments (+1) !
            int freePhys = 0;
            for (int i = 1; i <= SEGSUSED+1; ++i)
                if (memchr(newPhys, i, SEGSUSED) == 0)
                    freePhys = i;

            // prepare to update the unused segment, leaving behind the old one
            if (DEBUG)
                printf("reflash seg %d: phys %d to %d\n",
                    nextSeg, newPhys[nextSeg], freePhys);

            // erase the segment, then copy the latest sector versions into it
            Flash::erasePage(segs[freePhys]);
            for (int i = 0; i < SEC_PER_SEG; ++i) {
                uint8_t buf [SECLEN];
                readSector(nextSeg * SEC_PER_SEG + i, buf);
                if (DEBUG)
                    printf("rewrite %d seg %d\n", i, freePhys);
                Flash::write32buf(segs[freePhys][i], (uint32_t*) buf, SECLEN/4);
            }

            newPhys[nextSeg] = freePhys;
            counts[nextSeg] = 0;
        }
    }

public:
    static bool valid () {
        for (int i = 0; i < SEGSUSED; ++i)
            if (mods.phys[0] <= 0 || mods.phys[0] > SEGSUSED+1)
                return false;
        return true;
    }

    static int init (bool erase =false) {
        if (DEBUG)
            printf("FlashWear %d, ModPage %d, Segment %d\n",
                    sizeof (FlashWear), sizeof (ModPage), sizeof (Segment));
        if (erase || !valid()) {
            printf("initialising internal flash\n");
            Flash::erasePage(&mods);
            for (int i = 0; i < SEGSUSED; ++i)
                Flash::write8(mods.phys + i, i+2);
        }
        for (fill = NUM_MODS; mods.map[fill-1] == 0xFFFF; --fill)
            if (fill == 0)
                break;
        if (DEBUG) {
            printf("fill %d, phys:", fill);
            for (int i = 0; i < SEGSUSED; ++i)
                printf(" %d", mods.phys[i]);
            printf("\n");
        }
        uint32_t memSizeKb = MMIO16(0x1FFF7A22); // TODO F407-specific?
        return (memSizeKb/256-1) * (256*8); // 512 = 1 disk, 1024 = 3 disks
    }

    static void readSector (int pos, void* buf) {
        for (int i = fill; --i >= 0; )
            if (mods.map[i] == pos) {
                if (DEBUG)
                    printf("readSector %d mod %d\n", pos, i);
                memcpy(buf, mods.sectors[i], SECLEN);
                return; // return modified sector
            }
        // no changed version found, return the original sector
        int segPhys = mods.phys[pos/SEC_PER_SEG];
        if (DEBUG)
            printf("readSector %d seg %d @ %d\n",
                    pos, segPhys, pos % SEC_PER_SEG);
        memcpy(buf, segs[segPhys][pos%SEC_PER_SEG], SECLEN);
    }

    static void writeSector (int pos, void const* buf) {
        if (fill >= NUM_MODS)
            reFlashChanges();
        int n = fill++;
        if (DEBUG)
            printf("writeSector %d mod %d\n", pos, n);
        Flash::write16(mods.map + n, pos);
        Flash::write32buf(mods.sectors[n], (uint32_t const*) buf, SECLEN/4);
    }
};

FlashWear::ModPage const& FlashWear::mods =
    *(FlashWear::ModPage const*) 0x00010000;  // @ 64K
FlashWear::Segment const* FlashWear::segs =
    (FlashWear::Segment const*) 0x00020000;  // @ 128K
uint16_t FlashWear::fill = 0;
