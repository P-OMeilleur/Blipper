# Blipper firmware - nice!nano v2 (nRF52840) + S340
#
#   make                 build build/blipper.hex
#   make flash           erase chip, flash S340 + app   (nrfjprog / J-Link)
#   make flash-pyocd     same, with pyOCD (Pico debugprobe, CMSIS-DAP, ST-Link...)
#   make flash-app       re-flash only the app (S340 already on the chip)

SDK_ROOT   ?= ../../nRF5_SDK_17.1.0_ddde560
S340_DIR   ?= $(SDK_ROOT)/components/softdevice/s340
S340_INC   ?= $(S340_DIR)/headers
S340_HEX   ?= $(firstword $(wildcard $(S340_DIR)/hex/*.hex) $(wildcard $(S340_DIR)/*.hex))
GNU_PREFIX ?= arm-none-eabi-

CC      := $(GNU_PREFIX)gcc
OBJCOPY := $(GNU_PREFIX)objcopy
SIZE    := $(GNU_PREFIX)size

BUILD  := build
TARGET := $(BUILD)/blipper

MDK := $(SDK_ROOT)/modules/nrfx/mdk

SRC_C := src/main.c $(MDK)/system_nrf52840.c
SRC_S := $(MDK)/gcc_startup_nrf52840.S

INC := -Isrc \
       -I$(S340_INC) -I$(S340_INC)/nrf52 \
       -I$(MDK) \
       -I$(SDK_ROOT)/components/toolchain/cmsis/include

CPU := -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=hard -mfpu=fpv4-sp-d16

DEFS := -DNRF52840_XXAA -DS340 -DSOFTDEVICE_PRESENT -DFLOAT_ABI_HARD \
        -D__HEAP_SIZE=0 -D__STACK_SIZE=4096

CFLAGS  := $(CPU) $(DEFS) $(INC) -std=gnu11 -Os -g3 -Wall -Wextra -Wno-unused-parameter \
           -ffunction-sections -fdata-sections -fno-strict-aliasing -fno-builtin -fshort-enums
ASFLAGS := $(CPU) $(DEFS) $(INC) -g3
LDFLAGS := $(CPU) -Tblipper_s340.ld -L$(MDK) -Wl,--gc-sections --specs=nano.specs \
           -Wl,-Map=$(TARGET).map

OBJS := $(addprefix $(BUILD)/,$(notdir $(SRC_C:.c=.o) $(SRC_S:.S=.o)))
vpath %.c $(sort $(dir $(SRC_C)))
vpath %.S $(sort $(dir $(SRC_S)))

.PHONY: all clean flash flash-app flash-pyocd check

all: check $(TARGET).hex

check:
	@test -f src/keys.h || (echo "ERROR: copy src/keys.h.example to src/keys.h and fill in the keys"; exit 1)
	@test -f $(S340_INC)/ant_interface.h || (echo "ERROR: S340 headers not found in $(S340_INC) (set S340_INC=...)"; exit 1)
	@test -f $(MDK)/system_nrf52840.c || (echo "ERROR: nRF5 SDK not found at $(SDK_ROOT) (set SDK_ROOT=...)"; exit 1)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c src/config.h src/keys.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) blipper_s340.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -lc -lnosys -lm
	$(SIZE) $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

# --- Flashing (SWD) --------------------------------------------------------
# A full erase removes the nice!nano UF2 bootloader and its S140 SoftDevice.

flash: all
	@test -n "$(S340_HEX)" || (echo "ERROR: S340 hex not found (set S340_HEX=...)"; exit 1)
	nrfjprog -f nrf52 --eraseall
	nrfjprog -f nrf52 --program $(S340_HEX) --verify
	nrfjprog -f nrf52 --program $(TARGET).hex --sectorerase --verify
	nrfjprog -f nrf52 --reset

flash-app: all
	nrfjprog -f nrf52 --program $(TARGET).hex --sectorerase --verify
	nrfjprog -f nrf52 --reset

flash-pyocd: all
	@test -n "$(S340_HEX)" || (echo "ERROR: S340 hex not found (set S340_HEX=...)"; exit 1)
	pyocd erase -t nrf52840 --chip
	pyocd flash -t nrf52840 $(S340_HEX)
	pyocd flash -t nrf52840 $(TARGET).hex
	pyocd reset -t nrf52840

clean:
	rm -rf $(BUILD)
