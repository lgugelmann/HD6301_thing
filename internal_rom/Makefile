PROGRAMS = serial_romdump

########################################

ASL   = asl
P2BIN = p2bin

########################################

.PHONY: all clean dis

all: $(patsubst %, %.bin, $(PROGRAMS))

$(patsubst %,%_prog,$(PROGRAMS)): PROGRAM=$(subst _prog,,$@)
$(patsubst %,%_prog,$(PROGRAMS)):
	minipro -z -p AT28C64B -w $(PROGRAM).bin

%.p: %.s
	$(ASL) -L $<

%.bin: %.p
	$(P2BIN) $<

dis:
	dasmfw -dasm=6301 -showzero -offset f000 -noupmnemo \
	-info dumped_internal_rom.info dumped_internal_rom.bin \
	-out dumped_internal_rom_disassembly.s

clean:
	$(RM) $(patsubst %,%.bin,$(PROGRAMS))
	$(RM) $(patsubst %,%.lst,$(PROGRAMS))
	$(RM) $(patsubst %,%.p,$(PROGRAMS))
