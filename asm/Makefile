TARGETS = monitor programs
DEP = $(TARGETS:%=%.d)

########################################

ASL   = asl
P2BIN = p2bin

########################################

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

%.d: %.s
	./depgen.py $< > $@

%.p: %.s
	$(ASL) -Werror -q -L $<

%.bin: %.p
	$(P2BIN) -q -segment CODE $<

clean:
	$(RM) $(patsubst %,%.bin,$(TARGETS))
	$(RM) $(patsubst %,%.lst,$(TARGETS))
	$(RM) $(patsubst %,%.p,$(TARGETS))
	$(RM) $(patsubst %,%.d,$(TARGETS))
