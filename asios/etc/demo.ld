MEMORY
{
    RAM (xrw)       : ORIGIN = 0x2001C000, LENGTH = 4K
    FLASH (rx)      : ORIGIN = 0x08008000, LENGTH = 32K
}

INCLUDE etc/f407vg.ld
