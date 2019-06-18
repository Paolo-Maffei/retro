F = -f ibm-3740-noskew
O = flashmem.img
A = z80-asm

ls:
	cpmls $F -d $O

boot: empty.img
	cp empty.img $O
	cpmcp $F $O z80-cpm2/* 0:
	ls -l $O

empty.img: $A/boot.com $A/bdos22.com $A/bios.com
	cd $A && $(MAKE)
	mkfs.cpm $F -b $A/boot.com -b $A/bdos22.com -b $A/bios.com $@

.PHONY: boot
