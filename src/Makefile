PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
INCLUDE = -I ../include/ -lopencm3_stm32f1 -L ../lib/
CFLAGS = -c -MMD -O0 -mcpu=cortex-m3 -mthumb -Wall -Wno-unused-but-set-variable -g3

C_SOURCES = $(filter-out $(wildcard dispatch/*.c), $(wildcard *.c */*.c */*/*.c))
OBJECT_FILES = $(C_SOURCES:.c=.o)

../bin/main.bin: ../bin/main.elf
	$(PREFIX)objcopy -O binary $< $@

../bin/main.elf: $(OBJECT_FILES)
	$(PREFIX)ld $^ $(INCLUDE) -T./stm32f103c8t6.ld -o $@

%.o: %.c
	$(CC) $(INCLUDE) $(CFLAGS) $< -o $@

-include *.d

upload: ../bin/main.bin
	st-flash write $< 0x8000000

reset:
	st-flash reset

download:
	st-flash read ../bin/download.bin 0x8000000 0x1ff00

debug: ../bin/main.elf
	-openocd &
	-gdb-multiarch -tui -q $<
	kill $$(pgrep openocd)

all: upload debug

kill_ocd:
	kill $$(pgrep openocd)
	
clean:
	-rm *.o
	-rm *.d
	-rm */*.o
	-rm */*.d
	-rm */*/*.o
	-rm */*/*.d
	-rm ../bin/*.elf
	-rm ../bin/*.bin
