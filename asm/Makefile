PROGRAMS = blink sci_hello

########################################

ASL   = asl
P2BIN = p2bin

########################################

.PHONY: all

all: $(patsubst %, %.bin, $(PROGRAMS))

$(patsubst %,%_prog,$(PROGRAMS)): PROGRAM=$(subst _prog,,$@)
$(patsubst %,%_prog,$(PROGRAMS)):
	minipro -z -p AT28C64B -w $(PROGRAM).bin

%.p: %.s
	$(ASL) -L $<

%.bin: %.p
	$(P2BIN) $<

clean:
	$(RM) $(patsubst %,%.bin,$(PROGRAMS))
	$(RM) $(patsubst %,%.lst,$(PROGRAMS))
	$(RM) $(patsubst %,%.p,$(PROGRAMS))
