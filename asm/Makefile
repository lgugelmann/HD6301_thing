TARGETS = monitor programs
DEP = $(TARGETS:%=%.d)

########################################

ASL   = asl
P2BIN = p2bin

########################################

VERBOSE?=0
QUIET=-q

ifeq ($(VERBOSE),1)
	QUIET=
endif

.SUFFIXES:

.PHONY: all monitor_prog programs_prog prog

all: $(TARGETS)

$(TARGETS): %: %.bin

monitor_prog: monitor.bin
	../pico_programmer/programmer.py write_flash $<

programs_prog: programs.bin
	../pico_programmer/programmer.py write_flash --address 0x8000 $<

prog: monitor_prog programs_prog

-include $(DEP)

monitor.p: monitor.s
	$(ASL) -Werror $(QUIET) -U -a -L $<

programs.p: programs.s
	$(ASL) -Werror $(QUIET) -U -L $<

%.d: %.s monitor.p
	./depgen.py $< > $@

%.bin: %.p
	$(P2BIN) -q -segment CODE $<

clean:
	$(RM) $(patsubst %,%.bin,$(TARGETS))
	$(RM) $(patsubst %,%.lst,$(TARGETS))
	$(RM) $(patsubst %,%.p,$(TARGETS))
	$(RM) $(patsubst %,%.d,$(TARGETS))
	$(RM) $(patsubst %,%.inc,$(TARGETS))
