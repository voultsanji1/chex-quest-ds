# ARM7 build for NDS.
#
# Compiles the secondary processor binary. The ARM7 handles audio
# mixing (via MaxMod), touchscreen input, WiFi, and real-time clock.
# This binary runs alongside the ARM9 (main game) on the NDS.

export BLOCKSDS		?= /opt/blocksds/core
export BLOCKSDSEXT	?= /opt/blocksds/external
export WONDERFUL_TOOLCHAIN	?= /opt/wonderful
ARM_NONE_EABI_PATH	?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/

SOURCEDIRS	:= source
INCLUDEDIRS	:=

# -DUSE_MAXMOD: enables MaxMod integration in libnds ARM7 headers,
# activating the audio mixer interrupt handlers on the ARM7 side.
DEFINES		:= -DUSE_MAXMOD

# DEBUG=1 enables the ARM7 exception handler (defaultExceptionHandler)
# which sends crash info to ARM9 for display.
ifeq ($(DEBUG),1)
DEFINES		+= -DDEBUG_LIBS
endif

# -lmm7:  MaxMod ARM7 library (audio mixing, module playback)
# -lnds7: libnds ARM7 library (hardware abstraction for ARM7 peripherals)
# -lc:    newlib C standard library
LIBS		:= -lmm7 -lnds7 -lc
LIBDIRS		:= $(BLOCKSDS)/libs/maxmod \
		   $(BLOCKSDS)/libs/libnds

NAME		:= arm7
BUILDDIR	:= build/$(NAME)
ELF		:= build/$(NAME).elf
MAP		:= build/$(NAME).map

PREFIX		:= $(ARM_NONE_EABI_PATH)arm-none-eabi-
CC		:= $(PREFIX)gcc
LD		:= $(PREFIX)gcc
MKDIR		:= mkdir
RM		:= rm -rf

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

SOURCES_C	:= $(shell find -L $(SOURCEDIRS) -name "*.c")

# ARCH flags:
#   -mthumb       = 16-bit Thumb ISA for smaller code
#   -mcpu=arm7tdmi = NDS sub CPU (ARMv4T, no Thumb-2, no DSP extensions)
ARCH		:= -mthumb -mcpu=arm7tdmi
SPECS		:= $(BLOCKSDS)/sys/crts/ds_arm7.specs
WARNFLAGS	:= -Wall -Wextra -Wpedantic -Wshadow

INCLUDEFLAGS	:= $(foreach path,$(INCLUDEDIRS),-I$(path)) \
		   $(foreach path,$(LIBDIRS),-I$(path)/include)

LIBDIRSFLAGS	:= $(foreach path,$(LIBDIRS),-L$(path)/lib)

CFLAGS		:= -std=gnu17 $(WARNFLAGS) $(INCLUDEFLAGS) $(DEFINES) \
		   $(ARCH) -O2 -g -ffunction-sections -fdata-sections \
		   -specs=$(SPECS)

LDFLAGS		:= $(ARCH) $(LIBDIRSFLAGS) -Wl,-Map,$(MAP) $(DEFINES) \
		   -Wl,--start-group $(LIBS) -Wl,--end-group -specs=$(SPECS)

OBJS		:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_C)))
DEPS		:= $(OBJS:.o=.d)

.PHONY: all clean

all: $(ELF)

$(ELF): $(OBJS)
	@echo "  LD.7    $@"
	$(V)$(LD) -o $@ $(OBJS) $(LDFLAGS)

clean:
	@echo "  CLEAN.7"
	$(V)$(RM) $(ELF) $(MAP) $(BUILDDIR)

$(BUILDDIR)/%.c.o : %.c
	@echo "  CC.7    $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)
