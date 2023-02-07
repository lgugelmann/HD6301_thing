PROGRAMS = blink blink_parametrized sci_hello sci_echo

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

%.p: %.s
	$(ASL) -L $<

%.bin: %.p
	$(P2BIN) $<

clean:
	$(RM) $(patsubst %,%.bin,$(PROGRAMS))
	$(RM) $(patsubst %,%.lst,$(PROGRAMS))
	$(RM) $(patsubst %,%.p,$(PROGRAMS))
