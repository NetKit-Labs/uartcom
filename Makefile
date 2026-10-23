# Host tests use the newest of C++23 / C++20 / C++17 the host compiler accepts.
# Firmware does the same with arm-none-eabi-g++. This machine resolves both to C++23.

HOST_CXX ?= c++
PYTHON ?= python3
OPENOCD ?= openocd

# arm-none-eabi-g++ on PATH is used when it can see the C++ library. Homebrew's
# GCC 16 bottle is built --without-headers, so it accepts -std=c++23 and then
# fails to find <cstdint>. tools/find-arm-cxx.sh falls back to a full toolchain.
ARM_CXX := $(shell ARM_CXX_REQUEST='$(ARM_CXX_REQUEST)' sh tools/find-arm-cxx.sh)

pick_std = $(shell if printf '%s\n' 'int main(){}' | $(1) -std=c++23 -fsyntax-only -xc++ - >/dev/null 2>&1; then echo c++23; \
elif printf '%s\n' 'int main(){}' | $(1) -std=c++20 -fsyntax-only -xc++ - >/dev/null 2>&1; then echo c++20; \
else echo c++17; fi)
std_num = $(if $(filter c++23,$(1)),202302,$(if $(filter c++20,$(1)),202002,201703))

HOST_STD := $(call pick_std,$(HOST_CXX))
ARM_STD := $(call pick_std,$(ARM_CXX))
HOST_STD_NUM := $(call std_num,$(HOST_STD))
ARM_STD_NUM := $(call std_num,$(ARM_STD))

BUILD := build
CPU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
WARN := -Wall -Wextra -Wpedantic -Werror -Wshadow
HOST_FLAGS := -std=$(HOST_STD) $(WARN) -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	-Iinclude -Itests -DUARTCOM_CXX_STANDARD=$(HOST_STD_NUM)L
ARM_FLAGS := -std=$(ARM_STD) $(WARN) $(CPU) -O2 -ffunction-sections -fdata-sections \
	-fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-use-cxa-atexit \
	-Iinclude -Ifirmware/include -DUARTCOM_CXX_STANDARD=$(ARM_STD_NUM)L -MMD -MP
ARM_LINK := $(CPU) -Wl,--gc-sections -Wl,--print-memory-usage -Wl,-Map,$(BUILD)/uartcom.map \
	-T firmware/linker/stm32f446re.ld

FW_SRCS := firmware/src/startup.cpp firmware/src/clock.cpp firmware/src/board.cpp firmware/src/main.cpp
FW_OBJS := $(patsubst firmware/src/%.cpp,$(BUILD)/%.o,$(FW_SRCS))

UNIT_SRCS := tests/test_harness.cpp tests/test_baud.cpp tests/test_ring.cpp tests/test_driver.cpp tests/test_protocol.cpp
REG_SRCS := tests/test_harness.cpp tests/test_regression.cpp

.PHONY: all test test-unit test-regression test-python firmware flash hwtest check-target identify clean std

all: test firmware

std:
	@echo "host $(HOST_CXX) -> $(HOST_STD) ($(HOST_STD_NUM))"
	@echo "firmware $(ARM_CXX) -> $(ARM_STD) ($(ARM_STD_NUM))"

test: test-unit test-regression test-python

test-unit: $(BUILD)/test_unit
	./$(BUILD)/test_unit

test-regression: $(BUILD)/test_regression
	./$(BUILD)/test_regression

# Baud math and CRC vectors only. The board cases in tools/hwtest.py stay on hwtest.
test-python:
	$(PYTHON) -m unittest discover -s tools -p hwtest.py -k BaudMathTests

firmware: $(BUILD)/uartcom.elf $(BUILD)/firmware_cxx.txt
	@echo "firmware C++ standard $(ARM_STD) ($(ARM_STD_NUM))"
	@arm-none-eabi-size $(BUILD)/uartcom.elf

$(BUILD)/firmware_cxx.txt: | $(BUILD)
	@echo $(ARM_STD_NUM) > $@
	@echo $(HOST_STD_NUM) > $(BUILD)/host_cxx.txt

$(BUILD)/test_unit: $(UNIT_SRCS) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(UNIT_SRCS) -o $@

$(BUILD)/test_regression: $(REG_SRCS) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(REG_SRCS) -o $@

$(BUILD)/%.o: firmware/src/%.cpp | $(BUILD)
	$(ARM_CXX) $(ARM_FLAGS) -c $< -o $@

$(BUILD)/uartcom.elf: $(FW_OBJS) firmware/linker/stm32f446re.ld
	$(ARM_CXX) $(ARM_LINK) $(FW_OBJS) -nostdlib -lgcc -o $@

-include $(FW_OBJS:.o=.d)

$(BUILD):
	mkdir -p $(BUILD)

flash: firmware
	$(OPENOCD) -f interface/stlink.cfg -f firmware/openocd/nucleo_f446re.cfg \
		-c "program $(CURDIR)/$(BUILD)/uartcom.elf verify reset exit"

hwtest: $(BUILD)/firmware_cxx.txt
	$(PYTHON) tools/hwtest.py

check-target: flash hwtest

identify:
	$(OPENOCD) -f interface/stlink.cfg -f firmware/openocd/nucleo_f446re.cfg \
		-c "init" -c "reset halt" -c "mdw 0xE0042000 1" -c "exit"

clean:
	rm -rf $(BUILD)
