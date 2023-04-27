PROGRAMS = sci_hello sci_echo monitor
DEP = $(PROGRAMS:%=%.d)

########################################

ASL   = asl
P2BIN = p2bin

########################################

.SUFFIXES:

.PHONY: all

all: $(PROGRAMS)

$(PROGRAMS): %: %.bin


$(patsubst %,%_prog,$(PROGRAMS)): %_prog: %.bin
	../pico_programmer/programmer.py write_flash $<

$(patsubst %,%_minipro_prog,$(PROGRAMS)): %_minipro_prog: %.bin
	minipro -z -p AT28C64B -w $<

-include $(DEP)

%.d: %.s
	./depgen.py $< > $@

%.p: %.s
	$(ASL) -Werror -q -L $<

%.bin: %.p
	$(P2BIN) -q -segment CODE $<

clean:
	$(RM) $(patsubst %,%.bin,$(PROGRAMS))
	$(RM) $(patsubst %,%.lst,$(PROGRAMS))
	$(RM) $(patsubst %,%.p,$(PROGRAMS))
	$(RM) $(patsubst %,%.d,$(PROGRAMS))
