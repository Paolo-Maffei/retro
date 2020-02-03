MEMORY
{
    RAM (xrw)       : ORIGIN = 0x2001E000, LENGTH = 8K
    FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 16K
}

INCLUDE etc/f407vg.ld
