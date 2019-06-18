# Uses the "zmac" assembler by George Phillips: http://48k.ca/zmac.html

SRCS = bdos22.z80 bios2.z80 boot.z80 hexsave.z80 zexall.z80 zexdoc.z80
OBJS = ${SRCS:.z80=.com}

all: $(OBJS)
	cat boot.com bios2.com >system.bin
	dd if=bdos22.com of=system.bin bs=1k seek=1

%.com: %.z80
	zmac -o aout.cim $< && mv aout.cim $@
