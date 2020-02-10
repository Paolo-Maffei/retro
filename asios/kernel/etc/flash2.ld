MEMORY
{
    RAM (xrw)       : ORIGIN = 0x2001CC00, LENGTH = 1K
    FLASH (rx)      : ORIGIN = 0x08008000, LENGTH = 16K
}

INCLUDE etc/f407vg.ld
