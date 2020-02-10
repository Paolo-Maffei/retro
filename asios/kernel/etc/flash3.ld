MEMORY
{
    RAM (xrw)       : ORIGIN = 0x2001C000, LENGTH = 3K
    FLASH (rx)      : ORIGIN = 0x0800C000, LENGTH = 16K
}

INCLUDE etc/f407vg.ld
