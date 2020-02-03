MEMORY
{
    RAM (xrw)       : ORIGIN = 0x2001D000, LENGTH = 4K
    FLASH (rx)      : ORIGIN = 0x08004000, LENGTH = 16K
}

INCLUDE etc/f407vg.ld
