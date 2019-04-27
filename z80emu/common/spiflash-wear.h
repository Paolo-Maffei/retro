// Simple wear-leveling wrapper around a flash memory driver.
//
// This still has some hot spots, but it considerably reduces rewrite activity.
// Basic idea is to collect blockSize-byte block writes up in a dedicated page,
// and then rewrite the other pages once that special "remap page" fills up.
// Currently assumes a 4 KB erase page size and one remap page per 256 pages.

template< typename FLASH, int BYTESPERBLOCK >
class SpiFlashWear {
    constexpr static bool DEBUG = true;
    constexpr static uint32_t pageSize      = FLASH::pageSize;
    constexpr static uint32_t pagesPerGroup = 256; // TODO currently 1 MB
    constexpr static uint32_t groupSize     = pagesPerGroup * pageSize;
    constexpr static uint32_t blockSize     = BYTESPERBLOCK;
    constexpr static uint32_t groupBlocks   = groupSize / blockSize;
    constexpr static uint32_t pageBlocks    = pageSize / blockSize;

    int mapBase = -1;
    uint16_t map [blockSize/2];  // only [1..pageBlocks) entries actually used
    uint8_t flushBuf [pageSize];

    void loadMap (int blk) {
        // the map is on the first block of the last page of each group
        int base = (blk / groupBlocks + 1) * groupBlocks - pageBlocks;
        if (mapBase != base) {
            mapBase = base;
            readUnmapped(mapBase, map);
        }
    }

    int findFreeSlot () {
        for (uint32_t i = 1; i < pageBlocks; ++i)
            if (map[i] == 0xFFFF)
                return i;  // return first one found
        return 0;          // ... or zero if none
    }

    // find last mapped entry, or use the unmapped one if not found
    int remap (int blk) {
        int actual = blk;
        for (uint32_t i = 1; i < pageBlocks; ++i)
            if (map[i] == blk)
                actual = mapBase + i;
        if (DEBUG && actual != blk) printf("remap %d -> %d\n", blk, actual);
        return actual;
    }

    void writeNewSlot (int n, int blk, const void* buf) {
        if (DEBUG) printf("writeNewSlot n %d blk %d\n", n, blk);
        map[n] = blk;
        // first write the two changed bytes in the map
        FLASH::write(blockSize * mapBase + 2 * n, map + n, 2);
        // then write the block in the freshly allocated slot
        writeUnmapped(mapBase + n, buf);
    }

    void flushMapEntries () {
        int groupBase = (mapBase / groupBlocks) * groupBlocks;
        if (DEBUG) printf("flushMapEntries %d..%d\n", groupBase, mapBase);

        if (DEBUG) {
            printf("map:");
            for (uint32_t i = 0; i < pageBlocks; ++i)
                printf(" %d", map[i]);
            printf("\n");
        }

        // go through all the pages and rewrite them if they have map entries
        for (int g = groupBase; g < mapBase; g += pageBlocks) {
            for (uint32_t slot = 1; slot < pageBlocks; ++slot)
                if (g <= map[slot] && map[slot] < g + pageBlocks) {
                    if (DEBUG) printf("flushing %d..%d\n", g, g+pageBlocks-1);
                    // there is at least one remapped block we need to flush
                    // so first, read all the blocks, with remapping
                    for (uint32_t i = 0; i < pageBlocks; ++i)
                        readBlock(g + i, flushBuf + blockSize * i);
                    // ... and then, write out all the blocks, unmapped
                    // XXX power loss after this point can lead to data loss
                    // the reason is that the first write will do a page erase
                    for (uint32_t i = 0; i < pageBlocks; ++i)
                        writeUnmapped(g + i, flushBuf + blockSize * i);
                    // XXX end of critical area, power loss is no longer risky
                    break;
                }
        }

        if (DEBUG) printf("flush done, clear map %d\n", mapBase);
        memset(map, 0xFF, sizeof map);
        FLASH::erase(blockSize * mapBase);
    }

    void readUnmapped (int blk, void* buf) {
        FLASH::read(blockSize * blk, buf, blockSize);
    }

    void writeUnmapped (int blk, const void* buf) {
        if (blk % pageBlocks == 0)
            FLASH::erase(blockSize * blk);
        FLASH::write(blockSize * blk, buf, blockSize);
    }

    int remapBlock (int blknum) {
        // renumber block accesses so they never refer to the remap pages
        return (blknum / (groupBlocks-pageBlocks)) * groupBlocks;
    }

public:
    void init () {}

    void readBlock (int blknum, void* buf) {
        int blk = remapBlock(blknum);
        loadMap(blk);
        readUnmapped(remap(blk), buf);
    }

    void writeBlock (int blknum, const void* buf) {
        int blk = remapBlock(blknum);
        loadMap(blk);
        int slot = findFreeSlot();
        if (slot == 0) {
            flushMapEntries();
            slot = 1; // the map is now free again
        }
        writeNewSlot(slot, blk, buf);
    }
};
