F = -f ibm-3740-noskew
P = -f p112
O = flashmem.img
S = sdcard.img
A = z80-asm

ls:
	cpmls $F -D $O
	cpmls $P -D $S

disk1: $A/boot.com $A/bdos22.com $A/bios.com
	cd $A && $(MAKE)
	mkfs.cpm $F -b $A/boot.com -b $A/bdos22.com -b $A/bios.com $O
	cpmcp $F $O z80-cpm2/* 0:

cpm2gen: disk1
#	cpmrm $F $O ddtz.com l80.com
	cpmcp $F -t $O $A/cpm2gen.sub $A/b*.z80 0:
	ls -l $O

cpm3ldr: disk1
	cpmcp $F -t $O $A/cpm3ldr.sub $A/cpmldr.z80 $A/ldrbios.z80 0:
	ls -l $O

disk2:
	mkfs.cpm $P -b $A/boot.com $S
	cpmcp $P $S z80-cpm3/* 0:

.PHONY: disk1 cpm2gen cpm3ldr
