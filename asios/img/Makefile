IMGS = $(patsubst %.txt,%.svg,$(wildcard diag-*.txt))

all: $(IMGS)

%.svg: %.txt
	mscgen -T svg -o $@ $<

clean:
	rm -f $(IMGS)
