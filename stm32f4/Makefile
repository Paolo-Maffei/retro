F = -f ibm-3740-noskew
P = -f p112
O = flashmem.img
S = sdcard.img
A = z80-asm
B = z80-cpm3
C = z80-cpm2

ls:
	cpmls $F -D $O
	cpmls $P -D $S

disk1: $A/boot.com $A/bdos22.com $A/bios2.com
	cd $A && $(MAKE)
	rm -f $O
	mkfs.cpm $F -b $A/boot.com -b $A/bdos22.com -b $A/bios2.com $O
	dd if=/dev/zero of=$O bs=1 seek=256255 count=1
	cpmcp $F $O $C/*.com 0:

cpm2: disk1
	cpmcp $F -t $O $C/*.sub $A/b*.z80 0:
	ls -l $O

cpm3: disk1
	cpmcp $F $O $B/gencpm.com $B/link.com 0:
	cpmcp $F -t $O $B/defsnb.lib 0:defs.lib
	cpmcp $F -t $O $B/gencpmnb.dat 0:gencpm.dat
	cpmcp $F -t $O $B/bdos3.spr $B/*.sub $B/*.z80 0:
	ls -l $O

disk2:
	mkfs.cpm $P -b $A/boot.com $S
	dd if=/dev/zero of=$S bs=1k seek=1439 count=1
	cpmcp $P $S z80-cpm3/* 0:

.PHONY: disk1 cpm2gen cpm3ldr
