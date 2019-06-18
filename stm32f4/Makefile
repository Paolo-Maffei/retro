F = -f ibm-3740-noskew
O = flashmem.img
A = z80-asm

ls:
	cpmls $F -d $O

boot: empty.img
	cp empty.img $O
	cpmcp $F $O z80-cpm2/* 0:
	ls -l $O

cpm2gen: boot
#	cpmrm $F $O ddtz.com l80.com
	cpmcp $F -t $O $A/cpm2gen.sub $A/b*.z80 0:
	ls -l $O

cpm3ldr: boot
	cpmcp $F -t $O $A/cpm3ldr.sub $A/cpmldr.z80 $A/ldrbios.z80 0:
	ls -l $O

empty.img: $A/boot.com $A/bdos22.com $A/bios.com
	cd $A && $(MAKE)
	mkfs.cpm $F -b $A/boot.com -b $A/bdos22.com -b $A/bios.com $@

.PHONY: boot cpm2gen cpm3ldr
