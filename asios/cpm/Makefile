# Uses the "zmac" assembler by George Phillips: http://48k.ca/zmac.html

OBJS = $(patsubst %.z80,%.com,$(wildcard *.z80))

all: $(OBJS)
	cat boot.com bdos22.com bios2.com >system.bin
	xxd -i <system.bin >system.h

%.com: %.z80
	zmac -o aout.cim $< && mv aout.cim $@

clean:
	rm -f $(OBJS)
